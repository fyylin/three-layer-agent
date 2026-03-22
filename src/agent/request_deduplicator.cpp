#include "agent/request_deduplicator.hpp"

namespace agent {

bool RequestDeduplicator::is_in_flight(const RequestKey& key) const {
    std::lock_guard<std::mutex> lk(mu_);
    return in_flight_.count(key) > 0;
}

std::optional<std::string> RequestDeduplicator::wait_for_result(
        const RequestKey& key, std::chrono::milliseconds timeout) {

    std::shared_ptr<std::promise<std::string>> promise;
    {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = in_flight_.find(key);
        if (it == in_flight_.end())
            return std::nullopt;
        promise = it->second;
    }

    auto future = promise->get_future();
    if (future.wait_for(timeout) == std::future_status::ready) {
        try {
            return future.get();
        } catch (...) {
            return std::nullopt;
        }
    }

    return std::nullopt;
}

void RequestDeduplicator::register_request(const RequestKey& key) {
    std::lock_guard<std::mutex> lk(mu_);
    in_flight_[key] = std::make_shared<std::promise<std::string>>();
    timestamps_[key] = std::chrono::steady_clock::now();
}

void RequestDeduplicator::complete_request(const RequestKey& key, const std::string& result) {
    std::shared_ptr<std::promise<std::string>> promise;
    {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = in_flight_.find(key);
        if (it != in_flight_.end()) {
            promise = it->second;
            in_flight_.erase(it);
            timestamps_.erase(key);
        }
    }

    if (promise) {
        promise->set_value(result);
    }
}

void RequestDeduplicator::fail_request(const RequestKey& key) {
    std::shared_ptr<std::promise<std::string>> promise;
    {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = in_flight_.find(key);
        if (it != in_flight_.end()) {
            promise = it->second;
            in_flight_.erase(it);
            timestamps_.erase(key);
        }
    }

    if (promise) {
        try {
            promise->set_exception(std::make_exception_ptr(
                std::runtime_error("Request failed")));
        } catch (...) {}
    }
}

void RequestDeduplicator::cleanup_expired(std::chrono::seconds ttl) {
    std::lock_guard<std::mutex> lk(mu_);
    auto now = std::chrono::steady_clock::now();

    std::vector<RequestKey> expired;
    for (const auto& [key, timestamp] : timestamps_) {
        if (now - timestamp > ttl) {
            expired.push_back(key);
        }
    }

    for (const auto& key : expired) {
        in_flight_.erase(key);
        timestamps_.erase(key);
    }
}

} // namespace agent
