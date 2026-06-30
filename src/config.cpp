#include "config.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <stdexcept>

namespace pt {

using nlohmann::json;

namespace {

Proto parse_proto(const std::string& s) {
    if (s == "icmp") return Proto::Icmp;
    if (s == "tcp")  return Proto::Tcp;
    throw std::runtime_error("unknown proto '" + s + "' (expected icmp|tcp)");
}

}  // namespace

Config load_config(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("cannot open config file: " + path);
    }

    json j;
    try {
        in >> j;
    } catch (const json::parse_error& e) {
        throw std::runtime_error("config JSON parse error: " + std::string(e.what()));
    }

    Config cfg;

    if (auto it = j.find("storage"); it != j.end()) {
        const auto& s = *it;
        cfg.storage.dir                = s.value("dir", cfg.storage.dir);
        cfg.storage.segment_minutes    = s.value("segment_minutes", cfg.storage.segment_minutes);
        cfg.storage.raw_retention_days = s.value("raw_retention_days", cfg.storage.raw_retention_days);
    }

    if (auto it = j.find("defaults"); it != j.end()) {
        const auto& d = *it;
        cfg.default_interval_ms = d.value("interval_ms", cfg.default_interval_ms);
        cfg.default_timeout_ms  = d.value("timeout_ms", cfg.default_timeout_ms);
    }

    if (auto it = j.find("events"); it != j.end()) {
        const auto& e = *it;
        cfg.events.fail_threshold     = e.value("fail_threshold", cfg.events.fail_threshold);
        cfg.events.min_outage_ms      = e.value("min_outage_ms", cfg.events.min_outage_ms);
        cfg.events.trigger_traceroute = e.value("trigger_traceroute", cfg.events.trigger_traceroute);
    }

    const auto tj = j.find("targets");
    if (tj == j.end() || !tj->is_array() || tj->empty()) {
        throw std::runtime_error("config has no 'targets' array");
    }

    uint16_t next_id = 1;
    for (const auto& t : *tj) {
        Target tg;
        tg.id          = next_id++;
        tg.name        = t.value("name", std::string{});
        tg.address     = t.value("address", std::string{});
        if (tg.address.empty()) {
            throw std::runtime_error("target '" + tg.name + "' has empty address");
        }
        if (tg.name.empty()) tg.name = tg.address;
        tg.proto       = parse_proto(t.value("proto", std::string{"icmp"}));
        tg.port        = t.value("port", uint16_t{0});
        tg.interval_ms = t.value("interval_ms", cfg.default_interval_ms);
        tg.timeout_ms  = t.value("timeout_ms", cfg.default_timeout_ms);
        tg.path_group  = t.value("path_group", std::string{});
        tg.hop_index   = t.value("hop_index", -1);
        tg.probe       = t.value("probe", true);
        tg.ttl         = static_cast<uint8_t>(t.value("ttl", 0));
        tg.aim         = t.value("aim", std::string{});
        tg.group       = t.value("group", std::string{});
        if (tg.ttl != 0 && tg.aim.empty()) {
            throw std::runtime_error("ttl target '" + tg.name + "' requires an 'aim' address");
        }

        if (tg.proto == Proto::Tcp && tg.port == 0) {
            throw std::runtime_error("tcp target '" + tg.name + "' requires a port");
        }
        cfg.targets.push_back(std::move(tg));
    }

    return cfg;
}

}  // namespace pt
