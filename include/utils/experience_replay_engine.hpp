#pragma once
#include <string>
#include <vector>
#include <map>
#include <nlohmann/json.hpp>

namespace agent {

struct SimilarTask {
    std::string task_name;
    double similarity;
    std::string best_variant;
    double success_rate;
};

class ExperienceReplayEngine {
public:
    explicit ExperienceReplayEngine(const std::string& experience_file);

    void load_from_experience();
    void save_prompt_stats(const std::string& task_name,
                          const std::string& best_variant,
                          double success_rate, int avg_tokens);
    std::vector<SimilarTask> find_similar_tasks(const std::string& new_task);
    void append_experience(const std::string& category, const std::string& lesson);

private:
    std::string experience_file_;
    nlohmann::json experience_db_;
    double compute_similarity(const std::string& a, const std::string& b);
};

} // namespace agent
