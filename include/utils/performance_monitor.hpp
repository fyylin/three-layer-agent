#pragma once
#include <string>
#include <map>
#include <mutex>
#include <chrono>
#include <atomic>
#include <sstream>

namespace agent {

struct PerformanceMetrics {
    std::atomic<int> total_tasks{0};
    std::atomic<int> completed_tasks{0};
    std::atomic<int> failed_tasks{0};
    std::atomic<double> total_latency_ms{0.0};
    std::chrono::steady_clock::time_point start_time;

    double throughput() const {
        auto elapsed = std::chrono::steady_clock::now() - start_time;
        auto seconds = std::chrono::duration<double>(elapsed).count();
        return seconds > 0 ? completed_tasks.load() / seconds : 0.0;
    }

    double avg_latency_ms() const {
        int completed = completed_tasks.load();
        return completed > 0 ? total_latency_ms.load() / completed : 0.0;
    }

    double success_rate() const {
        int total = completed_tasks.load() + failed_tasks.load();
        return total > 0 ? (double)completed_tasks.load() / total : 0.0;
    }
};

class PerformanceMonitor {
public:
    PerformanceMonitor() {
        metrics_.start_time = std::chrono::steady_clock::now();
    }

    void record_task_start() {
        metrics_.total_tasks++;
    }

    void record_task_complete(double latency_ms, bool success) {
        if (success) {
            metrics_.completed_tasks++;
            metrics_.total_latency_ms = metrics_.total_latency_ms.load() + latency_ms;
        } else {
            metrics_.failed_tasks++;
        }
    }

    PerformanceMetrics get_metrics() const {
        return metrics_;
    }

    std::string generate_report() const {
        auto m = get_metrics();
        std::ostringstream oss;
        oss << "Performance Report:\n"
            << "  Total tasks: " << m.total_tasks.load() << "\n"
            << "  Completed: " << m.completed_tasks.load() << "\n"
            << "  Failed: " << m.failed_tasks.load() << "\n"
            << "  Success rate: " << (m.success_rate() * 100) << "%\n"
            << "  Throughput: " << m.throughput() << " tasks/sec\n"
            << "  Avg latency: " << m.avg_latency_ms() << " ms\n";
        return oss.str();
    }

private:
    PerformanceMetrics metrics_;
};

} // namespace agent
