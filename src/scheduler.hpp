#pragma once

#include <cstddef>
#include <queue>
#include <vector>

namespace pt {

// Monotonic per-target scheduler driven by mono_ms() timestamps. Each target
// fires every `interval_ms`. Missed slots (e.g. after a long blocking probe)
// are skipped rather than fired in a burst.
class Scheduler {
public:
    // Registers a target index that fires every interval_ms, first due at `now`.
    void add(size_t idx, double interval_ms, double now);

    bool empty() const { return heap_.empty(); }

    // Monotonic time the next target is due, or +inf when empty.
    double earliest() const;

    // Appends indices of all targets due at or before `now` to `out` and
    // reschedules them.
    void collect_due(double now, std::vector<size_t>& out);

private:
    struct Entry {
        double due;
        double interval;
        size_t idx;
    };
    struct Later {
        bool operator()(const Entry& a, const Entry& b) const { return a.due > b.due; }
    };

    std::priority_queue<Entry, std::vector<Entry>, Later> heap_;
};

}  // namespace pt
