#pragma once
// =============================================================================
// include/utils/thread_pool.hpp
//
// Fixed-size thread pool backed by std::thread + std::condition_variable.
// No third-party dependency.  Supports submit<T>() returning std::future<T>.
//
// Shutdown is cooperative: destructor sets stop flag, notifies all threads,
// and joins them.  In-flight tasks complete before the pool is destroyed.
// =============================================================================

#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <vector>

namespace agent {

class ThreadPool {
public:
    explicit ThreadPool(size_t num_threads) {
        if (num_threads == 0)
            throw std::invalid_argument("ThreadPool: num_threads must be > 0");
        workers_.reserve(num_threads);
        for (size_t i = 0; i < num_threads; ++i) {
            workers_.emplace_back([this] { worker_loop(); });
        }
    }

    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lk(mu_);
            stop_ = true;
        }
        cv_.notify_all();
        for (auto& t : workers_) t.join();
    }

    // Non-copyable, non-movable (threads hold a pointer to *this)
    ThreadPool(const ThreadPool&)            = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    // Submit a callable and return a future for its result.
    // Works with lambdas, function objects, and free functions.
    template<typename F, typename... Args>
    auto submit(F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<F, Args...>>
    {
        using ReturnType = std::invoke_result_t<F, Args...>;

        auto task = std::make_shared<std::packaged_task<ReturnType()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
        std::future<ReturnType> fut = task->get_future();

        {
            std::unique_lock<std::mutex> lk(mu_);
            if (stop_)
                throw std::runtime_error("ThreadPool: submit on stopped pool");
            queue_.emplace([task]() { (*task)(); });
        }
        cv_.notify_one();
        return fut;
    }

    [[nodiscard]] size_t thread_count() const noexcept { return workers_.size(); }

private:
    void worker_loop() {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lk(mu_);
                cv_.wait(lk, [this] { return stop_ || !queue_.empty(); });
                if (stop_ && queue_.empty()) return;
                task = std::move(queue_.front());
                queue_.pop();
            }
            task();
        }
    }

    std::vector<std::thread>          workers_;
    std::queue<std::function<void()>> queue_;
    std::mutex                        mu_;
    std::condition_variable           cv_;
    bool                              stop_ = false;
};

} // namespace agent
