#pragma once
#include <chrono>
#include <mutex>

namespace agent {

class CircuitBreaker {
public:
    enum class State { Closed, Open, HalfOpen };

    struct Config {
        int failure_threshold = 5;
        int success_threshold = 2;
        std::chrono::seconds timeout = std::chrono::seconds(30);
    };

    explicit CircuitBreaker(Config cfg = Config{}) : config_(cfg) {}

    bool allow_request();
    void record_success();
    void record_failure();
    State get_state() const;

private:
    mutable std::mutex mu_;
    State state_ = State::Closed;
    int consecutive_failures_ = 0;
    int consecutive_successes_ = 0;
    std::chrono::steady_clock::time_point open_time_;
    Config config_;

    void transition_to_open();
    void transition_to_half_open();
    void transition_to_closed();
};

} // namespace agent
