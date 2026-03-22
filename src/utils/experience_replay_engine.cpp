#include "utils/experience_replay_engine.hpp"
#include "utils/utf8_fstream.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <set>

namespace agent {

ExperienceReplayEngine::ExperienceReplayEngine(const std::string& experience_file)
    : experience_file_(experience_file) {}

void ExperienceReplayEngine::load_from_experience() {
    std::ifstream ifs(experience_file_);
    if (!ifs.is_open()) {
        experience_db_ = nlohmann::json::object();
        return;
    }
    try {
        ifs >> experience_db_;
    } catch (...) {
        experience_db_ = nlohmann::json::object();
    }
}

void ExperienceReplayEngine::save_prompt_stats(const std::string& task_name,
                                               const std::string& best_variant,
                                               double success_rate, int avg_tokens) {
    if (!experience_db_.is_object()) experience_db_ = nlohmann::json::object();
    if (!experience_db_.contains("prompts")) experience_db_["prompts"] = nlohmann::json::object();

    nlohmann::json task_data = nlohmann::json::parse(
        "{\"best_variant\":\"" + best_variant +
        "\",\"success_rate\":" + std::to_string(success_rate) +
        ",\"avg_tokens\":" + std::to_string(avg_tokens) + "}"
    );
    experience_db_["prompts"][task_name] = task_data;

    utf8_ofstream ofs(experience_file_);
    ofs << experience_db_.dump(2);
}

double ExperienceReplayEngine::compute_similarity(const std::string& a, const std::string& b) {
    std::set<std::string> words_a, words_b;
    std::istringstream iss_a(a), iss_b(b);
    std::string word;
    while (iss_a >> word) words_a.insert(word);
    while (iss_b >> word) words_b.insert(word);

    size_t intersection = 0;
    for (const auto& w : words_a)
        if (words_b.count(w)) ++intersection;

    size_t union_size = words_a.size() + words_b.size() - intersection;
    return union_size > 0 ? (double)intersection / union_size : 0.0;
}

std::vector<SimilarTask> ExperienceReplayEngine::find_similar_tasks(const std::string& new_task) {
    std::vector<SimilarTask> results;
    if (!experience_db_.contains("prompts")) return results;

    for (auto& [task_name, stats] : experience_db_["prompts"].items()) {
        double sim = compute_similarity(new_task, task_name);
        if (sim > 0.3) {
            std::string variant = stats.contains("best_variant") ? stats["best_variant"].get<std::string>() : "";
            double rate = stats.contains("success_rate") ? stats["success_rate"].get<double>() : 0.0;
            results.push_back({task_name, sim, variant, rate});
        }
    }

    std::sort(results.begin(), results.end(),
              [](const auto& a, const auto& b) { return a.similarity > b.similarity; });
    return results;
}

void ExperienceReplayEngine::append_experience(const std::string& category,
                                               const std::string& lesson) {
    if (!experience_db_.contains("lessons")) experience_db_["lessons"] = nlohmann::json::array();

    nlohmann::json entry = nlohmann::json::parse(
        "{\"category\":\"" + category + "\",\"lesson\":\"" + lesson + "\"}"
    );
    experience_db_["lessons"].push_back(entry);

    utf8_ofstream ofs(experience_file_);
    ofs << experience_db_.dump(2);
}

} // namespace agent
