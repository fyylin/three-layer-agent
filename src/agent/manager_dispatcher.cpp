#include "agent/manager_dispatcher.hpp"
#include "agent/worker_agent.hpp"
#include <future>
#include <unordered_map>

namespace agent {

ManagerDispatcher::ManagerDispatcher(ThreadPool& pool, WorkerSelector selector)
    : pool_(pool), worker_selector_(std::move(selector)) {}

std::vector<AtomicResult> ManagerDispatcher::dispatch_parallel(
        const std::vector<AtomicTask>& tasks, const std::string& subtask_id) {

    std::vector<std::future<AtomicResult>> futures;
    futures.reserve(tasks.size());

    for (size_t i = 0; i < tasks.size(); ++i) {
        const auto& task = tasks[i];
        auto* worker = worker_selector_(task.tool, i);

        futures.push_back(pool_.submit([worker, task]() {
            return worker->execute(task);
        }));
    }

    std::vector<AtomicResult> results;
    results.reserve(tasks.size());
    for (auto& fut : futures) {
        results.push_back(fut.get());
    }

    return results;
}

std::vector<AtomicResult> ManagerDispatcher::dispatch_with_dependencies(
        const std::vector<AtomicTask>& tasks, const std::string& subtask_id) {

    auto graph = dep_analyzer_.analyze(tasks);
    auto batches = dep_analyzer_.get_parallel_batches(graph);

    std::vector<AtomicResult> all_results;
    std::unordered_map<std::string, std::string> task_outputs;

    for (const auto& batch : batches) {
        auto batch_results = execute_batch(batch, tasks);

        for (const auto& result : batch_results) {
            all_results.push_back(result);
            if (result.status == TaskStatus::Done) {
                task_outputs[result.task_id] = result.output;
            }
        }
    }

    return all_results;
}

std::vector<AtomicResult> ManagerDispatcher::execute_batch(
        const std::vector<size_t>& batch_indices,
        const std::vector<AtomicTask>& tasks) {

    std::vector<std::future<AtomicResult>> futures;
    futures.reserve(batch_indices.size());

    for (size_t idx : batch_indices) {
        const auto& task = tasks[idx];
        auto* worker = worker_selector_(task.tool, idx);

        futures.push_back(pool_.submit([worker, task]() {
            return worker->execute(task);
        }));
    }

    std::vector<AtomicResult> results;
    results.reserve(batch_indices.size());
    for (auto& fut : futures) {
        results.push_back(fut.get());
    }

    return results;
}

} // namespace agent
