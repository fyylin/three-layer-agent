#pragma once
// =============================================================================
// include/utils/tool_set.hpp
//
// Comprehensive built-in tool set.  Each tool is a free function with signature:
//   std::string tool_name(const std::string& input)
//
// Tools are organised into three families:
//   1. Filesystem   --  list_dir, stat_file, find_files, read_file, write_file, delete_file
//   2. System API   --  get_env, set_env, get_sysinfo, get_process_list, get_current_dir
//   3. Shell        --  run_command (sandboxed; blocked commands list configurable)
//
// All tools are registered into ToolRegistry by register_all_tools().
// build_tool_doc() generates the LLM-facing documentation string.
// =============================================================================

#include "agent/tool_registry.hpp"
#include <string>
#include <vector>

namespace agent {

// -- Filesystem ----------------------------------------------------------------

// list_dir: list directory contents
// Input:  path  --  "Desktop", absolute path, or env-var path
// Output: [DIR]/[FILE] entries with sizes
std::string tool_list_dir(const std::string& path);

// stat_file: get metadata for a single file or directory
// Input:  path
// Output: JSON: {"name":"..", "type":"file|dir", "size":N, "modified":".."}
std::string tool_stat_file(const std::string& path);

// find_files: search for files matching a pattern
// Input:  "dir_path\npattern"  (second line is glob pattern, e.g. "*.txt")
// Output: list of matching paths
std::string tool_find_files(const std::string& input);

// read_file: read file contents (text files only; first 64 KB)
// Input:  path
// Output: file text (truncated at 65536 bytes with a notice)
std::string tool_read_file(const std::string& path);

// write_file: write text to a file (creates parent dirs)
// Input:  "path\ncontent..."  (first line = path, rest = content)
// Output: confirmation
std::string tool_write_file(const std::string& input);

// delete_file: delete a file (NOT a directory; no recursive delete)
// Input:  path
// Output: confirmation or error
std::string tool_delete_file(const std::string& path);

// -- System API ----------------------------------------------------------------

// get_env: read one environment variable
// Input:  variable name (e.g. USERPROFILE, HOME, PATH)
// Output: value or "(not set)"
std::string tool_get_env(const std::string& name);

// get_sysinfo: return OS, hostname, username, CPU count, memory
// Input:  "" or "all"
// Output: JSON object with system details
std::string tool_get_sysinfo(const std::string& /*ignored*/);

// get_process_list: list running processes (name + PID)
// Input:  "" or filter substring
// Output: one "PID  name" per line
std::string tool_get_process_list(const std::string& filter);

// get_current_dir: return current working directory
// Input:  "" (ignored)
// Output: absolute path string
std::string tool_get_current_dir(const std::string&);

// -- Shell ---------------------------------------------------------------------

// run_command: execute a shell command and capture output
// Input:  "command_string"   --  e.g. "dir C:\\Users\\Alice\\Desktop" or "ls ~"
// Output: stdout + stderr (first 8 KB), exit code on last line
// Safety: blocked commands: rm -rf, del /f /s, format, mkfs, shutdown, etc.
std::string tool_run_command(const std::string& cmd);

// echo: return input unchanged (for testing / data relay)
std::string tool_echo(const std::string& input);

// -- Registration --------------------------------------------------------------

// Register all built-in tools into the given registry.
void register_all_tools(ToolRegistry& registry);
void load_dynamic_tools(ToolRegistry& registry);  // load tools from workspace/tools/

// Build the LLM documentation string for the registered tool names.
std::string build_tool_doc(const std::vector<std::string>& tool_names);

// Resolve user-friendly path aliases ("Desktop", "~", "<HOME>", etc.)
std::string resolve_path(const std::string& raw);

} // namespace agent
