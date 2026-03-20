// =============================================================================
// tests/test_models.cpp
//
// Unit tests for models.hpp: serialisation round-trips, status helpers,
// AgentConfig loading, JSON field validation.
// No external test framework  --  uses assert() and reports to stdout.
// =============================================================================

#include "agent/models.hpp"
#include <cassert>
#include <iostream>
#include <sstream>
#include <stdexcept>

using namespace agent;

// -----------------------------------------------------------------------------
#define TEST(name) static void name()
#define RUN(name)  do { name(); std::cout << "  PASS  " #name "\n"; } while(0)
// -----------------------------------------------------------------------------

TEST(test_status_round_trip) {
    for (auto s [[maybe_unused]] : {TaskStatus::Pending, TaskStatus::Running,
                   TaskStatus::Done, TaskStatus::Failed, TaskStatus::Rejected}) {
        assert(status_from_string(status_to_string(s)) == s);
    }
}

TEST(test_status_unknown_throws) {
    bool threw [[maybe_unused]] = false;
    try { (void)status_from_string("bogus"); }
    catch (const std::runtime_error&) { threw = true; }
    assert(threw);
}

TEST(test_atomic_task_round_trip) {
    AtomicTask original;
    original.id          = "mgr-1-atomic-2";
    original.parent_id   = "subtask-1";
    original.description = "Search for recent AI papers";
    original.tool        = "web_search";
    original.input       = "recent AI papers 2025";

    nlohmann::json j;
    to_json(j, original);

    AtomicTask restored;
    from_json(j, restored);

    assert(restored.id          == original.id);
    assert(restored.parent_id   == original.parent_id);
    assert(restored.description == original.description);
    assert(restored.tool        == original.tool);
    assert(restored.input       == original.input);
}

TEST(test_atomic_result_round_trip) {
    AtomicResult r;
    r.task_id = "atomic-1";
    r.status  = TaskStatus::Done;
    r.output  = "Found 42 papers.";
    r.error   = "";

    nlohmann::json j;
    to_json(j, r);

    AtomicResult r2;
    from_json(j, r2);

    assert(r2.task_id == r.task_id);
    assert(r2.status  == r.status);
    assert(r2.output  == r.output);
    assert(r2.error   == r.error);
}

TEST(test_subtask_round_trip) {
    SubTask t;
    t.id              = "subtask-1";
    t.description     = "Research quantum computing";
    t.expected_output = "Summary with at least 3 key developments";
    t.retry_feedback  = "";

    nlohmann::json j;
    to_json(j, t);

    SubTask t2;
    from_json(j, t2);

    assert(t2.id              == t.id);
    assert(t2.description     == t.description);
    assert(t2.expected_output == t.expected_output);
    assert(t2.retry_feedback  == t.retry_feedback);
}

TEST(test_subtask_report_round_trip) {
    SubTaskReport r;
    r.subtask_id = "subtask-1";
    r.status     = TaskStatus::Done;
    r.summary    = "PASSED  --  2/2 atomic tasks succeeded";
    r.issues     = "";

    AtomicResult ar1;
    ar1.task_id = "atomic-1"; ar1.status = TaskStatus::Done;
    ar1.output  = "Result A";

    AtomicResult ar2;
    ar2.task_id = "atomic-2"; ar2.status = TaskStatus::Done;
    ar2.output  = "Result B";

    r.results = {ar1, ar2};

    nlohmann::json j;
    to_json(j, r);

    SubTaskReport r2;
    from_json(j, r2);

    assert(r2.subtask_id    == r.subtask_id);
    assert(r2.status        == r.status);
    assert(r2.summary       == r.summary);
    assert(r2.results.size()== r.results.size());
    assert(r2.results[0].task_id == "atomic-1");
    assert(r2.results[1].output  == "Result B");
}

TEST(test_review_feedback_round_trip) {
    ReviewFeedback f;
    f.subtask_id = "subtask-2";
    f.approved   = false;
    f.feedback   = "Missing error analysis section";

    nlohmann::json j;
    to_json(j, f);

    ReviewFeedback f2;
    from_json(j, f2);

    assert(f2.subtask_id == f.subtask_id);
    assert(f2.approved   == f.approved);
    assert(f2.feedback   == f.feedback);
}

TEST(test_final_result_serialisation) {
    FinalResult r;
    r.status = TaskStatus::Done;
    r.answer = "The answer is 42.";
    r.error  = "";

    SubTaskReport sr;
    sr.subtask_id = "subtask-1";
    sr.status     = TaskStatus::Done;
    sr.summary    = "PASSED";
    r.sub_reports = {sr};

    nlohmann::json j;
    to_json(j, r);

    assert(j["status"].get<std::string>() == "done");
    assert(j["answer"].get<std::string>() == "The answer is 42.");
    assert(j["sub_reports"].size() == 1u);
}

TEST(test_agent_config_defaults) {
    AgentConfig cfg;
    assert(cfg.default_model            == "claude-opus-4-5-20251101");
    assert(cfg.max_tokens       == 2048);
    assert(cfg.request_timeout  == 60);
    assert(cfg.worker_threads   == 4);
    assert(cfg.log_level        == "info");
}

TEST(test_agent_config_from_json) {
    const std::string json_str = R"({
        "api_key": "sk-ant-test",
        "model": "claude-sonnet-4-20250514",
        "max_tokens": 1024,
        "worker_threads": 8,
        "log_level": "debug"
    })";

    auto j = nlohmann::json::parse(json_str);
    AgentConfig cfg;
    from_json(j, cfg);

    assert(cfg.api_key        == "sk-ant-test");
    assert(cfg.default_model          == "claude-sonnet-4-20250514");
    assert(cfg.max_tokens     == 1024);
    assert(cfg.worker_threads == 8);
    assert(cfg.log_level      == "debug");
    // Defaults preserved for unspecified fields
    assert(cfg.max_atomic_retries == 3);
}

TEST(test_failed_result_has_error) {
    AtomicResult r;
    r.task_id = "atomic-fail";
    r.status  = TaskStatus::Failed;
    r.output  = "";
    r.error   = "Tool not available";

    nlohmann::json j;
    to_json(j, r);

    assert(j["status"].get<std::string>() == "failed");
    assert(j["error"].get<std::string>()  == "Tool not available");
}

// -----------------------------------------------------------------------------
int main() {
    std::cout << "=== test_models ===\n";
    RUN(test_status_round_trip);
    RUN(test_status_unknown_throws);
    RUN(test_atomic_task_round_trip);
    RUN(test_atomic_result_round_trip);
    RUN(test_subtask_round_trip);
    RUN(test_subtask_report_round_trip);
    RUN(test_review_feedback_round_trip);
    RUN(test_final_result_serialisation);
    RUN(test_agent_config_defaults);
    RUN(test_agent_config_from_json);
    RUN(test_failed_result_has_error);
    std::cout << "All tests passed.\n";
    return 0;
}
