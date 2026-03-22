#pragma once
// =============================================================================
// include/agent/agent_context.hpp
//
// AgentContext: the single injection point for all cross-cutting concerns.
//
// Every Agent receives one AgentContext at construction time.  The context
// provides:
//   - Identity (agent_id, layer, run_id, parent_id)
//   - Workspace paths (for file I/O sandboxing)
//   - Structured logger (NDJSON event log)
//   - Memory store (short-term + session + long-term)
//   - Message bus (inter-agent communication)
//   - State machine (queryable by Supervisor)
//   - Cancellation / pause flags (set by Supervisor)
//
// BACKWARD COMPATIBILITY:
//   A default-constructed AgentContext has nullptrs for optional fields and
//   a NullStructuredLogger.  Existing Agent constructors can add an optional
//   AgentContext parameter with a default value  --  they compile and run without
//   any workspace/bus/memory plumbing until explicitly wired up.
//
//   Recommended migration pattern:
//     WorkerAgent(std::string id, ApiClient&, ToolRegistry&,
//                 std::string system_prompt, int max_retries = 3,
//                 AgentContext ctx = AgentContext{});
// =============================================================================

#include "agent/agent_state.hpp"
#include "utils/message_bus.hpp"
#include "utils/memory_store.hpp"
#include "utils/env_knowledge.hpp"
#include "utils/workspace.hpp"
#include "agent/skill_registry.hpp"
#include "utils/structured_log.hpp"
#include "utils/tool_stats.hpp"
#include "utils/result_cache.hpp"
#include "utils/incremental_engine.hpp"
#include "utils/prompt_optimizer.hpp"
#include "utils/multimodal_handler.hpp"
#include "utils/token_budget_allocator.hpp"
#include "utils/experience_replay_engine.hpp"
#include "agent/tool_cache.hpp"
#include "distributed/node_registry.hpp"
#include "distributed/task_dispatcher.hpp"
#include "api/batch_api_client.hpp"

#include <atomic>
#include <thread>
#include <chrono>
#include <memory>
#include <string>

namespace agent {

// -----------------------------------------------------------------------------
// AgentContext
// -----------------------------------------------------------------------------
struct AgentContext {
    // -- Identity ----------------------------------------------------------
    std::string agent_id;
    std::string layer;        // "Supervisor"|"Director"|"Manager"|"Worker"
    std::string run_id;
    std::string parent_id;    // empty for Director; Worker → Manager

    // -- Workspace ---------------------------------------------------------
    WorkspacePaths workspace;   // populated by WorkspaceManager::init_agent_dir

    // -- Infrastructure (shared_ptr = null-safe, shared across agents) -----
    std::shared_ptr<StructuredLogger> slog;     // null → no structured logging
    std::shared_ptr<MemoryStore>      memory;   // null → no memory
    std::shared_ptr<MessageBus>       bus;      // null → no messaging
    std::shared_ptr<StateMachine>     state;    // null → no state tracking

    // -- Control flags (written by Supervisor, read by Agent) --------------
    // These are separate from StateMachine so they can be polled atomically
    // in hot paths without acquiring the state mutex.
    std::shared_ptr<std::atomic<bool>> cancel_flag;  // null = never cancelled
    std::shared_ptr<std::atomic<bool>> pause_flag;   // null = never paused

    // -- Optional skill/capability registry (shared across agents) ----------
    std::shared_ptr<SkillRegistry>     skills;       // null = no skill system
    std::shared_ptr<MemoryStore>        session_mem;  // cross-run persistent memory
    double                             budget_usd = 0.0;  // 0 = unlimited
    std::shared_ptr<EnvKnowledgeBase>  env_kb;           // environment facts (paths, etc.)
    std::shared_ptr<void>               exp_mgr_ptr;      // ExperienceManager (type-erased, optional)
    std::shared_ptr<ToolStatsTracker>  tool_stats;       // tool performance tracking
    std::shared_ptr<ResultCache>       result_cache;     // task result caching
    std::shared_ptr<IncrementalEngine> incremental;      // incremental computation
    std::shared_ptr<PromptOptimizer>        prompt_opt;       // adaptive prompt optimization
    std::shared_ptr<TokenBudgetAllocator>   budget_alloc;     // token budget management (Phase 9)
    std::shared_ptr<ExperienceReplayEngine> exp_replay;       // cross-session learning (Phase 10)
    std::shared_ptr<ToolCache>              tool_cache;       // tool call caching
    std::shared_ptr<NodeRegistry>           node_registry;    // distributed node management
    std::shared_ptr<TaskDispatcher>         task_dispatcher;  // distributed task dispatch
    std::shared_ptr<class GlobalSummary>    global_summary;   // global context shared across agents

    // -- Helpers -----------------------------------------------------------

    // Returns true if this context is fully wired (non-null infrastructure).
    [[nodiscard]] bool is_active() const noexcept {
        return slog != nullptr && memory != nullptr &&
               bus  != nullptr && state  != nullptr;
    }

    // Safe cancellation check (false if cancel_flag is null).
    [[nodiscard]] bool cancelled() const noexcept {
        return cancel_flag && cancel_flag->load(std::memory_order_relaxed);
    }

    // Safe pause check.
    [[nodiscard]] bool paused() const noexcept {
        return pause_flag && pause_flag->load(std::memory_order_relaxed);
    }

    // Block in a spin-sleep until pause is lifted or cancel fires.
    // Returns false if cancelled while paused.
    bool wait_while_paused() const noexcept {
        while (paused()) {
            if (cancelled()) return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        return true;
    }

    // Structured log shortcuts (safe even if slog is null).
    void log_info(const std::string& task_id,
                  const std::string& event_type,
                  const std::string& message,
                  const std::string& data = "") const noexcept {
        if (slog) slog->info(task_id, event_type, message, data);
    }
    void log_warn(const std::string& task_id,
                  const std::string& event_type,
                  const std::string& message,
                  const std::string& data = "") const noexcept {
        if (slog) slog->warn(task_id, event_type, message, data);
    }
    void log_error(const std::string& task_id,
                   const std::string& event_type,
                   const std::string& message,
                   const std::string& data = "") const noexcept {
        if (slog) slog->error(task_id, event_type, message, data);
    }

    // Send a message (safe if bus is null).
    void send(const std::string& to,
              const std::string& type,
              const std::string& payload = "") const noexcept {
        if (bus) bus->send(agent_id, to, type, payload);
    }
};

// -----------------------------------------------------------------------------
// AgentContextBuilder: fluent builder for constructing a wired context
// -----------------------------------------------------------------------------
class AgentContextBuilder {
public:
    AgentContextBuilder& agent(std::string id, std::string layer_name) {
        ctx_.agent_id = std::move(id);
        ctx_.layer    = std::move(layer_name);
        return *this;
    }
    AgentContextBuilder& run(std::string run_id) {
        ctx_.run_id = std::move(run_id);
        return *this;
    }
    AgentContextBuilder& parent(std::string parent_id) {
        ctx_.parent_id = std::move(parent_id);
        return *this;
    }
    AgentContextBuilder& with_workspace(const WorkspacePaths& wp) {
        ctx_.workspace = wp;
        return *this;
    }
    AgentContextBuilder& with_bus(std::shared_ptr<MessageBus> bus) {
        ctx_.bus = std::move(bus);
        return *this;
    }
    AgentContextBuilder& with_memory(std::shared_ptr<MemoryStore> mem) {
        ctx_.memory = std::move(mem);
        return *this;
    }
    AgentContextBuilder& with_state(std::shared_ptr<StateMachine> sm) {
        ctx_.state = std::move(sm);
        return *this;
    }
    AgentContextBuilder& with_cancel(std::shared_ptr<std::atomic<bool>> f) {
        ctx_.cancel_flag = std::move(f);
        return *this;
    }
    AgentContextBuilder& with_pause(std::shared_ptr<std::atomic<bool>> f) {
        ctx_.pause_flag = std::move(f);
        return *this;
    }

    // Build and wire the StructuredLogger from workspace paths.
    AgentContextBuilder& with_structured_log(const WorkspacePaths& wp,
                                              const std::string& run_id) {
        auto global_log = wp.global_log;
        auto agent_log  = WorkspaceManager::join(
            wp.agent_dir(ctx_.agent_id), "agent.log");
        ctx_.slog = std::make_shared<StructuredLogger>(
            ctx_.agent_id, ctx_.layer, run_id, global_log, agent_log);
        return *this;
    }

    [[nodiscard]] AgentContext build() { return std::move(ctx_); }

private:
    AgentContext ctx_;
};

} // namespace agent

