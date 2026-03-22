#include "agent/retry_policy.hpp"
#include <algorithm>
#include <cmath>

namespace agent {

RetryPolicy::RetryDecision RetryPolicy::should_retry(
        int attempt, const std::string& error, int http_code) const {

    RetryDecision decision;
    decision.should_retry = false;
    decision.wait_ms = 0;

    if (attempt >= 5) {
        decision.reason = "Max attempts reached";
        return decision;
    }

    auto type = classify_error(error, http_code);

    if (type == ErrorType::Permanent) {
        decision.reason = "Permanent error, no retry";
        return decision;
    }

    if (type == ErrorType::Transient) {
        decision.should_retry = true;
        decision.wait_ms = calculate_backoff(attempt);
        decision.reason = "Transient error, retry with backoff";
        return decision;
    }

    // Unknown: retry with caution
    if (attempt < 3) {
        decision.should_retry = true;
        decision.wait_ms = calculate_backoff(attempt);
        decision.reason = "Unknown error, limited retry";
    } else {
        decision.reason = "Unknown error, max retries for unknown";
    }

    return decision;
}

RetryPolicy::ErrorType RetryPolicy::classify_error(
        const std::string& error, int http_code) const {

    // HTTP status codes
    if (http_code == 429 || http_code == 503 || http_code == 504)
        return ErrorType::Transient;

    if (http_code == 401 || http_code == 403 || http_code == 400 || http_code == 404)
        return ErrorType::Permanent;

    // Error message patterns
    std::string lower;
    for (char c : error)
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));

    // Transient
    if (lower.find("timeout") != std::string::npos ||
        lower.find("connection") != std::string::npos ||
        lower.find("network") != std::string::npos ||
        lower.find("temporary") != std::string::npos ||
        lower.find("rate limit") != std::string::npos)
        return ErrorType::Transient;

    // Permanent
    if (lower.find("unauthorized") != std::string::npos ||
        lower.find("forbidden") != std::string::npos ||
        lower.find("invalid") != std::string::npos ||
        lower.find("not found") != std::string::npos ||
        lower.find("bad request") != std::string::npos)
        return ErrorType::Permanent;

    return ErrorType::Unknown;
}

int RetryPolicy::calculate_backoff(int attempt) const {
    // Exponential backoff: 100ms, 200ms, 400ms, 800ms, 1600ms
    int base_ms = 100;
    int backoff = base_ms * (1 << attempt);
    return std::min(backoff, 5000);
}

} // namespace agent
