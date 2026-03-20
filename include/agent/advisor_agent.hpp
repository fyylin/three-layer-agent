#pragma once
// =============================================================================
// include/agent/advisor_agent.hpp   --   全局顾问 Agent
//
// AdvisorAgent 在以下情况被接入：
//   - 任务连续失败超过阈值（默认2次）
//   - Supervisor 检测到明显的系统性错误（工具调用失败、路径问题等）
//   - 任何 Agent 请求建议时
//
// 顾问不执行任务，只分析问题并输出：
//   1. 根因分析（What went wrong）
//   2. 多种修正方案（Fix strategies，至少2种）
//   3. 推荐方案（Recommended）
//   4. 修正后的目标描述（Refined goal）
//
// 接入原则：
//   - 只在问题明显时介入，避免过度干预
//   - 给出的建议通过 Supervisor 的 correction 机制注入
//   - 自身不修改任何系统状态
// =============================================================================

#include "agent/models.hpp"
#include "agent/api_client.hpp"
#include <string>
#include <vector>

namespace agent {

struct AdviceResult {
    bool        should_retry    = true;  // 顾问认为是否值得重试
    std::string root_cause;              // 根因分析
    std::vector<std::string> strategies; // 修正方案列表
    std::string recommended;             // 推荐的修正方案
    std::string refined_goal;            // 修正后的目标（注入 Director）
};

class AdvisorAgent {
public:
    AdvisorAgent(std::string id,
                 ApiClient&  client,
                 std::string system_prompt = "");

    // 分析失败情况，返回修正建议
    // goal:          原始用户目标
    // error_history: 历次失败的错误信息列表（最近的在后）
    // context:       额外上下文（工具列表、环境信息等）
    [[nodiscard]] AdviceResult advise(
        const std::string&              goal,
        const std::vector<std::string>& error_history,
        const std::string&              context = "") const noexcept;

    [[nodiscard]] const std::string& id() const noexcept { return id_; }

private:
    std::string id_;
    ApiClient&  client_;
    std::string system_prompt_;

    [[nodiscard]] static std::string default_prompt();
};

} // namespace agent
