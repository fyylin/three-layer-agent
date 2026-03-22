#include "agent/experience_db.hpp"
#include <algorithm>
#include <regex>

namespace agent {

void ExperienceDB::record_failure(
        const std::string& task, const std::string& error) {

    std::lock_guard<std::mutex> lk(mu_);

    std::string pattern = extract_pattern(task);
    auto& fp = patterns_[pattern];
    fp.task_pattern = pattern;
    fp.error_pattern = extract_pattern(error);
    ++fp.total_attempts;
}

void ExperienceDB::record_success(
        const std::string& task, const std::string& solution) {

    std::lock_guard<std::mutex> lk(mu_);

    std::string pattern = extract_pattern(task);
    auto& fp = patterns_[pattern];
    fp.task_pattern = pattern;
    fp.solution = solution;
    ++fp.success_count;
    ++fp.total_attempts;
}

std::optional<std::string> ExperienceDB::query_solution(
        const std::string& task) const {

    std::lock_guard<std::mutex> lk(mu_);

    std::string pattern = extract_pattern(task);
    auto it = patterns_.find(pattern);

    if (it != patterns_.end() && !it->second.solution.empty()) {
        // 只返回成功率 > 50% 的解决方案
        if (it->second.success_count * 2 > it->second.total_attempts) {
            return it->second.solution;
        }
    }

    return std::nullopt;
}

std::vector<ExperienceDB::FailurePattern> ExperienceDB::get_all_patterns() const {
    std::lock_guard<std::mutex> lk(mu_);

    std::vector<FailurePattern> result;
    for (const auto& [_, fp] : patterns_) {
        result.push_back(fp);
    }

    return result;
}

void ExperienceDB::clear() {
    std::lock_guard<std::mutex> lk(mu_);
    patterns_.clear();
}

std::string ExperienceDB::normalize_task(const std::string& task) const {
    std::string result;
    for (char c : task) {
        result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return result;
}

std::string ExperienceDB::extract_pattern(const std::string& text) const {
    // 提取关键词作为模式
    std::string lower = normalize_task(text);

    // 移除路径和具体值
    std::regex path_regex(R"([/\\][^\s]+)");
    lower = std::regex_replace(lower, path_regex, "<PATH>");

    std::regex num_regex(R"(\d+)");
    lower = std::regex_replace(lower, num_regex, "<NUM>");

    // 提取前 50 个字符作为模式
    if (lower.length() > 50)
        lower = lower.substr(0, 50);

    return lower;
}

} // namespace agent
