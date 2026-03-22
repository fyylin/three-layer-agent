// =============================================================================
// src/agent/api_client.cpp
// Multi-provider HTTP client  --  Windows WinHTTP / Linux OpenSSL-free POSIX TLS
//
// Supports:
//   Provider::Anthropic  → Messages API  (anthropic-specific headers + body)
//   Provider::OpenAI     → Chat Completions (Bearer auth, standard body)
//   Provider::Azure      → Chat Completions (api-key header, deployment URL)
//   Provider::Ollama     → Chat Completions (no auth, localhost)
//   Provider::Custom     → Chat Completions (user-supplied base_url)
// =============================================================================

#include "agent/api_client.hpp"
#include "utils/logger.hpp"
#include <nlohmann/json.hpp>
#include <functional>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <winhttp.h>
#  pragma comment(lib,"winhttp.lib")
#else
#  include <arpa/inet.h>
#  include <netdb.h>
#  include <netinet/in.h>
#  include <sys/socket.h>
#  include <unistd.h>
#  include <openssl/ssl.h>
#  include <openssl/err.h>
#endif

#include <chrono>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace agent {

// -----------------------------------------------------------------------------
// make_api_config   --   flatten AgentConfig + per-layer ModelSpec into ApiConfig
// -----------------------------------------------------------------------------
ApiConfig make_api_config(const AgentConfig& cfg, const ModelSpec& layer_spec) {
    ApiConfig a;
    a.provider        = cfg.provider;
    a.api_key         = cfg.api_key;
    a.base_url        = cfg.base_url;
    a.api_version     = cfg.api_version;
    a.organization    = cfg.organization;
    a.model           = cfg.effective_model(layer_spec);
    a.max_tokens      = cfg.effective_max_tokens(layer_spec);
    a.temperature     = cfg.effective_temperature(layer_spec);
    a.top_p           = (layer_spec.top_p >= 0) ? layer_spec.top_p : cfg.top_p;
    a.request_timeout = cfg.request_timeout;
    a.connect_timeout = cfg.connect_timeout;
    a.max_retries     = cfg.max_network_retries;
    return a;
}

// -----------------------------------------------------------------------------
// Constants
// -----------------------------------------------------------------------------
namespace {
constexpr const char* kDefaultAnthropicVersion = "2023-06-01";

struct ParsedUrl {
    bool        tls  = true;
    std::string host;
    int         port = 443;
    std::string path;
};

ParsedUrl parse_url(const std::string& url) {
    ParsedUrl r;
    std::string u = url;
    if(u.substr(0,8)=="https://") { r.tls=true;  u=u.substr(8); }
    else if(u.substr(0,7)=="http://") { r.tls=false; u=u.substr(7); r.port=80; }
    auto slash = u.find('/');
    std::string hostport = (slash==std::string::npos) ? u : u.substr(0,slash);
    r.path = (slash==std::string::npos) ? "/" : u.substr(slash);
    auto colon = hostport.rfind(':');
    if(colon!=std::string::npos){
        r.host = hostport.substr(0,colon);
        r.port = std::stoi(hostport.substr(colon+1));
    } else {
        r.host = hostport;
    }
    return r;
}
} // anon

// -----------------------------------------------------------------------------
// Endpoint resolution
// -----------------------------------------------------------------------------
ApiClient::Endpoint ApiClient::resolve_endpoint() const {
    // If user supplied a base_url, parse it
    if(!cfg_.base_url.empty()){
        auto p = parse_url(cfg_.base_url);
        std::string path = p.path;
        // Append correct API path if base_url doesn't already include it
        if(cfg_.provider == Provider::Anthropic){
            if(path.back()=='/') path+="v1/messages";
            else if(path.find("/v1/messages")==std::string::npos) path+="/v1/messages";
        } else {
            if(path.back()=='/') path+="v1/chat/completions";
            else if(path.find("/chat/completions")==std::string::npos) path+="/v1/chat/completions";
        }
        return {p.host, path, p.tls, p.port};
    }
    // Provider defaults
    switch(cfg_.provider){
        case Provider::Anthropic:
            return {"api.anthropic.com", "/v1/messages", true, 443};
        case Provider::OpenAI:
            return {"api.openai.com", "/v1/chat/completions", true, 443};
        case Provider::Azure: {
            // base_url required for Azure; if missing, fail gracefully
            if(cfg_.base_url.empty())
                throw NetworkException("Azure provider requires base_url "
                    "(e.g. https://{resource}.openai.azure.com/openai/deployments/{deployment})");
            auto p = parse_url(cfg_.base_url);
            std::string path = p.path;
            if(path.find("chat/completions")==std::string::npos)
                path += "/chat/completions";
            if(!cfg_.api_version.empty()) path += "?api-version=" + cfg_.api_version;
            return {p.host, path, p.tls, p.port};
        }
        case Provider::Ollama:
            return {"localhost", "/v1/chat/completions", false, 11434};
        case Provider::Custom:
            throw NetworkException("Custom provider requires base_url");
    }
    return {"api.anthropic.com","/v1/messages",true,443};
}

// -----------------------------------------------------------------------------
// Body builders
// -----------------------------------------------------------------------------
std::string ApiClient::build_anthropic_body(
        const std::string& system, const std::vector<Message>& msgs) const {
    nlohmann::json messages = nlohmann::json::array();
    for(auto& m : msgs){
        if(m.role=="system") continue; // system handled separately
        nlohmann::json jm; jm["role"]=m.role; jm["content"]=m.content;
        messages.push_back(std::move(jm));
    }
    nlohmann::json body;
    body["model"]      = cfg_.model;
    body["max_tokens"] = nlohmann::json::from_int((int64_t)cfg_.max_tokens);
    // Cache system prompt (Anthropic prompt caching — reduces repeated token cost)
    if (!system.empty()) {
        nlohmann::json sys_arr = nlohmann::json::array();
        nlohmann::json sys_block;
        sys_block["type"] = "text";
        sys_block["text"] = system;
        sys_block["cache_control"] = nlohmann::json::object();
        sys_block["cache_control"]["type"] = "ephemeral";
        sys_arr.push_back(std::move(sys_block));
        body["system"] = sys_arr;
    }
    body["messages"]   = messages;
    if(cfg_.temperature >= 0) body["temperature"] = nlohmann::json::from_float(cfg_.temperature);
    if(cfg_.top_p       >= 0) body["top_p"]       = nlohmann::json::from_float(cfg_.top_p);
    return body.dump();
}

std::string ApiClient::build_openai_body(
        const std::string& system, const std::vector<Message>& msgs) const {
    nlohmann::json messages = nlohmann::json::array();
    // system message first
    if(!system.empty()){
        nlohmann::json sm; sm["role"]="system"; sm["content"]=system;
        messages.push_back(std::move(sm));
    }
    for(auto& m : msgs){
        nlohmann::json jm; jm["role"]=m.role; jm["content"]=m.content;
        messages.push_back(std::move(jm));
    }
    nlohmann::json body;
    body["model"]    = cfg_.model;
    body["messages"] = messages;
    if(cfg_.max_tokens > 0) body["max_tokens"] = nlohmann::json::from_int((int64_t)cfg_.max_tokens);
    if(cfg_.temperature >= 0) body["temperature"] = nlohmann::json::from_float(cfg_.temperature);
    if(cfg_.top_p       >= 0) body["top_p"]       = nlohmann::json::from_float(cfg_.top_p);
    return body.dump();
}

// -----------------------------------------------------------------------------
// HTTP header string builder
// -----------------------------------------------------------------------------
std::string ApiClient::build_headers_str() const {
    std::ostringstream h;
    h << "content-type: application/json\r\n";
    switch(cfg_.provider){
        case Provider::Anthropic:
            h << "x-api-key: " << cfg_.api_key << "\r\n";
            h << "anthropic-version: "
              << (cfg_.api_version.empty() ? kDefaultAnthropicVersion : cfg_.api_version)
              << "\r\n";
            break;
        case Provider::Azure:
            h << "api-key: " << cfg_.api_key << "\r\n";
            break;
        case Provider::Ollama:
            // Ollama: no auth by default; support optional bearer
            if(!cfg_.api_key.empty())
                h << "Authorization: Bearer " << cfg_.api_key << "\r\n";
            break;
        default: // OpenAI / Custom
            if(!cfg_.api_key.empty())
                h << "Authorization: Bearer " << cfg_.api_key << "\r\n";
            if(!cfg_.organization.empty())
                h << "OpenAI-Organization: " << cfg_.organization << "\r\n";
            break;
    }
    return h.str();
}

// -----------------------------------------------------------------------------
// extract_text  --  handles both Anthropic and OpenAI response formats
// -----------------------------------------------------------------------------
std::string ApiClient::extract_text(const std::string& raw,
                                     const std::string& task_id) const {
    try {
        auto j = nlohmann::json::parse(raw);
        // Error check (both APIs return "error" object on failure)
        if(j.contains("error")){
            std::string msg = j["error"].contains("message")
                ? j["error"]["message"].get<std::string>() : raw.substr(0,300);
            throw ModelException("API error: "+msg, 0, task_id, "ApiClient");
        }
        // Parse token usage (Anthropic + OpenAI both return usage field)
        if(j.contains("usage")){
            auto& u = j["usage"];
            last_usage_.input_tokens  += u.contains("input_tokens")    ? u["input_tokens"].get<int64_t>()    :
                                         u.contains("prompt_tokens")   ? u["prompt_tokens"].get<int64_t>()   : 0;
            last_usage_.output_tokens += u.contains("output_tokens")   ? u["output_tokens"].get<int64_t>()   :
                                         u.contains("completion_tokens")? u["completion_tokens"].get<int64_t>(): 0;
            last_usage_.cached_tokens += u.contains("cache_read_input_tokens")
                                         ? u["cache_read_input_tokens"].get<int64_t>() : 0;
            // Dynamic per-model pricing ($ per M tokens)
            // Update when Anthropic changes prices
            struct ModelPrice { double in_m; double out_m; double cache_m; };
            auto get_price = [&](const std::string& m) -> ModelPrice {
                // haiku family — cheapest
                if (m.find("haiku") != std::string::npos)
                    return {0.25, 1.25, 0.03};
                // sonnet family
                if (m.find("sonnet") != std::string::npos)
                    return {3.0, 15.0, 0.30};
                // opus family (default/fallback)
                return {15.0, 75.0, 1.50};
            };
            auto pr = get_price(cfg_.model);
            int64_t billable_input = last_usage_.input_tokens - last_usage_.cached_tokens;
            double in_cost    = (double)billable_input            / 1e6 * pr.in_m;
            double cache_cost = (double)last_usage_.cached_tokens / 1e6 * pr.cache_m;
            double out_cost   = (double)last_usage_.output_tokens / 1e6 * pr.out_m;
            last_usage_.estimated_cost = in_cost + cache_cost + out_cost;
        }

        // Anthropic format: content[0].text
        if(j.contains("content") && j["content"].is_array() && j["content"].size()>0){
            auto& c0 = j["content"][0];
            if(c0.contains("text") && c0["text"].is_string())
                return c0["text"].get<std::string>();
            // text is null (e.g. tool_use block)  --  try to get text from any block
            for(size_t ci=0; ci<j["content"].size(); ++ci){
                auto& cb = j["content"][ci];
                if(cb.contains("text") && cb["text"].is_string())
                    return cb["text"].get<std::string>();
            }
            throw ParseException("Anthropic response has no text content block", raw, task_id, "ApiClient");
        }
        // OpenAI format: choices[0].message.content
        if(j.contains("choices") && j["choices"].is_array() && j["choices"].size()>0){
            auto& ch0 = j["choices"][0];
            if(ch0.contains("message") && ch0["message"].contains("content")){
                auto& content = ch0["message"]["content"];
                if(content.is_string()) return content.get<std::string>();
                if(content.is_null()) {
                    // finish_reason=tool_calls or similar  --  return empty
                    return "";
                }
            }
        }
        throw ParseException("extract_text: unrecognised response format", raw, task_id, "ApiClient");
    } catch(const ModelException&){ throw;
    } catch(const ParseException&){ throw;
    } catch(const std::exception& e){
        throw ParseException(std::string("extract_text: ")+e.what(), raw, task_id, "ApiClient");
    }
}

// -----------------------------------------------------------------------------
// Public interface
// -----------------------------------------------------------------------------
ApiClient::ApiClient(ApiConfig cfg) : cfg_(std::move(cfg)) {
#ifndef _WIN32
    SSL_library_init();
    OpenSSL_add_all_algorithms();
    SSL_load_error_strings();
#endif
}
ApiClient::~ApiClient() = default;

std::string ApiClient::complete(const std::string& sys,
                                const std::string& user,
                                const std::string& tid) {
    return complete(sys, std::vector<Message>{{"user",user}}, tid);
}

std::string ApiClient::complete(const std::string& sys,
                                const std::vector<Message>& hist,
                                const std::string& tid) {
    std::string body = (cfg_.provider == Provider::Anthropic)
        ? build_anthropic_body(sys, hist)
        : build_openai_body(sys, hist);
    std::string raw = post_with_retry(body, tid);
    LOG_DEBUG("ApiClient", "api", tid,
        "RAW LLM RESPONSE (" + std::to_string(raw.size()) + " bytes):\n" + raw.substr(0, 2000));
    return extract_text(raw, tid);
}

void ApiClient::complete_stream(const std::string& sys,
                                const std::string& user,
                                std::function<void(const std::string&)> on_chunk,
                                const std::string& tid) {
    std::string body = (cfg_.provider == Provider::Anthropic)
        ? build_anthropic_body(sys, {{"user",user}})
        : build_openai_body(sys, {{"user",user}});

    // Add stream parameter by string manipulation (MSVC nlohmann::json bool issue)
    size_t pos = body.rfind('}');
    if (pos != std::string::npos) {
        body.insert(pos, ",\"stream\":true");
    }

    http_post_stream(body, on_chunk);
}

// -----------------------------------------------------------------------------
// Retry wrapper
// -----------------------------------------------------------------------------
// Parse Retry-After header value (seconds) from a JSON error body or header hint
static int parse_retry_after(const std::string& body) {
    // Anthropic 429 body: {"error":{"type":"rate_limit_error","message":"..."}}
    // Some providers include retry-after as a number in the body or headers
    // Try to find "retry-after" or "retry_after" as a number
    auto try_key = [&](const char* key) -> int {
        auto pos = body.find(key);
        if (pos == std::string::npos) return -1;
        pos = body.find_first_of("0123456789", pos + std::strlen(key));
        if (pos == std::string::npos) return -1;
        int v = 0;
        while (pos < body.size() && std::isdigit((unsigned char)body[pos]))
            v = v * 10 + (body[pos++] - '0');
        return (v > 0 && v <= 3600) ? v * 1000 : -1;  // convert to ms, cap at 1h
    };
    int v = try_key("retry-after");
    if (v < 0) v = try_key("retry_after");
    if (v < 0) v = try_key("\"retry\":");
    return v;  // -1 if not found
}

std::string ApiClient::post_with_retry(const std::string& body,
                                        const std::string& task_id) {
    int delay_ms = 1000;
    for(int attempt=0; attempt<=cfg_.max_retries; ++attempt){
        try {
            auto [status, resp] = http_post(body);
            if(status==200) return resp;

            // 429: Rate Limited — honour Retry-After, don't burn an attempt
            if(status==429) {
                int wait_ms = parse_retry_after(resp);
                if (wait_ms < 0) wait_ms = std::max(delay_ms, 5000);  // min 5s
                LOG_WARN("ApiClient","api",task_id,
                    "Rate limited (429), waiting "+std::to_string(wait_ms/1000)+"s");
                std::this_thread::sleep_for(std::chrono::milliseconds(wait_ms));
                delay_ms = std::min(delay_ms*2, 60000);
                continue;  // retry WITHOUT consuming attempt count
            }

            // 5xx / non-200 non-4xx: transient, use exponential backoff
            if(status>=500 || (status>200 && status<400)) {
                if(attempt==cfg_.max_retries)
                    throw ModelException("HTTP "+std::to_string(status)+" after "+
                                         std::to_string(attempt+1)+" attempts",
                                         status, task_id, "ApiClient", attempt);
                LOG_WARN("ApiClient","api",task_id,
                    "HTTP "+std::to_string(status)+", retry "+std::to_string(attempt+1));
            } else {
                // 4xx (not 429): client error, don't retry
                throw ModelException("HTTP "+std::to_string(status)+": "+resp.substr(0,300),
                                     status, task_id, "ApiClient", attempt);
            }
        } catch(const ModelException&){ throw;
        } catch(const std::exception& e){
            if(attempt==cfg_.max_retries)
                throw NetworkException(std::string("Network error after ")+
                    std::to_string(attempt+1)+" attempts: "+e.what(),
                    task_id,"ApiClient",attempt);
            LOG_WARN("ApiClient","api",task_id,
                std::string("Network error: ")+e.what()+", retry in "+std::to_string(delay_ms)+"ms");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        delay_ms = std::min(delay_ms*2, 30000);
    }
    throw NetworkException("post_with_retry: exhausted retries", task_id, "ApiClient");
}

// -----------------------------------------------------------------------------
// Platform-specific HTTP POST
// -----------------------------------------------------------------------------
#ifdef _WIN32

namespace {
std::wstring to_wide(const std::string& s){
    if(s.empty()) return {};
    int n=MultiByteToWideChar(CP_UTF8,0,s.data(),(int)s.size(),nullptr,0);
    if(n<=0) return {};
    std::wstring out(n,L'\0');
    MultiByteToWideChar(CP_UTF8,0,s.data(),(int)s.size(),out.data(),n);
    return out;
}
std::string win_err(DWORD code){
    LPWSTR buf=nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM|
                   FORMAT_MESSAGE_IGNORE_INSERTS,nullptr,code,
                   MAKELANGID(LANG_NEUTRAL,SUBLANG_DEFAULT),(LPWSTR)&buf,0,nullptr);
    if(!buf) return "WinHTTP error "+std::to_string(code);
    int n=WideCharToMultiByte(CP_UTF8,0,buf,-1,nullptr,0,nullptr,nullptr);
    std::string msg; if(n>0){msg.resize(n-1);WideCharToMultiByte(CP_UTF8,0,buf,-1,msg.data(),n,nullptr,nullptr);}
    LocalFree(buf);
    while(!msg.empty()&&(msg.back()=='\r'||msg.back()=='\n'))msg.pop_back();
    return msg;
}
struct WHandle{
    HINTERNET h=nullptr;
    explicit WHandle(HINTERNET h_):h(h_){}
    ~WHandle(){if(h)WinHttpCloseHandle(h);}
    WHandle(const WHandle&)=delete;
    explicit operator bool()const noexcept{return h!=nullptr;}
};
} // anon

std::pair<int,std::string> ApiClient::http_post(const std::string& body){
    auto ep = resolve_endpoint();

    WHandle session(WinHttpOpen(L"ThreeLayerAgent/2.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0));
    if(!session) throw NetworkException("WinHttpOpen: "+win_err(GetLastError()));

    int cto=cfg_.connect_timeout*1000, rto=cfg_.request_timeout*1000;
    WinHttpSetTimeouts(session.h,cto,cto,rto,rto);

    std::wstring whost=to_wide(ep.host);
    WHandle conn(WinHttpConnect(session.h,whost.c_str(),(INTERNET_PORT)ep.port,0));
    if(!conn) throw NetworkException("WinHttpConnect to "+ep.host+": "+win_err(GetLastError()));

    DWORD flags=ep.tls?WINHTTP_FLAG_SECURE:0;
    std::wstring wpath=to_wide(ep.path);
    WHandle req(WinHttpOpenRequest(conn.h,L"POST",wpath.c_str(),nullptr,
                                   WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,flags));
    if(!req) throw NetworkException("WinHttpOpenRequest: "+win_err(GetLastError()));

    std::string hdr_str=build_headers_str();
    std::wstring whdr=to_wide(hdr_str);
    WinHttpAddRequestHeaders(req.h,whdr.c_str(),(DWORD)whdr.size(),WINHTTP_ADDREQ_FLAG_ADD);

    if(!WinHttpSendRequest(req.h,WINHTTP_NO_ADDITIONAL_HEADERS,0,
                           (LPVOID)body.data(),(DWORD)body.size(),(DWORD)body.size(),0))
        throw NetworkException("WinHttpSendRequest: "+win_err(GetLastError()));
    if(!WinHttpReceiveResponse(req.h,nullptr))
        throw NetworkException("WinHttpReceiveResponse: "+win_err(GetLastError()));

    DWORD status=0,sz=sizeof(status);
    WinHttpQueryHeaders(req.h,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX,&status,&sz,WINHTTP_NO_HEADER_INDEX);

    std::string rbody; DWORD avail=0;
    while(WinHttpQueryDataAvailable(req.h,&avail)&&avail>0){
        std::vector<char> buf(avail+1,'\0'); DWORD read=0;
        if(!WinHttpReadData(req.h,buf.data(),avail,&read)) break;
        rbody.append(buf.data(),read);
    }
    return {(int)status,rbody};
}

void ApiClient::http_post_stream(const std::string& body, std::function<void(const std::string&)> on_chunk){
    auto ep = resolve_endpoint();
    WHandle session(WinHttpOpen(L"ThreeLayerAgent/2.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0));
    if(!session) throw NetworkException("WinHttpOpen: "+win_err(GetLastError()));

    int cto=cfg_.connect_timeout*1000, rto=cfg_.request_timeout*1000;
    WinHttpSetTimeouts(session.h,cto,cto,rto,rto);

    std::wstring whost=to_wide(ep.host);
    WHandle conn(WinHttpConnect(session.h,whost.c_str(),(INTERNET_PORT)ep.port,0));
    if(!conn) throw NetworkException("WinHttpConnect: "+win_err(GetLastError()));

    DWORD flags=ep.tls?WINHTTP_FLAG_SECURE:0;
    std::wstring wpath=to_wide(ep.path);
    WHandle req(WinHttpOpenRequest(conn.h,L"POST",wpath.c_str(),nullptr,
                                   WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,flags));
    if(!req) throw NetworkException("WinHttpOpenRequest: "+win_err(GetLastError()));

    std::string hdr_str=build_headers_str();
    std::wstring whdr=to_wide(hdr_str);
    WinHttpAddRequestHeaders(req.h,whdr.c_str(),(DWORD)whdr.size(),WINHTTP_ADDREQ_FLAG_ADD);

    if(!WinHttpSendRequest(req.h,WINHTTP_NO_ADDITIONAL_HEADERS,0,
                           (LPVOID)body.data(),(DWORD)body.size(),(DWORD)body.size(),0))
        throw NetworkException("WinHttpSendRequest: "+win_err(GetLastError()));
    if(!WinHttpReceiveResponse(req.h,nullptr))
        throw NetworkException("WinHttpReceiveResponse: "+win_err(GetLastError()));

    std::string buffer; DWORD avail=0;
    while(WinHttpQueryDataAvailable(req.h,&avail)&&avail>0){
        std::vector<char> buf(avail+1,'\0'); DWORD read=0;
        if(!WinHttpReadData(req.h,buf.data(),avail,&read)) break;
        buffer.append(buf.data(),read);

        size_t pos;
        while((pos=buffer.find('\n'))!=std::string::npos){
            std::string line=buffer.substr(0,pos);
            buffer.erase(0,pos+1);
            if(line.substr(0,6)=="data: "){
                std::string data=line.substr(6);
                if(data=="[DONE]") return;
                try{
                    auto j=nlohmann::json::parse(data);
                    if(cfg_.provider==Provider::Anthropic){
                        if(j.contains("delta")&&j["delta"].contains("text"))
                            on_chunk(j["delta"]["text"].get<std::string>());
                    }else{
                        if(j.contains("choices")&&!j["choices"].empty()&&
                           j["choices"][0].contains("delta")&&j["choices"][0]["delta"].contains("content"))
                            on_chunk(j["choices"][0]["delta"]["content"].get<std::string>());
                    }
                }catch(...){}
            }
        }
    }
}

#else // Linux/macOS  --  POSIX + OpenSSL

namespace {
struct TlsConn {
    int fd=-1; SSL_CTX* ctx=nullptr; SSL* ssl=nullptr;
    ~TlsConn(){
        if(ssl){SSL_shutdown(ssl);SSL_free(ssl);}
        if(ctx)SSL_CTX_free(ctx);
        if(fd>=0)::close(fd);
    }
    bool connect_tls(const std::string& host, int port, int timeout_sec){
        addrinfo hints{},*res=nullptr;
        hints.ai_family=AF_UNSPEC; hints.ai_socktype=SOCK_STREAM;
        if(::getaddrinfo(host.c_str(),std::to_string(port).c_str(),&hints,&res)!=0) return false;
        for(auto* rp=res;rp;rp=rp->ai_next){
            fd=::socket(rp->ai_family,rp->ai_socktype,rp->ai_protocol);
            if(fd<0) continue;
            timeval tv{timeout_sec,0};
            ::setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv));
            ::setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&tv,sizeof(tv));
            if(::connect(fd,rp->ai_addr,rp->ai_addrlen)==0) break;
            ::close(fd); fd=-1;
        }
        ::freeaddrinfo(res);
        if(fd<0) return false;
        ctx=SSL_CTX_new(TLS_client_method());
        if(!ctx) return false;
        SSL_CTX_set_verify(ctx,SSL_VERIFY_PEER,nullptr);
        SSL_CTX_set_default_verify_paths(ctx);
        ssl=SSL_new(ctx);
        if(!ssl) return false;
        SSL_set_fd(ssl,fd);
        SSL_set_tlsext_host_name(ssl,host.c_str());
        return SSL_connect(ssl)==1;
    }
    bool connect_plain(const std::string& host, int port, int timeout_sec){
        addrinfo hints{},*res=nullptr;
        hints.ai_family=AF_UNSPEC; hints.ai_socktype=SOCK_STREAM;
        if(::getaddrinfo(host.c_str(),std::to_string(port).c_str(),&hints,&res)!=0) return false;
        for(auto* rp=res;rp;rp=rp->ai_next){
            fd=::socket(rp->ai_family,rp->ai_socktype,rp->ai_protocol);
            if(fd<0) continue;
            timeval tv{timeout_sec,0};
            ::setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv));
            ::setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&tv,sizeof(tv));
            if(::connect(fd,rp->ai_addr,rp->ai_addrlen)==0) break;
            ::close(fd); fd=-1;
        }
        ::freeaddrinfo(res);
        return fd>=0;
    }
    bool write_all(const char* buf,size_t len){
        if(ssl){ while(len>0){int n=SSL_write(ssl,buf,(int)len);if(n<=0)return false;buf+=n;len-=(size_t)n;} }
        else    { while(len>0){ssize_t n=::write(fd,buf,len);if(n<=0)return false;buf+=n;len-=(size_t)n;} }
        return true;
    }
    std::pair<int,std::string> read_response(){
        std::string raw; char buf[4096]; int n;
        size_t hend=std::string::npos;
        auto read_chunk=[&]()->int{
            if(ssl) return SSL_read(ssl,buf,sizeof(buf));
            return (int)::read(fd,buf,sizeof(buf));
        };
        while((n=read_chunk())>0){
            raw.append(buf,(size_t)n);
            if(hend==std::string::npos){
                hend=raw.find("\r\n\r\n");
                if(hend!=std::string::npos){
                    auto cl=raw.find("Content-Length:");
                    if(cl==std::string::npos) cl=raw.find("content-length:");
                    if(cl!=std::string::npos){
                        size_t cle=raw.find("\r\n",cl);
                        size_t clv=std::stoull(raw.substr(cl+15,cle-cl-15));
                        size_t bs=hend+4;
                        while(raw.size()-bs<clv){n=read_chunk();if(n<=0)break;raw.append(buf,(size_t)n);}
                        break;
                    }
                }
            }
        }
        int status=0;
        if(raw.size()>=12&&raw.substr(0,5)=="HTTP/") status=std::stoi(raw.substr(9,3));
        size_t bs=(hend!=std::string::npos)?hend+4:0;
        return {status,raw.substr(bs)};
    }
};
} // anon

std::pair<int,std::string> ApiClient::http_post(const std::string& body){
    auto ep=resolve_endpoint();
    TlsConn conn;
    bool ok = ep.tls
        ? conn.connect_tls(ep.host, ep.port, cfg_.connect_timeout)
        : conn.connect_plain(ep.host, ep.port, cfg_.connect_timeout);
    if(!ok) throw NetworkException("Cannot connect to "+ep.host+":"+std::to_string(ep.port));

    std::ostringstream req;
    req<<"POST "<<ep.path<<" HTTP/1.1\r\n"
       <<"Host: "<<ep.host<<"\r\n"
       <<build_headers_str()
       <<"content-length: "<<body.size()<<"\r\n"
       <<"connection: close\r\n\r\n"
       <<body;
    std::string rs=req.str();
    if(!conn.write_all(rs.data(),rs.size())) throw NetworkException("HTTP write failed");
    return conn.read_response();
}

void ApiClient::http_post_stream(const std::string& body, std::function<void(const std::string&)> on_chunk){
    auto ep=resolve_endpoint();
    TlsConn conn;
    bool ok = ep.tls
        ? conn.connect_tls(ep.host, ep.port, cfg_.connect_timeout)
        : conn.connect_plain(ep.host, ep.port, cfg_.connect_timeout);
    if(!ok) throw NetworkException("Cannot connect to "+ep.host+":"+std::to_string(ep.port));

    std::ostringstream req;
    req<<"POST "<<ep.path<<" HTTP/1.1\r\n"
       <<"Host: "<<ep.host<<"\r\n"
       <<build_headers_str()
       <<"content-length: "<<body.size()<<"\r\n"
       <<"connection: close\r\n\r\n"
       <<body;
    std::string rs=req.str();
    if(!conn.write_all(rs.data(),rs.size())) throw NetworkException("HTTP write failed");

    std::string buffer; char buf[4096]; int n;
    auto read_chunk=[&]()->int{
        if(conn.ssl) return SSL_read(conn.ssl,buf,sizeof(buf));
        return (int)::read(conn.fd,buf,sizeof(buf));
    };
    bool header_done=false;
    while((n=read_chunk())>0){
        buffer.append(buf,(size_t)n);
        if(!header_done){
            auto hend=buffer.find("\r\n\r\n");
            if(hend!=std::string::npos){
                buffer.erase(0,hend+4);
                header_done=true;
            }
        }
        if(header_done){
            size_t pos;
            while((pos=buffer.find('\n'))!=std::string::npos){
                std::string line=buffer.substr(0,pos);
                buffer.erase(0,pos+1);
                if(line.substr(0,6)=="data: "){
                    std::string data=line.substr(6);
                    if(data=="[DONE]") return;
                    try{
                        auto j=nlohmann::json::parse(data);
                        if(cfg_.provider==Provider::Anthropic){
                            if(j.contains("delta")&&j["delta"].contains("text"))
                                on_chunk(j["delta"]["text"].get<std::string>());
                        }else{
                            if(j.contains("choices")&&!j["choices"].empty()&&
                               j["choices"][0].contains("delta")&&j["choices"][0]["delta"].contains("content"))
                                on_chunk(j["choices"][0]["delta"]["content"].get<std::string>());
                        }
                    }catch(...){}
                }
            }
        }
    }
}
#endif // platform

} // namespace agent
