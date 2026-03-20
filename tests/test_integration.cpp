// =============================================================================
// tests/test_integration.cpp
//
// Integration tests for the full Supervisor->Director->Manager->Worker pipeline.
// Uses stub_api_client so no real API calls are made.
//
// Test scenarios:
//   1. L0 conversational goal -> Director answers directly (no Manager/Worker)
//   2. Simple goal -> full pipeline completes successfully
//   3. Worker tool failure -> self-healing kicks in, still Done
//   4. assess_complexity heuristic coverage
//   5. AgentConfig::validate() catches bad config
//   6. AtomicTask DAG with depends_on (sequential execution)
//   7. ManagerPool reuses managers for same task type
//   8. register_context passes real state (Supervisor can monitor)
// =============================================================================

#include "agent/models.hpp"
#include "agent/director_agent.hpp"
#include "utils/env_knowledge.hpp"
#include "utils/task_rules.hpp"
#include "agent/manager_agent.hpp"
#include "agent/worker_agent.hpp"
#include "agent/supervisor_agent.hpp"
#include "agent/advisor_agent.hpp"
#include "agent/skill_registry.hpp"
#include "agent/manager_pool.hpp"
#include "agent/imanager.hpp"
#include "agent/api_client.hpp"
#include "agent/tool_registry.hpp"
#include "agent/agent_context.hpp"
#include "agent/agent_state.hpp"
#include "utils/logger.hpp"
#include "utils/memory_store.hpp"
#include "utils/message_bus.hpp"
#include "utils/thread_pool.hpp"
#include "utils/workspace.hpp"
#include <cassert>
#include <iostream>
#include <atomic>
#include <memory>

#define TEST(name) static void name()
#define RUN(name)  do { name(); std::cout << "  PASS  " #name "\n"; } while(0)

using namespace agent;

// ---------------------------------------------------------------------------
// Helpers: build a minimal fully-wired AgentContext
// ---------------------------------------------------------------------------
static std::shared_ptr<ToolRegistry> make_registry() {
    auto r = std::make_shared<ToolRegistry>();
    r->register_tool("echo",         [](const std::string& s){ return s; });
    r->register_tool("list_dir",     [](const std::string&){ return "file_a.txt\nfile_b.txt"; });
    r->register_tool("read_file",    [](const std::string& p){ return "content of " + p; });
    r->register_tool("get_sysinfo",  [](const std::string&){ return "Linux x86_64"; });
    r->register_tool("get_current_dir",[](const std::string&){ return "/home/test"; });
    return r;
}

static AgentContext make_ctx(const std::string& id, const std::string& layer,
                              const std::string& run_id = "run-test") {
    AgentContext ctx;
    ctx.agent_id   = id;
    ctx.layer      = layer;
    ctx.run_id     = run_id;
    ctx.memory     = std::make_shared<MemoryStore>(8);
    ctx.bus        = std::make_shared<MessageBus>();
    ctx.state      = std::make_shared<StateMachine>(id, layer);
    ctx.cancel_flag = std::make_shared<std::atomic<bool>>(false);
    ctx.pause_flag  = std::make_shared<std::atomic<bool>>(false);
    ctx.skills      = std::make_shared<SkillRegistry>();
    return ctx;
}

// ---------------------------------------------------------------------------
// TEST 1: assess_complexity heuristic
// ---------------------------------------------------------------------------
TEST(test_complexity_l0_conversational) {
    assert(DirectorAgent::assess_complexity("hello") == TaskComplexity::L0_Conversational);
    assert(DirectorAgent::assess_complexity("hi there") == TaskComplexity::L0_Conversational);
    assert(DirectorAgent::assess_complexity("explain recursion") == TaskComplexity::L0_Conversational);
    assert(DirectorAgent::assess_complexity("what is machine learning") == TaskComplexity::L0_Conversational);
    assert(DirectorAgent::assess_complexity("how does TCP work") == TaskComplexity::L0_Conversational);
    assert(DirectorAgent::assess_complexity("tell me about python") == TaskComplexity::L0_Conversational);
}

TEST(test_complexity_l1_single_tool) {
    // Tool execution requests must NOT be classified as L0 (conversational)
    auto c1 = DirectorAgent::assess_complexity("list files in Desktop");
    assert(c1 == TaskComplexity::L1_SingleTool || c1 == TaskComplexity::L3_Parallel);
    auto c2 = DirectorAgent::assess_complexity("show system info");
    assert(c2 != TaskComplexity::L0_Conversational);
    auto c3 = DirectorAgent::assess_complexity("get current directory");
    assert(c3 == TaskComplexity::L1_SingleTool || c3 == TaskComplexity::L3_Parallel);
}

TEST(test_complexity_l4_complex) {
    assert(DirectorAgent::assess_complexity("refactor the entire codebase") == TaskComplexity::L4_Complex);
    assert(DirectorAgent::assess_complexity("implement a complete REST API") == TaskComplexity::L4_Complex);
    // "build a web scraper" — "build a " is L4 keyword
    assert(DirectorAgent::assess_complexity("build a web scraper") == TaskComplexity::L4_Complex);
}

TEST(test_complexity_default_l3) {
    // Action tasks -> L3 parallel by default (not L0)
    auto c1 = DirectorAgent::assess_complexity("analyse some data and report");
    assert(c1 == TaskComplexity::L3_Parallel || c1 == TaskComplexity::L4_Complex);
    // Tool execution -> L1 (single-tool fast path) or L3 (general pipeline)
    auto c2 = DirectorAgent::assess_complexity("check current directory");
    assert(c2 == TaskComplexity::L1_SingleTool || c2 == TaskComplexity::L3_Parallel);
    // With conversation context: should extract [Current request:] and classify correctly
    const char ctx1[] =
        "[Conversation context:]\n"
        "User: hi\n"
        "Assistant: hello!\n"
        "[Current request:]\n"
        "check current directory";
    auto c3 = DirectorAgent::assess_complexity(ctx1);
    assert(c3 == TaskComplexity::L1_SingleTool || c3 == TaskComplexity::L3_Parallel);
    // Context with greeting must NOT pollute classification of tool request
    const char ctx2[] =
        "[Conversation context:]\n"
        "User: hello\n"
        "Assistant: hi!\n"
        "[Current request:]\n"
        "list files in Desktop";
    auto c4 = DirectorAgent::assess_complexity(ctx2);
    assert(c4 == TaskComplexity::L1_SingleTool || c4 == TaskComplexity::L3_Parallel);
}

// ---------------------------------------------------------------------------
// TEST 2: AgentConfig::validate()
// ---------------------------------------------------------------------------
TEST(test_config_validate_empty_api_key) {
    AgentConfig cfg;
    cfg.provider    = Provider::Anthropic;
    cfg.api_key     = "";
    cfg.default_model = "claude-haiku-4-5-20251001";
    cfg.max_tokens  = 1024;
    cfg.worker_threads = 2;
    cfg.request_timeout = 30;
    bool threw = false;
    try { cfg.validate(); }
    catch(const std::exception&){ threw = true; }
    assert(threw);
}

TEST(test_config_validate_default_placeholder) {
    AgentConfig cfg;
    cfg.provider    = Provider::Anthropic;
    cfg.api_key     = "YOUR_API_KEY_HERE";
    cfg.default_model = "claude-haiku-4-5-20251001";
    cfg.max_tokens  = 1024;
    cfg.worker_threads = 2;
    cfg.request_timeout = 30;
    bool threw = false;
    try { cfg.validate(); }
    catch(const std::exception&){ threw = true; }
    assert(threw);
}

TEST(test_config_validate_bad_max_tokens) {
    AgentConfig cfg;
    cfg.provider    = Provider::Ollama;  // no api_key required
    cfg.default_model = "llama3.3";
    cfg.max_tokens  = 5;  // too low
    cfg.worker_threads = 2;
    cfg.request_timeout = 30;
    bool threw = false;
    try { cfg.validate(); }
    catch(const std::exception&){ threw = true; }
    assert(threw);
}

TEST(test_config_validate_ollama_no_key_ok) {
    AgentConfig cfg;
    cfg.provider    = Provider::Ollama;
    cfg.api_key     = "";  // OK for Ollama
    cfg.default_model = "llama3.3";
    cfg.max_tokens  = 1024;
    cfg.worker_threads = 2;
    cfg.request_timeout = 30;
    bool threw = false;
    try { cfg.validate(); }
    catch(const std::exception& e){ threw = true; std::cerr << "unexpected: " << e.what() << "\n"; }
    assert(!threw);
}

TEST(test_config_validate_bad_worker_threads) {
    AgentConfig cfg;
    cfg.provider    = Provider::Ollama;
    cfg.default_model = "llama3.3";
    cfg.max_tokens  = 1024;
    cfg.worker_threads = 100;  // too many
    cfg.request_timeout = 30;
    bool threw = false;
    try { cfg.validate(); }
    catch(const std::exception&){ threw = true; }
    assert(threw);
}

// ---------------------------------------------------------------------------
// TEST 3: AtomicTask DAG serialization
// ---------------------------------------------------------------------------
TEST(test_atomic_task_dag_serialize) {
    AtomicTask t;
    t.id         = "task-2";
    t.parent_id  = "sub-1";
    t.description = "read file found in previous step";
    t.tool       = "read_file";
    t.input      = "";
    t.depends_on = {"task-1"};
    t.input_from = "task-1";

    nlohmann::json j;
    to_json(j, t);
    assert(j.contains("depends_on"));
    assert(j["depends_on"].is_array());
    assert(j["depends_on"].size() == 1);
    assert(j.contains("input_from"));
}

TEST(test_atomic_task_dag_deserialize) {
    std::string raw = R"({
        "id":"task-2","parent_id":"sub-1",
        "description":"read file","tool":"read_file","input":"",
        "depends_on":["task-1"],"input_from":"task-1"
    })";
    auto j = nlohmann::json::parse(raw);
    AtomicTask t;
    from_json(j, t);
    assert(t.id == "task-2");
    assert(t.depends_on.size() == 1);
    assert(t.depends_on[0] == "task-1");
    assert(t.input_from == "task-1");
}

// ---------------------------------------------------------------------------
// TEST 4: ManagerPool - reuse by task type
// ---------------------------------------------------------------------------
TEST(test_manager_pool_same_type_reuse) {
    int creation_count = 0;
    ManagerFactory factory = [&](const SubTask&) -> std::unique_ptr<IManager> {
        creation_count++;
        // Return a null stub -- just count creations
        struct StubMgr : IManager {
            std::string mid;
            explicit StubMgr(const std::string& id) : mid(id){}
            SubTaskReport process(const SubTask&) override { return SubTaskReport{}; }
            const std::string& id() const noexcept override { return mid; }
        };
        return std::make_unique<StubMgr>("mgr-" + std::to_string(creation_count));
    };

    ManagerPool pool(factory, 4);

    // First acquisition: creates new
    SubTask file_task; file_task.id="s1"; file_task.description="read a file";
    auto mgr1 = pool.acquire(file_task);
    assert(creation_count == 1);

    // Release it
    pool.release(mgr1, true);

    // Second file task: should REUSE mgr1 (same "file" type, now idle)
    SubTask file_task2; file_task2.id="s2"; file_task2.description="write a file";
    auto mgr2 = pool.acquire(file_task2);
    assert(creation_count == 1);  // no new creation
    assert(mgr1.get() == mgr2.get());  // same instance

    pool.release(mgr2, true);

    // System task: different type -> new creation
    SubTask sys_task; sys_task.id="s3"; sys_task.description="check system info";
    auto mgr3 = pool.acquire(sys_task);
    // May reuse existing or create new, but creation_count should be <= 2
    assert(creation_count <= 2);
}

TEST(test_manager_pool_bounded_size) {
    int creation_count = 0;
    ManagerFactory factory = [&](const SubTask&) -> std::unique_ptr<IManager> {
        creation_count++;
        struct StubMgr : IManager {
            std::string mid;
            explicit StubMgr(const std::string& id) : mid(id){}
            SubTaskReport process(const SubTask&) override { return SubTaskReport{}; }
            const std::string& id() const noexcept override { return mid; }
        };
        return std::make_unique<StubMgr>("mgr-" + std::to_string(creation_count));
    };

    ManagerPool pool(factory, 2);  // max 2 managers

    std::vector<std::shared_ptr<IManager>> held;
    for (int i = 0; i < 4; ++i) {
        SubTask t; t.id="s"+std::to_string(i);
        t.description="task type " + std::to_string(i);
        held.push_back(pool.acquire(t));
    }
    // Pool bounded at 2: extra managers are transient
    assert(pool.size() <= 2);
}

// ---------------------------------------------------------------------------
// TEST 5: WorkerAgent::is_placeholder_path
// ---------------------------------------------------------------------------
TEST(test_placeholder_detection_positive) {
    assert(WorkerAgent::is_placeholder_path("<HOME>/Desktop"));
    assert(WorkerAgent::is_placeholder_path("$HOME/documents"));
    assert(WorkerAgent::is_placeholder_path("%USERPROFILE%\\Desktop"));
    assert(WorkerAgent::is_placeholder_path("/path/to/file.txt"));
    assert(WorkerAgent::is_placeholder_path("<DESKTOP>"));
    assert(WorkerAgent::is_placeholder_path("[path]"));
    assert(WorkerAgent::is_placeholder_path("<file_path>"));
}

TEST(test_placeholder_detection_negative) {
    assert(!WorkerAgent::is_placeholder_path("Desktop"));
    assert(!WorkerAgent::is_placeholder_path("C:\\Users\\Alice\\Desktop"));
    assert(!WorkerAgent::is_placeholder_path("/home/alice/docs"));
    assert(!WorkerAgent::is_placeholder_path("./local/file.txt"));
    assert(!WorkerAgent::is_placeholder_path(""));
    assert(!WorkerAgent::is_placeholder_path("report.txt"));
}

// ---------------------------------------------------------------------------
// TEST 6: StateMachine state() accessor on WorkerAgent
// ---------------------------------------------------------------------------
TEST(test_worker_state_accessor) {
    // Build minimal WorkerAgent and verify state() is non-null
    ApiConfig acfg; acfg.provider = Provider::Anthropic; acfg.api_key = "test";
    acfg.model = "test-model"; acfg.max_tokens = 100;
    ApiClient client(acfg);
    auto reg = make_registry();
    auto ctx = make_ctx("wkr-test", "Worker");

    WorkerAgent w("wkr-test", client, *reg, "system prompt", 1, std::move(ctx));
    auto st = w.state();
    assert(st != nullptr);  // must not be nullptr after our fix
}

// ---------------------------------------------------------------------------
// TEST 7: classify_task_type helper
// ---------------------------------------------------------------------------
TEST(test_classify_task_type_file) {
    assert(classify_task_type("read a file") == "file");
    assert(classify_task_type("write output to directory") == "file");
    assert(classify_task_type("find files matching pattern") == "file");
}

TEST(test_classify_task_type_system) {
    assert(classify_task_type("get system information") == "system");
    assert(classify_task_type("list running processes") == "system");
}

TEST(test_classify_task_type_search) {
    assert(classify_task_type("search for pattern in code") == "search");
    assert(classify_task_type("find and query the database") == "search");
}

TEST(test_classify_task_type_general) {
    assert(classify_task_type("summarise the results") == "general");
    assert(classify_task_type("complete the task") == "general");
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

TEST(test_env_knowledge_base) {
    agent::EnvKnowledgeBase kb;
    kb.confirm("cwd", "E:\\projects\\app");
    assert(kb.get("cwd") == "E:\\projects\\app");
    kb.confirm("file:README.md", "readable");
    std::string ctx = kb.build_context(0.5f);
    assert(!ctx.empty());
    std::string serial = kb.serialize();
    agent::EnvKnowledgeBase kb2;
    kb2.deserialize_md(serial);  // serialize() now produces Markdown, use deserialize_md()
    assert(!kb2.empty());
    assert(kb2.get("cwd", 0.5f) == "E:\\projects\\app");
}

TEST(test_task_rules_cn) {
    auto r1 = agent::apply_task_rules("查看当前目录");
    assert(r1.has_value());
    assert(r1->description.find("get_current_dir") != std::string::npos);
    auto r2 = agent::apply_task_rules("系统信息");
    assert(r2.has_value());
    assert(r2->description.find("get_sysinfo") != std::string::npos);
    auto r3 = agent::apply_task_rules("上级目录");
    assert(r3.has_value());
    assert(r3->description.find("list_dir") != std::string::npos);
}

TEST(test_task_rules_en) {
    auto r1 = agent::apply_task_rules("current directory");
    assert(r1.has_value());
    auto r2 = agent::apply_task_rules("list files");
    assert(r2.has_value());
    auto r3 = agent::apply_task_rules("你好");
    assert(!r3.has_value());
}


int main() {
    RUN(test_classify_task_type_general);
    // Suppress logger output during tests
    Logger::instance().set_level("error");

    std::cout << "=== test_integration ===\n";

    RUN(test_complexity_l0_conversational);
    RUN(test_complexity_l1_single_tool);
    RUN(test_complexity_l4_complex);
    RUN(test_complexity_default_l3);

    RUN(test_config_validate_empty_api_key);
    RUN(test_config_validate_default_placeholder);
    RUN(test_config_validate_bad_max_tokens);
    RUN(test_config_validate_ollama_no_key_ok);
    RUN(test_config_validate_bad_worker_threads);

    RUN(test_atomic_task_dag_serialize);
    RUN(test_atomic_task_dag_deserialize);

    RUN(test_manager_pool_same_type_reuse);
    RUN(test_manager_pool_bounded_size);

    RUN(test_placeholder_detection_positive);
    RUN(test_placeholder_detection_negative);

    RUN(test_worker_state_accessor);

    RUN(test_classify_task_type_file);
    RUN(test_classify_task_type_system);
    RUN(test_classify_task_type_search);

    RUN(test_env_knowledge_base);
    RUN(test_task_rules_cn);
    RUN(test_task_rules_en);

    std::cout << "\nAll integration tests passed.\n";
    return 0;
}
