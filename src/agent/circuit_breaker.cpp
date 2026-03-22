#include "agent/circuit_breaker.hpp"

namespace agent {

bool CircuitBreaker::allow_request() {
    std::lock_guard<std::mutex> lk(mu_);

    if (state_ == State::Closed)
        return true;

    if (state_ == State::Open) {
        auto now = std::chrono::steady_clock::now();
        if (now - open_time_ >= config_.timeout) {
            transition_to_half_open();
            return true;
        }
        return false;
    }

    // HalfOpen: allow one request
    return true;
}

void CircuitBreaker::record_success() {
    std::lock_guard<std::mutex> lk(mu_);

    consecutive_failures_ = 0;

    if (state_ == State::HalfOpen) {
        ++consecutive_successes_;
        if (consecutive_successes_ >= config_.success_threshold) {
            transition_to_closed();
        }
    }
}

void CircuitBreaker::record_failure() {
    std::lock_guard<std::mutex> lk(mu_);

    consecutive_successes_ = 0;
    ++consecutive_failures_;

    if (state_ == State::Closed) {
        if (consecutive_failures_ >= config_.failure_threshold) {
            transition_to_open();
        }
    } else if (state_ == State::HalfOpen) {
        transition_to_open();
    }
}

CircuitBreaker::State CircuitBreaker::get_state() const {
    std::lock_guard<std::mutex> lk(mu_);
    return state_;
}

void CircuitBreaker::transition_to_open() {
    state_ = State::Open;
    open_time_ = std::chrono::steady_clock::now();
    consecutive_failures_ = 0;
}

void CircuitBreaker::transition_to_half_open() {
    state_ = State::HalfOpen;
    consecutive_successes_ = 0;
}

void CircuitBreaker::transition_to_closed() {
    state_ = State::Closed;
    consecutive_successes_ = 0;
    consecutive_failures_ = 0;
}

} // namespace agent
