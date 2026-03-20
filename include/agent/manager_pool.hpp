#pragma once
// =============================================================================
// include/agent/manager_pool.hpp
//
// ManagerPool: replaces one-shot ManagerFactory with a persistent pool.
//
// Key properties:
//   - Managers are classified by task type (file/system/network/general)
//   - Same-type tasks reuse the same Manager -> memory accumulates across tasks
//   - Pool is bounded (max_managers); when full, LRU Manager is recycled
//   - Thread-safe: multiple Director threads can acquire/release concurrently
// =============================================================================

#include "agent/imanager.hpp"
#include "agent/models.hpp"
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace agent {


// Classify a subtask description into a broad category
// Used to route similar tasks to the same Manager for memory continuity
[[nodiscard]] inline std::string classify_task_type(const std::string& desc) {
    std::string d = desc;
    for (auto& c : d) c = (char)std::tolower((unsigned char)c);

    if (d.find("file") != std::string::npos ||
        d.find("read") != std::string::npos ||
        d.find("write") != std::string::npos ||
        d.find("dir") != std::string::npos ||
        d.find("folder") != std::string::npos ||
        d.find("path") != std::string::npos)   return "file";

    if (d.find("process") != std::string::npos ||
        d.find("system") != std::string::npos  ||
        d.find("cpu") != std::string::npos     ||
        d.find("memory") != std::string::npos  ||
        d.find("disk") != std::string::npos)   return "system";

    if (d.find("search") != std::string::npos ||
        d.find("find") != std::string::npos   ||
        d.find("query") != std::string::npos  ||
        d.find("lookup") != std::string::npos) return "search";

    return "general";
}

class ManagerPool {
public:
    explicit ManagerPool(ManagerFactory factory, int max_managers = 4)
        : factory_(std::move(factory))
        , max_managers_(max_managers)
    {}

    // Acquire a Manager suited for this task type.
    // Returns an existing idle same-type Manager (with its memory) if available,
    // otherwise creates a new one (or recycles the oldest idle one if at capacity).
    std::shared_ptr<IManager> acquire(const SubTask& task);

    // Return a Manager to the pool. Memory is preserved for next acquirer.
    void release(std::shared_ptr<IManager> mgr, bool success);

    // Number of alive Managers in pool
    int size() const {
        std::lock_guard<std::mutex> lk(mu_);
        return (int)pool_.size();
    }

private:
    struct Entry {
        std::shared_ptr<IManager> mgr;
        std::string               task_type;
        int                       tasks_handled = 0;
        int                       tasks_failed  = 0;
        bool                      in_use        = false;
        int64_t                   last_used_ms  = 0;  // for LRU eviction
    };

    ManagerFactory       factory_;
    int                  max_managers_;
    mutable std::mutex   mu_;
    std::vector<Entry>   pool_;

    static int64_t now_ms() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }
};

} // namespace agent
