#include "agent/task_router.hpp"

#include <algorithm>
#include <cctype>
#include <regex>
#include <string>
#include <vector>

namespace agent {

namespace {

std::string ascii_lower_copy(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (unsigned char c : text)
        out.push_back(static_cast<char>(std::tolower(c)));
    return out;
}

bool contains_any(const std::string& text, const std::vector<std::string>& needles) {
    return std::any_of(needles.begin(), needles.end(), [&](const std::string& needle) {
        return text.find(needle) != std::string::npos;
    });
}

bool contains_any_utf8(const std::string& text, const std::vector<std::string>& needles) {
    return contains_any(text, needles);
}

bool has_file_reference(const std::string& goal) {
    static const std::regex kFilePattern(
        R"(([A-Za-z0-9_\-./\\]+\.(json|txt|md|py|cpp|hpp|h|js|ts|yaml|yml|toml|ini|log|csv)))",
        std::regex::icase);
    return std::regex_search(goal, kFilePattern);
}

bool has_multi_action_signal(const std::string& goal, const std::string& lower_goal) {
    static const std::vector<std::string> kMultiStepEn = {
        " and ", " then ", " after ", " next ", "count ", "count files", "stat "
    };
    static const std::vector<std::string> kMultiStepZh = {
        u8"并", u8"然后", u8"接着", u8"之后", u8"统计"
    };
    return contains_any(lower_goal, kMultiStepEn) || contains_any_utf8(goal, kMultiStepZh);
}

} // namespace

TaskRouter::RouteDecision TaskRouter::analyze(
        const std::string& goal,
        const std::vector<std::string>& available_tools) const {
    if (is_single_tool_call(goal, available_tools))
        return RouteDecision::FastPath;

    if (is_simple_action(goal))
        return RouteDecision::ManagerPath;

    return RouteDecision::FullPath;
}

bool TaskRouter::is_single_tool_call(
        const std::string& goal,
        const std::vector<std::string>& tools) const {
    std::string lower_goal = ascii_lower_copy(goal);

    for (const auto& tool : tools) {
        if (lower_goal.find(ascii_lower_copy(tool)) != std::string::npos)
            return true;
    }

    static const std::vector<std::string> kReadVerbsEn = {
        "read", "open", "show", "view", "cat"
    };
    static const std::vector<std::string> kReadVerbsZh = {
        u8"读取", u8"读", u8"查看", u8"打开", u8"显示"
    };
    if (!has_multi_action_signal(goal, lower_goal) &&
        has_file_reference(goal) &&
        (contains_any(lower_goal, kReadVerbsEn) || contains_any_utf8(goal, kReadVerbsZh))) {
        return true;
    }

    static const std::vector<std::regex> kPatterns = {
        std::regex(R"(^(read|open|show|view|cat)\s+.+)", std::regex::icase),
        std::regex(R"(^(write|create)\s+.+)", std::regex::icase),
        std::regex(R"(^(list|ls|dir|pwd)\b.*)", std::regex::icase),
        std::regex(R"(^(find|search)\s+.+)", std::regex::icase),
        std::regex(R"(^(run|execute)\s+[^,]+$)", std::regex::icase)
    };
    for (const auto& pattern : kPatterns) {
        if (std::regex_search(lower_goal, pattern))
            return true;
    }

    static const std::vector<std::string> kZhSingleIntent = {
        u8"读取", u8"查看", u8"列出", u8"查找", u8"搜索", u8"运行", u8"执行"
    };
    return !has_multi_action_signal(goal, lower_goal) &&
           contains_any_utf8(goal, kZhSingleIntent);
}

bool TaskRouter::is_simple_action(const std::string& goal) const {
    std::string lower_goal = ascii_lower_copy(goal);

    static const std::vector<std::string> kComplexKeywordsEn = {
        "refactor", "rewrite", "design", "architecture",
        "implement", "develop", "analyze", "optimize"
    };
    static const std::vector<std::string> kComplexKeywordsZh = {
        u8"重构", u8"重写", u8"设计", u8"架构",
        u8"实现", u8"开发", u8"分析", u8"优化"
    };
    if (contains_any(lower_goal, kComplexKeywordsEn) ||
        contains_any_utf8(goal, kComplexKeywordsZh)) {
        return false;
    }

    int step_count = 0;
    static const std::vector<std::string> kStepMarkersEn = {
        "then", "next", "after", "finally", "and then", " and "
    };
    static const std::vector<std::string> kStepMarkersZh = {
        u8"并", u8"然后", u8"接着", u8"之后", u8"最后"
    };
    for (const auto& marker : kStepMarkersEn) {
        if (lower_goal.find(marker) != std::string::npos)
            ++step_count;
    }
    for (const auto& marker : kStepMarkersZh) {
        if (goal.find(marker) != std::string::npos)
            ++step_count;
    }

    return step_count <= 1;
}

} // namespace agent
