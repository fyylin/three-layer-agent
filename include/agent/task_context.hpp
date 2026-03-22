#pragma once
#include <string>
#include <vector>
#include <unordered_map>

namespace agent {

struct TaskContext {
    std::string original_goal;                              // 用户原始目标
    std::string current_subtask;                            // 当前子任务描述
    std::vector<std::string> completed_steps;               // 已完成步骤
    std::unordered_map<std::string, std::string> shared_state; // 共享状态

    // 添加已完成步骤
    void add_completed(const std::string& step) {
        completed_steps.push_back(step);
    }

    // 设置共享状态
    void set_state(const std::string& key, const std::string& value) {
        shared_state[key] = value;
    }

    // 获取共享状态
    std::string get_state(const std::string& key) const {
        auto it = shared_state.find(key);
        return it != shared_state.end() ? it->second : "";
    }

    // 生成上下文摘要（用于 LLM prompt）
    std::string to_prompt_context() const;
};

} // namespace agent
