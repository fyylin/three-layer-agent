#pragma once
// =============================================================================
// include/agent/tool_registry.hpp
//
// Thread-safe registry of named tool functions available to WorkerAgents.
// Tools are registered at startup (main.cpp) and are read-only during
// execution, so we use std::shared_mutex for maximum read concurrency.
// =============================================================================

#include "agent/exceptions.hpp"
#include "agent/tool_cache.hpp"
#include <functional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace agent {

// All tools must conform to this signature: string input → string output.
// Throw std::exception on failure; the caller catches and creates ToolException.
using ToolFn = std::function<std::string(const std::string& input)>;

class ToolRegistry {
public:
    ToolRegistry() = default;

    // Non-copyable (mutex is not copyable)
    ToolRegistry(const ToolRegistry&)            = delete;
    ToolRegistry& operator=(const ToolRegistry&) = delete;

    // Register a new tool. Overwrites if name already exists.
    void register_tool(const std::string& name, ToolFn fn);

    // Returns true if `name` is registered.
    [[nodiscard]] bool has_tool(const std::string& name) const noexcept;

    // Execute tool `name` with `input`. Wraps any exception in ToolException.
    [[nodiscard]] std::string invoke(const std::string& name,
                                     const std::string& input,
                                     const std::string& task_id = "") const;

    // Returns sorted list of all registered tool names (for prompt injection).
    [[nodiscard]] std::vector<std::string> tool_names() const;

private:
    mutable std::shared_mutex                      mu_;
    std::unordered_map<std::string, ToolFn>        tools_;
    mutable ToolCache                              cache_;
};

} // namespace agent
