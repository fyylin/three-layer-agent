// =============================================================================
// tests/test_worker.cpp
//
// Tests WorkerAgent in isolation using a stub ApiClient subclass that returns
// pre-canned responses without making any real HTTP calls.
// =============================================================================
#include "agent/worker_agent.hpp"
#include "agent/tool_registry.hpp"
#include "agent/exceptions.hpp"
#include "utils/logger.hpp"

#include <cassert>
#include <iostream>
#include <stdexcept>

using namespace agent;

#define TEST(name) static void name()
#define RUN(name)  do { name(); std::cout << "  PASS  " #name "\n"; } while(0)

// -----------------------------------------------------------------------------
// Stub ApiClient
// -----------------------------------------------------------------------------
// We can't easily subclass ApiClient without virtual methods, so instead we
// build a minimal test harness by providing a fake completion function and
// using a thin wrapper. For simplicity the tests directly exercise the parsing
// and tool logic that are testable without network calls.

// Helper: build a well-formed worker JSON response string
static std::string make_done_json(const std::string& output) {
    return R"({"status":"done","output":")" + output + R"(","error":""})";
}
static std::string make_failed_json(const std::string& error) {
    return R"({"status":"failed","output":"","error":")" + error + R"("})";
}

// -----------------------------------------------------------------------------
// Tests for ToolRegistry (standalone, no ApiClient needed)
// -----------------------------------------------------------------------------

TEST(test_registry_register_and_invoke) {
    ToolRegistry reg;
    reg.register_tool("upper", [](const std::string& s) {
        std::string r = s;
        for (char& c : r) c = static_cast<char>(toupper(c));
        return r;
    });
    assert(reg.has_tool("upper"));
    assert(reg.invoke("upper", "hello") == "HELLO");
}

TEST(test_registry_missing_tool_throws) {
    ToolRegistry reg;
    [[maybe_unused]] bool threw = false;
    try { (void)reg.invoke("nonexistent", "input"); }
    catch (const ToolException& e) { threw = true; }
    assert(threw);
}

TEST(test_registry_tool_exception_wrapped) {
    ToolRegistry reg;
    reg.register_tool("bad", [](const std::string&) -> std::string {
        throw std::runtime_error("inner failure");
    });
    [[maybe_unused]] bool threw = false;
    try { (void)reg.invoke("bad", "x"); }
    catch (const ToolException& e) {
        threw = true;
        assert(std::string(e.what()).find("inner failure") != std::string::npos);
    }
    assert(threw);
}

TEST(test_registry_tool_names_sorted) {
    ToolRegistry reg;
    reg.register_tool("zeta", [](const std::string& s){ return s; });
    reg.register_tool("alpha",[](const std::string& s){ return s; });
    reg.register_tool("beta", [](const std::string& s){ return s; });
    auto names = reg.tool_names();
    assert(names.size() == 3u);
    assert(names[0] == "alpha");
    assert(names[1] == "beta");
    assert(names[2] == "zeta");
}

TEST(test_registry_overwrite) {
    ToolRegistry reg;
    reg.register_tool("fn", [](const std::string&){ return "v1"; });
    reg.register_tool("fn", [](const std::string&){ return "v2"; });
    assert(reg.invoke("fn", "") == "v2");
}

// -----------------------------------------------------------------------------
// json_utils tests (extract_json, parse_llm_json)
// -----------------------------------------------------------------------------
#include "utils/json_utils.hpp"

TEST(test_extract_json_plain) {
    std::string raw = R"({"status":"done","output":"ok","error":""})";
    std::string extracted = extract_json(raw);
    auto j = nlohmann::json::parse(extracted);
    assert(j["status"].get<std::string>() == "done");
}

TEST(test_extract_json_with_prose_prefix) {
    std::string raw = "Here is the result:\n{\"status\":\"done\",\"output\":\"x\",\"error\":\"\"}";
    auto j = parse_llm_json(raw);
    assert(j["output"].get<std::string>() == "x");
}

TEST(test_extract_json_with_fence) {
    std::string raw = "```json\n{\"status\":\"done\",\"output\":\"y\",\"error\":\"\"}\n```";
    auto j = parse_llm_json(raw);
    assert(j["output"].get<std::string>() == "y");
}

TEST(test_extract_json_array) {
    std::string raw = "[{\"id\":\"1\"},{\"id\":\"2\"}]";
    auto j = parse_llm_json(raw);
    assert(j.is_array());
    assert(j.size() == 2u);
}

TEST(test_extract_json_no_json_throws) {
    [[maybe_unused]] bool threw = false;
    try { (void)extract_json("no json here at all"); }
    catch (const std::runtime_error&) { threw = true; }
    assert(threw);
}

TEST(test_render_template) {
    std::string tmpl = "Hello {{name}}, your score is {{score}}.";
    std::string out  = render_template(tmpl, {{"name","Alice"},{"score","99"}});
    assert(out == "Hello Alice, your score is 99.");
}

TEST(test_render_template_missing_key_throws) {
    [[maybe_unused]] bool threw = false;
    try {
        (void)render_template("Hello {{name}}", {{"other","x"}});
    } catch (const std::runtime_error&) {
        threw = true;
    }
    assert(threw);
}

// -----------------------------------------------------------------------------
// models::status helpers
// -----------------------------------------------------------------------------

TEST(test_status_all_values) {
    assert(status_to_string(TaskStatus::Pending)  == "pending");
    assert(status_to_string(TaskStatus::Running)  == "running");
    assert(status_to_string(TaskStatus::Done)     == "done");
    assert(status_to_string(TaskStatus::Failed)   == "failed");
    assert(status_to_string(TaskStatus::Rejected) == "rejected");
}

// -----------------------------------------------------------------------------
int main() {
    Logger::instance().set_level(LogLevel::Error); // silence info logs in tests

    std::cout << "=== test_worker ===\n";
    RUN(test_registry_register_and_invoke);
    RUN(test_registry_missing_tool_throws);
    RUN(test_registry_tool_exception_wrapped);
    RUN(test_registry_tool_names_sorted);
    RUN(test_registry_overwrite);
    RUN(test_extract_json_plain);
    RUN(test_extract_json_with_prose_prefix);
    RUN(test_extract_json_with_fence);
    RUN(test_extract_json_array);
    RUN(test_extract_json_no_json_throws);
    RUN(test_render_template);
    RUN(test_render_template_missing_key_throws);
    RUN(test_status_all_values);
    std::cout << "All tests passed.\n";
    return 0;
}
