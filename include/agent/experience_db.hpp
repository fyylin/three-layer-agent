#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <mutex>

namespace agent {

class ExperienceDB {
public:
    struct FailurePattern {
        std::string task_pattern;
        std::string error_pattern;
        std::string solution;
        int success_count = 0;
        int total_attempts = 0;
    };

    void record_failure(const std::string& task, const std::string& error);
    void record_success(const std::string& task, const std::string& solution);
    std::optional<std::string> query_solution(const std::string& task) const;

    std::vector<FailurePattern> get_all_patterns() const;
    void clear();

private:
    mutable std::mutex mu_;
    std::unordered_map<std::string, FailurePattern> patterns_;

    std::string normalize_task(const std::string& task) const;
    std::string extract_pattern(const std::string& text) const;
};

} // namespace agent
