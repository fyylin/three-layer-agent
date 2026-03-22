#pragma once
// =============================================================================
// include/agent/api_client.hpp
//
// Multi-provider HTTP client.  Supports:
//   Provider::Anthropic   --  Messages API  (api.anthropic.com/v1/messages)
//   Provider::OpenAI      --  Chat Completions (api.openai.com/v1/chat/completions)
//   Provider::Azure       --  Azure OpenAI deployment endpoint
//   Provider::Ollama      --  Ollama local server (OpenAI-compatible)
//   Provider::Custom      --  Any OpenAI-compatible endpoint via base_url
// =============================================================================

#include "agent/exceptions.hpp"
#include "agent/models.hpp"
#include <functional>
#include <string>
#include <vector>

namespace agent {

// Per-call configuration (derived from AgentConfig + per-layer ModelSpec)
struct ApiConfig {
    // Connection
    Provider    provider         = Provider::Anthropic;
    std::string api_key;
    std::string base_url;        // empty = use provider default
    std::string api_version;     // Anthropic header / Azure api-version param
    std::string organization;    // OpenAI Org-ID

    // Model
    std::string model            = "claude-opus-4-5-20251101";
    int         max_tokens       = 2048;
    double      temperature      = -1;   // -1 = omit
    double      top_p            = -1;   // -1 = omit

    // Network
    int         request_timeout  = 60;
    int         connect_timeout  = 10;
    int         max_retries      = 3;
};

struct Message {
    std::string role;     // "user" | "assistant" | "system"
    std::string content;
};

// -----------------------------------------------------------------------------
class ApiClient {
public:
    explicit ApiClient(ApiConfig cfg);
    ~ApiClient();

    ApiClient(const ApiClient&)            = delete;
    ApiClient& operator=(const ApiClient&) = delete;

    // Single-turn convenience
    [[nodiscard]] std::string complete(
        const std::string& system_prompt,
        const std::string& user_message,
        const std::string& task_id = "");

    // Full history
    [[nodiscard]] std::string complete(
        const std::string&          system_prompt,
        const std::vector<Message>& history,
        const std::string&          task_id = "");

    // Streaming API - callback receives each chunk as it arrives
    using ChunkCallback = std::function<void(const std::string&)>;
    void complete_stream(
        const std::string& system_prompt,
        const std::string& user_message,
        ChunkCallback      on_chunk,
        const std::string& task_id = "");

    // Change model/params at runtime (e.g. per-layer spec)
    void reconfigure(const ApiConfig& cfg) { cfg_ = cfg; }
    [[nodiscard]] const ApiConfig& config() const noexcept { return cfg_; }

    // Token usage accumulated since last reset_usage() call
    [[nodiscard]] const TokenUsage& usage() const noexcept { return last_usage_; }
    void reset_usage() noexcept { last_usage_ = TokenUsage{}; }

private:
    ApiConfig  cfg_;
    mutable TokenUsage last_usage_;  // mutable: updated by const extract_text()

    // Provider-specific body builders
    [[nodiscard]] std::string build_anthropic_body(
        const std::string& system, const std::vector<Message>& msgs) const;
    [[nodiscard]] std::string build_openai_body(
        const std::string& system, const std::vector<Message>& msgs) const;

    // Resolve the effective endpoint URL + path
    struct Endpoint { std::string host; std::string path; bool tls; int port; };
    [[nodiscard]] Endpoint resolve_endpoint() const;

    // Build HTTP request headers map
    [[nodiscard]] std::string build_headers_str() const;

    [[nodiscard]] std::string post_with_retry(
        const std::string& body, const std::string& task_id);

    [[nodiscard]] std::pair<int,std::string> http_post(
        const std::string& body);

    void http_post_stream(const std::string& body, ChunkCallback on_chunk);

    [[nodiscard]] std::string extract_text(
        const std::string& raw, const std::string& task_id) const;
};

// Factory: build ApiConfig for a specific layer from the global AgentConfig
[[nodiscard]] ApiConfig make_api_config(
    const AgentConfig& cfg,
    const ModelSpec&   layer_spec);

} // namespace agent
