#include "events.hpp"

#include "util/log.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>

namespace pt {

namespace {
constexpr char kHeader[] =
    "event_id,target_id,target,start_utc,end_utc,duration_s,sent_in_window,lost,type,trace_ref";
}

EventLog::EventLog(const std::string& storage_dir) : path_(storage_dir + "/events.csv") {
    // Continue id numbering from the existing file (count data rows).
    std::ifstream f(path_);
    if (f) {
        std::string line;
        bool first = true;
        while (std::getline(f, line)) {
            if (first) { first = false; continue; }
            if (!line.empty()) ++counter_;
        }
    }
}

void EventLog::append(const OutageEvent& e) {
    const bool fresh = !std::filesystem::exists(path_);
    std::ofstream f(path_, std::ios::app);
    if (!f) {
        log_error("events: cannot open '" + path_ + "'");
        return;
    }
    if (fresh) f << kHeader << "\n";

    const double duration_s = (e.end_utc > e.start_utc)
                                  ? (e.end_utc - e.start_utc) / 1000.0 : 0.0;
    char dur[24];
    std::snprintf(dur, sizeof(dur), "%.1f", duration_s);

    f << e.event_id << ',' << e.target_id << ',' << e.target << ','
      << e.start_utc << ',' << e.end_utc << ',' << dur << ','
      << e.sent_in_window << ',' << e.lost << ',' << e.type << ',' << e.trace_ref << "\n";
}

EventDetector::EventDetector(const EventsConfig& cfg, std::map<uint16_t, std::string> names,
                             EventLog* log, size_t total_targets, OutageOpenFn on_open)
    : cfg_(cfg), names_(std::move(names)), log_(log),
      total_targets_(total_targets), on_open_(std::move(on_open)) {}

size_t EventDetector::count_failing() const {
    size_t n = 0;
    for (const auto& [tid, st] : state_)
        if (st.in_outage || st.consec > 0) ++n;
    return n;
}

void EventDetector::reset_after_gap() {
    for (auto& [tid, st] : state_)
        if (!st.in_outage) st.consec = 0;
}

void EventDetector::add(const ProbeSample& s) {
    State& st = state_[s.target_id];
    st.last_utc = s.utc_ms;
    const bool failed = (s.result.status != Status::Ok);

    if (st.in_outage) {
        ++st.sent;
        if (failed) {
            ++st.lost;
            st.consec++;
        } else {
            // First success ends the outage.
            close_outage(st, s.target_id, s.utc_ms);
        }
        return;
    }

    if (!failed) {
        st.consec = 0;
        return;
    }

    // A failure while healthy.
    if (st.consec == 0) st.cand_start = s.utc_ms;
    ++st.consec;
    if (st.consec >= cfg_.fail_threshold) {
        st.in_outage = true;
        st.start     = st.cand_start;
        st.sent      = static_cast<uint64_t>(st.consec);  // the threshold probes, all failures
        st.lost      = static_cast<uint64_t>(st.consec);
        // If a large fraction of all targets is failing at once, the fault is
        // shared (our uplink / monitor), not the individual host. We don't
        // require literally ALL targets: the LAN gateway (and other near hops)
        // usually stay up during an uplink drop, so >=75% simultaneous is local.
        const bool widespread =
            total_targets_ > 2 && count_failing() * 100 >= total_targets_ * 75;
        st.type      = widespread ? "local" : "host";
        st.trace_ref.clear();
        if (cfg_.trigger_traceroute) {
            st.trace_ref = "traces/" + std::to_string(st.start) + "_" +
                           std::to_string(s.target_id) + ".txt";
            if (on_open_) on_open_(st.start, s.target_id);
        }
    }
}

void EventDetector::close_outage(State& st, uint16_t tid, uint64_t end_utc) {
    const uint64_t duration = (end_utc > st.start) ? (end_utc - st.start) : 0;
    if (duration >= static_cast<uint64_t>(cfg_.min_outage_ms)) {
        OutageEvent e;
        e.event_id       = log_->next_id();
        e.target_id      = tid;
        e.target         = names_.count(tid) ? names_[tid] : std::string();
        e.start_utc      = st.start;
        e.end_utc        = end_utc;
        e.sent_in_window = st.sent;
        e.lost           = st.lost;
        e.type           = st.type;
        e.trace_ref      = st.trace_ref;
        log_->append(e);
        log_info("outage: " + e.target + " " + std::to_string(duration / 1000) + "s lost=" +
                 std::to_string(e.lost));
    }
    st.in_outage = false;
    st.consec    = 0;
    st.sent      = 0;
    st.lost      = 0;
    st.type      = "host";
}

void EventDetector::finalize(uint64_t now_utc) {
    for (auto& [tid, st] : state_) {
        if (st.in_outage) {
            const uint64_t end = (now_utc > st.last_utc) ? now_utc : st.last_utc;
            close_outage(st, tid, end);
        }
    }
}

}  // namespace pt
