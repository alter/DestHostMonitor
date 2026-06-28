#include "storage/index.hpp"

#include "storage/segment_reader.hpp"
#include "util/log.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace pt {

namespace {
constexpr char kHeader[] = "start_utc,end_utc,file,record_count,targets";

std::string join_target_ids(const SegmentHeader& hdr) {
    std::string s;
    for (const auto& a : hdr.addrs) {
        if (!s.empty()) s += ';';
        s += std::to_string(a.id);
    }
    return s;
}
}  // namespace

void Index::append(const IndexEntry& e) {
    const bool fresh = !std::filesystem::exists(path());
    std::ofstream f(path(), std::ios::app);
    if (!f) {
        log_error("cannot open index '" + path() + "' for append");
        return;
    }
    if (fresh) f << kHeader << "\n";
    f << e.start_utc << ',' << e.end_utc << ',' << e.file << ','
      << e.record_count << ',' << e.targets << "\n";
}

std::vector<IndexEntry> Index::load() const {
    std::vector<IndexEntry> out;
    std::ifstream f(path());
    if (!f) return out;

    std::string line;
    bool first = true;
    while (std::getline(f, line)) {
        if (first) { first = false; continue; }  // skip header
        if (line.empty()) continue;
        std::stringstream ss(line);
        std::string col;
        IndexEntry e;
        std::getline(ss, col, ','); e.start_utc    = std::strtoull(col.c_str(), nullptr, 10);
        std::getline(ss, col, ','); e.end_utc      = std::strtoull(col.c_str(), nullptr, 10);
        std::getline(ss, e.file, ',');
        std::getline(ss, col, ','); e.record_count = std::strtoull(col.c_str(), nullptr, 10);
        std::getline(ss, e.targets, ',');
        out.push_back(std::move(e));
    }
    return out;
}

size_t Index::rebuild() {
    namespace fs = std::filesystem;
    std::vector<IndexEntry> entries;

    std::error_code ec;
    for (const auto& de : fs::directory_iterator(dir_, ec)) {
        if (ec) break;
        if (!de.is_regular_file()) continue;
        const std::string name = de.path().filename().string();
        if (name.size() < 5 || name.compare(name.size() - 4, 4, ".zst") != 0) continue;
        if (name.compare(0, 4, "seg_") != 0) continue;

        std::vector<uint8_t> bytes;
        if (!load_segment_bytes(de.path().string(), bytes)) continue;
        SegmentHeader hdr;
        if (!parse_header(bytes, hdr)) continue;

        std::vector<RawRecord> recs;
        decode_records(bytes, hdr, recs);
        uint64_t end = hdr.start_utc_ms;
        for (const auto& r : recs) end = std::max(end, hdr.start_utc_ms + r.t_offset_ms);

        IndexEntry e;
        e.start_utc    = hdr.start_utc_ms;
        e.end_utc      = end;
        e.file         = name;
        e.record_count = recs.size();
        e.targets      = join_target_ids(hdr);
        entries.push_back(std::move(e));
    }

    std::sort(entries.begin(), entries.end(),
              [](const IndexEntry& a, const IndexEntry& b) { return a.start_utc < b.start_utc; });

    std::ofstream f(path(), std::ios::trunc);
    if (!f) {
        log_error("cannot rewrite index '" + path() + "'");
        return 0;
    }
    f << kHeader << "\n";
    for (const auto& e : entries) {
        f << e.start_utc << ',' << e.end_utc << ',' << e.file << ','
          << e.record_count << ',' << e.targets << "\n";
    }
    return entries.size();
}

}  // namespace pt
