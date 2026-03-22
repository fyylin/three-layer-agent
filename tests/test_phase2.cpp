#include <iostream>
#include <cassert>
#include <thread>
#include "agent/retry_policy.hpp"
#include "agent/circuit_breaker.hpp"
#include "agent/request_deduplicator.hpp"
#include "agent/event_bus.hpp"

using namespace agent;

void test_retry_policy() {
    std::cout << "=== Testing RetryPolicy ===" << std::endl;

    RetryPolicy policy;

    // Transient error
    auto decision = policy.should_retry(0, "Connection timeout", 0);
    assert(decision.should_retry);
    assert(decision.wait_ms == 100);
    std::cout << "✓ Transient error: retry with 100ms backoff" << std::endl;

    // Exponential backoff
    decision = policy.should_retry(2, "timeout", 0);
    assert(decision.should_retry);
    assert(decision.wait_ms == 400);
    std::cout << "✓ Exponential backoff: 400ms on attempt 2" << std::endl;

    // Permanent error
    decision = policy.should_retry(0, "Unauthorized", 401);
    assert(!decision.should_retry);
    std::cout << "✓ Permanent error: no retry" << std::endl;

    // Rate limit
    decision = policy.should_retry(0, "Rate limit exceeded", 429);
    assert(decision.should_retry);
    std::cout << "✓ Rate limit: retry allowed" << std::endl;

    // Max attempts
    decision = policy.should_retry(5, "timeout", 0);
    assert(!decision.should_retry);
    std::cout << "✓ Max attempts: no more retries" << std::endl;
}

void test_circuit_breaker() {
    std::cout << "\n=== Testing CircuitBreaker ===" << std::endl;

    CircuitBreaker::Config cfg;
    cfg.failure_threshold = 3;
    cfg.success_threshold = 2;
    cfg.timeout = std::chrono::seconds(1);

    CircuitBreaker cb(cfg);

    // Initial state: Closed
    assert(cb.get_state() == CircuitBreaker::State::Closed);
    assert(cb.allow_request());
    std::cout << "✓ Initial state: Closed, allows requests" << std::endl;

    // Record failures
    cb.record_failure();
    cb.record_failure();
    assert(cb.get_state() == CircuitBreaker::State::Closed);
    cb.record_failure();
    assert(cb.get_state() == CircuitBreaker::State::Open);
    assert(!cb.allow_request());
    std::cout << "✓ After 3 failures: Open, blocks requests" << std::endl;

    // Wait for timeout
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    assert(cb.allow_request());
    assert(cb.get_state() == CircuitBreaker::State::HalfOpen);
    std::cout << "✓ After timeout: HalfOpen, allows test request" << std::endl;

    // Success in HalfOpen
    cb.record_success();
    cb.record_success();
    assert(cb.get_state() == CircuitBreaker::State::Closed);
    std::cout << "✓ After 2 successes: Closed again" << std::endl;
}

void test_request_deduplicator() {
    std::cout << "\n=== Testing RequestDeduplicator ===" << std::endl;

    RequestDeduplicator dedup;
    RequestDeduplicator::RequestKey key{"read file", "hash123"};

    // Register request
    dedup.register_request(key);
    assert(dedup.is_in_flight(key));
    std::cout << "✓ Request registered, in-flight" << std::endl;

    // Simulate concurrent request
    std::thread t([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        dedup.complete_request(key, "file content");
    });

    auto result = dedup.wait_for_result(key, std::chrono::milliseconds(500));
    assert(result.has_value());
    assert(*result == "file content");
    std::cout << "✓ Concurrent request got result: " << *result << std::endl;

    t.join();

    assert(!dedup.is_in_flight(key));
    std::cout << "✓ Request completed, no longer in-flight" << std::endl;

    // Test cleanup
    RequestDeduplicator::RequestKey key2{"old task", "hash456"};
    dedup.register_request(key2);
    dedup.cleanup_expired(std::chrono::seconds(0));
    assert(!dedup.is_in_flight(key2));
    std::cout << "✓ Expired requests cleaned up" << std::endl;
}

void test_event_bus() {
    std::cout << "\n=== Testing EventBus ===" << std::endl;

    EventBus bus;
    int task_started_count = 0;
    int task_completed_count = 0;

    bus.subscribe(EventType::TaskStarted, [&](const Event& e) {
        ++task_started_count;
        assert(e.type == EventType::TaskStarted);
    });

    bus.subscribe(EventType::TaskCompleted, [&](const Event& e) {
        ++task_completed_count;
        assert(e.type == EventType::TaskCompleted);
    });

    bus.publish(Event(EventType::TaskStarted, "agent1", "task1"));
    bus.publish(Event(EventType::TaskStarted, "agent2", "task2"));
    bus.publish(Event(EventType::TaskCompleted, "agent1", "task1", "success"));

    assert(task_started_count == 2);
    assert(task_completed_count == 1);
    std::cout << "✓ Events published and handled correctly" << std::endl;

    bus.unsubscribe_all();
    bus.publish(Event(EventType::TaskStarted, "agent3", "task3"));
    assert(task_started_count == 2);
    std::cout << "✓ Unsubscribe works" << std::endl;
}

int main() {
    try {
        test_retry_policy();
        test_circuit_breaker();
        test_request_deduplicator();
        test_event_bus();

        std::cout << "\n=== 所有第二阶段测试通过 ===" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "测试失败: " << e.what() << std::endl;
        return 1;
    }
}
