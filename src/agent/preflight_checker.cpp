#include "agent/preflight_checker.hpp"
#include <filesystem>
#include <algorithm>

namespace agent {

PreflightChecker::CheckResult PreflightChecker::check(
        const AtomicTask& task, const TaskContext& ctx) const {

    CheckResult result;
    result.should_proceed = true;

    // 检查路径有效性
    if (task.tool == "read_file" || task.tool == "write_file") {
        if (!task.input.empty() && !check_path_validity(task.input)) {
            result.should_proceed = false;
            result.warning = "Path may not exist or be accessible: " + task.input;
            result.suggestion = "Verify path exists or use list_dir first";
            return result;
        }
    }

    // 检查目标对齐
    if (!check_goal_alignment(task, ctx)) {
        result.should_proceed = false;
        result.warning = "Task seems unrelated to original goal";
        result.suggestion = "Review task decomposition";
        return result;
    }

    // 检查与已完成步骤的冲突
    if (check_conflict_with_completed(task, ctx)) {
        result.should_proceed = false;
        result.warning = "Task conflicts with completed steps";
        result.suggestion = "Skip redundant operation";
        return result;
    }

    return result;
}

bool PreflightChecker::check_path_validity(const std::string& path) const {
    if (path.empty() || path.find("<") != std::string::npos)
        return false;

    // 简单检查：如果是绝对路径，验证父目录存在
    std::filesystem::path p(path);
    if (p.is_absolute() && p.has_parent_path()) {
        return std::filesystem::exists(p.parent_path());
    }

    return true;
}

bool PreflightChecker::check_goal_alignment(
        const AtomicTask& task, const TaskContext& ctx) const {

    if (ctx.original_goal.empty())
        return true;

    // 简单启发式：检查任务描述是否包含目标关键词
    std::string lower_goal, lower_desc;
    for (char c : ctx.original_goal)
        lower_goal.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    for (char c : task.description)
        lower_desc.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));

    // 提取关键词（简化版）
    std::vector<std::string> keywords;
    std::istringstream iss(lower_goal);
    std::string word;
    while (iss >> word) {
        if (word.length() > 3)
            keywords.push_back(word);
    }

    // 至少匹配一个关键词
    for (const auto& kw : keywords) {
        if (lower_desc.find(kw) != std::string::npos)
            return true;
    }

    return keywords.empty(); // 如果没有关键词，默认通过
}

bool PreflightChecker::check_conflict_with_completed(
        const AtomicTask& task, const TaskContext& ctx) const {

    // 检查是否重复执行相同操作
    for (const auto& step : ctx.completed_steps) {
        std::string lower_step, lower_desc;
        for (char c : step)
            lower_step.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        for (char c : task.description)
            lower_desc.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));

        // 如果描述高度相似，可能是重复
        if (lower_step.find(lower_desc) != std::string::npos ||
            lower_desc.find(lower_step) != std::string::npos) {
            return true;
        }
    }

    return false;
}

} // namespace agent
