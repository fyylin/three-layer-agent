#pragma once
#include "agent/models.hpp"
#include <vector>
#include <utility>

namespace agent {

class DependencyAnalyzer {
public:
    struct TaskGraph {
        std::vector<AtomicTask> tasks;
        std::vector<std::pair<int, int>> dependencies; // (from_idx, to_idx)
    };

    TaskGraph analyze(const std::vector<AtomicTask>& tasks) const;
    std::vector<std::vector<int>> get_parallel_batches(const TaskGraph& graph) const;

private:
    bool has_dependency(const AtomicTask& a, const AtomicTask& b) const;
    bool writes_to_same_resource(const AtomicTask& a, const AtomicTask& b) const;
    bool depends_on_output(const AtomicTask& a, const AtomicTask& b) const;
};

} // namespace agent
