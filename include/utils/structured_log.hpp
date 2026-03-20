#pragma once
#include <atomic>
// =============================================================================
// include/utils/structured_log.hpp
//
// NDJSON structured logger.  Each event is one JSON object per line.
// Two outputs:
//   1. Global log: workspace/<run-id>/global.log   (all agents)
//   2. Agent log:  workspace/<agent-dir>/agent.log  (per-agent)
//
// Log record schema:
//   {
//     "ts":       "2026-03-18T09:34:19.280Z",
//     "level":    "INFO",
//     "layer":    "Worker",
//     "agent_id": "wkr-1",
//     "run_id":   "run-2",
//     "task_id":  "subtask-1-atomic-1",
//     "event":    "task_start",         // structured event type
//     "message":  "starting execution", // human-readable
//     "data":     { ... }               // optional structured payload
//   }
//
// Thread-safety: all writes are protected by a per-file mutex.
// =============================================================================

#include <mutex>
#include <string>
#include <unordered_map>

namespace agent {

// -----------------------------------------------------------------------------
// Event type constants (use these in code, not raw strings)
// -----------------------------------------------------------------------------
namespace EvType {
    constexpr const char* AgentCreated    = "agent_created";
    constexpr const char* StateChanged    = "state_changed";
    constexpr const char* TaskStart       = "task_start";
    constexpr const char* TaskEnd         = "task_end";
    constexpr const char* ToolCall        = "tool_call";
    constexpr const char* ToolResult      = "tool_result";
    constexpr const char* LlmCall         = "llm_call";
    constexpr const char* LlmResponse     = "llm_response";
    constexpr const char* ParseFail       = "parse_fail";
    constexpr const char* SubReport       = "sub_report";
    constexpr const char* SupervisorAction= "supervisor_action";
    constexpr const char* MessageSent     = "message_sent";
    constexpr const char* MessageReceived = "message_received";
    constexpr const char* MemoryAccess    = "memory_access";
    constexpr const char* WorkspaceWrite  = "workspace_write";
    constexpr const char* WorkspaceClean  = "workspace_cleanup";
    constexpr const char* ProgressReport  = "progress_report";
}

// -----------------------------------------------------------------------------
// StructuredLogger: writes NDJSON to one or two files simultaneously
// -----------------------------------------------------------------------------
class StructuredLogger {
public:
    // Create a logger writing to global_log_path AND agent_log_path.
    // Pass empty string for either to disable that output.
    StructuredLogger(std::string agent_id,
                     std::string layer,
                     std::string run_id,
                     std::string global_log_path,
                     std::string agent_log_path);

    // Destructor flushes open file handles.
    ~StructuredLogger();

    // Log a structured event.
    // data_json: optional JSON object string for structured payload.
    // Override the structured output path (default = agent_log_path)
    void set_structured_path(const std::string& path) {
        structured_path_ = path;
    }

    void log(const std::string& level,      // "DEBUG"|"INFO"|"WARN"|"ERROR"
             const std::string& task_id,
             const std::string& event_type,
             const std::string& message,
             const std::string& data_json = "");

    // Convenience wrappers
    void info (const std::string& tid, const std::string& ev,
               const std::string& msg, const std::string& d = "");
    void warn (const std::string& tid, const std::string& ev,
               const std::string& msg, const std::string& d = "");
    void error(const std::string& tid, const std::string& ev,
               const std::string& msg, const std::string& d = "");
    void debug(const std::string& tid, const std::string& ev,
               const std::string& msg, const std::string& d = "");

    // No-op if files are not configured
    void flush();

private:
    std::string  agent_id_;
    std::string  layer_;
    std::string  run_id_;
    mutable std::atomic<uint64_t> span_counter_{0};  // OTel-lite span_id
    std::string  global_path_;
    std::string  agent_path_;
    std::string  structured_path_;  // workspace/logs/structured.ndjson

    // Global log uses a process-wide mutex keyed by path
    static std::mutex& global_mutex(const std::string& path);

    mutable std::mutex  agent_mu_;

    void write_line(const std::string& path,
                    std::mutex& mu,
                    const std::string& line) noexcept;

    [[nodiscard]] std::string build_record(
        const std::string& level,
        const std::string& task_id,
        const std::string& event_type,
        const std::string& message,
        const std::string& data_json) const noexcept;

    [[nodiscard]] static std::string iso_now() noexcept;
};

// -----------------------------------------------------------------------------
// Null logger: used when workspace is disabled (backward-compat mode)
// -----------------------------------------------------------------------------
class NullStructuredLogger final : public StructuredLogger {
public:
    NullStructuredLogger()
        : StructuredLogger("", "", "", "", "") {}
};

} // namespace agent
