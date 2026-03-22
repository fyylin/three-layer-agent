// tests/stub_api_client.cpp  --  no-network ApiClient for test builds
#include "agent/api_client.hpp"
#include <string>
#include <utility>
#include <vector>

namespace agent {

thread_local std::string g_stub_response =
    R"({"status":"done","output":"stub output","error":""})";
thread_local std::vector<std::string> g_stub_responses;
thread_local bool g_stub_throw = false;

static std::string next_stub_response() {
    if (!g_stub_responses.empty()) {
        std::string out = std::move(g_stub_responses.front());
        g_stub_responses.erase(g_stub_responses.begin());
        return out;
    }
    return g_stub_response;
}

ApiClient::ApiClient(ApiConfig cfg) : cfg_(std::move(cfg)) {}
ApiClient::~ApiClient() = default;

std::string ApiClient::complete(const std::string&,const std::string&,const std::string&) {
    if(g_stub_throw) throw NetworkException("stub: forced error");
    return next_stub_response();
}
std::string ApiClient::complete(const std::string& s,const std::vector<Message>& h,const std::string& t) {
    return complete(s, h.empty()?"":h.back().content, t);
}
void ApiClient::complete_stream(const std::string&, const std::string&, ChunkCallback on_chunk, const std::string&) {
    if (g_stub_throw) throw NetworkException("stub: forced error");
    std::string out = next_stub_response();
    if (on_chunk) on_chunk(out);
}
std::string ApiClient::build_anthropic_body(const std::string&,const std::vector<Message>&) const { return "{}"; }
std::string ApiClient::build_openai_body(const std::string&,const std::vector<Message>&) const { return "{}"; }
ApiClient::Endpoint ApiClient::resolve_endpoint() const { return {"localhost","/",false,8080}; }
std::string ApiClient::build_headers_str() const { return ""; }
std::string ApiClient::post_with_retry(const std::string&,const std::string&) { return next_stub_response(); }
std::pair<int,std::string> ApiClient::http_post(const std::string&) { return {200, next_stub_response()}; }
std::string ApiClient::extract_text(const std::string& r,const std::string&) const { return r; }

ApiConfig make_api_config(const AgentConfig& cfg, const ModelSpec& spec) {
    ApiConfig a;
    a.provider    = cfg.provider;
    a.api_key     = cfg.api_key;
    a.model       = cfg.effective_model(spec);
    a.max_tokens  = cfg.effective_max_tokens(spec);
    a.temperature = cfg.effective_temperature(spec);
    return a;
}

} // namespace agent

