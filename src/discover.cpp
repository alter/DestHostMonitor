#include "discover.hpp"

#include "prober/icmp_probe.hpp"
#include "prober/traceroute.hpp"
#include "util/log.hpp"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <fstream>
#include <set>
#include <string>

namespace pt {

using nlohmann::json;

int run_discover(const std::string& dest, bool add, const std::string& config_path) {
    const uint32_t addr = resolve_ipv4(dest);
    if (addr == 0) {
        log_error("cannot resolve '" + dest + "'");
        return 1;
    }
    std::printf("traceroute to %s (%s):\n", dest.c_str(), ipv4_to_string(addr).c_str());

    const auto hops = traceroute(addr);
    for (const auto& h : hops) {
        std::printf("  %2d  ", h.ttl);
        if (h.replies.empty()) {
            std::printf("*");
        } else {
            // One line may list several responders when ECMP splits the hop.
            for (size_t i = 0; i < h.replies.size(); ++i) {
                const auto& r = h.replies[i];
                char rtt[16];
                if (r.rtt_tenths == kRttNa) std::snprintf(rtt, sizeof(rtt), "-");
                else std::snprintf(rtt, sizeof(rtt), "%.1fms", r.rtt_tenths / 10.0);
                std::printf("%s%s (%s)", (i ? " , " : ""), ipv4_to_string(r.addr).c_str(), rtt);
            }
        }
        std::printf("%s\n", h.reached ? "  (dest)" : "");
    }

    if (!add) {
        std::printf("\nRe-run with --add to append the responding hops as targets.\n");
        return 0;
    }

    // Load the existing config and collect addresses already present.
    std::ifstream in(config_path);
    if (!in) {
        log_error("cannot open config '" + config_path + "' to add targets");
        return 1;
    }
    json j;
    try {
        in >> j;
    } catch (const json::parse_error& e) {
        log_error(std::string("config parse error: ") + e.what());
        return 1;
    }
    in.close();

    if (!j.contains("targets") || !j["targets"].is_array()) j["targets"] = json::array();
    std::set<std::string> existing;
    for (const auto& t : j["targets"]) {
        if (t.contains("address")) existing.insert(t["address"].get<std::string>());
    }

    int added = 0;
    for (const auto& h : hops) {
        int leg = 0;
        for (const auto& r : h.replies) {  // ECMP: a hop may have several responders
            if (r.addr == 0) continue;
            const std::string ip = ipv4_to_string(r.addr);
            if (existing.count(ip)) continue;

            const std::string suffix = h.replies.size() > 1
                ? ("-" + std::string(1, static_cast<char>('a' + leg))) : "";
            json t;
            t["name"]       = dest + "-hop" + std::to_string(h.ttl) + suffix;
            t["address"]    = ip;
            t["proto"]      = "icmp";
            t["path_group"] = dest;
            t["hop_index"]  = h.ttl;
            j["targets"].push_back(t);
            existing.insert(ip);
            ++added;
            ++leg;
        }
    }

    std::ofstream out(config_path, std::ios::trunc);
    if (!out) {
        log_error("cannot write config '" + config_path + "'");
        return 1;
    }
    out << j.dump(2) << "\n";
    log_info("added " + std::to_string(added) + " hop target(s) to " + config_path);
    return 0;
}

}  // namespace pt
