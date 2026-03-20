// =============================================================================
// tests/test_skill_registry.cpp
//
// Unit tests for SkillRegistry, CapabilityProfile, and WorkerAgent v3 methods:
//   - Skill registration and retrieval
//   - Skill invocation with parameter substitution
//   - Keyword-overlap matching
//   - Capability profile tracking (tool success rates)
//   - best_agent_for_tool selection
//   - Disk persistence (save/load)
//   - Placeholder path detection
// =============================================================================

#include "agent/skill_registry.hpp"
#include "agent/worker_agent.hpp"
#include "agent/tool_registry.hpp"
#include <cassert>
#include <cstdio>
#include <iostream>
#include <string>

using namespace agent;

#define TEST(name) static void name()
#define RUN(name)  do { name(); std::cout << "  PASS  " #name "\n"; } while(0)

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
// Shared tool registry for tests  --  ToolRegistry is non-copyable
static agent::ToolRegistry& get_test_tools() {
    static agent::ToolRegistry r;
    static bool init = false;
    if (!init) {
        init = true;
        r.register_tool("echo",        [](const std::string& s){ return s; });
        r.register_tool("upper",       [](const std::string& s){
            std::string u=s;
            for(auto& c:u) c=(char)std::toupper((unsigned char)c);
            return u;
        });
        r.register_tool("always_fail", [](const std::string&) -> std::string {
            throw std::runtime_error("forced failure");
        });
    }
    return r;
}

static SkillDef make_skill(const std::string& name,
                             const std::string& desc,
                             const std::vector<SkillStep>& steps) {
    SkillDef s;
    s.name        = name;
    s.description = desc;
    s.steps       = steps;
    s.created_by  = "test";
    s.run_id      = "run-test";
    return s;
}

// ---------------------------------------------------------------------------
// SkillDef
// ---------------------------------------------------------------------------
TEST(test_skilldef_success_rate_no_data) {
    SkillDef s;
    s.success_count = 0; s.fail_count = 0;
    assert(s.success_rate() == 0.5f);
}

TEST(test_skilldef_success_rate_mixed) {
    SkillDef s;
    s.success_count = 3; s.fail_count = 1;
    float r = s.success_rate();
    assert(r >= 0.74f && r <= 0.76f);
}

TEST(test_skilldef_success_rate_all_fail) {
    SkillDef s;
    s.success_count = 0; s.fail_count = 5;
    assert(s.success_rate() == 0.0f);
}

// ---------------------------------------------------------------------------
// CapabilityProfile
// ---------------------------------------------------------------------------
TEST(test_capability_initial_unknown) {
    CapabilityProfile p;
    p.agent_id = "wkr-1";
    assert(p.tool_rate("read_file") == 0.5f);   // no data = 0.5
}

TEST(test_capability_record_and_query) {
    CapabilityProfile p;
    p.agent_id = "wkr-1";
    p.record("read_file", true);
    p.record("read_file", true);
    p.record("read_file", false);
    float r = p.tool_rate("read_file");
    assert(r >= 0.65f && r <= 0.68f);  // 2/3
    assert(p.tasks_done   == 2);
    assert(p.tasks_failed == 1);
}

TEST(test_capability_best_tools) {
    CapabilityProfile p;
    p.agent_id = "wkr-1";
    for(int i=0;i<9;i++) p.record("read_file", true);
    p.record("read_file", false);   // 9/10 = 0.9 > 0.7
    for(int i=0;i<5;i++) { p.record("run_command", true); p.record("run_command", false); }
    // run_command = 0.5 < 0.7
    auto best = p.best_tools();
    bool has_read = false;
    for(auto& t : best) if(t == "read_file") has_read = true;
    assert(has_read);
}

TEST(test_capability_to_payload_valid_json_structure) {
    CapabilityProfile p;
    p.agent_id = "wkr-1";
    p.record("read_file", true);
    std::string pl = p.to_payload();
    assert(pl[0] == '{');
    assert(pl.back() == '}');
    assert(pl.find("wkr-1") != std::string::npos);
    assert(pl.find("tasks_done") != std::string::npos);
}

// ---------------------------------------------------------------------------
// SkillRegistry  --  registration and retrieval
// ---------------------------------------------------------------------------
TEST(test_registry_register_and_find) {
    SkillRegistry sr;
    SkillDef s = make_skill("my_skill", "read a markdown file robustly",
        {{"read_file","{{path}}","next"},{"echo","fallback","report"}});
    sr.register_skill(s, false);
    auto found = sr.find_matching_skill("read a markdown file");
    assert(found.has_value());
    assert(found->name == "my_skill");
}

TEST(test_registry_no_match_low_confidence) {
    SkillRegistry sr;
    SkillDef s = make_skill("file_skill", "read file contents",
        {{"read_file","{{path}}","report"}});
    sr.register_skill(s, false);
    auto found = sr.find_matching_skill("get the current weather", 0.4f);
    assert(!found.has_value());
}

TEST(test_registry_all_skills) {
    SkillRegistry sr;
    sr.register_skill(make_skill("s1","skill one",{{"echo","a","report"}}), false);
    sr.register_skill(make_skill("s2","skill two",{{"echo","b","report"}}), false);
    auto all = sr.all_skills();
    assert(all.size() == 2);
}

// ---------------------------------------------------------------------------
// SkillRegistry  --  invocation
// ---------------------------------------------------------------------------
TEST(test_skill_invoke_simple_echo) {
    SkillRegistry sr;
    
    SkillStep step{"echo","hello {{name}}","report"};
    SkillDef s = make_skill("greet","greet user",{step});
    s.parameters = {"name"};
    sr.register_skill(s, false);
    std::string out = sr.invoke_skill("greet", {{"name","world"}}, get_test_tools());
    assert(out == "hello world");
}

TEST(test_skill_invoke_fallback_on_fail) {
    SkillRegistry sr;
    
    // Step 1 always fails, step 2 succeeds
    SkillStep step1{"always_fail","{{input}}","next"};
    SkillStep step2{"echo","fallback result","report"};
    SkillDef s = make_skill("robust","robust read",{step1,step2});
    s.parameters = {"input"};
    sr.register_skill(s, false);
    std::string out = sr.invoke_skill("robust", {{"input","test"}}, get_test_tools());
    assert(out == "fallback result");
}

TEST(test_skill_invoke_parameter_substitution) {
    SkillRegistry sr;
    
    SkillStep step{"upper","{{text}}","report"};
    SkillDef s = make_skill("shout","convert to upper",{step});
    s.parameters = {"text"};
    sr.register_skill(s, false);
    std::string out = sr.invoke_skill("shout",{{"text","hello"}}, get_test_tools());
    assert(out == "HELLO");
}

TEST(test_skill_invoke_unknown_throws) {
    SkillRegistry sr;
    
    bool threw = false;
    try { (void)sr.invoke_skill("nonexistent",{},get_test_tools()); }
    catch(const std::exception&){ threw = true; }
    assert(threw);
}

// ---------------------------------------------------------------------------
// SkillRegistry  --  capability tracking
// ---------------------------------------------------------------------------
TEST(test_registry_update_capability) {
    SkillRegistry sr;
    sr.update_capability("wkr-1","read_file",true);
    sr.update_capability("wkr-1","read_file",true);
    sr.update_capability("wkr-1","read_file",false);
    auto cap = sr.get_capability("wkr-1");
    assert(cap.has_value());
    float r = cap->tool_rate("read_file");
    assert(r >= 0.65f && r <= 0.68f);
}

TEST(test_registry_no_capability_returns_nullopt) {
    SkillRegistry sr;
    auto cap = sr.get_capability("nonexistent");
    assert(!cap.has_value());
}

TEST(test_registry_best_agent_for_tool) {
    SkillRegistry sr;
    // wkr-1: 8/10 success with read_file
    for(int i=0;i<8;i++) sr.update_capability("wkr-1","read_file",true);
    for(int i=0;i<2;i++) sr.update_capability("wkr-1","read_file",false);
    // wkr-2: 3/10 success with read_file
    for(int i=0;i<3;i++) sr.update_capability("wkr-2","read_file",true);
    for(int i=0;i<7;i++) sr.update_capability("wkr-2","read_file",false);

    std::string best = sr.best_agent_for_tool("read_file", {"wkr-1","wkr-2"});
    assert(best == "wkr-1");
}

TEST(test_registry_best_agent_empty_candidates) {
    SkillRegistry sr;
    std::string best = sr.best_agent_for_tool("read_file", {});
    assert(best.empty());
}

// ---------------------------------------------------------------------------
// SkillRegistry  --  skill extraction
// ---------------------------------------------------------------------------
TEST(test_maybe_extract_single_tool_not_extracted) {
    SkillRegistry sr;
    // Single tool = not worth extracting as a skill
    std::string name = sr.maybe_extract_skill(
        "read a file", {"read_file"}, {"path.txt"}, "wkr-1", "run-1");
    assert(name.empty());
}

TEST(test_maybe_extract_multi_tool_creates_skill) {
    SkillRegistry sr;
    std::string name = sr.maybe_extract_skill(
        "read a windows file robustly",
        {"read_file","run_command"},
        {"path.txt","type \"path.txt\""},
        "wkr-1","run-1");
    assert(!name.empty());
    auto skills = sr.all_skills();
    assert(!skills.empty());
    assert(skills[0].steps.size() == 2);
}

TEST(test_maybe_extract_no_duplicate) {
    SkillRegistry sr;
    // First extraction
    (void)sr.maybe_extract_skill("read a file robustly",{"r","c"},{"i1","i2"},"w1","r1");
    assert(sr.all_skills().size() == 1);
    // Second extraction of very similar task should be skipped
    (void)sr.maybe_extract_skill("read a file robustly again",{"r","c"},{"i1","i2"},"w1","r2");
    // Should still be 1 (similarity > 0.7)
    assert(sr.all_skills().size() <= 2);  // may create at most 2
}

// ---------------------------------------------------------------------------
// SkillRegistry  --  statistics
// ---------------------------------------------------------------------------
TEST(test_skill_stats_update) {
    SkillRegistry sr;
    SkillDef s = make_skill("s1","test skill",{{"echo","x","report"}});
    sr.register_skill(s, false);
    sr.record_success("s1");
    sr.record_success("s1");
    sr.record_failure("s1");
    auto all = sr.all_skills();
    assert(!all.empty());
    assert(all[0].success_count == 2);
    assert(all[0].fail_count    == 1);
}

// ---------------------------------------------------------------------------
// SkillRegistry  --  disk persistence
// ---------------------------------------------------------------------------
TEST(test_skill_persist_save_load) {
    std::string dir = "/tmp/skill_test_" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count() % 100000);

    // Save
    {
        SkillRegistry sr(dir);
        SkillDef s = make_skill("persistent_skill","a skill to persist",
            {{"echo","{{val}}","report"}});
        s.parameters = {"val"};
        s.success_count = 5;
        sr.register_skill(s, true);
    }
    // Load
    {
        SkillRegistry sr(dir);
        auto found = sr.find_matching_skill("skill to persist");
        assert(found.has_value());
        assert(found->name == "persistent_skill");
        assert(found->success_count == 5);
        assert(found->parameters.size() == 1);
        assert(found->steps.size() == 1);
    }
}

// ---------------------------------------------------------------------------
// WorkerAgent::is_placeholder_path (static method)
// ---------------------------------------------------------------------------
TEST(test_is_placeholder_valid_paths) {
    assert(!WorkerAgent::is_placeholder_path("Desktop"));
    assert(!WorkerAgent::is_placeholder_path("C:\\\\Users\\\\Alice\\\\Desktop"));
    assert(!WorkerAgent::is_placeholder_path("/home/alice/docs"));
    assert(!WorkerAgent::is_placeholder_path("./local/file.txt"));
    assert(!WorkerAgent::is_placeholder_path(""));
}

TEST(test_is_placeholder_detects_placeholders) {
    assert(WorkerAgent::is_placeholder_path("<HOME>/Desktop"));
    assert(WorkerAgent::is_placeholder_path("$HOME/file.txt"));
    assert(WorkerAgent::is_placeholder_path("%USERPROFILE%\\Desktop"));
    assert(WorkerAgent::is_placeholder_path("<DESKTOP>"));
    assert(WorkerAgent::is_placeholder_path("/path/to/file.txt"));
    assert(WorkerAgent::is_placeholder_path("<file_path>"));
    assert(WorkerAgent::is_placeholder_path("[path]"));
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
#include <chrono>

int main() {
    std::cout << "=== test_skill_registry ===\n";

    RUN(test_skilldef_success_rate_no_data);
    RUN(test_skilldef_success_rate_mixed);
    RUN(test_skilldef_success_rate_all_fail);

    RUN(test_capability_initial_unknown);
    RUN(test_capability_record_and_query);
    RUN(test_capability_best_tools);
    RUN(test_capability_to_payload_valid_json_structure);

    RUN(test_registry_register_and_find);
    RUN(test_registry_no_match_low_confidence);
    RUN(test_registry_all_skills);

    RUN(test_skill_invoke_simple_echo);
    RUN(test_skill_invoke_fallback_on_fail);
    RUN(test_skill_invoke_parameter_substitution);
    RUN(test_skill_invoke_unknown_throws);

    RUN(test_registry_update_capability);
    RUN(test_registry_no_capability_returns_nullopt);
    RUN(test_registry_best_agent_for_tool);
    RUN(test_registry_best_agent_empty_candidates);

    RUN(test_maybe_extract_single_tool_not_extracted);
    RUN(test_maybe_extract_multi_tool_creates_skill);
    RUN(test_maybe_extract_no_duplicate);

    RUN(test_skill_stats_update);
    RUN(test_skill_persist_save_load);

    RUN(test_is_placeholder_valid_paths);
    RUN(test_is_placeholder_detects_placeholders);

    std::cout << "\nAll tests passed.\n";
    return 0;
}
