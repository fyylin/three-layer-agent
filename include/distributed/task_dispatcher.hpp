#pragma once
#include "node_registry.hpp"
#include <queue>
#include <mutex>
#include <string>

namespace agent {

struct DistributedTask {
    std::string id;
    std::string description;
};

class TaskDispatcher {
public:
    explicit TaskDispatcher(std::shared_ptr<NodeRegistry> registry)
        : registry_(std::move(registry)) {}

    void submit(const DistributedTask& task) {
        std::lock_guard<std::mutex> lk(mu_);
        queue_.push(task);
    }

    bool dispatch_next() {
        std::lock_guard<std::mutex> lk(mu_);
        if (queue_.empty()) return false;

        auto* node = registry_->select_node();
        if (!node) return false;

        DistributedTask task = queue_.front();
        queue_.pop();

        // Send task to node (simplified - real impl uses RPC)
        node->active_tasks++;
        return true;
    }

    size_t pending_count() const {
        std::lock_guard<std::mutex> lk(mu_);
        return queue_.size();
    }

private:
    std::shared_ptr<NodeRegistry> registry_;
    mutable std::mutex mu_;
    std::queue<DistributedTask> queue_;
};

} // namespace agent
