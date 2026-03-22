#pragma once
#include <vector>
#include <string>
#include <map>
#include <mutex>
#include <atomic>

namespace agent {

struct WorkerLoad {
    std::string worker_id;
    std::atomic<int> active_tasks{0};
    std::atomic<int> total_completed{0};
};

class LoadBalancer {
public:
    explicit LoadBalancer(std::vector<std::string> worker_ids) {
        for (auto& id : worker_ids)
            workers_[id] = WorkerLoad{id, 0, 0};
    }

    std::string select_worker() {
        std::lock_guard<std::mutex> lk(mu_);
        std::string best;
        int min_load = INT_MAX;
        for (auto& [id, w] : workers_) {
            int load = w.active_tasks.load();
            if (load < min_load) {
                min_load = load;
                best = id;
            }
        }
        if (!best.empty())
            workers_[best].active_tasks++;
        return best;
    }

    void task_completed(const std::string& worker_id) {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = workers_.find(worker_id);
        if (it != workers_.end()) {
            it->second.active_tasks--;
            it->second.total_completed++;
        }
    }

    std::map<std::string, int> get_loads() const {
        std::lock_guard<std::mutex> lk(mu_);
        std::map<std::string, int> result;
        for (auto& [id, w] : workers_)
            result[id] = w.active_tasks.load();
        return result;
    }

private:
    mutable std::mutex mu_;
    std::map<std::string, WorkerLoad> workers_;
};

} // namespace agent
