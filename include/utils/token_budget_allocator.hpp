#pragma once
#include <string>
#include <map>
#include <memory>

namespace agent {

class TokenBudgetAllocator {
public:
    enum class FallbackStrategy { UseHaiku, UseCachedOnly, Abort };

    explicit TokenBudgetAllocator(double total_budget_usd);

    std::string select_prompt(const std::string& task_name, double remaining_budget);
    double estimate_cost(const std::string& task_id);
    bool can_afford(const std::string& task_id);
    FallbackStrategy get_fallback(double remaining);
    void record_usage(const std::string& task_id, int input_tokens, int output_tokens);
    double remaining() const { return total_budget_ - used_budget_; }

private:
    double total_budget_;
    double used_budget_ = 0.0;
    std::map<std::string, double> task_cost_history_;
    static constexpr double OPUS_INPUT_PRICE = 15.0 / 1000000;
    static constexpr double OPUS_OUTPUT_PRICE = 75.0 / 1000000;
};

} // namespace agent
