#pragma once
// =============================================================================
// include/agent/skill_registry.hpp
//
// SkillRegistry: runtime skill system for Three-Layer Agent
//
// Skills are reusable, parameterized task templates extracted from successful
// runs. They exist at three levels:
//
//   L1 Built-in tools    -- compiled into binary (tool_set.cpp)
//   L2 Composite skills  -- multi-step templates stored in workspace/skills/
//   L3 Experience recipes-- extracted from successful runs, in memory/long_term/
//
// A skill is a JSON file with this structure:
//   {
//     "name": "read_windows_file_robust",
//     "description": "Read a file with fallback chain",
//     "parameters": ["file_path"],
//     "steps": [
//       {"tool":"read_file",   "input":"{{file_path}}", "on_fail":"next"},
//       {"tool":"run_command", "input":"type \"{{file_path}}\"", "on_fail":"report"}
//     ],
//     "success_count": 7,
//     "fail_count": 1,
//     "created_by": "wkr-2",
//     "run_id": "run-042"
//   }
// =============================================================================

#include "agent/tool_registry.hpp"
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace agent {

// ---------------------------------------------------------------------------
// SkillStep: one step in a composite skill
// ---------------------------------------------------------------------------
struct SkillStep {
    std::string tool;       // tool name (from ToolRegistry)
    std::string input_tpl;  // input template with {{param}} placeholders
    std::string on_fail;    // "next" (try next step) | "report" (surface error) | "abort"
};

// ---------------------------------------------------------------------------
// SkillDef: full definition of a composite skill
// ---------------------------------------------------------------------------
struct SkillDef {
    std::string              name;
    std::string              description;
    std::vector<std::string> parameters;   // parameter names (used in {{...}})
    std::vector<SkillStep>   steps;
    int                      success_count = 0;
    int                      fail_count    = 0;
    std::string              created_by;
    std::string              run_id;

    // Compute success rate (0.0 - 1.0); returns 0.5 if no data
    [[nodiscard]] float success_rate() const noexcept {
        int total = success_count + fail_count;
        return (total == 0) ? 0.5f : static_cast<float>(success_count) / total;
    }
};

// ---------------------------------------------------------------------------
// CapabilityProfile: per-Worker tool success statistics
// ---------------------------------------------------------------------------
struct CapabilityProfile {
    std::string                     agent_id;
    std::map<std::string, int>      tool_success;  // tool -> success count
    std::map<std::string, int>      tool_fail;     // tool -> fail count
    int                             tasks_done   = 0;
    int                             tasks_failed = 0;

    // Get success rate for a specific tool (0.0-1.0, default 0.5 = unknown)
    [[nodiscard]] float tool_rate(const std::string& tool) const noexcept {
        auto si = tool_success.find(tool);
        auto fi = tool_fail.find(tool);
        int s = (si != tool_success.end()) ? si->second : 0;
        int f = (fi != tool_fail.end())    ? fi->second : 0;
        int total = s + f;
        return (total == 0) ? 0.5f : static_cast<float>(s) / total;
    }

    // Best tools (rate > 0.7)
    [[nodiscard]] std::vector<std::string> best_tools() const {
        std::vector<std::string> result;
        for (auto& [t, s] : tool_success) {
            if (tool_rate(t) > 0.7f) result.push_back(t);
        }
        return result;
    }

    // Serialize to JSON payload for Capability message
    [[nodiscard]] std::string to_payload() const;

    // Update from AtomicResult
    void record(const std::string& tool, bool success) {
        if (success) tool_success[tool]++;
        else         tool_fail[tool]++;
        if (success) tasks_done++;
        else         tasks_failed++;
    }
};

// ---------------------------------------------------------------------------
// SkillRegistry
// ---------------------------------------------------------------------------
class SkillRegistry {
public:
    explicit SkillRegistry(const std::string& skills_dir = "");

    // ------------------------------------------------------------------
    // Skill execution
    // ------------------------------------------------------------------

    // Execute a named skill with parameters.
    // Returns output string on success, throws on total failure.
    [[nodiscard]] std::string invoke_skill(
        const std::string&              name,
        const std::map<std::string,
                       std::string>&    params,
        ToolRegistry&                   tools) const;

    // ------------------------------------------------------------------
    // Skill discovery
    // ------------------------------------------------------------------

    // Find a skill whose description semantically matches the task.
    // Uses keyword overlap scoring (no LLM needed).
    [[nodiscard]] std::optional<SkillDef> find_matching_skill(
        const std::string& task_description,
        float              min_confidence = 0.4f) const;

    // List all registered skills
    [[nodiscard]] std::vector<SkillDef> all_skills() const;

    // ------------------------------------------------------------------
    // Skill creation and persistence
    // ------------------------------------------------------------------

    // Register a new skill (in memory + optionally write to disk)
    void register_skill(const SkillDef& skill, bool persist = true);

    // Extract a skill from a successful multi-step execution.
    // Returns the generated skill name, or "" if not worth extracting.
    [[nodiscard]] std::string maybe_extract_skill(
        const std::string&              task_description,
        const std::vector<std::string>& tools_used,
        const std::vector<std::string>& inputs_used,
        const std::string&              created_by,
        const std::string&              run_id);

    // ------------------------------------------------------------------
    // Statistics update
    // ------------------------------------------------------------------
    void record_success(const std::string& skill_name);
    void record_failure(const std::string& skill_name);

    // ------------------------------------------------------------------
    // Disk I/O
    // ------------------------------------------------------------------
    void load_from_dir(const std::string& dir);
    void save_to_dir(const std::string& dir) const;

    // ------------------------------------------------------------------
    // Capability profiles
    // ------------------------------------------------------------------
    void update_capability(const std::string& agent_id,
                            const std::string& tool,
                            bool success);

    [[nodiscard]] std::optional<CapabilityProfile> get_capability(
        const std::string& agent_id) const;

    // Find the agent_id with the best success rate for a given tool
    [[nodiscard]] std::string best_agent_for_tool(
        const std::string&                    tool,
        const std::vector<std::string>&       candidate_ids) const;

    // Update tool success rate after task completes (EMA: α=0.1)
    // Call with tool_name and success=true/false after Worker execute()
    void record_tool_outcome(const std::string& agent_id,
                             const std::string& tool,
                             bool               success) noexcept;

    // Convenience: update for a tool by current agent (agent_id inferred)
    void record_tool_outcome(const std::string& tool, bool success) noexcept;

private:
    std::string                               skills_dir_;
    std::map<std::string, SkillDef>           skills_;
    std::map<std::string, CapabilityProfile>  capabilities_;
    mutable std::mutex                         mu_;

    // Substitute {{param}} placeholders in a template string
    static std::string render(const std::string& tpl,
                               const std::map<std::string,std::string>& params);

    // Compute keyword overlap score between two strings
    static float keyword_overlap(const std::string& a, const std::string& b);
};

} // namespace agent
