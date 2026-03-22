#pragma once
#include <string>
#include <map>
#include <vector>
#include <mutex>
#include <chrono>
#include <algorithm>

namespace agent {

struct ToolStats {
    int total_calls = 0;
    int successes = 0;
    int failures = 0;
    double total_duration_ms = 0.0;
    std::chrono::system_clock::time_point last_used;

    double success_rate() const {
        return total_calls > 0 ? (double)successes / total_calls : 0.0;
    }

    double avg_duration_ms() const {
        return total_calls > 0 ? total_duration_ms / total_calls : 0.0;
    }
};

class ToolStatsTracker {
public:
    void record_call(const std::string& tool, bool success, double duration_ms) {
        std::lock_guard<std::mutex> lk(mu_);
        auto& stats = stats_[tool];
        ++stats.total_calls;
        if (success) ++stats.successes;
        else ++stats.failures;
        stats.total_duration_ms += duration_ms;
        stats.last_used = std::chrono::system_clock::now();
    }

    ToolStats get_stats(const std::string& tool) const {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = stats_.find(tool);
        return it != stats_.end() ? it->second : ToolStats{};
    }

    std::map<std::string, ToolStats> get_all_stats() const {
        std::lock_guard<std::mutex> lk(mu_);
        return stats_;
    }

    std::vector<std::string> recommend_tools(double min_success_rate = 0.7, int min_calls = 3) const {
        std::lock_guard<std::mutex> lk(mu_);
        std::vector<std::pair<std::string, double>> candidates;
        for (auto& [tool, stats] : stats_) {
            if (stats.total_calls >= min_calls && stats.success_rate() >= min_success_rate)
                candidates.push_back({tool, stats.success_rate()});
        }
        std::sort(candidates.begin(), candidates.end(),
            [](auto& a, auto& b) { return a.second > b.second; });
        std::vector<std::string> result;
        for (auto& [tool, _] : candidates) result.push_back(tool);
        return result;
    }

    std::string get_best_tool_for_task(const std::vector<std::string>& candidates) const {
        std::lock_guard<std::mutex> lk(mu_);
        std::string best;
        double best_score = -1.0;
        for (auto& tool : candidates) {
            auto it = stats_.find(tool);
            if (it == stats_.end() || it->second.total_calls < 2) continue;
            double score = it->second.success_rate() * 0.7 +
                          (1.0 / (1.0 + it->second.avg_duration_ms() / 1000.0)) * 0.3;
            if (score > best_score) {
                best_score = score;
                best = tool;
            }
        }
        return best;
    }

private:
    mutable std::mutex mu_;
    std::map<std::string, ToolStats> stats_;
};

} // namespace agent
