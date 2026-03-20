#pragma once
// =============================================================================
// include/agent/reflection.hpp
//
// Reflection: every Agent layer can reflect on failures before escalating.
// Each layer has a ReflectionEngine that:
//   1. Analyses what went wrong (local LLM call, cheap/fast model)
//   2. Generates alternatives to try
//   3. Decides: self-fix vs escalate
//
// Worker level  -- tries alternative tools, rephrases task
// Manager level -- re-decomposes with different strategy
// Director level -- adjusts subtask boundaries, injects context
//
// Experience accumulation: successful patterns stored in MemoryStore
//   key = "exp:tool:<tool_name>"  -> what inputs work for this tool
//   key = "exp:task:<keywords>"   -> which approach worked for similar tasks
//   key = "exp:fail:<error_type>" -> known failure patterns + fixes
// =============================================================================

#include "agent/models.hpp"
#include "utils/memory_store.hpp"
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace agent {

// ---------------------------------------------------------------------------
// ReflectionResult: what a layer decided after reflecting
// ---------------------------------------------------------------------------
struct ReflectionResult {
    bool        should_retry     = false;   // true = try the fix, false = escalate
    std::string fix_description;            // human-readable description of the fix
    std::string new_tool;                   // if switching tools
    std::string new_input;                  // new tool input or rephrased description
    std::string context_note;              // extra context to inject into next attempt
};

// ---------------------------------------------------------------------------
// ReflectionEngine: pure functions  --  no state, no LLM calls
// Fast heuristic-based reflection that runs synchronously before any LLM call
// ---------------------------------------------------------------------------
class ReflectionEngine {
public:
    // -----------------------------------------------------------------------
    // Worker-level: given a tool failure, suggest an alternative approach
    // Returns nullopt if no better approach is known (-> escalate)
    // -----------------------------------------------------------------------
    static std::optional<ReflectionResult> reflect_tool_failure(
        const std::string& failed_tool,
        const std::string& error_output,
        const std::string& original_input,
        const std::string& task_description,
        int                attempt_count);

    // -----------------------------------------------------------------------
    // Manager-level: given a validation failure, suggest different decomposition
    // -----------------------------------------------------------------------
    static std::optional<ReflectionResult> reflect_validation_failure(
        const std::string& subtask_description,
        const std::string& validation_feedback,
        const std::string& available_tools,
        int                attempt_count);

    // -----------------------------------------------------------------------
    // Experience: record what worked so future tasks can learn
    // -----------------------------------------------------------------------
    static void record_success(MemoryStore&       memory,
                                const std::string& tool,
                                const std::string& input_pattern,
                                const std::string& output_summary);

    static void record_failure(MemoryStore&       memory,
                                const std::string& error_type,
                                const std::string& context,
                                const std::string& what_fixed_it);

    // -----------------------------------------------------------------------
    // Experience: look up relevant past experience
    // Returns "" if nothing relevant found
    // -----------------------------------------------------------------------
    static std::string recall_experience(const MemoryStore& memory,
                                          const std::string& query);

    // -----------------------------------------------------------------------
    // Classify error type from tool output
    // -----------------------------------------------------------------------
    static std::string classify_error(const std::string& tool_output);

    // -----------------------------------------------------------------------
    // Build a run_command fallback for a failed file operation
    // Returns "" if no useful fallback exists
    // -----------------------------------------------------------------------
    static std::string build_file_fallback_cmd(const std::string& path,
                                                const std::string& operation);
};

} // namespace agent
