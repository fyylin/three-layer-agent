#pragma once
#include <string>
#include <unordered_map>
#include <memory>
#include <future>
#include <mutex>
#include <chrono>
#include <optional>

namespace agent {

class RequestDeduplicator {
public:
    struct RequestKey {
        std::string goal;
        std::string context_hash;

        bool operator==(const RequestKey& other) const {
            return goal == other.goal && context_hash == other.context_hash;
        }
    };

    struct RequestKeyHash {
        std::size_t operator()(const RequestKey& k) const {
            return std::hash<std::string>{}(k.goal) ^
                   (std::hash<std::string>{}(k.context_hash) << 1);
        }
    };

    bool is_in_flight(const RequestKey& key) const;
    std::optional<std::string> wait_for_result(
        const RequestKey& key, std::chrono::milliseconds timeout);
    void register_request(const RequestKey& key);
    void complete_request(const RequestKey& key, const std::string& result);
    void fail_request(const RequestKey& key);
    void cleanup_expired(std::chrono::seconds ttl = std::chrono::seconds(300));

private:
    mutable std::mutex mu_;
    std::unordered_map<RequestKey, std::shared_ptr<std::promise<std::string>>, RequestKeyHash> in_flight_;
    std::unordered_map<RequestKey, std::chrono::steady_clock::time_point, RequestKeyHash> timestamps_;
};

} // namespace agent
