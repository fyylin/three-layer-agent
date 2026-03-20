// =============================================================================
// tests/test_manager.cpp
//
// Tests the ManagerAgent decomposition parsing, result summarisation, and
// retry-count logic using controlled inputs.  Network-dependent paths (LLM
// calls) are tested at integration level; here we exercise parsing helpers
// and the thread-pool dispatch contract.
// =============================================================================
#include "agent/models.hpp"
#include "agent/exceptions.hpp"
#include "utils/json_utils.hpp"
#include "utils/thread_pool.hpp"
#include "utils/logger.hpp"

#include <cassert>
#include <iostream>
#include <vector>
#include <future>
#include <string>

using namespace agent;

#define TEST(name) static void name()
#define RUN(name)  do { name(); std::cout << "  PASS  " #name "\n"; } while(0)

// -----------------------------------------------------------------------------
// Helpers that mirror ManagerAgent's internal logic for isolated testing
// -----------------------------------------------------------------------------

// Simulates decompose(): parse a JSON array into AtomicTask list
static std::vector<AtomicTask> parse_atomic_tasks(const std::string& json_str,
                                                    const std::string& parent_id) {
    auto j = parse_llm_json(json_str);
    if (!j.is_array())
        throw ParseException("expected array", json_str, parent_id, "Manager");

    std::vector<AtomicTask> tasks;
    for (size_t i = 0; i < j.size(); ++i) {
        AtomicTask t;
        from_json(j[i], t);
        if (t.parent_id.empty()) t.parent_id = parent_id;
        if (t.id.empty()) t.id = parent_id + "-atomic-" + std::to_string(i + 1);
        tasks.push_back(std::move(t));
    }
    return tasks;
}

// Simulates build_summary()
static std::string build_summary(const std::vector<AtomicResult>& results, bool approved) {
    size_t done = 0, failed = 0;
    for (const auto& r : results) {
        if (r.status == TaskStatus::Done)   ++done;
        if (r.status == TaskStatus::Failed) ++failed;
    }
    std::string s = (approved ? "PASSED" : "FAILED");
    s += "  --  " + std::to_string(done) + "/" + std::to_string(results.size())
       + " atomic tasks succeeded";
    if (failed) s += ", " + std::to_string(failed) + " failed";
    return s;
}

// -----------------------------------------------------------------------------
TEST(test_decompose_valid_json) {
    const std::string json = R"([
        {"id":"st1-atomic-1","parent_id":"st1","description":"Step A","tool":"","input":""},
        {"id":"st1-atomic-2","parent_id":"st1","description":"Step B","tool":"read_file","input":"/tmp/x"}
    ])";
    auto tasks = parse_atomic_tasks(json, "st1");
    assert(tasks.size() == 2u);
    assert(tasks[0].id          == "st1-atomic-1");
    assert(tasks[0].description == "Step A");
    assert(tasks[1].tool        == "read_file");
    assert(tasks[1].input       == "/tmp/x");
}

TEST(test_decompose_missing_ids_autofilled) {
    const std::string json = R"([
        {"description":"Step A","tool":"","input":""},
        {"description":"Step B","tool":"","input":""}
    ])";
    auto tasks = parse_atomic_tasks(json, "subtask-99");
    assert(tasks.size() == 2u);
    assert(tasks[0].id        == "subtask-99-atomic-1");
    assert(tasks[0].parent_id == "subtask-99");
    assert(tasks[1].id        == "subtask-99-atomic-2");
}

TEST(test_decompose_not_array_throws) {
    const std::string json = R"({"description":"oops"})";
    [[maybe_unused]] bool threw = false;
    try { parse_atomic_tasks(json, "st1"); }
    catch (const ParseException&) { threw = true; }
    assert(threw);
}

TEST(test_decompose_fenced_json) {
    const std::string json = "```json\n[{\"id\":\"a-1\",\"description\":\"X\",\"tool\":\"\",\"input\":\"\"}]\n```";
    auto tasks = parse_atomic_tasks(json, "a");
    assert(tasks.size() == 1u);
    assert(tasks[0].description == "X");
}

TEST(test_summary_all_done) {
    std::vector<AtomicResult> results;
    for (int i = 0; i < 3; ++i) {
        AtomicResult r;
        r.task_id = "t" + std::to_string(i);
        r.status  = TaskStatus::Done;
        r.output  = "ok";
        results.push_back(r);
    }
    std::string s = build_summary(results, true);
    assert(s.find("PASSED") != std::string::npos);
    assert(s.find("3/3")    != std::string::npos);
}

TEST(test_summary_partial_failure) {
    std::vector<AtomicResult> results;
    AtomicResult r1; r1.task_id="t1"; r1.status=TaskStatus::Done;   results.push_back(r1);
    AtomicResult r2; r2.task_id="t2"; r2.status=TaskStatus::Failed;  results.push_back(r2);
    std::string s = build_summary(results, false);
    assert(s.find("FAILED") != std::string::npos);
    assert(s.find("1/2")    != std::string::npos);
    assert(s.find("1 failed")!= std::string::npos);
}

TEST(test_review_feedback_parse) {
    const std::string json = R"([
        {"subtask_id":"st-1","approved":true,"feedback":""},
        {"subtask_id":"st-2","approved":false,"feedback":"Missing error section"}
    ])";
    auto j = parse_llm_json(json);
    assert(j.is_array());
    assert(j.size() == 2u);

    ReviewFeedback f1, f2;
    from_json(j[0], f1);
    from_json(j[1], f2);

    assert(f1.approved   == true);
    assert(f1.subtask_id == "st-1");
    assert(f2.approved   == false);
    assert(f2.feedback   == "Missing error section");
}

TEST(test_threadpool_dispatch_pattern) {
    // Simulates how ManagerAgent dispatches atomic tasks to the pool
    ThreadPool pool(2);
    std::vector<AtomicTask> tasks;
    for (int i = 0; i < 5; ++i) {
        AtomicTask t;
        t.id = "atomic-" + std::to_string(i);
        t.description = "Task " + std::to_string(i);
        tasks.push_back(t);
    }

    std::vector<std::future<AtomicResult>> futures;
    for (const auto& task : tasks) {
        futures.push_back(pool.submit([t = task]() -> AtomicResult {
            AtomicResult r;
            r.task_id = t.id;
            r.status  = TaskStatus::Done;
            r.output  = "Result for " + t.id;
            return r;
        }));
    }

    std::vector<AtomicResult> results;
    for (auto& f : futures)
        results.push_back(f.get());

    assert(results.size() == 5u);
    for ([[maybe_unused]] const auto& r : results)
        assert(r.status == TaskStatus::Done);
}

TEST(test_subtask_with_retry_feedback_serialises) {
    SubTask t;
    t.id             = "subtask-3";
    t.description    = "Analyse dataset";
    t.expected_output= "Statistical summary";
    t.retry_feedback = "Previous attempt missed standard deviation";

    nlohmann::json j;
    to_json(j, t);

    SubTask t2;
    from_json(j, t2);
    assert(t2.retry_feedback == "Previous attempt missed standard deviation");
}

// -----------------------------------------------------------------------------
int main() {
    Logger::instance().set_level(LogLevel::Error);

    std::cout << "=== test_manager ===\n";
    RUN(test_decompose_valid_json);
    RUN(test_decompose_missing_ids_autofilled);
    RUN(test_decompose_not_array_throws);
    RUN(test_decompose_fenced_json);
    RUN(test_summary_all_done);
    RUN(test_summary_partial_failure);
    RUN(test_review_feedback_parse);
    RUN(test_threadpool_dispatch_pattern);
    RUN(test_subtask_with_retry_feedback_serialises);
    std::cout << "All tests passed.\n";
    return 0;
}
