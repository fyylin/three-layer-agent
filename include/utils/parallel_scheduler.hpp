#pragma once
#include <vector>
#include <string>
#include <functional>
#include <map>
#include <set>
#include <thread>
#include <mutex>
#include <condition_variable>

namespace agent {

struct Task {
    std::string id;
    std::function<void()> work;
    std::vector<std::string> dependencies;
};

class ParallelScheduler {
public:
    void add_task(std::string id, std::function<void()> work,
                  std::vector<std::string> deps = {}) {
        tasks_[id] = {id, work, deps};
    }

    void execute() {
        std::set<std::string> completed;
        std::set<std::string> running;
        std::mutex mtx;
        std::condition_variable cv;
        std::vector<std::thread> threads;

        auto launch_ready_tasks = [&]() {
            for (auto& [id, task] : tasks_) {
                if (completed.count(id) || running.count(id))
                    continue;

                bool ready = true;
                for (auto& dep : task.dependencies) {
                    if (!completed.count(dep)) {
                        ready = false;
                        break;
                    }
                }

                if (ready) {
                    running.insert(id);
                    threads.emplace_back([this, id, &mtx, &completed, &running, &cv]() {
                        tasks_[id].work();
                        {
                            std::lock_guard<std::mutex> lock(mtx);
                            running.erase(id);
                            completed.insert(id);
                        }
                        cv.notify_all();
                    });
                }
            }
        };

        while (completed.size() < tasks_.size()) {
            {
                std::unique_lock<std::mutex> lock(mtx);
                launch_ready_tasks();
                if (completed.size() < tasks_.size() && running.empty()) {
                    break; // deadlock detection
                }
                cv.wait_for(lock, std::chrono::milliseconds(10));
            }
        }

        for (auto& t : threads) {
            if (t.joinable()) t.join();
        }
    }

private:
    std::map<std::string, Task> tasks_;
};

} // namespace agent
