#pragma once
// =============================================================================
// include/agent/models.hpp
// =============================================================================

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <nlohmann/json.hpp>

namespace agent {

// -----------------------------------------------------------------------------
// TaskStatus
// -----------------------------------------------------------------------------

enum class TaskStatus : uint8_t {
    Pending   = 0,
    Running   = 1,
    Done      = 2,
    Failed    = 3,
    Rejected  = 4,
    Cancelled = 5
};

[[nodiscard]] std::string  status_to_string(TaskStatus s) noexcept;
[[nodiscard]] TaskStatus   status_from_string(const std::string& s);

// -----------------------------------------------------------------------------
// Layer 3 ↔ Layer 2
// -----------------------------------------------------------------------------

struct TaskContext;  // forward declaration

struct AtomicTask {
    std::string id;
    std::string parent_id;
    std::string description;
    std::string tool;
    std::string input;
    std::vector<std::string> depends_on;
    std::string input_from;
    std::shared_ptr<TaskContext> context;  // shared context
};

struct AtomicResult {
    std::string task_id;
    TaskStatus  status = TaskStatus::Pending;
    std::string output;
    std::string error;
    std::string thought;     // LLM reasoning (CoT)
    bool        fast_path = false;  // true = tool executed directly, no LLM needed
};

void to_json  (nlohmann::json& j, const AtomicTask&   t);
void from_json(const nlohmann::json& j, AtomicTask&   t);
void to_json  (nlohmann::json& j, const AtomicResult& r);
void from_json(const nlohmann::json& j, AtomicResult& r);

// -----------------------------------------------------------------------------
// Layer 2 ↔ Layer 1
// -----------------------------------------------------------------------------

struct SubTask {
    std::string id;
    std::string description;
    std::string expected_output;
    std::string retry_feedback;
    std::shared_ptr<TaskContext> context;
};

struct SubTaskReport {
    std::string               subtask_id;
    TaskStatus                status = TaskStatus::Pending;
    std::string               summary;
    std::vector<AtomicResult> results;
    std::string               issues;
};

void to_json  (nlohmann::json& j, const SubTask&       t);
void from_json(const nlohmann::json& j, SubTask&       t);
void to_json  (nlohmann::json& j, const SubTaskReport& r);
void from_json(const nlohmann::json& j, SubTaskReport& r);

// -----------------------------------------------------------------------------
// Layer 1
// -----------------------------------------------------------------------------

struct UserGoal {
    std::string description;
    std::shared_ptr<TaskContext> context;
};
struct ReviewFeedback {
    std::string subtask_id;
    bool        approved = false;
    std::string feedback;
};
// TokenUsage  --  accumulated API token consumption
// -----------------------------------------------------------------------------
struct TokenUsage {
    int64_t input_tokens    = 0;
    int64_t output_tokens   = 0;
    int64_t cached_tokens   = 0;   // Anthropic prompt caching hits
    double  estimated_cost  = 0.0; // USD (rough estimate)

    TokenUsage& operator+=(const TokenUsage& o) noexcept {
        input_tokens   += o.input_tokens;
        output_tokens  += o.output_tokens;
        cached_tokens  += o.cached_tokens;
        estimated_cost += o.estimated_cost;
        return *this;
    }
    [[nodiscard]] bool empty() const noexcept {
        return input_tokens == 0 && output_tokens == 0;
    }
};


struct FinalResult {
    TaskStatus                 status      = TaskStatus::Pending;
    std::string                answer;
    std::vector<SubTaskReport> sub_reports;
    std::string                error;
    std::string                started_at;
    std::string                finished_at;
    TokenUsage                 usage;   // accumulated token cost
};

void to_json  (nlohmann::json& j, const UserGoal&       g);
void from_json(const nlohmann::json& j, UserGoal&       g);
void to_json  (nlohmann::json& j, const ReviewFeedback& f);
void from_json(const nlohmann::json& j, ReviewFeedback& f);
void to_json  (nlohmann::json& j, const FinalResult&    r);

// -----------------------------------------------------------------------------
// Provider   --   which API format to use
// -----------------------------------------------------------------------------

enum class Provider : uint8_t {
    Anthropic = 0,   // Messages API  (api.anthropic.com)
    OpenAI    = 1,   // Chat Completions API (api.openai.com or compatible)
    Azure     = 2,   // Azure OpenAI  (Chat Completions with deployment URL)
    Ollama    = 3,   // Ollama local  (OpenAI-compatible, http://localhost:11434)
    Custom    = 4    // User-specified base_url + OpenAI-compatible format
};

[[nodiscard]] std::string provider_to_string(Provider p) noexcept;
[[nodiscard]] Provider    provider_from_string(const std::string& s);

// -----------------------------------------------------------------------------
// ModelSpec   --   per-layer model override
// If model is empty, falls back to AgentConfig.default_model.
// -----------------------------------------------------------------------------

struct ModelSpec {
    std::string model;      // e.g. "claude-opus-4-5" or "gpt-4o"  --  empty = use default
    int         max_tokens = 0;   // 0 = use AgentConfig.max_tokens
    double      temperature = -1; // -1 = use provider default (omit from request)
    double      top_p       = -1; // -1 = use provider default
};

void to_json  (nlohmann::json& j, const ModelSpec& s);
void from_json(const nlohmann::json& j, ModelSpec& s);

// -----------------------------------------------------------------------------
// AgentConfig   --   full runtime configuration
// -----------------------------------------------------------------------------

struct AgentConfig {
    // -- Provider & endpoint -----------------------------------------------
    Provider    provider          = Provider::Anthropic;
    std::string api_key;                             // auth token
    std::string base_url;                            // override endpoint URL
    std::string api_version;                         // Azure: deployment api-version
                                                     // Anthropic: anthropic-version header
    std::string organization;                        // OpenAI: Org-ID header

    // -- Default model (used when per-layer spec is empty) ----------------
    std::string default_model     = "claude-opus-4-5-20251101";

    // -- Per-layer model overrides ----------------------------------------
    // Each layer can use a different model, e.g. cheaper model for Workers
    ModelSpec   director_model;  // Layer 1  --  decompose + review + synthesise
    ModelSpec   manager_model;   // Layer 2  --  decompose subtasks + validate
    ModelSpec   worker_model;    // Layer 3  --  execute atomic tasks
    ModelSpec   supervisor_model; // Layer 0  --  quality eval

    // -- Generation defaults -----------------------------------------------
    int         max_tokens        = 2048;
    double      temperature       = -1;  // -1 = omit (use provider default)
    double      top_p             = -1;  // -1 = omit

    // -- Network -----------------------------------------------------------
    int         request_timeout   = 60;
    int         connect_timeout   = 10;
    int         max_network_retries = 3;

    // -- Agent retry logic -------------------------------------------------
    int         max_subtask_retries = 2;
    int         max_atomic_retries  = 3;

    // -- Runtime -----------------------------------------------------------
    int         worker_threads    = 4;
    std::string log_level         = "info";
    std::string prompt_dir        = "./prompts";
    // When true, .md files in prompt_dir take precedence over .txt fallbacks
    bool        use_md_prompts    = true;

    // -- Workspace (v2) --------------------------------------------------------
    std::string workspace_dir            = "workspace";

    // -- Memory (v2) -----------------------------------------------------------
    int         memory_short_term_window = 8;
    bool        memory_session_enabled   = true;
    bool        memory_long_term_enabled = false;

    // -- Supervisor (v2) -------------------------------------------------------
    int         supervisor_poll_interval_ms  = 5000;   // monitor poll interval
    int         supervisor_stuck_timeout_ms  = 300000; // 5min = stuck threshold
    int         supervisor_max_fail_count    = 5;      // per-agent fail threshold
    bool        supervisor_advisor_enabled   = true;   // use AdvisorAgent on failures
    int         supervisor_max_retries       = 2;      // quality gate retries

    // -- Budget control -------------------------------------------------------
    double      max_cost_per_run_usd  = 0.0;  // 0 = unlimited; stops run if exceeded
    int         max_tokens_per_run    = 0;     // 0 = unlimited

    // -- I/O ------------------------------------------------------------------
    static AgentConfig load(const std::string& path);
    void               save(const std::string& path) const;
    void               validate() const;  // throws if config is invalid

    // Resolve the effective model string for a layer
    [[nodiscard]] std::string effective_model(const ModelSpec& layer_spec) const {
        return layer_spec.model.empty() ? default_model : layer_spec.model;
    }
    [[nodiscard]] int effective_max_tokens(const ModelSpec& layer_spec) const {
        return (layer_spec.max_tokens > 0) ? layer_spec.max_tokens : max_tokens;
    }
    [[nodiscard]] double effective_temperature(const ModelSpec& layer_spec) const {
        return (layer_spec.temperature >= 0) ? layer_spec.temperature : temperature;
    }
};

void to_json  (nlohmann::json& j, const AgentConfig& c);
void from_json(const nlohmann::json& j, AgentConfig& c);

} // namespace agent
