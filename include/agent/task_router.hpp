#pragma once
#include <string>
#include <vector>
#include <regex>

namespace agent {

class TaskRouter {
public:
    enum class RouteDecision {
        FastPath,      // 直接执行（单工具调用）
        ManagerPath,   // 跳过 Director，直接 Manager 分解
        FullPath       // 完整三层流程
    };

    RouteDecision analyze(const std::string& goal,
                          const std::vector<std::string>& available_tools) const;

private:
    bool is_single_tool_call(const std::string& goal,
                            const std::vector<std::string>& tools) const;
    bool is_simple_action(const std::string& goal) const;
};

} // namespace agent
