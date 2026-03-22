#pragma once
// =============================================================================
// include/agent/director_agent.hpp   --   Layer 1
//
// ISOLATION CONSTRAINT: Only SubTask/Report/UserGoal/FinalResult visible here.
//
// v2 changes:
//   - Optional AgentContext (backward-compat)
//   - Updates global state.json on each phase transition
//   - Listens to Manager SubReport messages for live progress display
//   - Writes FinalResult to workspace/result.json on completion
// =============================================================================

#include "agent/imanager.hpp"
#include "agent/models.hpp"
#include "agent/manager_pool.hpp"
#include "agent/api_client.hpp"
#include "agent/agent_context.hpp"
#include "agent/task_router.hpp"
#include "agent/experience_db.hpp"
#include "agent/conversation_context.hpp"
#include "utils/thread_pool.hpp"

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace agent {

class ManagerAgent;  // forward-declaration





// -----------------------------------------------------------------------------
// Task complexity levels for intelligent routing
enum class TaskComplexity : uint8_t {
    L0_Conversational = 0, // Greetings, questions, explanations -> Director answers directly
    L1_SingleTool     = 1, // One tool call -> Director dispatches to 1 Worker via 1 Manager
    L2_SingleSubtask  = 2, // One logical unit -> 1 Manager handles it
    L3_Parallel       = 3, // Multiple independent subtasks -> multiple Managers
    L4_Complex        = 4, // Has dependencies -> Manager + topological dispatch
};

class DirectorAgent {
public:
    DirectorAgent(std::string     id,
                  ApiClient&      client,
                  ThreadPool&     pool,
                  ManagerFactory  factory,
                  std::string     decompose_prompt,
                  std::string     review_prompt,
                  std::string     synthesise_prompt,
                  std::string     classify_prompt = "",
                  int             max_subtask_retries = 2,
                  AgentContext    ctx = AgentContext{});   // ← v2

    [[nodiscard]] FinalResult run(const UserGoal& goal) noexcept;
    [[nodiscard]] std::shared_ptr<agent::StateMachine> state() const noexcept
        { return ctx_.state; }

    // Heuristic fallback complexity assessment used when LLM classification is unavailable.
    [[nodiscard]] static TaskComplexity assess_complexity(
        const std::string& description) noexcept;
    [[nodiscard]] const std::string& id() const noexcept { return id_; }

private:
    std::string    id_;
    ApiClient&     client_;
    ThreadPool&    pool_;        // Worker execution pool
    ManagerFactory factory_;     // initialized first (declaration order matters in C++)
    ManagerPool    mgr_pool_;    // initialized from factory_ after it is set
    std::string    decompose_prompt_;
    std::string    review_prompt_;
    std::string    synthesise_prompt_;
    std::string    classify_prompt_;
    int            max_subtask_retries_;
    AgentContext   ctx_;
    TaskRouter     router_;
    ExperienceDB   experience_;
    ConversationContext conv_ctx_;

    [[nodiscard]] TaskComplexity classify_complexity(
        const std::string& description) noexcept;

    [[nodiscard]] std::vector<SubTask> decompose_goal(
        const UserGoal& goal, const std::string& format_hint = "") const;

    [[nodiscard]] std::vector<SubTaskReport> dispatch_managers(
        const std::vector<SubTask>& tasks);

    [[nodiscard]] std::vector<ReviewFeedback> review(
        const UserGoal& goal,
        const std::vector<SubTask>& tasks,
        const std::vector<SubTaskReport>& reports) const;

    void retry_rejected(const UserGoal& goal,
                        const std::vector<SubTask>& tasks,
                        std::vector<SubTaskReport>& reports,
                        std::vector<ReviewFeedback>& feedbacks);

    [[nodiscard]] std::string synthesise(
        const UserGoal& goal,
        const std::vector<SubTaskReport>& reports) const;

    static std::string make_run_id() {
        static std::atomic<uint64_t> counter{0};
        return "run-" + std::to_string(++counter);
    }
};

} // namespace agent
