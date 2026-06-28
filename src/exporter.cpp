#include "exporter.hpp"

#include "prober/icmp_probe.hpp"  // ipv4_to_string
#include "storage/address_registry.hpp"
#include "storage/index.hpp"
#include "storage/segment_reader.hpp"
#include "util/log.hpp"
#include "util/time.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>
#include <vector>

namespace pt {

namespace {

// True if a target (by id/name) passes the filter (empty filter = pass all).
bool matches(const std::string& filter, uint16_t id, const std::string& name) {
    if (filter.empty()) return true;
    if (filter == name) return true;
    return filter == std::to_string(id);
}

}  // namespace

int run_export(const Config& cfg, uint64_t from_utc, uint64_t to_utc,
               const std::string& target_filter, const std::string& out_path) {
    Index index(cfg.storage.dir);
    auto entries = index.load();
    if (entries.empty()) {
        // Fall back to a scan if the index is missing.
        index.rebuild();
        entries = index.load();
    }
    std::sort(entries.begin(), entries.end(),
              [](const IndexEntry& a, const IndexEntry& b) { return a.start_utc < b.start_utc; });

    AddressRegistry registry(cfg.storage.dir);  // resolves src_id -> router ip

    std::ofstream fout;
    if (!out_path.empty()) {
        fout.open(out_path, std::ios::trunc);
        if (!fout) {
            log_error("export: cannot open '" + out_path + "'");
            return 1;
        }
    }
    std::ostream& out = out_path.empty() ? std::cout : fout;
    out << "utc_iso,utc_ms,target_id,target,status,rtt_ms,reply_ttl,src_ip\n";

    uint64_t written = 0;
    for (const auto& e : entries) {
        if (e.end_utc < from_utc || e.start_utc > to_utc) continue;  // no overlap

        std::vector<uint8_t> bytes;
        if (!load_segment_bytes(cfg.storage.dir + "/" + e.file, bytes)) continue;
        SegmentHeader hdr;
        if (!parse_header(bytes, hdr)) continue;

        std::map<uint16_t, std::string> name_of;
        for (const auto& a : hdr.addrs) name_of[a.id] = a.name;

        std::vector<RawRecord> recs;
        decode_records(bytes, hdr, recs);
        for (const auto& r : recs) {
            const uint64_t utc = hdr.start_utc_ms + r.t_offset_ms;
            if (utc < from_utc || utc > to_utc) continue;
            const std::string name = name_of.count(r.target_id) ? name_of[r.target_id] : std::string();
            if (!matches(target_filter, r.target_id, name)) continue;

            std::string rtt;
            if (r.rtt_tenths_ms != kRttNa) {
                char b[16];
                std::snprintf(b, sizeof(b), "%.1f", r.rtt_tenths_ms / 10.0);
                rtt = b;
            }
            // src_id resolves to the router that returned the ICMP error.
            std::string src;
            if (r.src_id != 0) {
                const uint32_t a = registry.addr_for(r.src_id);
                if (a != 0) src = ipv4_to_string(a);
            }

            out << format_utc_ms(utc) << ',' << utc << ',' << r.target_id << ',' << name << ','
                << to_string(static_cast<Status>(r.status)) << ',' << rtt << ','
                << static_cast<unsigned>(r.reply_ttl) << ',' << src << "\n";
            ++written;
        }
    }

    if (!out_path.empty())
        log_info("exported " + std::to_string(written) + " record(s) -> " + out_path);
    else
        log_info("exported " + std::to_string(written) + " record(s)");
    return 0;
}

}  // namespace pt
