#pragma once
// =============================================================================
// include/agent/manager_agent.hpp   --   Layer 2
//
// ISOLATION CONSTRAINT: Sees AtomicTask/Result and SubTask/SubTaskReport.
// Must NOT reference UserGoal, FinalResult, ReviewFeedback.
//
// v2 changes:
//   - Optional AgentContext (backward-compat)
//   - Sends SubReport message to Director when SubTask completes
//   - Uses ctx.memory for session-level caching of tool outputs
//   - Worker contexts share the same MessageBus + cancel/pause flags
// =============================================================================

#include "agent/models.hpp"
#include "agent/api_client.hpp"
#include "agent/imanager.hpp"
#include "agent/worker_agent.hpp"
#include "agent/agent_context.hpp"
#include "utils/thread_pool.hpp"

#include <memory>
#include <string>
#include <vector>

namespace agent {

class ManagerAgent final : public IManager {
public:
    ManagerAgent(std::string               id,
                 ApiClient&                client,
                 std::vector<WorkerAgent*> workers,
                 ThreadPool&               pool,
                 std::string               decompose_prompt,
                 std::string               validate_prompt,
                 std::vector<std::string>  available_tools,
                 int                       max_atomic_retries = 3,
                 AgentContext              ctx = AgentContext{});   // ← v2

    [[nodiscard]] SubTaskReport process(const SubTask& task) noexcept override;
    [[nodiscard]] const std::string& id() const noexcept override { return id_; }

private:
    std::string               id_;
    ApiClient&                client_;
    std::vector<WorkerAgent*> workers_;
    ThreadPool&               pool_;
    std::string               decompose_prompt_;
    std::string               validate_prompt_;
    std::vector<std::string>  available_tools_;
    int                       max_atomic_retries_;
    AgentContext              ctx_;   // ← v2

    [[nodiscard]] std::vector<AtomicTask> decompose(
        const SubTask& task,
        const std::string& format_hint = "",
        const std::string& supervisor_note = "") const;

    [[nodiscard]] std::vector<AtomicResult> dispatch(
        std::vector<AtomicTask> tasks, const std::string& subtask_id);

    [[nodiscard]] AtomicResult retry_atomic(const AtomicTask& task) noexcept;

    [[nodiscard]] std::pair<bool,std::string> validate(
        const SubTask& task, const std::vector<AtomicResult>& results) const;

    [[nodiscard]] WorkerAgent* select_worker(
        const std::string& tool, size_t hint) const noexcept;

    [[nodiscard]] static std::string build_summary(
        const std::vector<AtomicResult>& results, bool approved);
};

} // namespace agent
