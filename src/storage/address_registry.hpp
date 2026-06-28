#pragma once

#include <cstdint>
#include <map>
#include <string>

namespace pt {

// Persistent IPv4 <-> small-id map for ICMP responder addresses (the routers
// that send Time-Exceeded / Unreachable). Raw records store the id as `src_id`;
// readers resolve it back to an address. Backed by addresses.csv in the storage
// dir, shared across all segments so ids are stable over time.
class AddressRegistry {
public:
    explicit AddressRegistry(std::string storage_dir);

    // Returns the id for `addr` (network order), assigning and persisting a new
    // one on first sight. addr 0 maps to id 0.
    uint16_t id_for(uint32_t addr);

    // Resolves an id back to an address (network order); 0 if unknown.
    uint32_t addr_for(uint16_t id) const;

private:
    void load();

    std::string                  path_;
    std::map<uint32_t, uint16_t> by_addr_;
    std::map<uint16_t, uint32_t> by_id_;
    uint16_t                     next_id_ = 1;
};

}  // namespace pt
