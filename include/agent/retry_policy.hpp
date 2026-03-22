#pragma once
#include <string>
#include <chrono>

namespace agent {

class RetryPolicy {
public:
    enum class ErrorType { Transient, Permanent, Unknown };

    struct RetryDecision {
        bool should_retry;
        int wait_ms;
        std::string reason;
    };

    RetryDecision should_retry(int attempt, const std::string& error, int http_code = 0) const;

private:
    ErrorType classify_error(const std::string& error, int http_code) const;
    int calculate_backoff(int attempt) const;
};

} // namespace agent
