#include "prober/prober.hpp"

#include "events.hpp"
#include "prober/icmp_probe.hpp"
#include "prober/tcp_probe.hpp"
#include "prober/traceroute.hpp"
#include "rollup.hpp"
#include "scheduler.hpp"
#include "storage/address_registry.hpp"
#include "storage/index.hpp"
#include "storage/seal.hpp"
#include "storage/segment_writer.hpp"
#include "util/log.hpp"
#include "util/queue.hpp"
#include "util/time.hpp"

#include <windows.h>
#include <io.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace pt {

namespace {

// A target resolved and ready to probe.
struct ActiveTarget {
    uint16_t    id;
    std::string name;
    uint32_t    addr;        // IPv4 network order
    Proto       proto;
    uint16_t    port;
    uint32_t    interval_ms;
    uint32_t    timeout_ms;
};

// A unit of work handed to a worker.
struct Job {
    uint16_t id;
    uint32_t addr;
    Proto    proto;
    uint16_t port;
    uint32_t timeout_ms;
};

// A trace-on-event request handed to the trace thread.
struct TraceJob {
    uint64_t    start_utc;
    uint16_t    target_id;
    uint32_t    addr;
    std::string name;
};

// Worker: owns one ICMP handle, drains jobs (ICMP or TCP), pushes samples.
void worker_loop(BlockingQueue<Job>* jobs, BlockingQueue<ProbeSample>* samples) {
    IcmpProbe probe;
    if (!probe.valid()) {
        log_error("worker: IcmpCreateFile failed");
        return;
    }
    while (auto job = jobs->pop()) {
        ProbeSample s;
        s.target_id = job->id;
        if (job->proto == Proto::Tcp) {
            s.result = tcp_ping(job->addr, job->port, job->timeout_ms);
        } else {
            s.result = probe.ping(job->addr, job->timeout_ms);
        }
        s.utc_ms = now_utc_ms();
        samples->push(s);
    }
}

// Trace thread: runs one traceroute per request, saving traces/<start>.txt.
void trace_loop(BlockingQueue<TraceJob>* jobs, std::string dir,
                const std::atomic<bool>* stop) {
    const std::string tdir = dir + "/traces";
    while (auto job = jobs->pop()) {
        std::error_code ec;
        std::filesystem::create_directories(tdir, ec);
        const std::string path = tdir + "/" + std::to_string(job->start_utc) + "_" +
                                 std::to_string(job->target_id) + ".txt";
        std::FILE* f = std::fopen(path.c_str(), "wb");
        if (!f) {
            log_warn("trace: cannot write '" + path + "'");
            continue;
        }
        std::fprintf(f, "# pingtrace traceroute\n# target: %s (%s)\n# outage start: %s (local)\n",
                     job->name.c_str(), ipv4_to_string(job->addr).c_str(),
                     format_local_ms(job->start_utc).c_str());
        std::fprintf(f, "%-4s %-16s %s\n", "ttl", "ip", "rtt");
        const auto hops = traceroute(job->addr, 30, 1000, 2, stop);
        for (const auto& h : hops) {
            char rtt[16];
            if (h.addr == 0 || h.rtt_tenths == kRttNa) std::snprintf(rtt, sizeof(rtt), "-");
            else std::snprintf(rtt, sizeof(rtt), "%.1fms", h.rtt_tenths / 10.0);
            std::fprintf(f, "%-4d %-16s %s%s\n", h.ttl,
                         h.addr ? ipv4_to_string(h.addr).c_str() : "*",
                         rtt, h.reached ? "  (dest)" : "");
        }
        std::fclose(f);
        log_info("trace saved: " + path + " (" + std::to_string(hops.size()) + " hops)");
    }
}

// Writer: drains samples into hourly segments, sealing each at the boundary and
// on exit, and prints a periodic summary.
void writer_loop(BlockingQueue<ProbeSample>* samples, const Config* cfg,
                 const std::vector<ActiveTarget>* active, Index* index,
                 BlockingQueue<TraceJob>* trace_q) {
    const uint64_t seg_ms =
        std::max<uint64_t>(1, static_cast<uint64_t>(cfg->storage.segment_minutes)) * 60000ULL;
    const std::string& dir = cfg->storage.dir;

    std::vector<AddrEntry> addrs;
    std::map<uint16_t, std::string> name_of;
    std::map<uint16_t, uint32_t> addr_of;
    for (const auto& a : *active) {
        addrs.push_back({a.id, a.addr, a.name});
        name_of[a.id] = a.name;
        addr_of[a.id] = a.addr;
    }

    AddressRegistry registry(dir);
    RollupWriter rollup(dir, name_of);
    EventLog evlog(dir);

    // Trace-on-event: enqueue a one-shot traceroute when an outage opens.
    auto on_open = [trace_q, &name_of, &addr_of](uint64_t start_utc, uint16_t tid) {
        TraceJob tj;
        tj.start_utc = start_utc;
        tj.target_id = tid;
        tj.addr = addr_of.count(tid) ? addr_of[tid] : 0;
        tj.name = name_of.count(tid) ? name_of[tid] : std::string();
        if (tj.addr != 0) trace_q->push(tj);
    };
    EventDetector detector(cfg->events, name_of, &evlog, active->size(), on_open);

    SegmentWriter seg;
    auto open_aligned = [&](uint64_t utc) -> bool {
        uint64_t start = (utc / seg_ms) * seg_ms;  // align to segment boundary
        std::error_code ec;
        if (std::filesystem::exists(dir + "/seg_" + std::to_string(start) + ".zst", ec)) {
            start = utc;  // a sealed segment already owns this slot (crash+restart)
        }
        return seg.open(dir, start, addrs);
    };

    if (!open_aligned(now_utc_ms())) return;
    log_info("writing segment " + seg.path());

    uint64_t cur_end  = seg.start_utc_ms() + seg_ms;
    uint64_t last_utc = seg.start_utc_ms();

    struct Stat { uint64_t sent = 0, lost = 0; Status last = Status::Timeout; uint16_t rtt = kRttNa; };
    std::map<uint16_t, Stat> stats;
    std::map<uint16_t, std::pair<uint64_t, uint64_t>> base;  // window baseline (~10s ago)

    // Interactive console -> live in-place dashboard; piped/redirected -> append
    // blocks (clean in log files / under a service). PINGTRACE_TTY=1 forces the
    // dashboard regardless (e.g. when piping to a VT-capable viewer).
    const bool tty = (_isatty(_fileno(stdout)) != 0) || (std::getenv("PINGTRACE_TTY") != nullptr);
    const HANDLE h_in = GetStdHandle(STD_INPUT_HANDLE);
    DWORD in_mode_saved = 0;
    bool  in_mode_restore = false;
    if (tty) {
        const HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD m = 0;
        if (GetConsoleMode(hOut, &m))
            SetConsoleMode(hOut, m | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
        // Disable QuickEdit so a mouse click doesn't freeze output (which is what
        // makes an in-place redraw "stack up"). Restored on shutdown.
        if (GetConsoleMode(h_in, &in_mode_saved)) {
            SetConsoleMode(h_in, (in_mode_saved & ~ENABLE_QUICK_EDIT_MODE) | ENABLE_EXTENDED_FLAGS);
            in_mode_restore = true;
        }
        std::printf("pingtrace live - in-place; history on disk (%s); Ctrl+C to stop\n",
                    dir.c_str());
    }
    const double writer_start = mono_ms();
    int dash_lines = 0;  // lines drawn in the previous dashboard frame

    double last_render = mono_ms();
    double last_window = mono_ms();
    double last_flush  = mono_ms();
    uint64_t since_flush = 0;

    while (auto opt = samples->pop()) {
        const ProbeSample& s = *opt;

        // Rotate at the segment boundary.
        if (s.utc_ms >= cur_end) {
            seg.flush();
            const std::string part = seg.path();
            seg.close();
            seal_part(dir, part, last_utc, *index);
            if (!open_aligned(s.utc_ms)) return;
            log_info("writing segment " + seg.path());
            cur_end = seg.start_utc_ms() + seg_ms;
            since_flush = 0;
            last_flush = mono_ms();
        }
        last_utc = s.utc_ms;

        // Synthetic monitor-gap marker (sleep / clock jump): record it, log a
        // monitor_gap event, and clear pending failure counters. Not a probe.
        if (s.result.status == Status::MonitorGap) {
            RawRecord g{};
            g.target_id   = 0;
            const uint64_t goff = (s.utc_ms > seg.start_utc_ms()) ? (s.utc_ms - seg.start_utc_ms()) : 0;
            g.t_offset_ms = static_cast<uint32_t>(std::min<uint64_t>(goff, 0xFFFFFFFFu));
            g.status      = static_cast<uint8_t>(Status::MonitorGap);
            g.rtt_tenths_ms = kRttNa;
            seg.append(g);

            OutageEvent e;
            e.event_id  = evlog.next_id();
            e.target_id = 0;
            e.target    = "monitor";
            e.start_utc = (s.utc_ms > s.gap_ms) ? (s.utc_ms - s.gap_ms) : 0;
            e.end_utc   = s.utc_ms;
            e.type      = "monitor_gap";
            evlog.append(e);
            detector.reset_after_gap();
            log_warn("monitor gap " + std::to_string(s.gap_ms / 1000) + "s recorded (sleep/clock jump)");
            continue;
        }

        RawRecord r;
        r.target_id     = s.target_id;
        const uint64_t off = (s.utc_ms > seg.start_utc_ms()) ? (s.utc_ms - seg.start_utc_ms()) : 0;
        r.t_offset_ms   = static_cast<uint32_t>(std::min<uint64_t>(off, 0xFFFFFFFFu));
        r.status        = static_cast<uint8_t>(s.result.status);
        r.reply_ttl     = s.result.reply_ttl;
        r.rtt_tenths_ms = s.result.rtt_tenths_ms;
        r.src_id        = registry.id_for(s.result.src_addr);  // which router sent the ICMP error
        seg.append(r);

        // Online rollups and outage detection.
        rollup.add(s);
        rollup.flush_completed(s.utc_ms);
        detector.add(s);

        auto& st = stats[s.target_id];
        ++st.sent;
        if (s.result.status != Status::Ok) ++st.lost;
        st.last = s.result.status;
        st.rtt  = s.result.rtt_tenths_ms;

        const double now = mono_ms();
        if (++since_flush >= 50 || now - last_flush >= 1500.0) {
            seg.flush();
            since_flush = 0;
            last_flush = now;
        }

        const double render_int = tty ? 1000.0 : 10000.0;
        if (now - last_render >= render_int) {
            uint64_t total_probes = 0;
            for (const auto& [id, st2] : stats) total_probes += st2.sent;

            const char* eol = tty ? "\x1b[2K" : "";  // erase stale line content
            int lines = 0;

            if (tty) {
                // Redraw the block in place: jump back up to its first line,
                // then overwrite each line. No screen clear -> nothing piles up
                // in the scrollback even if the console is clicked/scrolled.
                if (dash_lines > 0) std::printf("\x1b[%dF", dash_lines);
                const int up = static_cast<int>((now - writer_start) / 1000.0);
                std::printf("%spingtrace live   %s UTC   uptime %02d:%02d:%02d   probes %llu\n",
                            eol, format_utc_ms(now_utc_ms()).c_str(),
                            up / 3600, (up % 3600) / 60, up % 60,
                            static_cast<unsigned long long>(total_probes));
                ++lines;
            } else {
                std::printf("\n==== %s UTC | window=last ~10s | tot=since start ====\n",
                            format_utc_ms(now_utc_ms()).c_str());
            }
            std::printf("%s  %-16s %14s   %16s   %9s\n",
                        eol, "target", "loss (10s)", "loss (total)", "last");
            ++lines;

            for (const auto& [id, st2] : stats) {
                const auto& b = base[id];
                const uint64_t w_sent = st2.sent - b.first;
                const uint64_t w_lost = st2.lost - b.second;
                const double w_loss = w_sent ? 100.0 * w_lost / w_sent : 0.0;
                const double c_loss = st2.sent ? 100.0 * st2.lost / st2.sent : 0.0;

                char rtt[16];
                if (st2.rtt == kRttNa) std::snprintf(rtt, sizeof(rtt), "%s", to_string(st2.last));
                else std::snprintf(rtt, sizeof(rtt), "%.1fms", st2.rtt / 10.0);

                const char* color = (tty && w_loss > 0.0) ? "\x1b[31m" : "";
                const char* reset = (tty && w_loss > 0.0) ? "\x1b[0m" : "";
                std::printf("%s  %s%-16s %4llu/%-5llu %5.1f%%   %6llu/%-7llu %5.1f%%   %9s%s%s\n",
                            eol, color, name_of[id].c_str(),
                            static_cast<unsigned long long>(w_lost),
                            static_cast<unsigned long long>(w_sent), w_loss,
                            static_cast<unsigned long long>(st2.lost),
                            static_cast<unsigned long long>(st2.sent), c_loss,
                            rtt, (w_loss > 0.0 ? "  <- loss" : ""), reset);
                ++lines;
            }
            std::fflush(stdout);
            dash_lines = lines;
            last_render = now;

            // Roll the window baseline every ~10s.
            if (now - last_window >= 10000.0 || base.empty()) {
                base.clear();
                for (const auto& [id, st2] : stats) base[id] = {st2.sent, st2.lost};
                last_window = now;
            }
        }
    }

    // Restore the console (re-enable QuickEdit) and drop below the dashboard
    // so shutdown logs print cleanly underneath it.
    if (in_mode_restore) SetConsoleMode(h_in, in_mode_saved);
    if (tty) { std::printf("\n"); std::fflush(stdout); }

    // Close out aggregation and seal the final active segment on shutdown.
    detector.finalize(last_utc);
    rollup.finalize();
    seg.flush();
    const std::string part = seg.path();
    seg.close();
    seal_part(dir, part, last_utc, *index);
}

}  // namespace

int run_prober(const Config& cfg, std::atomic<bool>* stop, uint32_t simulate_gap_ms) {
    // Resolve targets up front (ICMP and TCP).
    std::vector<ActiveTarget> active;
    for (const auto& t : cfg.targets) {
        const uint32_t addr = resolve_ipv4(t.address);
        if (addr == 0) {
            log_warn("cannot resolve '" + t.address + "' for target '" + t.name + "'");
            continue;
        }
        active.push_back({t.id, t.name, addr, t.proto, t.port, t.interval_ms, t.timeout_ms});
        const char* proto = (t.proto == Proto::Tcp) ? "tcp" : "icmp";
        log_info("target '" + t.name + "' -> " + ipv4_to_string(addr) + " (" + proto +
                 (t.proto == Proto::Tcp ? ":" + std::to_string(t.port) : "") + ")");
    }
    if (active.empty()) {
        log_error("no probeable targets");
        return 1;
    }

    // Crash recovery and retention before opening a fresh segment.
    Index index(cfg.storage.dir);
    {
        std::error_code ec;
        std::filesystem::create_directories(cfg.storage.dir, ec);
    }
    const size_t recovered = recover_dangling_parts(cfg.storage.dir, index);
    if (recovered > 0) log_info("recovered " + std::to_string(recovered) + " dangling segment(s)");
    enforce_retention(cfg.storage.dir, cfg.storage.raw_retention_days, now_utc_ms(), index);

    BlockingQueue<Job> jobs;
    BlockingQueue<ProbeSample> samples;
    BlockingQueue<TraceJob> traces;

    std::thread tracer(trace_loop, &traces, cfg.storage.dir, stop);
    std::thread writer(writer_loop, &samples, &cfg, &active, &index, &traces);

    const unsigned n_workers =
        std::max<unsigned>(2, std::min<unsigned>(static_cast<unsigned>(active.size()), 64));
    std::vector<std::thread> workers;
    for (unsigned i = 0; i < n_workers; ++i) {
        workers.emplace_back(worker_loop, &jobs, &samples);
    }
    log_info("started " + std::to_string(n_workers) + " worker(s) for " +
             std::to_string(active.size()) + " target(s)");

    // Scheduler loop on this thread.
    Scheduler sched;
    const double start = mono_ms();
    for (size_t i = 0; i < active.size(); ++i) {
        sched.add(i, static_cast<double>(active[i].interval_ms), start);
    }

    // Monitor-gap detection: wall clock advancing far more than the monotonic
    // clock between iterations means the host slept or the clock jumped.
    constexpr double kGapThresholdMs = 5000.0;
    double   prev_mono = mono_ms();
    uint64_t prev_wall = now_utc_ms();
    const double loop_start = prev_mono;
    bool gap_injected = false;

    std::vector<size_t> due;
    while (!stop->load()) {
        const double now = mono_ms();
        due.clear();
        sched.collect_due(now, due);
        for (size_t idx : due) {
            const auto& a = active[idx];
            jobs.push(Job{a.id, a.addr, a.proto, a.port, a.timeout_ms});
        }

        double wait = sched.earliest() - mono_ms();
        if (wait < 1.0) wait = 1.0;
        if (wait > 200.0) wait = 200.0;
        Sleep(static_cast<DWORD>(wait));

        const double   cur_mono = mono_ms();
        const uint64_t cur_wall = now_utc_ms();
        const double   d_mono = cur_mono - prev_mono;
        const double   d_wall = static_cast<double>(cur_wall) - static_cast<double>(prev_wall);
        if (d_wall - d_mono > kGapThresholdMs) {
            ProbeSample gap;
            gap.target_id     = 0;
            gap.utc_ms        = cur_wall;
            gap.result.status = Status::MonitorGap;
            gap.gap_ms        = static_cast<uint32_t>(d_wall - d_mono);
            samples.push(gap);
        }
        prev_mono = cur_mono;
        prev_wall = cur_wall;

        // Test hook: inject one synthetic gap a few seconds in.
        if (simulate_gap_ms > 0 && !gap_injected && cur_mono - loop_start > 3000.0) {
            ProbeSample gap;
            gap.target_id     = 0;
            gap.utc_ms        = cur_wall;
            gap.result.status = Status::MonitorGap;
            gap.gap_ms        = simulate_gap_ms;
            samples.push(gap);
            gap_injected = true;
        }
    }

    log_info("stopping...");
    jobs.close();
    for (auto& w : workers) w.join();
    samples.close();
    writer.join();       // writer may enqueue final traces during finalize()
    traces.close();
    tracer.join();
    return 0;
}

}  // namespace pt
