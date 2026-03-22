#include "agent/director_decomposer.hpp"
#include "agent/exceptions.hpp"
#include "utils/json_utils.hpp"
#include <nlohmann/json.hpp>

namespace agent {

DirectorDecomposer::DirectorDecomposer(ApiClient& client, std::string prompt)
    : client_(client), decompose_prompt_(std::move(prompt)) {}

std::vector<SubTask> DirectorDecomposer::decompose(
        const UserGoal& goal, const std::string& format_hint) {

    std::string user_msg = "Goal: " + goal.description;
    if (!format_hint.empty()) {
        user_msg += "\n" + format_hint;
    }

    std::string llm_output = client_.complete(decompose_prompt_, user_msg);
    return parse_subtasks(llm_output);
}

std::vector<SubTask> DirectorDecomposer::decompose_with_dedup(
        const UserGoal& goal, RequestDeduplicator& dedup) {

    RequestDeduplicator::RequestKey key{goal.description, ""};

    if (dedup.is_in_flight(key)) {
        auto cached = dedup.wait_for_result(key, std::chrono::seconds(30));
        if (cached.has_value()) {
            return parse_subtasks(*cached);
        }
    }

    dedup.register_request(key);

    try {
        auto result = decompose(goal);
        dedup.complete_request(key, "decomposed");
        return result;
    } catch (...) {
        dedup.fail_request(key);
        throw;
    }
}

std::vector<SubTask> DirectorDecomposer::parse_subtasks(const std::string& llm_output) const {
    nlohmann::json j;
    try {
        j = parse_llm_json(llm_output);
    } catch (const std::exception& e) {
        throw ParseException(e.what(), llm_output);
    }

    if (!j.is_array()) {
        throw ParseException("Expected JSON array of subtasks", llm_output);
    }

    std::vector<SubTask> subtasks;
    for (const auto& item : j) {
        SubTask st;
        agent::from_json(item, st);
        subtasks.push_back(st);
    }

    return subtasks;
}

} // namespace agent
