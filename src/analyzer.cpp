#include "analyzer.hpp"

#include "util/log.hpp"
#include "util/time.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace pt {

namespace {

struct RollupRow {
    uint64_t    minute_utc = 0;
    uint16_t    target_id  = 0;
    std::string target;
    uint64_t    sent = 0, lost = 0;
    bool        has_rtt = false;
    double      rtt_min = 0, rtt_avg = 0, rtt_max = 0;
};

bool matches(const std::string& filter, uint16_t id, const std::string& name) {
    if (filter.empty()) return true;
    return filter == name || filter == std::to_string(id);
}

// Splits on commas, preserving trailing empty fields (unlike getline-based
// splitting, which drops them — important for rows ending in empty columns).
std::vector<std::string> split(const std::string& line) {
    std::vector<std::string> out;
    size_t start = 0;
    while (true) {
        const size_t comma = line.find(',', start);
        if (comma == std::string::npos) {
            out.push_back(line.substr(start));
            break;
        }
        out.push_back(line.substr(start, comma - start));
        start = comma + 1;
    }
    return out;
}

// Loads rollup rows in [from,to] (minute_utc) passing the target filter.
std::vector<RollupRow> load_rollups(const std::string& dir, const AnalyzeOptions& opt) {
    std::vector<RollupRow> rows;
    const std::string rdir = dir + "/rollup";
    std::error_code ec;
    if (!std::filesystem::exists(rdir, ec)) return rows;

    for (const auto& de : std::filesystem::directory_iterator(rdir, ec)) {
        if (ec) break;
        if (de.path().extension() != ".csv") continue;
        std::ifstream f(de.path());
        std::string line;
        bool first = true;
        while (std::getline(f, line)) {
            if (first) { first = false; continue; }
            if (line.empty()) continue;
            const auto c = split(line);
            if (c.size() < 11) continue;
            RollupRow r;
            r.minute_utc = std::strtoull(c[0].c_str(), nullptr, 10);
            if (r.minute_utc < opt.from_utc || r.minute_utc > opt.to_utc) continue;
            r.target_id = static_cast<uint16_t>(std::strtoul(c[1].c_str(), nullptr, 10));
            r.target    = c[2];
            if (!matches(opt.target_filter, r.target_id, r.target)) continue;
            r.sent = std::strtoull(c[3].c_str(), nullptr, 10);
            r.lost = std::strtoull(c[4].c_str(), nullptr, 10);
            if (!c[6].empty()) {
                r.has_rtt = true;
                r.rtt_min = std::atof(c[6].c_str());
                r.rtt_avg = std::atof(c[7].c_str());
                r.rtt_max = std::atof(c[10].c_str());
            }
            rows.push_back(std::move(r));
        }
    }
    return rows;
}

// Per-target aggregate over the window.
struct Agg {
    std::string name;
    uint64_t sent = 0, lost = 0, ok = 0;
    double   rtt_min = std::numeric_limits<double>::infinity();
    double   rtt_max = 0, rtt_wsum = 0;
    double   loss_pct() const { return sent ? 100.0 * lost / sent : 0.0; }
    double   rtt_avg()  const { return ok ? rtt_wsum / ok : 0.0; }
};

std::map<uint16_t, Agg> aggregate(const std::vector<RollupRow>& rows) {
    std::map<uint16_t, Agg> agg;
    for (const auto& r : rows) {
        Agg& a = agg[r.target_id];
        a.name = r.target;
        a.sent += r.sent;
        a.lost += r.lost;
        if (r.has_rtt) {
            const uint64_t ok = (r.sent > r.lost) ? (r.sent - r.lost) : 0;
            a.ok += ok;
            a.rtt_wsum += r.rtt_avg * static_cast<double>(ok);
            a.rtt_min = std::min(a.rtt_min, r.rtt_min);
            a.rtt_max = std::max(a.rtt_max, r.rtt_max);
        }
    }
    return agg;
}

void print_summary(const std::map<uint16_t, Agg>& agg) {
    std::printf("\n%-16s %8s %7s %7s %9s %9s %9s\n",
                "target", "sent", "lost", "loss%", "rtt_min", "rtt_avg", "rtt_max");
    std::printf("%s\n", std::string(70, '-').c_str());
    for (const auto& [id, a] : agg) {
        char rmin[16] = "-", ravg[16] = "-", rmax[16] = "-";
        if (a.ok) {
            std::snprintf(rmin, sizeof(rmin), "%.1f", a.rtt_min);
            std::snprintf(ravg, sizeof(ravg), "%.1f", a.rtt_avg());
            std::snprintf(rmax, sizeof(rmax), "%.1f", a.rtt_max);
        }
        std::printf("%-16s %8llu %7llu %6.2f%% %9s %9s %9s\n",
                    a.name.c_str(),
                    static_cast<unsigned long long>(a.sent),
                    static_cast<unsigned long long>(a.lost),
                    a.loss_pct(), rmin, ravg, rmax);
    }
}

// Ladder view: for each path_group, walk its hops in hop_index order and show
// loss/rtt, so you see at which hop loss first climbs. Targets configured in a
// group but without data yet (e.g. a freshly added hop) show '-'.
void print_ladders(const Config& cfg, const std::map<uint16_t, Agg>& agg) {
    std::map<std::string, const Agg*> by_name;
    for (const auto& [id, a] : agg) by_name[a.name] = &a;

    // "*" means "every path" (e.g. the home gateway, shared hop 0 of all paths);
    // it is not itself a group.
    std::vector<std::string> groups;  // first-seen order
    for (const auto& t : cfg.targets) {
        if (t.path_group.empty() || t.path_group == "*") continue;
        if (std::find(groups.begin(), groups.end(), t.path_group) == groups.end())
            groups.push_back(t.path_group);
    }
    if (groups.empty()) {
        std::printf("\nladders: (no targets have a path_group)\n");
        return;
    }

    for (const auto& g : groups) {
        std::vector<const Target*> hops;
        for (const auto& t : cfg.targets)
            if (t.path_group == g || t.path_group == "*") hops.push_back(&t);
        std::sort(hops.begin(), hops.end(), [](const Target* a, const Target* b) {
            if (a->hop_index != b->hop_index) return a->hop_index < b->hop_index;
            return a->name < b->name;
        });

        std::printf("\npath: %s\n", g.c_str());
        std::printf("  %-3s %-20s %8s %9s\n", "hop", "target", "loss%", "rtt_avg");
        double prev_loss = -1.0;
        for (const Target* t : hops) {
            char loss[16] = "-", ravg[16] = "-";
            const char* mark = "";
            if (!t->probe) {
                // Ladder label only (not probed); its real state is in the
                // per-event traceroutes. Don't affect the loss-climb baseline.
                std::snprintf(loss, sizeof(loss), "trace");
            } else if (auto it = by_name.find(t->name); it != by_name.end()) {
                const Agg* a = it->second;
                std::snprintf(loss, sizeof(loss), "%.2f%%", a->loss_pct());
                if (a->ok) std::snprintf(ravg, sizeof(ravg), "%.1f", a->rtt_avg());
                // Flag the first hop where loss jumps >=1% over the previous hop.
                if (prev_loss >= 0.0 && a->loss_pct() - prev_loss >= 1.0)
                    mark = "  <- loss climbs here";
                prev_loss = a->loss_pct();
            }
            std::printf("  %-3d %-20.20s %8s %9s%s\n",
                        t->hop_index, t->name.c_str(), loss, ravg, mark);
        }
    }
}

void print_by_hour(const std::vector<RollupRow>& rows) {
    std::array<uint64_t, 24> sent{}, lost{};
    for (const auto& r : rows) {
        const int h = local_hour_of(r.minute_utc);
        sent[h] += r.sent;
        lost[h] += r.lost;
    }
    std::printf("\nloss by hour of day (local):\n");
    for (int h = 0; h < 24; ++h) {
        const double loss = sent[h] ? 100.0 * lost[h] / sent[h] : 0.0;
        const int bars = static_cast<int>(loss / 2.0 + 0.5);  // 1 bar per 2%
        std::printf("  %02d:00 %6.2f%% %s\n", h, loss, std::string(bars, '#').c_str());
    }
}

void print_heatmap(const std::vector<RollupRow>& rows) {
    static const char* dow[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    std::array<std::array<uint64_t, 24>, 7> sent{}, lost{};
    for (const auto& r : rows) {
        const int d = local_dow_of(r.minute_utc);
        const int h = local_hour_of(r.minute_utc);
        sent[d][h] += r.sent;
        lost[d][h] += r.lost;
    }
    std::printf("\nloss heatmap (day x hour, local). legend: '.'<1%%  '-'<5%%  '+'<20%%  '#'>=20%%\n     ");
    for (int h = 0; h < 24; ++h) std::printf("%d", h % 10);
    std::printf("\n");
    for (int d = 0; d < 7; ++d) {
        std::printf("  %s ", dow[d]);
        for (int h = 0; h < 24; ++h) {
            char cell = ' ';
            if (sent[d][h]) {
                const double loss = 100.0 * lost[d][h] / sent[d][h];
                cell = loss < 1.0 ? '.' : loss < 5.0 ? '-' : loss < 20.0 ? '+' : '#';
            }
            std::printf("%c", cell);
        }
        std::printf("\n");
    }
}

void print_outages(const Config& cfg, const AnalyzeOptions& opt) {
    std::ifstream f(cfg.storage.dir + "/events.csv");
    std::printf("\noutages:\n");
    if (!f) { std::printf("  (no events.csv)\n"); return; }

    std::string line;
    bool first = true;
    size_t shown = 0;
    while (std::getline(f, line)) {
        if (first) { first = false; continue; }
        if (line.empty()) continue;
        const auto c = split(line);
        if (c.size() < 10) continue;
        const uint16_t tid    = static_cast<uint16_t>(std::strtoul(c[1].c_str(), nullptr, 10));
        const std::string tgt = c[2];
        const uint64_t start  = std::strtoull(c[3].c_str(), nullptr, 10);
        if (start < opt.from_utc || start > opt.to_utc) continue;
        if (!matches(opt.target_filter, tid, tgt)) continue;
        std::printf("  #%-4s %-16s %s  dur=%ss lost=%s type=%s%s\n",
                    c[0].c_str(), tgt.c_str(), format_local_ms(start).c_str(),
                    c[5].c_str(), c[7].c_str(), c[8].c_str(),
                    c[9].empty() ? "" : ("  trace=" + c[9]).c_str());
        ++shown;
    }
    if (shown == 0) std::printf("  (none in window)\n");
}

}  // namespace

int run_analyze(const Config& cfg, const AnalyzeOptions& opt) {
    const auto rows = load_rollups(cfg.storage.dir, opt);

    std::printf("window: %s .. %s (local)\n",
                format_local_ms(opt.from_utc).c_str(), format_local_ms(opt.to_utc).c_str());
    if (rows.empty()) {
        std::printf("(no rollup data in window)\n");
    } else {
        const auto agg = aggregate(rows);
        print_summary(agg);
        if (opt.by_hour) print_by_hour(rows);
        if (opt.heatmap) print_heatmap(rows);
        if (opt.ladder)  print_ladders(cfg, agg);
    }
    print_outages(cfg, opt);
    return 0;
}

}  // namespace pt
