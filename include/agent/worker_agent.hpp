#pragma once
// =============================================================================
// include/agent/worker_agent.hpp   --   Layer 3
//
// ISOLATION CONSTRAINT: Only AtomicTask and AtomicResult cross this boundary.
//
// v2 changes:
//   - Optional AgentContext parameter (defaults to empty = backward-compat)
//   - Checks cancel/pause flags before each LLM call
//   - Sends progress messages via bus when context is active
//   - Writes to structured log (EvType::TaskStart, ToolCall, LlmCall, etc.)
//   - write_file tool sandboxed to ctx.workspace.artifact_dir(id)
// =============================================================================

#include "agent/models.hpp"
#include "agent/api_client.hpp"
#include "agent/tool_registry.hpp"
#include "agent/agent_context.hpp"
#include "agent/preflight_checker.hpp"
#include "utils/experience_manager.hpp"
#include <string>

namespace agent {

class WorkerAgent {
public:
    WorkerAgent(std::string    id,
                ApiClient&     client,
                ToolRegistry&  registry,
                std::string    system_prompt,
                int            max_atomic_retries = 3,
                AgentContext   ctx = AgentContext{});   // ← v2: optional

    [[nodiscard]] AtomicResult execute(const AtomicTask& task) noexcept;
    [[nodiscard]] const std::string& id() const noexcept { return id_; }

    // v3: capability profile for this Worker (tool success rates)
    [[nodiscard]] float tool_success_rate(const std::string& tool) const noexcept;

    // v3: broadcast capability profile to Manager via bus
    void broadcast_capability() const noexcept;

    // v3: check if task input looks like an LLM-generated placeholder
    [[nodiscard]] static bool is_placeholder_path(const std::string& input) noexcept;
    [[nodiscard]] static bool is_agent_giving_up(const std::string& llm_out) noexcept;

    // v3: check shared/ cache for a Peer result before calling tool
    [[nodiscard]] std::string check_peer_cache(
        const std::string& tool, const std::string& input) const noexcept;

    // Expose internal state machine for Supervisor registration
    [[nodiscard]] std::shared_ptr<agent::StateMachine> state() const noexcept
        { return ctx_.state; }

private:
    std::string   id_;
    ApiClient&    client_;
    ToolRegistry& registry_;
    std::string   system_prompt_;
    int           max_retries_;
    AgentContext  ctx_;
    PreflightChecker preflight_;

    [[nodiscard]] std::string build_user_message(
        const AtomicTask&  task,
        const std::string& tool_output,
        const std::string& format_hint) const;

    [[nodiscard]] std::string call_tool(const AtomicTask& task) const noexcept;
    [[nodiscard]] std::string call_tool_named(const std::string& tool,
        const std::string& input, const AtomicTask& parent) const noexcept;

    [[nodiscard]] bool parse_response(const std::string& llm_output,
                                      AtomicResult& out) const noexcept;

    // v2: check cancel/pause before a blocking call; returns false if cancelled
    [[nodiscard]] bool check_control() const noexcept;

    // v2: send progress update via MessageBus
    void report_progress(int pct, const std::string& step,
                         const std::string& task_id) const noexcept;

    // v2: drain Supervisor correction messages from bus; return combined note
    [[nodiscard]] std::string drain_corrections() const noexcept;

    // Classify tool failure into categories
    [[nodiscard]] FailureCategory classify_failure(const std::string& error) const noexcept;

};

} // namespace agent
