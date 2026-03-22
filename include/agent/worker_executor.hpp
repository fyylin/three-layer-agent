#pragma once
#include "agent/models.hpp"
#include "agent/api_client.hpp"
#include "agent/retry_policy.hpp"
#include "agent/circuit_breaker.hpp"
#include "agent/tool_registry.hpp"
#include <string>
#include <vector>

namespace agent {

class WorkerExecutor {
public:
    WorkerExecutor(ApiClient& client, ToolRegistry& registry, const std::string& system_prompt);

    AtomicResult execute_with_retry(const AtomicTask& task, int max_retries);
    std::string call_tool(const AtomicTask& task) const;

private:
    ApiClient& client_;
    ToolRegistry& registry_;
    std::string system_prompt_;
    RetryPolicy retry_policy_;
    CircuitBreaker circuit_breaker_;

    std::string build_user_message(const AtomicTask& task, const std::string& tool_output) const;
};

} // namespace agent
