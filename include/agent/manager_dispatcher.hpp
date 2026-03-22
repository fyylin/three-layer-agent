#pragma once
#include "agent/models.hpp"
#include "agent/dependency_analyzer.hpp"
#include "utils/thread_pool.hpp"
#include <vector>
#include <functional>

namespace agent {

class WorkerAgent;

class ManagerDispatcher {
public:
    using WorkerSelector = std::function<WorkerAgent*(const std::string&, size_t)>;

    ManagerDispatcher(ThreadPool& pool, WorkerSelector selector);

    std::vector<AtomicResult> dispatch_parallel(
        const std::vector<AtomicTask>& tasks,
        const std::string& subtask_id);

    std::vector<AtomicResult> dispatch_with_dependencies(
        const std::vector<AtomicTask>& tasks,
        const std::string& subtask_id);

private:
    ThreadPool& pool_;
    WorkerSelector worker_selector_;
    DependencyAnalyzer dep_analyzer_;

    std::vector<AtomicResult> execute_batch(
        const std::vector<size_t>& batch_indices,
        const std::vector<AtomicTask>& tasks);
};

} // namespace agent
