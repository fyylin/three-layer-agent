// =============================================================================
// src/agent/manager_pool.cpp
// =============================================================================
#include "agent/manager_pool.hpp"
#include <algorithm>

namespace agent {

std::shared_ptr<IManager> ManagerPool::acquire(const SubTask& task) {
    std::lock_guard<std::mutex> lk(mu_);
    std::string type = classify_task_type(task.description);

    // 1. Find an idle same-type Manager (best memory continuity)
    for (auto& e : pool_) {
        if (!e.in_use && e.task_type == type) {
            e.in_use      = true;
            e.last_used_ms = now_ms();
            e.tasks_handled++;
            return e.mgr;
        }
    }

    // 2. No same-type idle -> find any idle Manager (acceptable memory match)
    for (auto& e : pool_) {
        if (!e.in_use) {
            // Repurpose: update type label but keep the Manager's memory
            // (cross-type memory is still useful: path discovery, env facts)
            e.in_use      = true;
            e.task_type   = type;
            e.last_used_ms = now_ms();
            e.tasks_handled++;
            return e.mgr;
        }
    }

    // 3. Pool at capacity -> recycle LRU idle entry, or create if under limit
    if ((int)pool_.size() < max_managers_) {
        // Create new Manager
        auto mgr = std::shared_ptr<IManager>(factory_(task).release());
        pool_.push_back({mgr, type, 1, 0, true, now_ms()});
        return mgr;
    }

    // 4. All in use  --  find LRU idle to evict (graceful degradation)
    Entry* lru = nullptr;
    for (auto& e : pool_) {
        if (!e.in_use) {
            if (!lru || e.last_used_ms < lru->last_used_ms)
                lru = &e;
        }
    }
    if (lru) {
        // Replace LRU entry with fresh Manager
        lru->mgr           = std::shared_ptr<IManager>(factory_(task).release());
        lru->task_type     = type;
        lru->in_use        = true;
        lru->tasks_handled = 1;
        lru->tasks_failed  = 0;
        lru->last_used_ms  = now_ms();
        return lru->mgr;
    }

    // 5. All in use and at capacity: create a transient Manager (no pooling)
    return std::shared_ptr<IManager>(factory_(task).release());
}

void ManagerPool::release(std::shared_ptr<IManager> mgr, bool success) {
    std::lock_guard<std::mutex> lk(mu_);
    for (auto& e : pool_) {
        if (e.mgr.get() == mgr.get()) {
            e.in_use = false;
            if (!success) e.tasks_failed++;
            return;
        }
    }
    // Transient Manager (not in pool)  --  just let it go out of scope
}

} // namespace agent
