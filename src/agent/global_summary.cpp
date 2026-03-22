#include "agent/global_summary.hpp"
#include <algorithm>

namespace agent {

void GlobalSummary::set(const std::string& key, const std::string& value, int priority) {
    std::lock_guard<std::mutex> lock(mtx_);

    // Update existing or add new
    for (auto& e : entries_) {
        if (e.key == key) {
            e.value = value;
            e.priority = priority;
            return;
        }
    }
    entries_.push_back({key, value, priority});
}

std::string GlobalSummary::get(const std::string& key) const {
    std::lock_guard<std::mutex> lock(mtx_);
    for (const auto& e : entries_) {
        if (e.key == key) return e.value;
    }
    return "";
}

std::string GlobalSummary::build_context(int max_entries) const {
    std::lock_guard<std::mutex> lock(mtx_);

    // Sort by priority (high first)
    std::vector<SummaryEntry> sorted = entries_;
    std::sort(sorted.begin(), sorted.end(),
        [](const SummaryEntry& a, const SummaryEntry& b) {
            return a.priority > b.priority;
        });

    std::string ctx;
    int count = 0;
    for (const auto& e : sorted) {
        if (count++ >= max_entries) break;
        ctx += "[" + e.key + ": " + e.value + "] ";
    }

    return ctx.empty() ? "" : "[Global] " + ctx;
}

void GlobalSummary::clear_low_priority() {
    std::lock_guard<std::mutex> lock(mtx_);
    entries_.erase(
        std::remove_if(entries_.begin(), entries_.end(),
            [](const SummaryEntry& e) { return e.priority == 0; }),
        entries_.end());
}

} // namespace agent
