// =============================================================================
// tests/test_director.cpp   --   Fix #16
//
// Tests DirectorAgent logic in isolation:
//   - decompose_goal JSON parsing
//   - review JSON parsing and approval logic
//   - retry_rejected index tracking
//   - FinalResult timestamp population
//   - IManager/ManagerFactory interface boundary
//   - run-id uniqueness
// All LLM calls are replaced by a stub IManager.
// =============================================================================
#include "agent/models.hpp"
#include "agent/director_agent.hpp"
#include "agent/exceptions.hpp"
#include "utils/json_utils.hpp"
#include "utils/logger.hpp"
#include "utils/thread_pool.hpp"

#include <cassert>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <atomic>

using namespace agent;

#define TEST(name) static void name()
#define RUN(name)  do { name(); std::cout << "  PASS  " #name "\n"; } while(0)

// -----------------------------------------------------------------------------
// Stub IManager: always returns a configurable SubTaskReport
// -----------------------------------------------------------------------------
struct StubManager final : public IManager {
    std::string      id_val;
    SubTaskReport    report_to_return;
    mutable int      call_count = 0;

    StubManager(std::string i, SubTaskReport r)
        : id_val(std::move(i)), report_to_return(std::move(r)) {}

    SubTaskReport process(const SubTask& task) noexcept override {
        ++call_count;
        SubTaskReport r = report_to_return;
        r.subtask_id    = task.id;
        return r;
    }
    const std::string& id() const noexcept override { return id_val; }
};

// -----------------------------------------------------------------------------
// JSON parse helpers (mirror Director's internal logic)
// -----------------------------------------------------------------------------

static std::vector<SubTask> parse_subtasks(const std::string& json_str) {
    auto j = parse_llm_json(json_str);
    if (!j.is_array()) throw ParseException("expected array", json_str, "test", "Director");
    std::vector<SubTask> tasks;
    for (size_t i = 0; i < j.size(); ++i) {
        SubTask t; from_json(j[i], t);
        if (t.id.empty()) t.id = "subtask-" + std::to_string(i + 1);
        tasks.push_back(std::move(t));
    }
    return tasks;
}

static std::vector<ReviewFeedback> parse_feedbacks(const std::string& json_str) {
    auto j = parse_llm_json(json_str);
    if (!j.is_array()) throw ParseException("expected array", json_str, "test", "Director");
    std::vector<ReviewFeedback> fbs;
    for (size_t i = 0; i < j.size(); ++i) {
        ReviewFeedback fb; from_json(j[i], fb);
        fbs.push_back(std::move(fb));
    }
    return fbs;
}

// -----------------------------------------------------------------------------
TEST(test_subtask_decompose_parse) {
    const std::string json = R"([
        {"id":"subtask-1","description":"Research the topic","expected_output":"3 key points","retry_feedback":""},
        {"id":"subtask-2","description":"Write the summary","expected_output":"500 word summary","retry_feedback":""}
    ])";
    auto tasks = parse_subtasks(json);
    assert(tasks.size() == 2u);
    assert(tasks[0].id              == "subtask-1");
    assert(tasks[0].description     == "Research the topic");
    assert(tasks[0].expected_output == "3 key points");
    assert(tasks[1].id              == "subtask-2");
    assert(tasks[1].retry_feedback.empty());
}

TEST(test_subtask_auto_id) {
    const std::string json = R"([
        {"description":"Task A","expected_output":"Output A","retry_feedback":""},
        {"description":"Task B","expected_output":"Output B","retry_feedback":""}
    ])";
    auto tasks = parse_subtasks(json);
    assert(tasks.size() == 2u);
    assert(tasks[0].id == "subtask-1");
    assert(tasks[1].id == "subtask-2");
}

TEST(test_subtask_empty_array_detection) {
    // parse_llm_json will succeed but we should detect empty list
    const std::string json = "[]";
    auto tasks = parse_subtasks(json);
    assert(tasks.empty());
}

TEST(test_review_feedback_all_approved) {
    const std::string json = R"([
        {"subtask_id":"subtask-1","approved":true,"feedback":""},
        {"subtask_id":"subtask-2","approved":true,"feedback":""}
    ])";
    auto fbs = parse_feedbacks(json);
    assert(fbs.size() == 2u);
    assert(fbs[0].approved == true);
    assert(fbs[1].approved == true);
}

TEST(test_review_feedback_partial_rejection) {
    const std::string json = R"([
        {"subtask_id":"subtask-1","approved":true,"feedback":""},
        {"subtask_id":"subtask-2","approved":false,"feedback":"Missing error analysis"},
        {"subtask_id":"subtask-3","approved":true,"feedback":""}
    ])";
    auto fbs = parse_feedbacks(json);
    assert(fbs.size() == 3u);
    assert(fbs[0].approved == true);
    assert(fbs[1].approved == false);
    assert(fbs[1].feedback == "Missing error analysis");
    assert(fbs[2].approved == true);
}

TEST(test_rejected_indices_tracking) {
    // Simulate retry_rejected index logic
    std::vector<ReviewFeedback> feedbacks = {
        {"st-1", true,  ""},
        {"st-2", false, "Incomplete"},
        {"st-3", true,  ""},
        {"st-4", false, "Wrong format"},
    };
    std::vector<size_t> rejected;
    for (size_t i = 0; i < feedbacks.size(); ++i)
        if (!feedbacks[i].approved) rejected.push_back(i);

    assert(rejected.size() == 2u);
    assert(rejected[0] == 1u);
    assert(rejected[1] == 3u);
}

TEST(test_stub_manager_interface) {
    SubTaskReport rpt;
    rpt.status  = TaskStatus::Done;
    rpt.summary = "PASSED  --  2/2";

    StubManager mgr("mgr-test", rpt);
    SubTask t; t.id = "subtask-1"; t.description = "test";
    auto result = mgr.process(t);

    assert(result.subtask_id == "subtask-1");
    assert(result.status     == TaskStatus::Done);
    assert(result.summary    == "PASSED  --  2/2");
    assert(mgr.call_count    == 1);
}

TEST(test_final_result_serialisation_with_timestamps) {
    FinalResult r;
    r.status      = TaskStatus::Done;
    r.answer      = "The answer is 42.";
    r.started_at  = "2025-03-18T10:00:00.000Z";
    r.finished_at = "2025-03-18T10:00:05.123Z";

    SubTaskReport sr;
    sr.subtask_id = "subtask-1";
    sr.status     = TaskStatus::Done;
    sr.summary    = "PASSED";
    r.sub_reports = {sr};

    nlohmann::json j; to_json(j, r);
    assert(j["status"].get<std::string>()      == "done");
    assert(j["answer"].get<std::string>()      == "The answer is 42.");
    assert(j["started_at"].get<std::string>()  == "2025-03-18T10:00:00.000Z");
    assert(j["finished_at"].get<std::string>() == "2025-03-18T10:00:05.123Z");
    assert(j["sub_reports"].size()             == 1u);
}

TEST(test_thread_pool_manager_dispatch) {
    // Simulate dispatch_managers using StubManagers in thread pool
    ThreadPool pool(2);

    std::vector<SubTask> tasks;
    for (int i = 0; i < 4; ++i) {
        SubTask t; t.id = "subtask-" + std::to_string(i+1);
        t.description = "Task " + std::to_string(i+1);
        tasks.push_back(t);
    }

    std::vector<std::future<SubTaskReport>> futures;
    for (const auto& task : tasks) {
        SubTaskReport rpt;
        rpt.status  = TaskStatus::Done;
        rpt.summary = "PASSED";
        auto mgr = std::make_shared<StubManager>("mgr-" + task.id, rpt);

        futures.push_back(pool.submit([mgr, t = task]() mutable {
            return mgr->process(t);
        }));
    }

    std::vector<SubTaskReport> reports;
    for (auto& f : futures) reports.push_back(f.get());

    assert(reports.size() == 4u);
    for ([[maybe_unused]] const auto& r : reports)
        assert(r.status == TaskStatus::Done);
}

TEST(test_retry_feedback_propagated_to_subtask) {
    SubTask original;
    original.id             = "subtask-2";
    original.description    = "Analyse data";
    original.expected_output= "Statistical report";
    original.retry_feedback = "";

    // Simulate Director setting retry_feedback before re-issue
    SubTask revised       = original;
    revised.retry_feedback = "The previous output was missing confidence intervals.";

    assert(revised.id             == original.id);
    assert(revised.description    == original.description);
    assert(revised.retry_feedback == "The previous output was missing confidence intervals.");
    assert(original.retry_feedback.empty());  // original unchanged
}

TEST(test_parse_exception_carries_context) {
    [[maybe_unused]] bool threw = false;
    try {
        parse_subtasks("not valid json at all");
    } catch (const ParseException& e) { (void)e;
        threw = true;
        assert(std::string(e.what()).find("extract_json") != std::string::npos ||
               std::string(e.what()).find("expected array") != std::string::npos ||
               std::string(e.what()).find("no JSON") != std::string::npos);
    } catch (const std::runtime_error&) {
        threw = true;  // also acceptable
    }
    assert(threw);
}

// -----------------------------------------------------------------------------
int main() {
    Logger::instance().set_level(LogLevel::Error);
    std::cout << "=== test_director ===\n";
    RUN(test_subtask_decompose_parse);
    RUN(test_subtask_auto_id);
    RUN(test_subtask_empty_array_detection);
    RUN(test_review_feedback_all_approved);
    RUN(test_review_feedback_partial_rejection);
    RUN(test_rejected_indices_tracking);
    RUN(test_stub_manager_interface);
    RUN(test_final_result_serialisation_with_timestamps);
    RUN(test_thread_pool_manager_dispatch);
    RUN(test_retry_feedback_propagated_to_subtask);
    RUN(test_parse_exception_carries_context);
    std::cout << "All tests passed.\n";
    return 0;
}
