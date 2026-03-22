#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

#include "agent/circuit_breaker.hpp"
#include "agent/dependency_analyzer.hpp"
#include "agent/event_bus.hpp"
#include "agent/experience_db.hpp"
#include "agent/models.hpp"
#include "agent/request_deduplicator.hpp"
#include "agent/retry_policy.hpp"
#include "agent/task_router.hpp"

using namespace agent;

void test_task_router_fast_path() {
    TaskRouter router;
    auto decision = router.analyze("read config.json", {"read_file"});
    assert(decision == TaskRouter::RouteDecision::FastPath);
}

void test_dependency_batches() {
    std::vector<AtomicTask> tasks(3);
    tasks[0].id = "t1";
    tasks[0].tool = "write_file";
    tasks[0].input = "a.txt";

    tasks[1].id = "t2";
    tasks[1].tool = "read_file";
    tasks[1].input = "a.txt";

    tasks[2].id = "t3";
    tasks[2].tool = "read_file";
    tasks[2].input = "b.txt";

    DependencyAnalyzer analyzer;
    auto graph = analyzer.analyze(tasks);
    auto batches = analyzer.get_parallel_batches(graph);

    assert(batches.size() >= 2);
    assert(!batches.front().empty());
}

void test_circuit_breaker() {
    CircuitBreaker cb(CircuitBreaker::Config{3, 2, std::chrono::seconds(1)});
    cb.record_failure();
    cb.record_failure();
    cb.record_failure();
    assert(!cb.allow_request());
}

void test_request_deduplicator() {
    RequestDeduplicator dedup;
    RequestDeduplicator::RequestKey key{"task", "hash"};
    dedup.register_request(key);

    std::thread t([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        dedup.complete_request(key, "result");
    });

    auto result = dedup.wait_for_result(key, std::chrono::milliseconds(200));
    assert(result.has_value());
    assert(*result == "result");
    t.join();
}

void test_retry_policy() {
    RetryPolicy policy;
    auto decision = policy.should_retry(0, "timeout");
    assert(decision.should_retry);
    assert(decision.wait_ms == 100);

    decision = policy.should_retry(2, "timeout");
    assert(decision.should_retry);
    assert(decision.wait_ms == 400);
}

void test_experience_db() {
    ExperienceDB db;
    db.record_success("read file", "check file existence first");
    db.record_success("read file", "check file existence first");
    auto solution = db.query_solution("read file");
    assert(solution.has_value());
    assert(solution->find("check file existence first") != std::string::npos);
}

void test_event_bus() {
    EventBus bus;
    int count = 0;
    bus.subscribe(EventType::TaskStarted, [&](const Event&) { ++count; });
    bus.publish(Event(EventType::TaskStarted, "a1", "t1"));
    bus.publish(Event(EventType::TaskStarted, "a2", "t2"));
    assert(count == 2);
}

int main() {
    test_task_router_fast_path();
    test_dependency_batches();
    test_circuit_breaker();
    test_request_deduplicator();
    test_retry_policy();
    test_experience_db();
    test_event_bus();

    std::cout << "All integration tests passed." << std::endl;
    return 0;
}
