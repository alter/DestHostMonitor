#pragma once

#include "config.hpp"
#include "types.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <string>

namespace pt {

// A detected outage, one row of events.csv (plan §4).
struct OutageEvent {
    uint64_t    event_id      = 0;
    uint16_t    target_id     = 0;
    std::string target;
    uint64_t    start_utc     = 0;
    uint64_t    end_utc       = 0;
    uint64_t    sent_in_window = 0;
    uint64_t    lost          = 0;
    std::string type     = "host";  // host | local | monitor_gap (refined in stage 7)
    std::string trace_ref;          // filled by trace-on-event in stage 6
};

// Appends outages to events.csv, continuing event-id numbering across restarts.
class EventLog {
public:
    explicit EventLog(const std::string& storage_dir);
    uint64_t next_id() { return ++counter_; }
    void append(const OutageEvent& e);
    const std::string& path() const { return path_; }

private:
    std::string path_;
    uint64_t    counter_ = 0;
};

// Called when an outage is confirmed open, to kick off a one-shot traceroute.
using OutageOpenFn = std::function<void(uint64_t start_utc, uint16_t target_id)>;

// Online outage detector. An outage opens after `fail_threshold` consecutive
// failures (back-dated to the first of them) and closes on the next success;
// it is only logged if it lasted at least `min_outage_ms`. On open it fires
// `on_open` (trace-on-event) when trigger_traceroute is set.
class EventDetector {
public:
    EventDetector(const EventsConfig& cfg, std::map<uint16_t, std::string> names,
                  EventLog* log, size_t total_targets, OutageOpenFn on_open = nullptr);

    void add(const ProbeSample& s);

    // Clears pending failure counters after a monitor gap so pre-/post-gap
    // failures don't merge into a false outage.
    void reset_after_gap();

    // Closes any still-open outages (call on shutdown). `now_utc` ends them.
    void finalize(uint64_t now_utc);

private:
    struct State {
        bool        in_outage = false;
        int         consec    = 0;   // consecutive failures
        uint64_t    cand_start = 0;  // utc of the first failure in the current run
        uint64_t    start     = 0;   // confirmed outage start
        uint64_t    last_utc  = 0;   // most recent sample time seen
        uint64_t    sent      = 0;   // probes since outage start
        uint64_t    lost      = 0;
        std::string trace_ref;       // traces/<start>.txt when a trace was launched
        std::string type      = "host";
    };

    // Count of targets currently failing (in an outage or mid-failure-run).
    size_t count_failing() const;
    void close_outage(State& st, uint16_t tid, uint64_t end_utc);

    EventsConfig                    cfg_;
    std::map<uint16_t, std::string> names_;
    EventLog*                       log_;
    size_t                          total_targets_;
    OutageOpenFn                    on_open_;
    std::map<uint16_t, State>       state_;
};

}  // namespace pt
