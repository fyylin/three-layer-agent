#pragma once
#include <string>
#include <vector>
#include <functional>
#include <map>

namespace agent::cli {

struct CommandOption {
    std::string name;
    std::string description;
    std::string default_value;
    std::vector<std::string> choices;
};

struct Command {
    std::string name;
    std::string description;
    std::vector<CommandOption> options;
    std::function<void(const std::map<std::string, std::string>&)> handler;
};

class CommandSystem {
public:
    void register_command(const Command& cmd);
    bool execute(const std::string& input);
    std::vector<std::string> get_completions(const std::string& prefix) const;
    void show_help(const std::string& cmd_name = "") const;
    std::vector<std::string> list_commands() const;
    const Command* get_command(const std::string& name) const;

private:
    std::map<std::string, Command> commands_;
};

// Model configuration per agent
struct ModelConfig {
    std::string model = "claude-sonnet-4-5-20251015";
    std::string provider = "anthropic";
    std::string api_key;
    std::string base_url;
    double temperature = 0.7;
    int max_tokens = 4096;
    double top_p = 1.0;
    int top_k = -1;
};

// Global settings accessible to all agents
struct GlobalSettings {
    int max_retries = 3;
    bool auto_approve_after_retries = true;
    bool verbose_logging = false;
    bool use_global_context = true;
    std::string default_model = "claude-sonnet-4-5-20251015";
    int timeout_seconds = 120;
    bool parallel_execution = true;
    bool strict_validation = false;
    double temperature = 0.7;
    int max_tokens = 4096;

    // Per-agent model configs
    ModelConfig supervisor_model;
    ModelConfig director_model;
    ModelConfig manager_model;
    ModelConfig worker_model;

    // Multi-provider support
    bool enable_multi_provider = false;
    std::vector<std::string> fallback_providers;

    // Statistics
    uint64_t total_tasks = 0;
    uint64_t successful_tasks = 0;
    uint64_t failed_tasks = 0;
    uint64_t total_retries = 0;
    uint64_t cache_hits = 0;
    uint64_t cache_misses = 0;
};

extern GlobalSettings g_settings;

// Register all built-in commands
void register_all_commands(CommandSystem& sys);

} // namespace agent::cli
