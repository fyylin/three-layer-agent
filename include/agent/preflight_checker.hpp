#pragma once
#include "agent/task_context.hpp"
#include "agent/models.hpp"
#include <string>

namespace agent {

class PreflightChecker {
public:
    struct CheckResult {
        bool should_proceed;
        std::string warning;
        std::string suggestion;
    };

    CheckResult check(const AtomicTask& task, const TaskContext& ctx) const;

private:
    bool check_path_validity(const std::string& path) const;
    bool check_goal_alignment(const AtomicTask& task, const TaskContext& ctx) const;
    bool check_conflict_with_completed(const AtomicTask& task, const TaskContext& ctx) const;
};

} // namespace agent
