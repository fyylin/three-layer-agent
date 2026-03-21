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

// Register code intelligence tools
void register_indexer_tools(agent::ToolRegistry& registry);

} // namespace indexer
