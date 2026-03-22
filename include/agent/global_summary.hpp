#pragma once
#include <string>
#include <vector>
#include <mutex>

namespace agent {

struct SummaryEntry {
    std::string key;
    std::string value;
    int priority;  // 0=low, 1=normal, 2=high
};

class GlobalSummary {
public:
    void set(const std::string& key, const std::string& value, int priority = 1);
    std::string get(const std::string& key) const;
    std::string build_context(int max_entries = 5) const;
    void clear_low_priority();

private:
    mutable std::mutex mtx_;
    std::vector<SummaryEntry> entries_;
};

} // namespace agent
