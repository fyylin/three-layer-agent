// =============================================================================
// src/agent/reflection.cpp
// =============================================================================
#include "agent/reflection.hpp"
#include "utils/tfidf.hpp"
#include <algorithm>
#include <sstream>
#include <cstring>

namespace agent {

// Module-level TF-IDF index for semantic experience retrieval
// (shared across all ReflectionEngine calls in the same process)
static TFIDFIndex g_exp_index;


// ---------------------------------------------------------------------------
// classify_error: detect error type from tool output string
// ---------------------------------------------------------------------------
std::string ReflectionEngine::classify_error(const std::string& out) {
    auto has = [&](const char* s){ return out.find(s) != std::string::npos; };
    if (has("cannot open") || has("cannot access") || has("not found") ||
        has("error 2")     || has("error 3")       || has("No such file"))
        return "file_not_found";
    if (has("permission") || has("access denied") || has("Access is denied") ||
        has("error 5"))
        return "permission_denied";
    if (has("network") || has("12002") || has("12007") || has("timeout") ||
        has("connection"))
        return "network_error";
    if (has("path traversal"))
        return "path_traversal";
    if (has("blocked"))
        return "command_blocked";
    if (has("[Tool error:") || has("[Tool '"))
        return "tool_error";
    return "unknown_error";
}

// ---------------------------------------------------------------------------
// build_file_fallback_cmd: generate run_command equivalent for file ops
// ---------------------------------------------------------------------------
std::string ReflectionEngine::build_file_fallback_cmd(
        const std::string& path, const std::string& operation) {
    if (path.find("..") != std::string::npos) return "";  // no traversal
    if (path.empty()) return "";

    // Detect platform from path separators
    bool is_windows = (path.find('\\') != std::string::npos ||
                       (path.size() >= 2 && path[1] == ':'));

    if (operation == "read") {
        if (is_windows)
            return "type \"" + path + "\"";
        else
            return "cat \"" + path + "\"";
    }
    if (operation == "stat") {
        if (is_windows)
            return "dir \"" + path + "\"";
        else
            return "ls -la \"" + path + "\"";
    }
    if (operation == "list") {
        if (is_windows)
            return "dir \"" + path + "\"";
        else
            return "ls -la \"" + path + "\"";
    }
    return "";
}

// ---------------------------------------------------------------------------
// reflect_tool_failure: Worker-level heuristic reflection
// Returns a fix to try, or nullopt to escalate
// ---------------------------------------------------------------------------
std::optional<ReflectionResult> ReflectionEngine::reflect_tool_failure(
        const std::string& failed_tool,
        const std::string& error_output,
        const std::string& original_input,
        const std::string& task_description,
        int                attempt_count) {

    // Give up after 2 self-fix attempts -- don't loop forever
    if (attempt_count >= 2) return std::nullopt;

    std::string err_type = classify_error(error_output);
    ReflectionResult result;

    // --- File not found: try run_command as fallback ---
    if (err_type == "file_not_found" &&
        (failed_tool == "read_file" || failed_tool == "stat_file")) {

        std::string cmd = build_file_fallback_cmd(original_input, "read");
        if (!cmd.empty()) {
            result.should_retry    = true;
            result.fix_description = "file not found via " + failed_tool +
                                     "; retrying with run_command: " + cmd;
            result.new_tool  = "run_command";
            result.new_input = cmd;
            result.context_note = "[Previous " + failed_tool +
                                   " failed with: " + error_output + "]";
            return result;
        }
    }

    // --- File not found for list_dir: try run_command dir ---
    if (err_type == "file_not_found" && failed_tool == "list_dir") {
        bool is_win = (original_input.find('\\') != std::string::npos ||
                       (original_input.size() >= 2 && original_input[1] == ':') ||
                       original_input == "Desktop");
        std::string cmd = is_win
            ? "dir \"" + original_input + "\""
            : "ls -la \"" + original_input + "\"";
        result.should_retry    = true;
        result.fix_description = "list_dir failed; retrying with run_command";
        result.new_tool  = "run_command";
        result.new_input = cmd;
        result.context_note = "[Previous list_dir failed: " + error_output + "]";
        return result;
    }

    // --- Permission denied: report clearly, don't retry ---
    if (err_type == "permission_denied") {
        return std::nullopt;  // can't fix permission issues
    }

    // --- Network error: retry same tool once ---
    if (err_type == "network_error" && attempt_count < 1) {
        result.should_retry    = true;
        result.fix_description = "transient network error; retrying";
        result.new_tool  = failed_tool;
        result.new_input = original_input;
        return result;
    }

    // --- Command blocked: suggest safer alternative ---
    if (err_type == "command_blocked" && failed_tool == "run_command") {
        // Try without the dangerous part
        result.should_retry    = true;
        result.fix_description = "command blocked; using stat_file to verify";
        result.new_tool  = "stat_file";
        result.new_input = original_input;
        return result;
    }

    // No known fix
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// reflect_validation_failure: Manager-level reflection
// ---------------------------------------------------------------------------
std::optional<ReflectionResult> ReflectionEngine::reflect_validation_failure(
        const std::string& subtask_description,
        const std::string& validation_feedback,
        const std::string& /*available_tools*/,
        int                attempt_count) {

    if (attempt_count >= 2) return std::nullopt;

    // If feedback says "file not accessible" variants -- the subtask is done
    // Return nullopt to surface honest result rather than retry forever
    auto has = [&](const char* s){
        return validation_feedback.find(s) != std::string::npos ||
               subtask_description.find(s) != std::string::npos;
    };
    if (has("file") && (has("not found") || has("cannot") || has("error 3")))
        return std::nullopt;

    // If feedback says "wrong tool" -- suggest switching
    if (validation_feedback.find("tool") != std::string::npos &&
        attempt_count == 0) {
        ReflectionResult r;
        r.should_retry    = true;
        r.fix_description = "switching approach based on validation feedback";
        r.context_note    = "Previous validation feedback: " + validation_feedback;
        return r;
    }

    return std::nullopt;
}

// ---------------------------------------------------------------------------
// Experience recording
// ---------------------------------------------------------------------------
void ReflectionEngine::record_success(MemoryStore&       memory,
                                       const std::string& tool,
                                       const std::string& input_pattern,
                                       const std::string& output_summary) {
    std::string key = "exp:tool:" + tool;
    // Index into TF-IDF for semantic retrieval
    {
        std::string doc = tool + " " + input_pattern + " " + output_summary;
        g_exp_index.upsert(key + ":" + input_pattern.substr(0,30), doc);
    }
    std::string existing = memory.get(key);
    // Keep last 3 successful patterns
    std::string entry = input_pattern.substr(0,80) + " -> " + output_summary.substr(0,80);
    if (existing.empty()) {
        memory.set(key, entry);
    } else {
        // Append, cap at 3 entries
        auto lines = std::vector<std::string>();
        std::istringstream ss(existing);
        std::string line;
        while (std::getline(ss, line)) if (!line.empty()) lines.push_back(line);
        lines.push_back(entry);
        if (lines.size() > 3) lines.erase(lines.begin());
        std::string combined;
        for (auto& l : lines) combined += l + "\n";
        memory.set(key, combined);
    }
}

void ReflectionEngine::record_failure(MemoryStore&       memory,
                                       const std::string& error_type,
                                       const std::string& context,
                                       const std::string& what_fixed_it) {
    std::string key = "exp:fail:" + error_type;
    // Index failure+fix into TF-IDF
    {
        std::string doc = error_type + " " + context + " fix:" + what_fixed_it;
        g_exp_index.upsert(key + ":" + context.substr(0,30), doc);
    }
    std::string entry = context.substr(0,60) + " | fix: " + what_fixed_it.substr(0,80);
    std::string existing = memory.get(key);
    if (existing.empty()) {
        memory.set(key, entry);
    } else {
        // Keep last 3
        auto lines = std::vector<std::string>();
        std::istringstream ss(existing);
        std::string line;
        while (std::getline(ss, line)) if (!line.empty()) lines.push_back(line);
        lines.push_back(entry);
        if (lines.size() > 3) lines.erase(lines.begin());
        std::string combined;
        for (auto& l : lines) combined += l + "\n";
        memory.set(key, combined);
    }
}

std::string ReflectionEngine::recall_experience(const MemoryStore& memory,
                                                  const std::string& query) {
    std::ostringstream result;

    // ── Primary: TF-IDF semantic search over indexed experiences ──
    if (g_exp_index.size() > 0) {
        auto hits = g_exp_index.query(query, 3, 0.06);
        for (const auto& hit : hits) {
            result << "[Experience(sim=" << (int)(hit.score*100) << "%): "
                   << hit.text.substr(0, 100) << "] ";
        }
        if (!result.str().empty()) return result.str();
    }

    // ── Fallback: exact key lookup (original behaviour) ──
    for (const auto& tool : {"read_file","list_dir","run_command",
                               "stat_file","get_env","find_files",
                               "get_current_dir","get_sysinfo","write_file"}) {
        std::string key = std::string("exp:tool:") + tool;
        if (query.find(tool) != std::string::npos) {
            std::string val = const_cast<MemoryStore&>(memory).get(key);
            if (!val.empty())
                result << "[Past " << tool << ": " << val.substr(0,80) << "] ";
        }
    }
    for (const auto& etype : {"file_not_found","permission_denied","path_error"}) {
        if (query.find("cannot") != std::string::npos ||
            query.find("not found") != std::string::npos ||
            query.find(etype) != std::string::npos) {
            std::string val = const_cast<MemoryStore&>(memory).get(
                std::string("exp:fail:") + etype);
            if (!val.empty())
                result << "[Fix for " << etype << ": " << val.substr(0,80) << "] ";
        }
    }
    return result.str();
}

} // namespace agent
