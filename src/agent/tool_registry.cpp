// =============================================================================
// src/agent/tool_registry.cpp
// =============================================================================
#include "agent/tool_registry.hpp"
#include "agent/tool_cache.hpp"
#include "utils/logger.hpp"

#include <algorithm>
#include <stdexcept>
#include <set>
#include <sys/stat.h>

namespace agent {

namespace {
const std::set<std::string> IDEMPOTENT_TOOLS = {
    "read_file", "list_dir", "stat_file", "find_files",
    "get_current_dir", "get_sysinfo", "get_env"
};

std::time_t get_mtime(const std::string& path) {
    struct stat st;
    return (stat(path.c_str(), &st) == 0) ? st.st_mtime : 0;
}
}

void ToolRegistry::register_tool(const std::string& name, ToolFn fn) {
    std::unique_lock<std::shared_mutex> lk(mu_);
    tools_[name] = std::move(fn);
    LOG_INFO("ToolRegistry", "registry", "", "registered tool: " + name);
}

bool ToolRegistry::has_tool(const std::string& name) const noexcept {
    std::shared_lock<std::shared_mutex> lk(mu_);
    return tools_.count(name) > 0;
}

std::string ToolRegistry::invoke(const std::string& name,
                                  const std::string& input,
                                  const std::string& task_id) const {
    if (IDEMPOTENT_TOOLS.count(name)) {
        CacheKey key{name, input, get_mtime(input)};
        if (auto cached = cache_.get(key)) {
            LOG_DEBUG("ToolRegistry", "cache", task_id, "cache hit: " + name);
            return *cached;
        }
    }

    ToolFn fn;
    {
        std::shared_lock<std::shared_mutex> lk(mu_);
        auto it = tools_.find(name);
        if (it == tools_.end())
            throw ToolException(name, "tool not registered", task_id, "ToolRegistry");
        fn = it->second;
    }
    LOG_DEBUG("ToolRegistry", "registry", task_id, "invoking tool: " + name);
    try {
        auto result = fn(input);
        if (IDEMPOTENT_TOOLS.count(name)) {
            CacheKey key{name, input, get_mtime(input)};
            cache_.put(key, result);
        }
        return result;
    } catch (const std::exception& e) {
        throw ToolException(name, e.what(), task_id, "ToolRegistry");
    } catch (...) {
        throw ToolException(name, "unknown exception", task_id, "ToolRegistry");
    }
}

std::vector<std::string> ToolRegistry::tool_names() const {
    std::shared_lock<std::shared_mutex> lk(mu_);
    std::vector<std::string> names;
    names.reserve(tools_.size());
    for (const auto& [k, _] : tools_) names.push_back(k);
    std::sort(names.begin(), names.end());
    return names;
}

} // namespace agent

