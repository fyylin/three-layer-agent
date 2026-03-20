#pragma once
// =============================================================================
// include/utils/message_bus.hpp
//
// In-process async message bus.  All Agents in the same run share one bus
// instance (via AgentContext).  Designed for the following communication
// patterns:
//
//   Worker  → Manager   : progress / result reports
//   Manager → Director  : subtask completion notices
//   Supervisor → any    : pause / resume / cancel / correction instructions
//   any → Supervisor    : status push (mirrors StateInfo callbacks)
//
// Implementation: a single global queue (MPSC-friendly) protected by mutex.
// Routing is by to_id string matching ("*" = broadcast to all subscribers).
//
// Performance note: this is an in-process queue; no serialisation needed.
// If the system ever becomes multi-process, swap the backend without changing
// the API.
// =============================================================================

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>

namespace agent {

// -----------------------------------------------------------------------------
// Message types
// -----------------------------------------------------------------------------
namespace MsgType {
    // Worker -> Manager
    constexpr const char* Progress    = "progress";    // {"pct":50,"step":"..","detail":".."}
    constexpr const char* Result      = "result";      // {"task_id":"..","status":"done","output":"..","tools_tried":N}

    // Worker <-> Worker (peer collaboration)
    constexpr const char* Peer        = "peer";        // {"type":"dir_listing","path":"..","shared_key":".."}

    // Agent -> Agent (request/response protocol)
    constexpr const char* Request     = "request";     // {"request_id":"..","intent":"..","payload":".."}
    constexpr const char* Response    = "response";    // {"request_id":"..","status":"ok","payload":".."}

    // Worker -> Manager (capability declaration)
    constexpr const char* Capability  = "capability";  // {"agent_id":"..","best_tools":[],"success_rates":{}}

    // Manager -> Director
    constexpr const char* SubReport   = "sub_report";  // subtask_id + summary + worker_outputs

    // Supervisor -> Agent
    constexpr const char* Pause       = "pause";
    constexpr const char* Resume      = "resume";
    constexpr const char* Cancel      = "cancel";
    constexpr const char* Correct     = "correct";     // {"note":".."}  before next LLM call
    constexpr const char* Interrupt   = "interrupt";   // HIGH PRIORITY: abort current LLM

    // Agent -> Supervisor
    constexpr const char* StateChange = "state_change";

    // Worker -> Supervisor (tool evolution)
    constexpr const char* RequestTool  = "request_tool"; // {"task":"..","reason":"..","suggested_impl":".."}
    constexpr const char* ToolGranted  = "tool_granted";

    // System
    constexpr const char* SkillLearned = "skill_learned"; // {"skill_name":"..","description":".."}

    // Internal dialog — all LLM calls, tool calls, decisions broadcast to supervisor
    // Enables: Supervisor sees all internal agent communication
    // Payload: {"agent":"wkr-1","layer":"Worker","event":"llm_call",
    //           "task_id":"..","summary":"..","tool":"..","input":".."}
    constexpr const char* Dialog       = "dialog";
    constexpr const char* ToolCall     = "tool_call";   // agent called a tool
    constexpr const char* LlmDecision  = "llm_decision"; // agent got LLM response

    // D1: Event-driven Supervisor signals
    constexpr const char* ToolFailed   = "tool_failed";   // {agent,tool,error,attempt}
    constexpr const char* SlowWarning  = "slow_warning";  // {agent,elapsed_ms,task_id}
    constexpr const char* GivingUp     = "giving_up";     // {agent,task_id,reason}
}

struct AgentMessage {
    std::string from_id;       // sender agent_id
    std::string to_id;         // recipient agent_id, or "*" for broadcast
    std::string type;          // one of MsgType constants
    std::string payload;       // JSON string (may be empty)
    int64_t     timestamp_ms;  // Unix milliseconds
};

// -----------------------------------------------------------------------------
// MessageBus
// -----------------------------------------------------------------------------
class MessageBus {
public:
    MessageBus() = default;

    // Non-copyable (owns internal state)
    MessageBus(const MessageBus&)            = delete;
    MessageBus& operator=(const MessageBus&) = delete;

    // -- Sending ------------------------------------------------------------

    // Post a message (non-blocking).
    void send(AgentMessage msg);

    // Convenience: build and post in one call.
    void send(const std::string& from,
              const std::string& to,
              const std::string& type,
              const std::string& payload = "");

    // Broadcast to all registered subscribers.
    void broadcast(const std::string& from,
                   const std::string& type,
                   const std::string& payload = "");

    // -- Receiving ---------------------------------------------------------

    // Non-blocking poll: return a message addressed to agent_id (or broadcast),
    // or std::nullopt if the inbox is empty.
    std::optional<AgentMessage> try_receive(const std::string& agent_id);

    // Blocking receive with timeout_ms.  Returns nullopt on timeout.
    std::optional<AgentMessage> receive(const std::string& agent_id,
                                        int timeout_ms = 0);

    // -- Subscriptions (Supervisor uses this) ------------------------------

    // Register a callback invoked synchronously on send() for messages
    // addressed to agent_id (or broadcast "*").
    // Multiple callbacks per agent_id are allowed.
    void subscribe(const std::string& agent_id,
                   std::function<void(const AgentMessage&)> cb);

    void unsubscribe(const std::string& agent_id);

    // -- Diagnostics -------------------------------------------------------

    [[nodiscard]] size_t pending_count(const std::string& agent_id) const;
    [[nodiscard]] size_t total_sent() const noexcept { return total_sent_.load(); }

private:
    static int64_t now_ms() noexcept {
        using namespace std::chrono;
        return duration_cast<milliseconds>(
            system_clock::now().time_since_epoch()).count();
    }

    // Per-agent inbox
    mutable std::mutex                                       mu_;
    std::condition_variable                                  cv_;
    std::unordered_map<std::string, std::queue<AgentMessage>> inboxes_;
    std::unordered_map<std::string,
        std::vector<std::function<void(const AgentMessage&)>>> subscribers_;
    std::atomic<size_t> total_sent_{0};
};

} // namespace agent
