#include "scheduler.hpp"

#include <limits>

namespace pt {

void Scheduler::add(size_t idx, double interval_ms, double now) {
    heap_.push(Entry{now, interval_ms, idx});
}

double Scheduler::earliest() const {
    if (heap_.empty()) return std::numeric_limits<double>::infinity();
    return heap_.top().due;
}

void Scheduler::collect_due(double now, std::vector<size_t>& out) {
    while (!heap_.empty() && heap_.top().due <= now) {
        Entry e = heap_.top();
        heap_.pop();
        out.push_back(e.idx);

        e.due += e.interval;
        if (e.due <= now) {
            // Fell behind by at least one interval; skip missed slots.
            e.due = now + e.interval;
        }
        heap_.push(e);
    }
}

}  // namespace pt
