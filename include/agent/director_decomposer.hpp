#pragma once
#include "agent/models.hpp"
#include "agent/api_client.hpp"
#include "agent/task_router.hpp"
#include "agent/request_deduplicator.hpp"
#include <string>
#include <vector>

namespace agent {

class DirectorDecomposer {
public:
    DirectorDecomposer(ApiClient& client, std::string prompt);

    std::vector<SubTask> decompose(
        const UserGoal& goal,
        const std::string& format_hint = "");

    std::vector<SubTask> decompose_with_dedup(
        const UserGoal& goal,
        RequestDeduplicator& dedup);

private:
    ApiClient& client_;
    std::string decompose_prompt_;
    TaskRouter router_;

    std::vector<SubTask> parse_subtasks(const std::string& llm_output) const;
};

} // namespace agent
