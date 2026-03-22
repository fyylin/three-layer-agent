#pragma once
#include <string>
#include <vector>
#include <future>
#include <memory>

namespace agent { class ApiClient; }

namespace api {

struct BatchRequest {
    std::string system_prompt;
    std::string user_message;
    std::string task_id;
};

class BatchApiClient {
public:
    explicit BatchApiClient(agent::ApiClient& client);

    std::vector<std::future<std::string>>
    batch_complete(const std::vector<BatchRequest>& requests);

    std::vector<std::string>
    wait_all(std::vector<std::future<std::string>>& futures);

private:
    agent::ApiClient& client_;
};

} // namespace api
