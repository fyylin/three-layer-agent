#pragma once
// =============================================================================
// include/agent/supervisor_agent.hpp   --   Layer 0 (Independent)
//
// v2 SupervisorAgent: runs a background monitor thread independent of the
// main execution thread.  Can observe and intervene at any API call boundary.
//
// Responsibilities:
//   1. Quality gate (post-execution LLM evaluation)  --  same as v1
//   2. Active monitoring thread  --  polls state.json, listens to MessageBus
//   3. Intervention: set pause/cancel flags; inject correction via MessageBus
//   4. Configurable decision rules (stuck timeout, fail threshold, etc.)
// =============================================================================

#include "agent/models.hpp"
#include "agent/api_client.hpp"
#include "agent/director_agent.hpp"
#include "agent/agent_context.hpp"
#include "agent/advisor_agent.hpp"
#include "utils/message_bus.hpp"

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace agent {

// -----------------------------------------------------------------------------
// SupervisorConfig: tuning knobs for the monitor thread
// -----------------------------------------------------------------------------
struct SupervisorConfig {
    // Poll interval for the monitor thread
    std::chrono::milliseconds poll_interval{5000};

    // If an agent has not changed state in this duration, it is "stuck"
    std::chrono::milliseconds stuck_timeout{300'000};  // 5 min

    // If a single agent accumulates this many failures, intervene
    int max_fail_count = 5;

    // Maximum quality-gate retries (same as v1 max_retries)
    int max_quality_retries = 1;

    // If global failure rate exceeds this fraction, pause all agents
    double global_fail_rate_threshold = 0.5;
};

// -----------------------------------------------------------------------------
// SupervisorAgent
// -----------------------------------------------------------------------------
class SupervisorAgent {
public:
    // v2 constructor: takes a shared MessageBus so it can observe all agents
    SupervisorAgent(std::string           id,
                    ApiClient&            client,
                    DirectorAgent&        director,
                    std::string           system_prompt,
                    std::shared_ptr<MessageBus> bus = nullptr,
                    SupervisorConfig      config = {},
                    int                   max_retries = 1,
                    std::unique_ptr<AdvisorAgent> advisor = nullptr);

    ~SupervisorAgent();  // stops monitor thread

    // Main entry: run goal with full supervision (quality gate + monitoring).
    // Starts monitor thread, executes director.run(), joins monitor thread.
    [[nodiscard]] FinalResult run(const UserGoal& goal) noexcept;

    [[nodiscard]] const std::string& id() const noexcept { return id_; }

    // Register all active AgentContexts so monitor can poll them.
    // Called by main.cpp after building all agents.
    void register_context(const std::string& agent_id,
                          std::shared_ptr<StateMachine> state,
                          std::shared_ptr<std::atomic<bool>> cancel_flag,
                          std::shared_ptr<std::atomic<bool>> pause_flag);

    // Manually cancel a specific agent (exposed for interactive use).
    void cancel_agent(const std::string& agent_id);

    // Manually pause / resume a specific agent.
    void pause_agent (const std::string& agent_id);
    void resume_agent(const std::string& agent_id);

private:
    std::string       id_;
    ApiClient&        client_;
    DirectorAgent&    director_;
    std::string       system_prompt_;
    std::shared_ptr<MessageBus> bus_;
    SupervisorConfig  config_;
    int               max_retries_;

    // Registered agent controls
    struct AgentControls {
        std::shared_ptr<StateMachine>       state;
        std::shared_ptr<std::atomic<bool>>  cancel_flag;
        std::shared_ptr<std::atomic<bool>>  pause_flag;
    };
    std::mutex                                    reg_mu_;
    std::unordered_map<std::string, AgentControls> registry_;

    // Monitor thread
    std::thread             monitor_thread_;
    std::atomic<bool>       stop_monitor_{false};
    void monitor_loop();

    // Decision functions (called from monitor thread)
    void handle_stuck_agent(const std::string& agent_id,
                             const StateInfo& info,
                             int              stuck_count = 1);
    void handle_high_fail_rate(const std::string& agent_id,
                                const StateInfo& info);
    void inject_correction(const std::string& agent_id,
                            const std::string& note);

    // Quality gate (called from run(), on main thread)
    [[nodiscard]] std::pair<bool,std::string> evaluate(
        const UserGoal& goal, const FinalResult& result) const noexcept;

    [[nodiscard]] std::string detect_structural_issues(
        const FinalResult& result) const noexcept;

    // Advisor integration: call when consecutive failures exceed threshold
    // Returns a refined goal string (may be same as original if advisor unavailable)
    [[nodiscard]] std::string consult_advisor(
        const UserGoal&                 goal,
        const std::vector<std::string>& error_history) const noexcept;

    std::unique_ptr<AdvisorAgent> advisor_;       // may be null
    std::vector<std::string>      error_history_; // per-run failure log
    static constexpr int kAdvisorThreshold = 1;   // consult after this many failures
};

} // namespace agent
