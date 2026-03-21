#pragma once
// Code intelligence tools - find definitions, references, explain code
#include "agent/tool_registry.hpp"
#include <string>

namespace indexer {

// find_definition: locate where a symbol is defined
// Input:  "symbol_name\nroot_directory"
// Output: file:line:context or "not found"
std::string tool_find_definition(const std::string& input);

// find_references: find all usages of a symbol
// Input:  "symbol_name\nroot_directory"
// Output: list of file:line:context entries
std::string tool_find_references(const std::string& input);

// explain_code: extract and format code snippet with context
// Input:  "file_path\nstart_line\nend_line"
// Output: formatted code with line numbers
std::string tool_explain_code(const std::string& input);

// find_callers: find functions that call a given function
// Input:  "function_name\nroot_directory"
// Output: list of caller locations
std::string tool_find_callers(const std::string& input);

// find_implementations: find implementations of an interface/base class
// Input:  "class_name\nroot_directory"
// Output: list of implementation locations
std::string tool_find_implementations(const std::string& input);

// analyze_crash: parse stack trace and locate bug
// Input:  stack trace text
// Output: file locations and analysis
std::string tool_analyze_crash(const std::string& input);

// run_tests: detect test framework and run tests
// Input:  "root_directory"
// Output: test results
std::string tool_run_tests(const std::string& input);

// create_fix_branch: git workflow automation
// Input:  "branch_name"
// Output: branch creation status
std::string tool_create_fix_branch(const std::string& input);

// Register code intelligence tools
void register_indexer_tools(agent::ToolRegistry& registry);

} // namespace indexer
