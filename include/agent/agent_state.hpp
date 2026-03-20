#pragma once
// =============================================================================
// include/agent/agent_state.hpp
//
// AgentState: the state machine every Agent instance carries.
// Transitions are atomic and thread-safe.
// Any transition triggers a callback (used by Supervisor to receive events).
// =============================================================================

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>

namespace agent {

// -----------------------------------------------------------------------------
// State enumeration
// -----------------------------------------------------------------------------
enum class AgentState : uint8_t {
    Created    = 0,   // constructed, not yet started
    Idle       = 1,   // ready, waiting for a task
    Running    = 2,   // actively executing (use sub_state string for detail)
    Paused     = 3,   // paused by Supervisor; resumes on resume()
    Done       = 4,   // task completed successfully
    Failed     = 5,   // task failed; error stored in StateInfo
    Cancelled  = 6,   // cancelled by Supervisor or user; no further work
};

[[nodiscard]] inline const char* state_to_cstr(AgentState s) noexcept {
    switch (s) {
        case AgentState::Created:   return "created";
        case AgentState::Idle:      return "idle";
        case AgentState::Running:   return "running";
        case AgentState::Paused:    return "paused";
        case AgentState::Done:      return "done";
        case AgentState::Failed:    return "failed";
        case AgentState::Cancelled: return "cancelled";
    }
    return "unknown";
}

// -----------------------------------------------------------------------------
// StateInfo: snapshot carried in the global state.json
// -----------------------------------------------------------------------------
struct StateInfo {
    std::string agent_id;
    std::string layer;           // "Supervisor" | "Director" | "Manager" | "Worker"
    AgentState  state        = AgentState::Created;
    std::string sub_state;       // e.g. "Decomposing", "WaitingLLM", "CallingTool"
    std::string current_task_id;
    int         call_count   = 0;     // total LLM calls made
    int         fail_count   = 0;     // total failures (all retries)
    int64_t     started_ms   = 0;     // Unix ms when entering Running
    int64_t     last_event_ms= 0;     // Unix ms of last state change
    std::string last_error;
};

// -----------------------------------------------------------------------------
// StateMachine: thread-safe state holder with transition callback
// -----------------------------------------------------------------------------
class StateMachine {
public:
    using TransitionCb = std::function<void(const StateInfo&)>;

    explicit StateMachine(std::string agent_id, std::string layer)
    {
        info_.agent_id = std::move(agent_id);
        info_.layer    = std::move(layer);
        info_.state    = AgentState::Created;
        info_.last_event_ms = now_ms();
    }

    // Register a callback invoked on every state change.
    // Thread-safe; can be set once at construction time.
    void on_transition(TransitionCb cb) {
        std::lock_guard<std::mutex> lk(mu_);
        cb_ = std::move(cb);
    }

    // Transition to a new state; optionally record sub-state detail.
    // Returns false if transition is illegal (e.g. Done → Running).
    bool transition(AgentState next,
                    const std::string& sub = "",
                    const std::string& task_id = "")
    {
        std::lock_guard<std::mutex> lk(mu_);
        // Cancelled and Done are terminal  --  no further transitions allowed
        if (info_.state == AgentState::Cancelled ||
            info_.state == AgentState::Done)
            return false;
        info_.state          = next;
        info_.sub_state      = sub;
        if (!task_id.empty()) info_.current_task_id = task_id;
        info_.last_event_ms  = now_ms();
        if (next == AgentState::Running && info_.started_ms == 0)
            info_.started_ms = info_.last_event_ms;
        if (cb_) cb_(info_);
        return true;
    }

    void record_failure(const std::string& error) {
        std::lock_guard<std::mutex> lk(mu_);
        ++info_.fail_count;
        info_.last_error = error;
    }
    void record_call() {
        std::lock_guard<std::mutex> lk(mu_);
        ++info_.call_count;
    }

    [[nodiscard]] AgentState state() const noexcept {
        std::lock_guard<std::mutex> lk(mu_);
        return info_.state;
    }
    [[nodiscard]] StateInfo snapshot() const {
        std::lock_guard<std::mutex> lk(mu_);
        return info_;
    }

    // Checked by Agent before each LLM call to honour pause/cancel
    [[nodiscard]] bool is_cancelled() const noexcept {
        std::lock_guard<std::mutex> lk(mu_);
        return info_.state == AgentState::Cancelled;
    }
    [[nodiscard]] bool is_paused() const noexcept {
        std::lock_guard<std::mutex> lk(mu_);
        return info_.state == AgentState::Paused;
    }

private:
    mutable std::mutex mu_;
    StateInfo          info_;
    TransitionCb       cb_;

    static int64_t now_ms() noexcept {
        using namespace std::chrono;
        return duration_cast<milliseconds>(
            system_clock::now().time_since_epoch()).count();
    }
};

} // namespace agent
