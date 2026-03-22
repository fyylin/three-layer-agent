#include "utils/token_budget_allocator.hpp"
#include <algorithm>

namespace agent {

TokenBudgetAllocator::TokenBudgetAllocator(double total_budget_usd)
    : total_budget_(total_budget_usd) {}

std::string TokenBudgetAllocator::select_prompt(const std::string& task_name, double remaining_budget) {
    if (remaining_budget < 0.01) return "minimal";
    if (remaining_budget < 0.1) return "concise";
    return "default";
}

double TokenBudgetAllocator::estimate_cost(const std::string& task_id) {
    auto it = task_cost_history_.find(task_id);
    if (it != task_cost_history_.end()) return it->second;
    return 0.05; // default estimate
}

bool TokenBudgetAllocator::can_afford(const std::string& task_id) {
    return estimate_cost(task_id) <= remaining();
}

TokenBudgetAllocator::FallbackStrategy
TokenBudgetAllocator::get_fallback(double remaining) {
    if (remaining > 0.01) return FallbackStrategy::UseHaiku;
    if (remaining > 0.0) return FallbackStrategy::UseCachedOnly;
    return FallbackStrategy::Abort;
}

void TokenBudgetAllocator::record_usage(const std::string& task_id,
                                        int input_tokens, int output_tokens) {
    double cost = input_tokens * OPUS_INPUT_PRICE + output_tokens * OPUS_OUTPUT_PRICE;
    used_budget_ += cost;
    task_cost_history_[task_id] = cost;
}

} // namespace agent
