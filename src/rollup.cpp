#include "rollup.hpp"

#include "util/log.hpp"
#include "util/time.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace pt {

namespace {
constexpr uint64_t kMinuteMs = 60000ULL;
constexpr char kHeader[] =
    "minute_utc,target_id,target,sent,lost,loss_pct,rtt_min,rtt_avg,rtt_p50,rtt_p95,rtt_max";

// Nearest-rank percentile over a sorted vector of tenths-of-ms RTTs, in ms.
double pct(const std::vector<uint16_t>& sorted, double p) {
    if (sorted.empty()) return 0.0;
    size_t idx = static_cast<size_t>(p * (sorted.size() - 1) + 0.5);
    if (idx >= sorted.size()) idx = sorted.size() - 1;
    return sorted[idx] / 10.0;
}
}  // namespace

RollupWriter::RollupWriter(std::string storage_dir, std::map<uint16_t, std::string> names)
    : dir_(std::move(storage_dir)), names_(std::move(names)) {}

void RollupWriter::add(const ProbeSample& s) {
    const uint64_t minute = (s.utc_ms / kMinuteMs) * kMinuteMs;
    Bucket& b = buckets_[minute][s.target_id];
    ++b.sent;
    if (s.result.status != Status::Ok) {
        ++b.lost;
    } else if (s.result.rtt_tenths_ms != kRttNa) {
        b.rtt.push_back(s.result.rtt_tenths_ms);
    }
}

void RollupWriter::flush_completed(uint64_t now_utc) {
    const uint64_t cur_minute = (now_utc / kMinuteMs) * kMinuteMs;
    for (auto it = buckets_.begin(); it != buckets_.end();) {
        if (it->first >= cur_minute) break;  // map ordered: rest are current/future
        for (auto& [tid, bucket] : it->second) write_minute(it->first, tid, bucket);
        it = buckets_.erase(it);
    }
}

void RollupWriter::finalize() {
    for (auto& [minute, targets] : buckets_)
        for (auto& [tid, bucket] : targets) write_minute(minute, tid, bucket);
    buckets_.clear();
}

std::string RollupWriter::path_for(uint64_t minute_utc) const {
    return dir_ + "/rollup/" + format_utc_date(minute_utc) + ".csv";
}

void RollupWriter::write_minute(uint64_t minute_utc, uint16_t target_id, Bucket& b) {
    const std::string path = path_for(minute_utc);
    const bool fresh = !std::filesystem::exists(path);
    if (fresh) {
        std::error_code ec;
        std::filesystem::create_directories(dir_ + "/rollup", ec);
    }
    std::ofstream f(path, std::ios::app);
    if (!f) {
        log_error("rollup: cannot open '" + path + "'");
        return;
    }
    if (fresh) f << kHeader << "\n";

    const double loss_pct = b.sent ? (100.0 * static_cast<double>(b.lost) / static_cast<double>(b.sent)) : 0.0;

    std::string rmin, ravg, rp50, rp95, rmax;
    if (!b.rtt.empty()) {
        std::sort(b.rtt.begin(), b.rtt.end());
        uint64_t sum = 0;
        for (uint16_t v : b.rtt) sum += v;
        const double avg = (static_cast<double>(sum) / b.rtt.size()) / 10.0;
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.1f", b.rtt.front() / 10.0);            rmin = buf;
        std::snprintf(buf, sizeof(buf), "%.1f", avg);                             ravg = buf;
        std::snprintf(buf, sizeof(buf), "%.1f", pct(b.rtt, 0.50));                rp50 = buf;
        std::snprintf(buf, sizeof(buf), "%.1f", pct(b.rtt, 0.95));                rp95 = buf;
        std::snprintf(buf, sizeof(buf), "%.1f", b.rtt.back() / 10.0);             rmax = buf;
    }

    const std::string& name = names_.count(target_id) ? names_[target_id] : std::string();
    char loss[16];
    std::snprintf(loss, sizeof(loss), "%.2f", loss_pct);

    f << minute_utc << ',' << target_id << ',' << name << ','
      << b.sent << ',' << b.lost << ',' << loss << ','
      << rmin << ',' << ravg << ',' << rp50 << ',' << rp95 << ',' << rmax << "\n";
}

}  // namespace pt
