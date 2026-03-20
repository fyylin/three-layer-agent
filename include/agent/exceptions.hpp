#pragma once
// =============================================================================
// include/agent/exceptions.hpp
//
// Exception hierarchy (TDD §7.2).  All exceptions carry enough context for
// structured logging: task_id, layer name, and retry count at time of throw.
// =============================================================================

#include <stdexcept>
#include <string>

namespace agent {

// -----------------------------------------------------------------------------
// Base
// -----------------------------------------------------------------------------

class AgentException : public std::runtime_error {
public:
    AgentException(const std::string& msg,
                   std::string task_id   = "",
                   std::string layer     = "",
                   int         retry_cnt = 0)
        : std::runtime_error(msg)
        , task_id_(std::move(task_id))
        , layer_(std::move(layer))
        , retry_count_(retry_cnt)
    {}

    [[nodiscard]] const std::string& task_id()    const noexcept { return task_id_; }
    [[nodiscard]] const std::string& layer()      const noexcept { return layer_; }
    [[nodiscard]] int                retry_count()const noexcept { return retry_count_; }

private:
    std::string task_id_;
    std::string layer_;
    int         retry_count_;
};

// -----------------------------------------------------------------------------
// API / Network
// -----------------------------------------------------------------------------

class ApiException : public AgentException {
public:
    using AgentException::AgentException;
};

// libcurl or socket-level failure → always eligible for retry
class NetworkException : public ApiException {
public:
    using ApiException::ApiException;
};

// Anthropic returned an error body (4xx client error → do NOT retry;
// 5xx server error → retry)
class ModelException : public ApiException {
public:
    ModelException(const std::string& msg, int http_status,
                   std::string task_id = "", std::string layer = "",
                   int retry_cnt = 0)
        : ApiException(msg, std::move(task_id), std::move(layer), retry_cnt)
        , http_status_(http_status)
    {}
    [[nodiscard]] int http_status() const noexcept { return http_status_; }
    [[nodiscard]] bool is_retryable() const noexcept {
        return http_status_ >= 500;
    }
private:
    int http_status_;
};

// -----------------------------------------------------------------------------
// Parse
// -----------------------------------------------------------------------------

// LLM returned text that did not conform to the agreed JSON schema.
// The caller should re-issue the request with a format-correction hint.
class ParseException : public AgentException {
public:
    ParseException(const std::string& msg,
                   std::string        raw_response,
                   std::string        task_id   = "",
                   std::string        layer     = "",
                   int                retry_cnt = 0)
        : AgentException(msg, std::move(task_id), std::move(layer), retry_cnt)
        , raw_response_(std::move(raw_response))
    {}
    [[nodiscard]] const std::string& raw_response() const noexcept {
        return raw_response_;
    }
private:
    std::string raw_response_;
};

// -----------------------------------------------------------------------------
// Tool
// -----------------------------------------------------------------------------

// A registered ToolFn threw or returned an error sentinel.
// Worker catches this and includes the error text in the LLM prompt so the
// model can decide how to proceed.
class ToolException : public AgentException {
public:
    ToolException(const std::string& tool_name,
                  const std::string& inner_msg,
                  std::string        task_id   = "",
                  std::string        layer     = "",
                  int                retry_cnt = 0)
        : AgentException("Tool '" + tool_name + "' failed: " + inner_msg,
                         std::move(task_id), std::move(layer), retry_cnt)
        , tool_name_(tool_name)
    {}
    [[nodiscard]] const std::string& tool_name() const noexcept {
        return tool_name_;
    }
private:
    std::string tool_name_;
};

// -----------------------------------------------------------------------------
// Timeout
// -----------------------------------------------------------------------------

class TimeoutException : public AgentException {
public:
    using AgentException::AgentException;
};

} // namespace agent
