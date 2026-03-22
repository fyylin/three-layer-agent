#include "api/batch_api_client.hpp"
#include "agent/api_client.hpp"
#include <thread>

namespace api {

BatchApiClient::BatchApiClient(agent::ApiClient& client) : client_(client) {}

std::vector<std::future<std::string>>
BatchApiClient::batch_complete(const std::vector<BatchRequest>& requests) {
    std::vector<std::future<std::string>> futures;
    futures.reserve(requests.size());

    for (const auto& req : requests) {
        futures.push_back(std::async(std::launch::async, [this, req]() {
            return client_.complete(req.system_prompt, req.user_message, req.task_id);
        }));
    }

    return futures;
}

std::vector<std::string>
BatchApiClient::wait_all(std::vector<std::future<std::string>>& futures) {
    std::vector<std::string> results;
    results.reserve(futures.size());

    for (auto& fut : futures) {
        results.push_back(fut.get());
    }

    return results;
}

} // namespace api
