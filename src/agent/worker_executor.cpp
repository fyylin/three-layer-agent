#include "agent/worker_executor.hpp"
#include "agent/exceptions.hpp"
#include "utils/logger.hpp"
#include <thread>

namespace agent {

WorkerExecutor::WorkerExecutor(ApiClient& client, ToolRegistry& registry, const std::string& system_prompt)
    : client_(client), registry_(registry), system_prompt_(system_prompt) {}

AtomicResult WorkerExecutor::execute_with_retry(const AtomicTask& task, int max_retries) {
    AtomicResult result;
    result.task_id = task.id;
    result.status = TaskStatus::Running;

    for (int attempt = 0; attempt <= max_retries; ++attempt) {
        if (!circuit_breaker_.allow_request()) {
            result.status = TaskStatus::Failed;
            result.error = "Circuit breaker open";
            return result;
        }

        try {
            std::string tool_output = call_tool(task);

            std::string user_msg = build_user_message(task, tool_output);
            std::string llm_response = client_.complete(system_prompt_, user_msg);

            result.status = TaskStatus::Done;
            result.output = llm_response;
            circuit_breaker_.record_success();
            return result;

        } catch (const std::exception& e) {
            circuit_breaker_.record_failure();

            auto decision = retry_policy_.should_retry(attempt, e.what());
            if (!decision.should_retry) {
                result.status = TaskStatus::Failed;
                result.error = std::string(e.what()) + " (" + decision.reason + ")";
                return result;
            }

            if (decision.wait_ms > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(decision.wait_ms));
            }
        }
    }

    result.status = TaskStatus::Failed;
    result.error = "Max retries exceeded";
    return result;
}

std::string WorkerExecutor::call_tool(const AtomicTask& task) const {
    return registry_.invoke(task.tool, task.input, task.id);
}

std::string WorkerExecutor::build_user_message(
        const AtomicTask& task, const std::string& tool_output) const {
    std::string msg = "Task: " + task.description + "\n";
    msg += "Tool: " + task.tool + "\n";
    msg += "Tool output:\n" + tool_output;
    return msg;
}

} // namespace agent
