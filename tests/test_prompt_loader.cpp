// tests/test_prompt_loader.cpp
#include "utils/prompt_loader.hpp"
#include <cassert>
#include <iostream>
#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;

#define TEST(name) static void name()
#define RUN(name)  do { name(); std::cout << "  PASS  " #name "\n"; } while(0)

// Helper: create a temp prompt directory with test files
static std::string make_test_dir() {
    std::string dir = "/tmp/test_prompts_" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    fs::create_directories(dir + "/director/skills");
    fs::create_directories(dir + "/manager/skills");
    fs::create_directories(dir + "/worker/skills");
    fs::create_directories(dir + "/supervisor/skills");
    return dir;
}
static void write(const std::string& path, const std::string& content) {
    std::ofstream f(path); f << content;
}

// ── Tests ──────────────────────────────────────────────────────────────────

TEST(test_extract_body_no_frontmatter) {
    // When there is no frontmatter, body should be returned as-is
    agent::PromptLoader pl("/nonexistent");
    // We can't call private extract_body directly — test via assemble with a temp dir
    std::string dir = make_test_dir();
    write(dir + "/base.md", "Hello base");
    agent::PromptLoader pl2(dir);
    std::string result = pl2.assemble("director-decompose");
    assert(result.find("Hello base") != std::string::npos);
    fs::remove_all(dir);
}

TEST(test_extract_body_strips_frontmatter) {
    std::string dir = make_test_dir();
    write(dir + "/base.md", "---\nname: base\nrole: shared\n---\n\nBody content here");
    agent::PromptLoader pl(dir);
    std::string result = pl.assemble("worker-core");
    assert(result.find("Body content here") != std::string::npos);
    // frontmatter should be stripped
    assert(result.find("name: base") == std::string::npos);
    fs::remove_all(dir);
}

TEST(test_assemble_base_plus_soul) {
    std::string dir = make_test_dir();
    write(dir + "/base.md", "BASE_CONTENT");
    write(dir + "/director/SOUL.md", "SOUL_CONTENT");
    agent::PromptLoader pl(dir);
    std::string result = pl.assemble("director-decompose");
    assert(result.find("BASE_CONTENT") != std::string::npos);
    assert(result.find("SOUL_CONTENT") != std::string::npos);
    // base should come before soul
    assert(result.find("BASE_CONTENT") < result.find("SOUL_CONTENT"));
    fs::remove_all(dir);
}

TEST(test_assemble_includes_skill) {
    std::string dir = make_test_dir();
    write(dir + "/base.md", "BASE");
    write(dir + "/director/SOUL.md", "SOUL");
    write(dir + "/director/skills/decompose.md", "DECOMPOSE_SKILL");
    agent::PromptLoader pl(dir);
    std::string result = pl.assemble("director-decompose");
    assert(result.find("DECOMPOSE_SKILL") != std::string::npos);
    // order: base < soul < skill
    assert(result.find("BASE") < result.find("SOUL"));
    assert(result.find("SOUL") < result.find("DECOMPOSE_SKILL"));
    fs::remove_all(dir);
}

TEST(test_assemble_fallback_when_no_md) {
    std::string dir = make_test_dir();
    // No .md files exist in this dir
    agent::PromptLoader pl(dir);
    std::string result = pl.assemble("director-decompose", "FALLBACK_TXT");
    assert(result == "FALLBACK_TXT");
    fs::remove_all(dir);
}

TEST(test_assemble_different_roles) {
    std::string dir = make_test_dir();
    write(dir + "/base.md", "BASE");
    write(dir + "/manager/SOUL.md", "MANAGER_SOUL");
    write(dir + "/worker/SOUL.md", "WORKER_SOUL");
    write(dir + "/manager/skills/decompose.md", "MGR_DECOMPOSE");
    write(dir + "/worker/skills/tool_execution.md", "WKR_TOOL");
    agent::PromptLoader pl(dir);

    std::string mgr = pl.assemble("manager-decompose");
    assert(mgr.find("MANAGER_SOUL") != std::string::npos);
    assert(mgr.find("MGR_DECOMPOSE") != std::string::npos);
    assert(mgr.find("WORKER_SOUL") == std::string::npos);  // no cross-contamination

    std::string wkr = pl.assemble("worker-core");
    assert(wkr.find("WORKER_SOUL") != std::string::npos);
    assert(wkr.find("WKR_TOOL") != std::string::npos);
    assert(wkr.find("MANAGER_SOUL") == std::string::npos);

    fs::remove_all(dir);
}

TEST(test_list_prompts) {
    std::string dir = make_test_dir();
    write(dir + "/base.md", "BASE");
    write(dir + "/director/SOUL.md", "SOUL");
    write(dir + "/director/skills/decompose.md", "SKILL");
    agent::PromptLoader pl(dir);
    auto prompts = pl.list_prompts();
    assert(prompts.size() == 3);
    fs::remove_all(dir);
}

TEST(test_partial_md_uses_fallback_for_missing_parts) {
    // Only base.md exists, no SOUL or skills
    // assemble should still return base content (not empty fallback)
    std::string dir = make_test_dir();
    write(dir + "/base.md", "ONLY_BASE");
    agent::PromptLoader pl(dir);
    std::string result = pl.assemble("director-decompose", "FALLBACK");
    assert(result.find("ONLY_BASE") != std::string::npos);
    // Since we have SOME content, fallback should NOT be used
    assert(result.find("FALLBACK") == std::string::npos);
    fs::remove_all(dir);
}

TEST(test_real_prompts_directory) {
    // Test against the actual prompts/ directory
    agent::PromptLoader pl("prompts");
    auto files = pl.list_prompts();
    // We should have at least 15 .md files
    assert(files.size() >= 15);

    // All 7 roles should assemble non-empty prompts
    for (auto& role : {"director-decompose", "director-review", "director-synthesise",
                       "manager-decompose", "manager-validate", "worker-core",
                       "supervisor-evaluate"}) {
        std::string p = pl.assemble(role);
        assert(!p.empty());
        // Should contain content from base.md
        assert(p.find("Never do") != std::string::npos ||
               p.find("Always") != std::string::npos ||
               p.find("Ask first") != std::string::npos);
    }
}

TEST(test_assemble_order_prompt_cache_friendly) {
    // base (most stable) must always appear first
    std::string dir = make_test_dir();
    write(dir + "/base.md", "AAAA_BASE");
    write(dir + "/worker/SOUL.md", "BBBB_SOUL");
    write(dir + "/worker/skills/tool_execution.md", "CCCC_SKILL");
    agent::PromptLoader pl(dir);
    std::string result = pl.assemble("worker-core");
    size_t pos_base  = result.find("AAAA_BASE");
    size_t pos_soul  = result.find("BBBB_SOUL");
    size_t pos_skill = result.find("CCCC_SKILL");
    assert(pos_base  != std::string::npos);
    assert(pos_soul  != std::string::npos);
    assert(pos_skill != std::string::npos);
    assert(pos_base < pos_soul);
    assert(pos_soul < pos_skill);
    fs::remove_all(dir);
}

TEST(test_parse_meta_extracts_frontmatter) {
    std::string dir = make_test_dir();
    write(dir + "/base.md",
        "---\nname: test-base\nrole: shared\nversion: 2.0.0\n"
        "description: A test base prompt\n---\n\nBody content");
    agent::PromptLoader pl(dir);
    auto all = pl.list_all_meta();
    assert(all.size() == 1);
    assert(all[0].meta.name == "test-base");
    assert(all[0].meta.role == "shared");
    assert(all[0].meta.version == "2.0.0");
    assert(all[0].meta.description == "A test base prompt");
    fs::remove_all(dir);
}

TEST(test_list_skills_returns_skill_names) {
    agent::PromptLoader pl("prompts");
    auto skills = pl.list_skills();
    assert(skills.size() >= 5);  // file_ops, system_ops, code_exec, memory_ops, analysis
    bool has_file_ops = std::find(skills.begin(), skills.end(), "file_ops") != skills.end();
    assert(has_file_ops);
}

TEST(test_load_skill_returns_body) {
    agent::PromptLoader pl("prompts");
    std::string body = pl.load_skill("file_ops");
    assert(!body.empty());
    assert(body.find("read_file") != std::string::npos);
    // frontmatter should be stripped
    assert(body.find("---") == std::string::npos || body.find("---") > 10);
}

TEST(test_worker_core_gets_default_skills) {
    std::string dir = make_test_dir();
    write(dir + "/base.md", "BASE");
    write(dir + "/worker/SOUL.md", "SOUL");
    write(dir + "/worker/skills/tool_execution.md", "TOOL_EXEC");
    fs::create_directories(dir + "/skills");
    write(dir + "/skills/file_ops.md", "FILE_OPS_SKILL");
    write(dir + "/skills/system_ops.md", "SYS_OPS_SKILL");
    agent::PromptLoader pl(dir);
    std::string result = pl.assemble("worker-core");
    assert(result.find("FILE_OPS_SKILL") != std::string::npos);
    assert(result.find("SYS_OPS_SKILL") != std::string::npos);
    fs::remove_all(dir);
}

TEST(test_director_decompose_no_extra_skills) {
    // decompose role should NOT get analysis skill injected (keep tight)
    std::string dir = make_test_dir();
    write(dir + "/base.md", "BASE");
    write(dir + "/director/SOUL.md", "SOUL");
    write(dir + "/director/skills/decompose.md", "DECOMPOSE");
    fs::create_directories(dir + "/skills");
    write(dir + "/skills/analysis.md", "ANALYSIS_SKILL");
    agent::PromptLoader pl(dir);
    std::string result = pl.assemble("director-decompose");
    assert(result.find("DECOMPOSE") != std::string::npos);
    assert(result.find("ANALYSIS_SKILL") == std::string::npos);  // not injected for decompose
    fs::remove_all(dir);
}

int main() {
    std::cout << "=== test_prompt_loader ===\n";
    RUN(test_extract_body_no_frontmatter);
    RUN(test_extract_body_strips_frontmatter);
    RUN(test_assemble_base_plus_soul);
    RUN(test_assemble_includes_skill);
    RUN(test_assemble_fallback_when_no_md);
    RUN(test_assemble_different_roles);
    RUN(test_list_prompts);
    RUN(test_partial_md_uses_fallback_for_missing_parts);
    RUN(test_real_prompts_directory);
    RUN(test_assemble_order_prompt_cache_friendly);
    RUN(test_parse_meta_extracts_frontmatter);
    RUN(test_list_skills_returns_skill_names);
    RUN(test_load_skill_returns_body);
    RUN(test_worker_core_gets_default_skills);
    RUN(test_director_decompose_no_extra_skills);
    std::cout << "All prompt loader tests passed.\n";
}
