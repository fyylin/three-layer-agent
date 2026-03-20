#pragma once
// =============================================================================
// include/utils/workspace.hpp
//
// Workspace: filesystem layout for one run.
//
// Directory structure (see design doc chapter 4):
//   <root>/workspace/<run-id>/
//     global.log           NDJSON global log (all agents)
//     state.json           All agents' StateInfo snapshots
//     result.json          FinalResult after completion
//     shared/              Cross-agent resource exchange
//     memory/              Persistent memory
//     director/            Director agent workspace
//     mgr-<N>/             Manager agent workspaces
//     wkr-<N>/             Worker agent workspaces
//       artifacts/         Files written by write_file tool (sandboxed here)
//
// All path operations are cross-platform (forward slashes internally,
// converted on Windows where needed).
// =============================================================================

#include <optional>
#include <string>
#include <vector>

namespace agent {

// -----------------------------------------------------------------------------
// WorkspacePaths: all derived paths for a single run
// -----------------------------------------------------------------------------
struct WorkspacePaths {
    // ── Conversation-scoped (one per conversation, not per message) ───────
    std::string conv_root;       // workspace/conversations/<conv-id>/
    std::string conv_md;         // conv_root/CONVERSATION.md  (append per run)
    std::string conv_memory_md;  // conv_root/MEMORY.md        (conversation memory)
    std::string conv_runs_md;    // conv_root/runs.md          (all run logs in this conv)
    std::string conv_exp_md;     // conv_root/experience.md    (conv-scoped experience)
    // ── Persistent (shared across ALL conversations) ──────────────────────
    std::string current_dir;     // workspace/current/
    std::string files_dir;       // workspace/current/files/   (agent-created files)
    std::string shared_dir;      // workspace/current/shared/  (peer cache)
    std::string memory_dir;      // workspace/current/memory/  (long-term memory)
    std::string experience_md;   // workspace/current/memory/EXPERIENCE.md
    std::string workspace_md;    // workspace/current/WORKSPACE.md
    std::string env_knowledge_md;// workspace/current/env_knowledge.md (replaces .tsv)
    // ── Logs (append across all conversations) ────────────────────────────
    std::string activity_md;     // workspace/logs/activity.md  (human-readable append)
    std::string structured_log;  // workspace/logs/structured.ndjson (machine-readable)
    // ── Legacy compatibility aliases ──────────────────────────────────────
    std::string run_root;        // = conv_root  (for backward compat with agent code)
    std::string global_log;      // = activity_md (for backward compat)
    std::string state_json;      // = conv_root/state.md
    std::string result_json;     // = conv_root/result.md

    // Derive the workspace directory for a given agent_id.
    // e.g. "dir-001" → run_root/director/
    //      "mgr-3"   → run_root/mgr-3/
    //      "wkr-2"   → run_root/wkr-2/
    [[nodiscard]] std::string agent_dir(const std::string& agent_id) const;

    // Worker artifact sandbox: run_root/wkr-<N>/artifacts/
    [[nodiscard]] std::string artifact_dir(const std::string& agent_id) const;
};

// -----------------------------------------------------------------------------
// WorkspaceManager: static helpers (no state)
// -----------------------------------------------------------------------------
class WorkspaceManager {
public:
    // Create the full directory tree for a run.  Returns the WorkspacePaths.
    // Throws std::runtime_error on failure.
    [[nodiscard]] static WorkspacePaths init(const std::string& root,
                                              const std::string& run_id);

    // Create a single agent's subdirectory (called when Agent is constructed).
    static void init_agent_dir(const WorkspacePaths& wp,
                                const std::string& agent_id);

    // Write the current StateInfo snapshot of all agents to state.json.
    // Called by StateMachine transition callback (via AgentContext).
    static void write_state(const WorkspacePaths& wp,
                             const std::string& agent_id,
                             const std::string& state_json_fragment);

    // Append one NDJSON line to global.log (thread-safe  --  uses file mutex).
    static void append_log(const WorkspacePaths& wp,
                            const std::string& ndjson_line);

    // Sandbox check: returns true if path is inside artifact_dir or shared_dir.
    // Used by write_file tool to prevent directory traversal.
    [[nodiscard]] static bool is_sandboxed(const std::string& path,
                                            const WorkspacePaths& wp,
                                            const std::string& agent_id);

    // Cleanup: remove artifact files but keep logs and JSON.
    // Returns list of deleted paths.
    [[nodiscard]] static std::vector<std::string> cleanup_artifacts(
        const WorkspacePaths& wp);

    // Cross-platform mkdir -p.
    static void mkdirs(const std::string& path);

    // Cross-platform path join (handles / vs \\ on Windows).
    [[nodiscard]] static std::string join(const std::string& a,
                                           const std::string& b);
};


// =============================================================================
// ResourceManager: cross-agent resource sharing via the shared/ directory
// =============================================================================
class ResourceManager {
public:
    // Write a file to shared/; overwrites if exists.
    // writer_id is recorded in a sidecar .meta file for attribution.
    static void write_shared(const std::string& shared_path,
                              const std::string& filename,
                              const std::string& content,
                              const std::string& writer_id = "");

    // Read a shared file; returns nullopt if it doesn't exist.
    [[nodiscard]] static std::optional<std::string> read_shared(
        const std::string& shared_path,
        const std::string& filename);

    // List all files in shared/ with their writer_id and size.
    [[nodiscard]] static std::vector<std::string> list_shared(
        const std::string& shared_path);

    // Collect all files matching pattern from shared/ and return their contents.
    [[nodiscard]] static std::vector<std::string> collect(
        const std::string& shared_path,
        const std::string& pattern = "");

    // Atomically append a line to a shared log/accumulator file.
    static void append_shared(const std::string& shared_path,
                               const std::string& filename,
                               const std::string& line,
                               const std::string& writer_id = "");
};

} // namespace agent
