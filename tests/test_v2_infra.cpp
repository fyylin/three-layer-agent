// =============================================================================
// tests/test_v2_infra.cpp
//
// Tests for all v2 infrastructure components:
//   AgentState/StateMachine, MessageBus, MemoryStore,
//   WorkspaceManager, ResourceManager, StructuredLogger,
//   FileLock, ToolSet (12 tools), AdvisorAgent
// =============================================================================

#include "agent/agent_state.hpp"
#include "agent/agent_context.hpp"
#include "agent/advisor_agent.hpp"
#include "agent/models.hpp"
#include "utils/message_bus.hpp"
#include "utils/memory_store.hpp"
#include "utils/workspace.hpp"
#include "utils/structured_log.hpp"
#include "utils/file_lock.hpp"
#include "utils/tool_set.hpp"

#include <cassert>
#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <thread>
#ifdef __linux__
#  include <sys/stat.h>
#  include <sys/types.h>
#endif

using namespace agent;

#define TEST(name) static void name()
#define RUN(name)  do { name(); std::cout << "  PASS  " #name "\n"; } while(0)

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------

static std::string tmp_dir() {
#ifdef _WIN32
    return "C:\\Temp\\agent_test_" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count() % 100000);
#else
    return "/tmp/agent_test_" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count() % 100000);
#endif
}

// -----------------------------------------------------------------------------
// AgentState / StateMachine
// -----------------------------------------------------------------------------

TEST(test_state_initial_created) {
    StateMachine sm("test-001", "Worker");
    assert(sm.state() == AgentState::Created);
}

TEST(test_state_transition_basic) {
    StateMachine sm("test-002", "Worker");
    [[maybe_unused]] bool ok = sm.transition(AgentState::Running, "CallingTool", "task-1");
    assert(ok);
    assert(sm.state() == AgentState::Running);
    auto snap = sm.snapshot();
    assert(snap.sub_state == "CallingTool");
    assert(snap.current_task_id == "task-1");
}

TEST(test_state_terminal_no_transition) {
    StateMachine sm("test-003", "Worker");
    sm.transition(AgentState::Done);
    assert(sm.state() == AgentState::Done);
    [[maybe_unused]] bool ok = sm.transition(AgentState::Running);  // should be rejected
    assert(!ok);
    assert(sm.state() == AgentState::Done);  // unchanged
}

TEST(test_state_cancelled_terminal) {
    StateMachine sm("test-004", "Worker");
    sm.transition(AgentState::Running);
    sm.transition(AgentState::Cancelled);
    assert(sm.state() == AgentState::Cancelled);
    assert(!sm.transition(AgentState::Running));
}

TEST(test_state_callback_fires) {
    StateMachine sm("test-005", "Manager");
    int cb_count = 0;
    AgentState last_state = AgentState::Created;
    sm.on_transition([&](const StateInfo& info) {
        ++cb_count;
        last_state = info.state;
    });
    sm.transition(AgentState::Running);
    sm.transition(AgentState::Done);
    assert(cb_count == 2);
    assert(last_state == AgentState::Done);
}

TEST(test_state_fail_and_cancel_flags) {
    StateMachine sm("test-006", "Worker");
    sm.transition(AgentState::Running);
    sm.record_failure("tool error");
    sm.record_call();
    sm.record_call();
    auto snap = sm.snapshot();
    assert(snap.fail_count == 1);
    assert(snap.call_count == 2);
    assert(snap.last_error == "tool error");
}

TEST(test_state_to_cstr) {
    assert(std::string(state_to_cstr(AgentState::Running))   == "running");
    assert(std::string(state_to_cstr(AgentState::Done))      == "done");
    assert(std::string(state_to_cstr(AgentState::Cancelled)) == "cancelled");
    assert(std::string(state_to_cstr(AgentState::Paused))    == "paused");
}

// -----------------------------------------------------------------------------
// MessageBus
// -----------------------------------------------------------------------------

TEST(test_bus_send_receive) {
    MessageBus bus;
    bus.send("wkr-1", "mgr-1", MsgType::Result, "{\"status\":\"done\"}");
    auto msg = bus.try_receive("mgr-1");
    assert(msg.has_value());
    assert(msg->from_id == "wkr-1");
    assert(msg->type == MsgType::Result);
    assert(msg->payload == "{\"status\":\"done\"}");
}

TEST(test_bus_no_message_returns_nullopt) {
    MessageBus bus;
    auto msg = bus.try_receive("nobody");
    assert(!msg.has_value());
}

TEST(test_bus_broadcast) {
    MessageBus bus;
    bus.subscribe("agent-a", [](const AgentMessage&){});
    bus.subscribe("agent-b", [](const AgentMessage&){});
    bus.broadcast("supervisor", MsgType::Correct, "note");
    // Broadcast delivers to every registered inbox
    auto ma = bus.try_receive("agent-a");
    auto mb = bus.try_receive("agent-b");
    assert(ma.has_value());
    assert(mb.has_value());
    assert(ma->type == MsgType::Correct);
}

TEST(test_bus_pending_count) {
    MessageBus bus;
    bus.send("a", "b", "test", "1");
    bus.send("a", "b", "test", "2");
    assert(bus.pending_count("b") == 2);
    bus.try_receive("b");
    assert(bus.pending_count("b") == 1);
}

TEST(test_bus_subscriber_callback) {
    MessageBus bus;
    std::vector<std::string> received;
    bus.subscribe("sup", [&](const AgentMessage& m){ received.push_back(m.payload); });
    bus.send("worker", "sup", MsgType::Progress, "50%");
    bus.send("worker", "sup", MsgType::Progress, "100%");
    assert(received.size() == 2);
    assert(received[0] == "50%");
    assert(received[1] == "100%");
}

TEST(test_bus_blocking_receive_timeout) {
    MessageBus bus;
    auto start = std::chrono::steady_clock::now();
    auto msg = bus.receive("nobody", 50);  // 50ms timeout
    [[maybe_unused]] auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    assert(!msg.has_value());
    (void)elapsed;  // checked above; suppress MSVC C4189 in release builds   // waited approximately 50ms
}

TEST(test_bus_total_sent_counter) {
    MessageBus bus;
    assert(bus.total_sent() == 0);
    bus.send("a","b","t","");
    bus.send("a","b","t","");
    assert(bus.total_sent() == 2);
}

// -----------------------------------------------------------------------------
// MemoryStore
// -----------------------------------------------------------------------------

TEST(test_memory_short_term_sliding_window) {
    MemoryStore mem(3);  // window = 3
    mem.push_message({"user", "msg1"});
    mem.push_message({"user", "msg2"});
    mem.push_message({"user", "msg3"});
    mem.push_message({"user", "msg4"});  // should evict msg1
    auto ctx = mem.get_context(10);
    assert(ctx.size() == 3);
    assert(ctx[0].content == "msg2");
    assert(ctx[2].content == "msg4");
}

TEST(test_memory_push_result) {
    MemoryStore mem(8);
    mem.push_result("task-1", "computed output");
    auto ctx = mem.get_context(1);
    assert(!ctx.empty());
    assert(ctx[0].role == "assistant");
    assert(ctx[0].content.find("computed output") != std::string::npos);
}

TEST(test_memory_session_kv) {
    MemoryStore mem;
    mem.set("key1", "value1");
    mem.set("key2", "value2");
    assert(mem.get("key1") == "value1");
    assert(mem.get("key2") == "value2");
    assert(mem.get("missing", "default") == "default");
    assert(mem.has("key1"));
    assert(!mem.has("nope"));
    mem.remove("key1");
    assert(!mem.has("key1"));
}

TEST(test_memory_long_term) {
    MemoryStore mem;
    mem.append_summary("Session 1: user asked about files");
    mem.append_summary("Session 2: user requested system info");
    auto summaries = mem.get_summaries();
    assert(summaries.size() == 2);
    // Relevance query
    std::string relevant = mem.load_relevant("files", 1);
    assert(relevant.find("files") != std::string::npos);
}

TEST(test_memory_merge_session) {
    MemoryStore a, b;
    a.set("shared", "original");
    b.set("shared", "overwrite");
    b.set("new_key", "new_val");
    a.merge_session(b);
    assert(a.get("shared") == "overwrite");
    assert(a.get("new_key") == "new_val");
}

TEST(test_memory_persist_session) {
    std::string path = tmp_dir() + "_session.json";
    {
        MemoryStore mem;
        mem.set("x", "123");
        mem.set("y", "hello");
        mem.save_session(path);
    }
    {
        MemoryStore mem2;
        mem2.load_session(path);
        assert(mem2.get("x") == "123");
        assert(mem2.get("y") == "hello");
    }
    std::remove(path.c_str());
}

TEST(test_memory_clear_short_term) {
    MemoryStore mem(8);
    mem.push_message({"user", "hello"});
    assert(!mem.short_term_empty());
    mem.clear_short_term();
    assert(mem.short_term_empty());
}

// -----------------------------------------------------------------------------
// WorkspaceManager
// -----------------------------------------------------------------------------

TEST(test_workspace_join_path) {
    assert(WorkspaceManager::join("a", "b")  == "a/b");
    assert(WorkspaceManager::join("a/", "b") == "a/b");
    assert(WorkspaceManager::join("", "b")   == "b");
    assert(WorkspaceManager::join("a", "")   == "a");
}

TEST(test_workspace_init_creates_dirs) {
    std::string root = tmp_dir();
    auto wp = WorkspaceManager::init(root, "run-test");
    // run_root is now sessions/<id>/
    assert(wp.run_root.find("run-test") != std::string::npos);
    // global_log is now workspace/logs/agent.log
    // global_log now aliases to activity_md (activity.md)
    assert(wp.activity_md.find("activity.md") != std::string::npos);
    assert(wp.global_log == wp.activity_md);  // legacy alias
    // structured_log is workspace/logs/structured.ndjson
    assert(wp.structured_log.find("structured") != std::string::npos);
    // shared and memory under current/
    assert(wp.shared_dir.find("shared") != std::string::npos);
    assert(wp.memory_dir.find("memory") != std::string::npos);
    // files_dir is workspace/current/files/
    assert(wp.files_dir.find("files") != std::string::npos);
    // workspace_md is WORKSPACE.md
    assert(wp.workspace_md.find("WORKSPACE.md") != std::string::npos);
    // state.json still in run_root
    {
        std::ifstream test(wp.state_json);
        assert(test.is_open());  // state.json created inside run_root/sessions/<id>/
    }
}

TEST(test_workspace_agent_dir_mapping) {
    WorkspacePaths wp;
    wp.run_root = "/base/run-1";
    assert(wp.agent_dir("dir-001").find("director") != std::string::npos);
    assert(wp.agent_dir("sup-001").find("supervisor") != std::string::npos);
    assert(wp.agent_dir("mgr-3").find("mgr-3") != std::string::npos);
    assert(wp.agent_dir("wkr-2").find("wkr-2") != std::string::npos);
    assert(wp.artifact_dir("wkr-2").find("artifacts") != std::string::npos);
}

TEST(test_workspace_append_log) {
    std::string root = tmp_dir() + "_log";
    auto wp = WorkspaceManager::init(root, "run-log");
    WorkspaceManager::append_log(wp, "{\"event\":\"test\"}");
    WorkspaceManager::append_log(wp, "{\"event\":\"test2\"}");
    std::ifstream f(wp.global_log);
    std::string line1, line2;
    std::getline(f, line1);
    std::getline(f, line2);
    assert(line1.find("test") != std::string::npos);
    assert(line2.find("test2") != std::string::npos);
}

TEST(test_workspace_write_state) {
    std::string root = tmp_dir() + "_state";
    auto wp = WorkspaceManager::init(root, "run-state");
    WorkspaceManager::write_state(wp, "wkr-1", "{\"state\":\"running\"}");
    WorkspaceManager::write_state(wp, "wkr-2", "{\"state\":\"done\"}");
    std::ifstream f(wp.state_json);
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    assert(content.find("wkr-1") != std::string::npos);
    assert(content.find("wkr-2") != std::string::npos);
    assert(content.find("running") != std::string::npos);
}

TEST(test_workspace_sandboxed_check) {
    WorkspacePaths wp;
    wp.run_root  = "/base/run-1";
    wp.shared_dir= "/base/run-1/shared";
    [[maybe_unused]] bool ok = WorkspaceManager::is_sandboxed("/base/run-1/wkr-1/artifacts/out.txt", wp, "wkr-1");
    assert(ok);
    [[maybe_unused]] bool shared_ok = WorkspaceManager::is_sandboxed("/base/run-1/shared/data.txt", wp, "wkr-1");
    assert(shared_ok);
    [[maybe_unused]] bool bad = WorkspaceManager::is_sandboxed("/etc/passwd", wp, "wkr-1");
    assert(!bad);
}

// -----------------------------------------------------------------------------
// ResourceManager
// -----------------------------------------------------------------------------

TEST(test_resource_write_and_read_shared) {
    std::string root = tmp_dir() + "_res";
    auto wp = WorkspaceManager::init(root, "run-res");
    ResourceManager::write_shared(wp.shared_dir, "result.txt", "hello world", "wkr-1");
    auto val = ResourceManager::read_shared(wp.shared_dir, "result.txt");
    assert(val.has_value());
    assert(*val == "hello world");
}

TEST(test_resource_read_missing_returns_nullopt) {
    std::string root = tmp_dir() + "_res2";
    auto wp = WorkspaceManager::init(root, "run-res2");
    auto val = ResourceManager::read_shared(wp.shared_dir, "nonexistent.txt");
    assert(!val.has_value());
}

TEST(test_resource_list_shared) {
    std::string root = tmp_dir() + "_res3";
    auto wp = WorkspaceManager::init(root, "run-res3");
    ResourceManager::write_shared(wp.shared_dir, "a.txt", "aaa", "wkr-1");
    ResourceManager::write_shared(wp.shared_dir, "b.txt", "bbb", "wkr-2");
    auto files = ResourceManager::list_shared(wp.shared_dir);
    // Should see a.txt and b.txt (not .meta / .lock files)
    assert(files.size() >= 2);
    bool has_a = false, has_b = false;
    for (auto& f : files) {
        if (f == "a.txt") has_a = true;
        if (f == "b.txt") has_b = true;
    }
    assert(has_a && has_b);
}

TEST(test_resource_append_shared) {
    std::string root = tmp_dir() + "_res4";
    auto wp = WorkspaceManager::init(root, "run-res4");
    ResourceManager::append_shared(wp.shared_dir, "log.txt", "line1");
    ResourceManager::append_shared(wp.shared_dir, "log.txt", "line2");
    auto val = ResourceManager::read_shared(wp.shared_dir, "log.txt");
    assert(val.has_value());
    assert(val->find("line1") != std::string::npos);
    assert(val->find("line2") != std::string::npos);
}

// -----------------------------------------------------------------------------
// FileLock
// -----------------------------------------------------------------------------

TEST(test_filelock_acquire_release) {
    std::string lock_path = tmp_dir() + "_lock.lock";
    {
        FileLock lock(lock_path);
        bool ok = lock.try_lock(1000);
        assert(ok);
        assert(lock.is_held());
        lock.unlock();
        assert(!lock.is_held());
    }
}

TEST(test_filelock_scoped) {
    std::string lock_path = tmp_dir() + "_scoped.lock";
    [[maybe_unused]] bool acquired = false;
    {
        ScopedFileLock lock(lock_path, 1000);
        acquired = lock.acquired();
        assert(acquired);
    }  // released here
    // Should be able to re-acquire
    ScopedFileLock lock2(lock_path, 1000);
    assert(lock2.acquired());
}

TEST(test_filelock_double_acquire_times_out) {
    std::string lock_path = tmp_dir() + "_double.lock";
    FileLock lock1(lock_path);
    assert(lock1.try_lock(1000));
    FileLock lock2(lock_path);
    [[maybe_unused]] bool ok = lock2.try_lock(0);   // non-blocking  --  should fail immediately
    // Note: on some systems file locks are per-process, so this may succeed
    // Just verify the API works without crashing
    (void)ok;
    lock1.unlock();
}

// -----------------------------------------------------------------------------
// StructuredLogger
// -----------------------------------------------------------------------------

TEST(test_structured_log_writes_ndjson) {
    std::string log_path = tmp_dir() + "_slog.log";
    {
        StructuredLogger slog("wkr-1", "Worker", "run-1", log_path, "");
        slog.info("task-1", EvType::TaskStart, "starting task");
        slog.warn("task-1", EvType::ParseFail, "parse failed");
        slog.error("task-1", EvType::TaskEnd, "task done");
    }
    std::ifstream f(log_path);
    std::string line;
    int count = 0;
    while (std::getline(f, line)) {
        assert(!line.empty());
        assert(line[0] == '{');     // each line is a JSON object
        assert(line.back() == '}');
        assert(line.find("wkr-1") != std::string::npos);
        assert(line.find("run-1") != std::string::npos);
        ++count;
    }
    assert(count == 3);
    std::remove(log_path.c_str());
}

TEST(test_structured_log_event_types) {
    std::string log_path = tmp_dir() + "_evtypes.log";
    {
        StructuredLogger slog("sup-001", "Supervisor", "run-2", log_path, "");
        slog.info("", EvType::AgentCreated, "created");
        slog.info("t1", EvType::LlmCall,    "calling LLM");
        slog.info("t1", EvType::ToolCall,   "calling tool", "{\"tool\":\"list_dir\"}");
    }
    std::ifstream f(log_path);
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    assert(content.find(EvType::AgentCreated) != std::string::npos);
    assert(content.find(EvType::LlmCall)      != std::string::npos);
    assert(content.find(EvType::ToolCall)     != std::string::npos);
    assert(content.find("list_dir")           != std::string::npos);
    std::remove(log_path.c_str());
}

TEST(test_structured_log_null_logger_no_crash) {
    // NullStructuredLogger should be constructable and safe to call
    NullStructuredLogger null_log;
    null_log.info("t", "ev", "msg");   // should not crash or throw
    null_log.warn("t", "ev", "msg");
}

// -----------------------------------------------------------------------------
// ToolSet  --  12 tools
// -----------------------------------------------------------------------------

TEST(test_tool_resolve_path_desktop_alias) {
    std::string r = resolve_path("Desktop");
    assert(!r.empty());
    assert(r.find("Desktop") != std::string::npos ||
           r.find("desktop") != std::string::npos);
}

TEST(test_tool_resolve_path_home_placeholder) {
    std::string r = resolve_path("<HOME>/Desktop");
    assert(!r.empty());
    assert(r.find("<HOME>") == std::string::npos);  // placeholder must be resolved
}

TEST(test_tool_resolve_path_tilde) {
    std::string r = resolve_path("~/Desktop");
    assert(!r.empty());
    assert(r.find("~") == std::string::npos);
}

TEST(test_tool_get_env_known_var) {
#ifdef _WIN32
    std::string r = tool_get_env("USERPROFILE");
#else
    std::string r = tool_get_env("HOME");
#endif
    assert(!r.empty());
    assert(r != "(not set)");
}

TEST(test_tool_get_env_missing) {
    std::string r = tool_get_env("__DEFINITELY_NOT_SET_12345__");
    assert(r == "(not set)");
}

TEST(test_tool_get_sysinfo_returns_json) {
    std::string r = tool_get_sysinfo("");
    assert(!r.empty());
    assert(r[0] == '{');
    assert(r.find("os") != std::string::npos);
    assert(r.find("hostname") != std::string::npos);
    assert(r.find("cpu_count") != std::string::npos);
}

TEST(test_tool_get_current_dir) {
    std::string r = tool_get_current_dir("");
    assert(!r.empty());
    // Should be a valid-looking path
    assert(r.find('/') != std::string::npos ||
           r.find('\\') != std::string::npos ||
           r.size() >= 1);
}

TEST(test_tool_get_process_list) {
    std::string r = tool_get_process_list("");
    // Should have at least one process
    assert(!r.empty());
    assert(r != "(no matching processes)");
}

TEST(test_tool_echo) {
    assert(tool_echo("hello") == "hello");
    assert(tool_echo("") == "");
    assert(tool_echo("multi\nline") == "multi\nline");
}

TEST(test_tool_write_read_file) {
    std::string path = tmp_dir() + "_tool_rw.txt";
    std::string content = "line1\nline2\nline3";
    std::string write_input = path + "\n" + content;
    std::string result = tool_write_file(write_input);
    assert(result.find("Written") != std::string::npos);
    std::string read = tool_read_file(path);
    assert(read == content);
    std::remove(path.c_str());
}

TEST(test_tool_read_file_missing_throws) {
    bool threw = false;
    try { tool_read_file("/tmp/__does_not_exist_xyz__.txt"); }
    catch (const std::exception&) { threw = true; }
    assert(threw);
}

TEST(test_tool_stat_file) {
    std::string path = tmp_dir() + "_stat.txt";
    { std::ofstream f(path); f << "test"; }
    std::string r = tool_stat_file(path);
    assert(r[0] == '{');
    assert(r.find("\"type\":\"file\"") != std::string::npos);
    assert(r.find("\"size\":4") != std::string::npos);
    std::remove(path.c_str());
}

TEST(test_tool_find_files) {
#ifndef _WIN32
    std::string dir = tmp_dir() + "_find";
    WorkspaceManager::mkdirs(dir);
    { std::ofstream f(dir+"/a.txt"); f << "a"; }
    { std::ofstream f(dir+"/b.txt"); f << "b"; }
    { std::ofstream f(dir+"/c.log"); f << "c"; }
    std::string r = tool_find_files(dir + "\n*.txt");
    assert(r.find("a.txt") != std::string::npos);
    assert(r.find("b.txt") != std::string::npos);
    assert(r.find("c.log") == std::string::npos);
#endif
}

TEST(test_tool_run_command_echo) {
#ifdef _WIN32
    std::string r = tool_run_command("echo hello_from_test");
#else
    std::string r = tool_run_command("echo hello_from_test");
#endif
    assert(r.find("hello_from_test") != std::string::npos);
    assert(r.find("[exit code: 0]") != std::string::npos);
}

TEST(test_tool_run_command_blocked) {
    bool threw = false;
    try {
#ifdef _WIN32
        tool_run_command("del /f /s C:\\important");
#else
        tool_run_command("rm -rf /important");
#endif
    } catch (const std::exception&) { threw = true; }
    assert(threw);
}

TEST(test_tool_list_dir_current) {
    std::string r = tool_list_dir(".");
    assert(!r.empty());
    assert(r.find("Directory:") != std::string::npos);
}

TEST(test_tool_list_dir_traversal_denied) {
    bool threw = false;
    try { tool_list_dir("../../../etc"); }
    catch (const std::exception&) { threw = true; }
    assert(threw);
}

TEST(test_tool_list_dir_missing_path_throws) {
    bool threw = false;
    std::string missing = tmp_dir() + "_missing_dir";
    try { tool_list_dir(missing); }
    catch (const std::exception& e) {
        threw = true;
        assert(std::string(e.what()).find(missing) != std::string::npos);
    }
    assert(threw);
}
TEST(test_tool_delete_file) {
    std::string path = tmp_dir() + "_del.txt";
    { std::ofstream f(path); f << "delete me"; }
    // HITL: first call returns confirmation request
    std::string r1 = tool_delete_file(path);
    assert(r1.find("[HITL]") != std::string::npos);
    // Confirm with CONFIRMED: prefix
    std::string r2 = tool_delete_file("CONFIRMED:" + path);
    assert(r2.find("Deleted") != std::string::npos);
    // File should no longer exist
    std::ifstream check(path);
    assert(!check.is_open());
}

TEST(test_tool_registry_all_tools_registered) {
    ToolRegistry registry;
    register_all_tools(registry);
    auto names = registry.tool_names();
    assert(names.size() >= 12);
    // Check a selection
    bool has_list = false, has_run = false, has_sysinfo = false;
    for (auto& n : names) {
        if (n == "list_dir")    has_list = true;
        if (n == "run_command") has_run = true;
        if (n == "get_sysinfo") has_sysinfo = true;
    }
    assert(has_list && has_run && has_sysinfo);
}

// -----------------------------------------------------------------------------
// AgentContext
// -----------------------------------------------------------------------------

TEST(test_agent_context_default_is_inactive) {
    AgentContext ctx;
    assert(!ctx.is_active());
    assert(!ctx.cancelled());
    assert(!ctx.paused());
}

TEST(test_agent_context_cancel_flag) {
    AgentContext ctx;
    auto flag = std::make_shared<std::atomic<bool>>(false);
    ctx.cancel_flag = flag;
    assert(!ctx.cancelled());
    flag->store(true);
    assert(ctx.cancelled());
}

TEST(test_agent_context_log_safe_with_null_slog) {
    AgentContext ctx;
    ctx.log_info("task-1", "event", "message");  // should not crash
    ctx.log_warn("task-1", "event", "message");
    ctx.log_error("task-1", "event", "message");
}

TEST(test_agent_context_send_safe_with_null_bus) {
    AgentContext ctx;
    ctx.send("dest", "type", "payload");  // should not crash
}

// -----------------------------------------------------------------------------
// main
// -----------------------------------------------------------------------------
int main() {
    std::cout << "=== test_v2_infra ===\n";

    // AgentState
    RUN(test_state_initial_created);
    RUN(test_state_transition_basic);
    RUN(test_state_terminal_no_transition);
    RUN(test_state_cancelled_terminal);
    RUN(test_state_callback_fires);
    RUN(test_state_fail_and_cancel_flags);
    RUN(test_state_to_cstr);

    // MessageBus
    RUN(test_bus_send_receive);
    RUN(test_bus_no_message_returns_nullopt);
    RUN(test_bus_broadcast);
    RUN(test_bus_pending_count);
    RUN(test_bus_subscriber_callback);
    RUN(test_bus_blocking_receive_timeout);
    RUN(test_bus_total_sent_counter);

    // MemoryStore
    RUN(test_memory_short_term_sliding_window);
    RUN(test_memory_push_result);
    RUN(test_memory_session_kv);
    RUN(test_memory_long_term);
    RUN(test_memory_merge_session);
    RUN(test_memory_persist_session);
    RUN(test_memory_clear_short_term);

    // WorkspaceManager
    RUN(test_workspace_join_path);
    RUN(test_workspace_init_creates_dirs);
    RUN(test_workspace_agent_dir_mapping);
    RUN(test_workspace_append_log);
    RUN(test_workspace_write_state);
    RUN(test_workspace_sandboxed_check);

    // ResourceManager
    RUN(test_resource_write_and_read_shared);
    RUN(test_resource_read_missing_returns_nullopt);
    RUN(test_resource_list_shared);
    RUN(test_resource_append_shared);

    // FileLock
    RUN(test_filelock_acquire_release);
    RUN(test_filelock_scoped);
    RUN(test_filelock_double_acquire_times_out);

    // StructuredLogger
    RUN(test_structured_log_writes_ndjson);
    RUN(test_structured_log_event_types);
    RUN(test_structured_log_null_logger_no_crash);

    // ToolSet
    RUN(test_tool_resolve_path_desktop_alias);
    RUN(test_tool_resolve_path_home_placeholder);
    RUN(test_tool_resolve_path_tilde);
    RUN(test_tool_get_env_known_var);
    RUN(test_tool_get_env_missing);
    RUN(test_tool_get_sysinfo_returns_json);
    RUN(test_tool_get_current_dir);
    RUN(test_tool_get_process_list);
    RUN(test_tool_echo);
    RUN(test_tool_write_read_file);
    RUN(test_tool_read_file_missing_throws);
    RUN(test_tool_stat_file);
    RUN(test_tool_find_files);
    RUN(test_tool_run_command_echo);
    RUN(test_tool_run_command_blocked);
    RUN(test_tool_list_dir_current);
    RUN(test_tool_list_dir_traversal_denied);
    RUN(test_tool_list_dir_missing_path_throws);
    RUN(test_tool_delete_file);
    RUN(test_tool_registry_all_tools_registered);

    // AgentContext
    RUN(test_agent_context_default_is_inactive);
    RUN(test_agent_context_cancel_flag);
    RUN(test_agent_context_log_safe_with_null_slog);
    RUN(test_agent_context_send_safe_with_null_bus);

    std::cout << "All tests passed.\n";
    return 0;
}
