#include "storage/address_registry.hpp"

#include "prober/icmp_probe.hpp"  // ipv4_to_string
#include "util/log.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace pt {

namespace {
uint32_t parse_ipv4(const std::string& s) {
    in_addr a{};
    if (inet_pton(AF_INET, s.c_str(), &a) == 1) return a.s_addr;
    return 0;
}
}  // namespace

AddressRegistry::AddressRegistry(std::string storage_dir)
    : path_(std::move(storage_dir) + "/addresses.csv") {
    load();
}

void AddressRegistry::load() {
    std::ifstream f(path_);
    if (!f) return;
    std::string line;
    bool first = true;
    while (std::getline(f, line)) {
        if (first) { first = false; continue; }  // header
        if (line.empty()) continue;
        std::stringstream ss(line);
        std::string id_s, ip_s;
        std::getline(ss, id_s, ',');
        std::getline(ss, ip_s, ',');
        const uint16_t id = static_cast<uint16_t>(std::strtoul(id_s.c_str(), nullptr, 10));
        const uint32_t ip = parse_ipv4(ip_s);
        if (id == 0 || ip == 0) continue;
        by_addr_[ip] = id;
        by_id_[id] = ip;
        next_id_ = std::max<uint16_t>(next_id_, static_cast<uint16_t>(id + 1));
    }
}

uint16_t AddressRegistry::id_for(uint32_t addr) {
    if (addr == 0) return 0;
    if (auto it = by_addr_.find(addr); it != by_addr_.end()) return it->second;

    const uint16_t id = next_id_++;
    by_addr_[addr] = id;
    by_id_[id] = addr;

    const bool fresh = !std::filesystem::exists(path_);
    std::ofstream f(path_, std::ios::app);
    if (f) {
        if (fresh) f << "id,ip\n";
        f << id << ',' << ipv4_to_string(addr) << "\n";
    } else {
        log_warn("address registry: cannot persist to '" + path_ + "'");
    }
    return id;
}

uint32_t AddressRegistry::addr_for(uint16_t id) const {
    if (auto it = by_id_.find(id); it != by_id_.end()) return it->second;
    return 0;
}

}  // namespace pt
