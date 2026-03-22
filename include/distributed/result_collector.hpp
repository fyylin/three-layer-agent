#pragma once
#include "node_registry.hpp"
#include <string>
#include <map>
#include <mutex>

namespace agent {

struct TaskResult {
    std::string task_id;
    std::string node_id;
    bool success;
    std::string output;
};

class ResultCollector {
public:
    void submit(const TaskResult& result) {
        std::lock_guard<std::mutex> lk(mu_);
        results_[result.task_id] = result;
    }

    bool get(const std::string& task_id, TaskResult& out) const {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = results_.find(task_id);
        if (it == results_.end()) return false;
        out = it->second;
        return true;
    }

    std::vector<TaskResult> get_all() const {
        std::lock_guard<std::mutex> lk(mu_);
        std::vector<TaskResult> out;
        for (auto& [id, res] : results_) out.push_back(res);
        return out;
    }

private:
    mutable std::mutex mu_;
    std::map<std::string, TaskResult> results_;
};

} // namespace agent
