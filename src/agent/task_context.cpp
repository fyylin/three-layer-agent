#include "agent/task_context.hpp"
#include <sstream>

namespace agent {

std::string TaskContext::to_prompt_context() const {
    std::ostringstream oss;

    if (!original_goal.empty()) {
        oss << "## Original Goal\n" << original_goal << "\n\n";
    }

    if (!current_subtask.empty() && current_subtask != original_goal) {
        oss << "## Current Subtask\n" << current_subtask << "\n\n";
    }

    if (!completed_steps.empty()) {
        oss << "## Completed Steps\n";
        for (size_t i = 0; i < completed_steps.size(); ++i) {
            oss << (i + 1) << ". " << completed_steps[i] << "\n";
        }
        oss << "\n";
    }

    if (!shared_state.empty()) {
        oss << "## Shared State\n";
        for (const auto& [k, v] : shared_state) {
            oss << "- " << k << ": " << v << "\n";
        }
        oss << "\n";
    }

    return oss.str();
}

} // namespace agent
