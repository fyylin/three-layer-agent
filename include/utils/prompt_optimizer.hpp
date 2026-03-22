#pragma once
#include <string>
#include <map>
#include <vector>
#include <mutex>

namespace agent {

struct PromptVariant {
    std::string content;
    int success_count = 0;
    int total_count = 0;
    double avg_tokens = 0.0;

    double score() const {
        if (total_count == 0) return 0.5;
        double success_rate = (double)success_count / total_count;
        double token_penalty = avg_tokens > 1000 ? 0.9 : 1.0;
        return success_rate * token_penalty;
    }
};

class PromptOptimizer {
public:
    void register_variant(const std::string& role, const std::string& variant_id,
                         const std::string& content) {
        std::lock_guard<std::mutex> lk(mu_);
        variants_[role][variant_id].content = content;
    }

    std::string select_best(const std::string& role) {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = variants_.find(role);
        if (it == variants_.end() || it->second.empty()) return "";

        std::string best_id;
        double best_score = -1.0;
        for (auto& [id, var] : it->second) {
            double s = var.score();
            if (s > best_score) { best_score = s; best_id = id; }
        }
        return best_id.empty() ? "" : variants_[role][best_id].content;
    }

    void record_result(const std::string& role, const std::string& variant_id,
                      bool success, int tokens) {
        std::lock_guard<std::mutex> lk(mu_);
        auto& var = variants_[role][variant_id];
        var.total_count++;
        if (success) var.success_count++;
        var.avg_tokens = (var.avg_tokens * (var.total_count - 1) + tokens) / var.total_count;
    }

    std::map<std::string, double> get_scores(const std::string& role) const {
        std::lock_guard<std::mutex> lk(mu_);
        std::map<std::string, double> scores;
        auto it = variants_.find(role);
        if (it != variants_.end()) {
            for (auto& [id, var] : it->second)
                scores[id] = var.score();
        }
        return scores;
    }

private:
    mutable std::mutex mu_;
    std::map<std::string, std::map<std::string, PromptVariant>> variants_;
};

} // namespace agent
