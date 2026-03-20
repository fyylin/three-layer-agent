#pragma once
// =============================================================================
// include/utils/task_rules.hpp  --  Rule-based task decomposition
// Bypasses LLM decomposition for high-frequency single-tool operations.
// =============================================================================
#include <string>
#include <vector>
#include <optional>
#include "agent/models.hpp"

namespace agent {

struct TaskRule {
    // Patterns: if any of these appear in the request → apply rule
    std::vector<std::string> cn_patterns;  // UTF-8 Chinese patterns
    std::vector<std::string> en_patterns;  // lowercase English patterns
    std::string tool;           // tool name to use
    std::string input_value;    // tool input (empty = "" for no-arg tools)
    std::string description;    // subtask description
    size_t max_chars;           // max request length (0 = no limit)
};

// Try to match a request against the rule set.
// Returns a pre-built SubTask if a rule matches, nullopt otherwise.
inline std::optional<SubTask> apply_task_rules(const std::string& request) {
    static const TaskRule RULES[] = {
        // ── Directory / Path ──
        {{"当前目录","当前路径","工作目录"},{"current dir","current directory","pwd","cwd","where am i"},
         "get_current_dir","","Get current working directory",20},
        {{"上级目录","上一级","父目录"},{"parent dir","parent directory","go up","list parent"},
         "list_dir","..","List parent directory contents",20},
        {{"查看目录","列出目录","目录内容","列出文件","列出当前"},{"list dir","list files","ls","dir ","show files","what files"},
         "list_dir",".","List current directory",25},
        // ── System Info ──
        {{"系统信息","系统状态","系统详情"},{"sysinfo","system info","system information","about this system"},
         "get_sysinfo","","Get system information",20},
        {{"进程列表","运行的进程","运行中的进程","当前进程"},{"process list","running processes","list processes","ps "},
         "get_process_list","","List running processes",20},
        // ── Tools ──
        {{"有哪些工具","可用工具","工具列表"},{"list tools","what tools","available tools","show tools"},
         "list_tools","","List available tools",20},
    };

    static const int kNumRules = sizeof(RULES) / sizeof(RULES[0]);

    // Compute request char count (Unicode code points)
    size_t nchars = 0;
    for (unsigned char c : request) if ((c & 0xC0) != 0x80) ++nchars;

    // Lowercase copy for English matching
    std::string lower;
    lower.reserve(request.size());
    for (unsigned char c : request) lower.push_back((char)std::tolower(c));

    for (int i = 0; i < kNumRules; ++i) {
        const auto& rule = RULES[i];
        if (rule.max_chars > 0 && nchars > rule.max_chars) continue;

        bool matched = false;
        for (const auto& pat : rule.cn_patterns)
            if (request.find(pat) != std::string::npos) { matched = true; break; }
        if (!matched) for (const auto& pat : rule.en_patterns)
            if (lower.find(pat) != std::string::npos) { matched = true; break; }

        if (matched) {
            SubTask st;
            st.id              = "rule-subtask-1";
            st.description     = "Use " + rule.tool +
                                 (rule.input_value.empty() ? "" : " to " + rule.description) +
                                 ". Input: " + (rule.input_value.empty() ? "" : rule.input_value);
            st.expected_output = rule.description + " result";
            return st;
        }
    }
    return std::nullopt;
}

}  // namespace agent
