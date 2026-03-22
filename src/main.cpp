// =============================================================================
// src/main.cpp   --   v2: fully wired with workspace, memory, bus, state machine
// =============================================================================

#include "agent/models.hpp"
#include "agent/exceptions.hpp"
#include "agent/api_client.hpp"
#include "agent/tool_registry.hpp"
#include "agent/worker_agent.hpp"
#include "agent/manager_agent.hpp"
#include "agent/director_agent.hpp"
#include "agent/supervisor_agent.hpp"
#include "agent/advisor_agent.hpp"
#include "agent/agent_context.hpp"
#include "utils/logger.hpp"
#include "utils/thread_pool.hpp"
#include "utils/json_utils.hpp"
#include "utils/workspace.hpp"
#include "utils/prompt_loader.hpp"
#include "cli/commands.hpp"
#include "cli/command_system.hpp"
#include "cli/readline_helper.hpp"
#include "utils/experience_manager.hpp"
#include "utils/health_server.hpp"
#include "utils/message_bus.hpp"
#include "utils/memory_store.hpp"
#include "utils/structured_log.hpp"
#include "utils/tool_set.hpp"
#include "agent/skill_registry.hpp"
#include "utils/utf8_fstream.hpp"
#include "setup.hpp"

#include <nlohmann/json.hpp>
#include <atomic>
#include <csignal>
#include <signal.h>  // for sigaction (POSIX)
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <sstream>
#include <iomanip>
#include <string>
#include <vector>

#ifdef _WIN32
#  include <io.h>
#  include <windows.h>
#  include <sys/stat.h>
#  define IS_STDIN_TTY() (_isatty(_fileno(stdin)))
#else
#  include <unistd.h>
#  include <dirent.h>
#  include <sys/stat.h>
#  define IS_STDIN_TTY() (isatty(STDIN_FILENO))
#endif

// Tools: see src/utils/tool_set.cpp

// -----------------------------------------------------------------------------
// List models help
// -----------------------------------------------------------------------------
static void list_models() {
    std::cout <<
        "\nKnown models by provider\n========================\n\n"
        "Anthropic:  claude-opus-4-5-20251101  claude-sonnet-4-5-20251015  claude-haiku-4-5-20251001\n"
        "OpenAI:     gpt-4o  gpt-4o-mini  gpt-4-turbo  o1  o1-mini\n"
        "Ollama:     llama3.3  llama3.2  mistral  gemma2  qwen2.5  deepseek-r1\n"
        "Azure/Custom: use your deployment name\n\n"
        "Built-in tools (12):\n"
        "  Filesystem: list_dir  stat_file  find_files  read_file  write_file  delete_file\n"
        "  System API: get_env  get_sysinfo  get_process_list  get_current_dir\n"
        "  Shell:      run_command (sandboxed)\n"
        "  Utility:    echo\n";
}

static agent::ApiConfig make_layer_config(const agent::AgentConfig& cfg,
                                           const agent::ModelSpec& spec) {
    return agent::make_api_config(cfg, spec);
}

static std::filesystem::path executable_dir() {
#ifdef _WIN32
    wchar_t buf[MAX_PATH] = {};
    DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (len > 0)
        return std::filesystem::path(buf).parent_path();
#else
    std::error_code ec;
    auto p = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (!ec) return p.parent_path();
#endif
    return std::filesystem::current_path();
}

static std::string path_to_utf8(const std::filesystem::path& p) {
#ifdef _WIN32
    std::wstring ws = p.wstring();
    if (ws.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return "";
    std::string out(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, &out[0], len, nullptr, nullptr);
    return out;
#else
    return p.string();
#endif
}

static std::string resolve_runtime_path(const std::string& raw) {
    if (raw.empty()) return raw;

    std::filesystem::path p(raw);
    if (p.is_absolute() && std::filesystem::exists(p))
        return path_to_utf8(p);

    if (std::filesystem::exists(p))
        return path_to_utf8(std::filesystem::absolute(p));

    const auto exe_dir = executable_dir();
    const std::filesystem::path candidates[] = {
        exe_dir / p,
        exe_dir.parent_path() / p,
        exe_dir.parent_path().parent_path() / p
    };
    for (const auto& candidate : candidates) {
        if (std::filesystem::exists(candidate))
            return path_to_utf8(std::filesystem::weakly_canonical(candidate));
    }

    return raw;
}

// -----------------------------------------------------------------------------
// UTF-8 console output (Windows WriteConsoleW / POSIX stdout)
// -----------------------------------------------------------------------------
static void print_utf8(const std::string& s) {
    if (s.empty()) return;
#ifdef _WIN32
    HANDLE h=GetStdHandle(STD_OUTPUT_HANDLE); DWORD mode=0;
    if (h!=INVALID_HANDLE_VALUE&&GetConsoleMode(h,&mode)) {
        int wn=MultiByteToWideChar(CP_UTF8,0,s.c_str(),(int)s.size(),nullptr,0);
        if (wn>0) {
            std::wstring ws(wn,L'\0');
            MultiByteToWideChar(CP_UTF8,0,s.c_str(),(int)s.size(),&ws[0],wn);
            DWORD wr=0;
            if (WriteConsoleW(h,ws.c_str(),(DWORD)ws.size(),&wr,nullptr)) return;
        }
    }
    // Fallback: direct write to stdout (handles pipe/redirect)
    fputs(s.c_str(), stdout);
    fflush(stdout);
    return;
#endif
    std::cout<<s; std::cout.flush();
}

static std::string read_goal_line() {
#ifdef _WIN32
    HANDLE hIn=GetStdHandle(STD_INPUT_HANDLE); DWORD mode=0;
    if (GetConsoleMode(hIn,&mode)) {
        WCHAR wbuf[4096]={}; DWORD nr=0;
        ReadConsoleW(hIn,wbuf,4095,&nr,nullptr);
        while(nr>0&&(wbuf[nr-1]==L'\r'||wbuf[nr-1]==L'\n'))--nr;
        wbuf[nr]=L'\0';
        if (nr==0) return "";
        int n=WideCharToMultiByte(CP_UTF8,0,wbuf,(int)nr,nullptr,0,nullptr,nullptr);
        if (n<=0) return "";
        std::string r(n,'\0');
        WideCharToMultiByte(CP_UTF8,0,wbuf,(int)nr,&r[0],n,nullptr,nullptr);
        return r;
    }
#endif
    std::string line; std::getline(std::cin,line); return line;
}

// -----------------------------------------------------------------------------
// AgentContext factory: build a fully wired context for one agent
// -----------------------------------------------------------------------------
static agent::AgentContext make_context(
        const std::string&                         agent_id,
        const std::string&                         layer,
        const std::string&                         run_id,
        const std::string&                         parent_id,
        const agent::WorkspacePaths&               wp,
        std::shared_ptr<agent::MessageBus>         bus,
        std::shared_ptr<agent::MemoryStore>        memory,
        std::shared_ptr<std::atomic<bool>>         cancel,
        std::shared_ptr<std::atomic<bool>>         pause)
{
    // Init agent workspace dir
    agent::WorkspaceManager::init_agent_dir(wp, agent_id);

    // Build structured logger
    std::string global_log = wp.global_log;
    std::string agent_dir  = wp.agent_dir(agent_id);
    std::string agent_log  = agent::WorkspaceManager::join(agent_dir, "agent.log");
    auto slog = std::make_shared<agent::StructuredLogger>(
        agent_id, layer, run_id, global_log, agent_log);

    // Build state machine with transition → global state.json callback
    auto state = std::make_shared<agent::StateMachine>(agent_id, layer);
    state->on_transition([wp, agent_id](const agent::StateInfo& info) {
        // Serialise StateInfo to JSON fragment
        std::ostringstream jss;
        jss << "{"
            << "\"state\":\"" << agent::state_to_cstr(info.state) << "\","
            << "\"sub_state\":\"" << info.sub_state << "\","
            << "\"task_id\":\"" << info.current_task_id << "\","
            << "\"call_count\":" << info.call_count << ","
            << "\"fail_count\":" << info.fail_count << ","
            << "\"last_event_ms\":" << info.last_event_ms
            << "}";
        agent::WorkspaceManager::write_state(wp, agent_id, jss.str());
    });

    return agent::AgentContextBuilder{}
        .agent(agent_id, layer)
        .run(run_id)
        .parent(parent_id)
        .with_workspace(wp)
        .with_bus(bus)
        .with_memory(memory)
        .with_state(state)
        .with_cancel(cancel)
        .with_pause(pause)
        .build();
    // Note: slog set manually since AgentContextBuilder.with_structured_log needs wp
    // We set it directly after:
}

// Actually assign slog  --  helper since AgentContextBuilder returns a moved copy
static agent::AgentContext make_context_full(
        const std::string& agent_id,
        const std::string& layer,
        const std::string& run_id,
        const std::string& parent_id,
        const agent::WorkspacePaths& wp,
        std::shared_ptr<agent::MessageBus>  bus,
        std::shared_ptr<agent::MemoryStore> memory,
        std::shared_ptr<std::atomic<bool>>  cancel,
        std::shared_ptr<std::atomic<bool>>  pause)
{
    agent::WorkspaceManager::init_agent_dir(wp, agent_id);
    std::string agent_log = agent::WorkspaceManager::join(
        wp.agent_dir(agent_id), "agent.log");
    auto slog  = std::make_shared<agent::StructuredLogger>(
        agent_id, layer, run_id, wp.global_log, agent_log);
    auto state = std::make_shared<agent::StateMachine>(agent_id, layer);
    state->on_transition([wp, agent_id](const agent::StateInfo& info) {
        std::ostringstream jss;
        jss << "{\"state\":\"" << agent::state_to_cstr(info.state) << "\","
            << "\"sub\":\"" << info.sub_state << "\","
            << "\"task\":\"" << info.current_task_id << "\","
            << "\"calls\":" << info.call_count << ","
            << "\"fails\":" << info.fail_count << "}";
        agent::WorkspaceManager::write_state(wp, agent_id, jss.str());
    });

    agent::AgentContext ctx;
    ctx.agent_id    = agent_id;
    ctx.layer       = layer;
    ctx.run_id      = run_id;
    ctx.parent_id   = parent_id;
    ctx.workspace   = wp;
    ctx.slog        = slog;
    ctx.memory      = memory;
    ctx.bus         = bus;
    ctx.state       = state;
    ctx.cancel_flag = cancel;
    ctx.pause_flag  = pause;
    return ctx;
}

// -----------------------------------------------------------------------------
// main
// -----------------------------------------------------------------------------
// Global shutdown flag (plain function pointer for SIGINT/SIGTERM)
static volatile sig_atomic_t g_shutdown_requested = 0;
static void on_signal(int) { g_shutdown_requested = 1; }

int main(int argc, char* argv[]) {
    std::signal(SIGINT,  on_signal);
    std::signal(SIGTERM, on_signal);
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    // Windows: use SetConsoleCtrlHandler for Ctrl+C (SIGINT is unreliable on Windows)
    SetConsoleCtrlHandler([](DWORD t) -> BOOL {
        if (t == CTRL_C_EVENT || t == CTRL_BREAK_EVENT) {
            g_shutdown_requested = 1; return TRUE;
        }
        return FALSE;
    }, TRUE);
#endif

    std::string config_path   = "config/agent_config.json";
    std::string goal_text;
    std::string output_path;
    std::string workspace_root= "workspace";   // configurable
    bool do_setup    = false;
    bool do_list     = false;
    bool do_debug    = false;
    bool single_run  = false;
    bool no_workspace= false;   // --no-workspace: skip workspace init (lightweight mode)
    bool cleanup     = false;   // --cleanup: delete artifacts on exit

    for (int i=1; i<argc; ++i) {
        std::string arg=argv[i];
        if      ((arg=="--config"||arg=="-c")&&i+1<argc)     config_path  =argv[++i];
        else if ((arg=="--goal"  ||arg=="-g")&&i+1<argc)     goal_text    =argv[++i];
        else if ((arg=="--output"||arg=="-o")&&i+1<argc)     output_path  =argv[++i];
        else if ((arg=="--workspace"||arg=="-w")&&i+1<argc)  workspace_root=argv[++i];
        else if (arg=="--setup" ||arg=="-s")   do_setup     =true;
        else if (arg=="--list-models")          do_list      =true;
        else if (arg=="--debug" ||arg=="-d")    do_debug     =true;
        else if (arg=="--no-loop")              single_run   =true;
        else if ((arg=="--resume"||arg=="-r")&&i+1<argc) {
            // --resume: print checkpoint info and set workspace to that run
            workspace_root = std::string("workspace/") + argv[++i];
            print_utf8("[Resume] Loading workspace: "+workspace_root+"\n");
        }
        else if (arg=="--no-workspace")         no_workspace =true;
        else if ((arg=="--conv"||arg=="-C")&&i+1<argc) {
            // Captured early; applied to conv_id after while-loop declaration
            goal_text = std::string("__SWITCH_CONV__:") + argv[++i];
        }
        else if (arg=="--list-prompts") {
            // cfg not yet loaded at this point — use default prompts dir
            std::string pd = resolve_runtime_path("./prompts");
            agent::PromptLoader pl(pd);
            auto all = pl.list_all_meta();
            std::cout << "=== Prompts in " << pd << " ("
                      << all.size() << " files) ===\n";
            for (auto& pi : all) {
                std::string rel = pi.path;
                if (rel.rfind(pd,0)==0) rel=rel.substr(pd.size()+1);
                std::cout << "  " << rel;
                if (!pi.meta.role.empty())
                    std::cout << "  [" << pi.meta.role << " v" << pi.meta.version << "]";
                std::cout << "\n";
                if (!pi.meta.description.empty())
                    std::cout << "    " << pi.meta.description.substr(0,80) << "\n";
            }
            return 0;
        }
        else if (arg=="--list-skills") {
            std::string pd = resolve_runtime_path("./prompts");
            agent::PromptLoader pl(pd);
            auto skills = pl.list_skills();
            std::cout << "=== Skills (" << skills.size() << " available) ===\n";
            for (auto& s : skills) {
                std::cout << "  " << s << "\n";
                std::string body = pl.load_skill(s);
                // Find description line
                auto di = body.find("description: ");
                if (di!=std::string::npos) {
                    auto el = body.find("\n",di+13);
                    auto desc = body.substr(di+13, el==std::string::npos?60:el-di-13);
                    if (desc.size()>75) desc=desc.substr(0,75)+"...";
                    std::cout << "    " << desc << "\n";
                }
            }
            return 0;
        }
        else if (arg=="--cleanup")              cleanup      =true;
        else if (arg=="--help"  ||arg=="-h") {
            std::cout <<
                "Usage: agent_runner [options]\n\n"
                "  -c / --config <path>      Config JSON (default: config/agent_config.json)\n"
                "  -g / --goal   <text>      Goal (default: interactive loop)\n"
                "  -w / --workspace <dir>    Workspace root (default: workspace/)\n"
                "  -s / --setup              Interactive configuration wizard\n"
                "  -o / --output <path>      Wizard output path\n"
                "  -d / --debug              Show raw LLM responses\n"
                "       --no-loop            Exit after one goal\n"
                "       --no-workspace       Skip workspace/logging (lightweight mode)\n"
                "       --cleanup            Delete artifact files on exit\n"
                "       --list-models        List model IDs and tools\n"
                "  -h / --help               This help\n\n"
                "Environment: ANTHROPIC_API_KEY  AGENT_DUMP_AUDIT=1\n";
            return 0;
        }
    }

    if (do_list) { list_models(); return 0; }

    // -- Setup wizard ---------------------------------------------------------
    if (do_setup) {
        std::string save_path=output_path.empty()?config_path:output_path;
        try {
            agent::AgentConfig cfg=setup::run_wizard(config_path);
            print_utf8("\n  Save to "+save_path+"? [Y/n]: ");
            std::string ans=read_goal_line();
            if (!ans.empty()&&(ans[0]=='n'||ans[0]=='N')){print_utf8("Cancelled.\n");return 0;}
            {
                size_t sl=save_path.rfind('/');
#ifdef _WIN32
                size_t bs=save_path.rfind('\\');
                if(bs!=std::string::npos&&(sl==std::string::npos||bs>sl))sl=bs;
#endif
                if(sl!=std::string::npos){
                    std::string dir=save_path.substr(0,sl);
#ifdef _WIN32
                    (void)std::system(("mkdir \""+dir+"\" 2>nul").c_str());
#else
                    (void)std::system(("mkdir -p \""+dir+"\" 2>/dev/null").c_str());
#endif
                }
            }
            cfg.save(save_path);
            print_utf8("\nSaved to "+save_path+"\n\n"
                "Run:  agent_runner -c "+save_path+" -g \"your goal\"\n"
                "  or: agent_runner -c "+save_path+"  (for interactive mode)\n");
        } catch(const std::exception& e){
            std::cerr<<"[ERROR] Setup failed: "<<e.what()<<"\n"; return 1;
        }
        return 0;
    }

    // -- Load config -----------------------------------------------------------
    config_path = resolve_runtime_path(config_path);

    agent::AgentConfig cfg;
    try { cfg=agent::AgentConfig::load(config_path); }
    catch(const std::exception& e){
        std::cerr<<"[FATAL] Config load failed: "<<e.what()<<"\n"
                 <<"  Tip: run  agent_runner --setup  to create a config.\n";
        return 2;
    }
    const char* env_key=std::getenv("ANTHROPIC_API_KEY");
    if(env_key&&*env_key) cfg.api_key=env_key;
    try { cfg.validate(); }
    catch(const std::exception& e){
        std::cerr<<"[FATAL] Config invalid: "<<e.what()<<"\n"; return 2;
    }
    if(do_debug){cfg.log_level="debug"; std::cerr<<"[DEBUG mode]\n\n";}
    agent::Logger::instance().set_level(cfg.log_level);

    cfg.prompt_dir = resolve_runtime_path(cfg.prompt_dir);

    // -- Override workspace root from config if set ----------------------------
    if(cfg.workspace_dir.empty()) cfg.workspace_dir=workspace_root;
    else if(workspace_root!="workspace") cfg.workspace_dir=workspace_root; // CLI wins

    // -- Load prompts ----------------------------------------------------------
    std::string dir_decompose, dir_review, dir_synth, dir_classify, dir_supervise;
    std::string mgr_decompose, mgr_validate, wkr_system;
    try {
        const std::string& pd=cfg.prompt_dir;
        // ── PromptLoader: .md files take precedence, .txt fallback ──
        agent::PromptLoader pl(pd);
        {
            auto prompts = pl.list_prompts();
            if (!prompts.empty()) {
                LOG_INFO("Main","main","init",
                    "PromptLoader: " + std::to_string(prompts.size()) + " .md files found");
            }
        }
        // Legacy .txt fallbacks for each role
        std::string fb_dir_dec, fb_dir_rev, fb_dir_syn, fb_dir_cls, fb_mgr_dec, fb_mgr_val, fb_wkr, fb_sup;
        try { fb_dir_dec = agent::load_prompt(pd+"/director_decompose_system.txt"); } catch(const std::exception& e) { LOG_WARN("Main","main","init","Failed to load director_decompose_system.txt: " + std::string(e.what())); }
        try { fb_dir_rev = agent::load_prompt(pd+"/director_review_system.txt");    } catch(const std::exception& e) { LOG_WARN("Main","main","init","Failed to load director_review_system.txt: " + std::string(e.what())); }
        try { fb_dir_syn = agent::load_prompt(pd+"/director_synthesise_system.txt");} catch(const std::exception& e) { LOG_WARN("Main","main","init","Failed to load director_synthesise_system.txt: " + std::string(e.what())); }
        try { fb_mgr_dec = agent::load_prompt(pd+"/manager_decompose_system.txt");  } catch(const std::exception& e) { LOG_WARN("Main","main","init","Failed to load manager_decompose_system.txt: " + std::string(e.what())); }
        try { fb_mgr_val = agent::load_prompt(pd+"/manager_validate_system.txt");   } catch(const std::exception& e) { LOG_WARN("Main","main","init","Failed to load manager_validate_system.txt: " + std::string(e.what())); }
        try { fb_wkr     = agent::load_prompt(pd+"/worker_system.txt");             } catch(const std::exception& e) { LOG_WARN("Main","main","init","Failed to load worker_system.txt: " + std::string(e.what())); }
        try { fb_sup     = agent::load_prompt(pd+"/supervisor_system.txt");         } catch(const std::exception& e) { LOG_WARN("Main","main","init","Failed to load supervisor_system.txt: " + std::string(e.what())); }
        // Assemble from .md (with .txt fallback)
        dir_decompose = pl.assemble("director-decompose",    fb_dir_dec);
        dir_review    = pl.assemble("director-review",       fb_dir_rev);
        dir_synth     = pl.assemble("director-synthesise",   fb_dir_syn);
        dir_classify  = pl.assemble("director-classify",     fb_dir_cls);
        mgr_decompose = pl.assemble("manager-decompose",     fb_mgr_dec);
        mgr_validate  = pl.assemble("manager-validate",      fb_mgr_val);
        wkr_system    = pl.assemble("worker-core",           fb_wkr);
        dir_supervise = pl.assemble("supervisor-evaluate",   fb_sup);
    } catch(const std::exception& e){
        std::cerr<<"[FATAL] Prompt load failed: "<<e.what()<<"\n"; return 2;
    }

    // -- Tools -----------------------------------------------------------------
    agent::ToolRegistry registry;
    agent::register_all_tools(registry);   // registers all 12 built-in
    agent::load_dynamic_tools(registry);    // load persisted tools from workspace/tools/ tools
    std::vector<std::string> available_tools=registry.tool_names();
    std::string wkr_system_full=wkr_system+agent::build_tool_doc(available_tools);

    // -- API Clients -----------------------------------------------------------
    agent::ApiClient director_client  (make_layer_config(cfg,cfg.director_model));
    agent::ApiClient manager_client   (make_layer_config(cfg,cfg.manager_model));
    agent::ApiClient worker_client    (make_layer_config(cfg,cfg.worker_model));
    agent::ApiClient supervisor_client(make_layer_config(cfg,cfg.supervisor_model));

    // -- Shared infrastructure (per session) -----------------------------------
    auto bus    = std::make_shared<agent::MessageBus>();
    // Session-level shared memory (Director + Managers read/write, Workers read)
    auto session_memory = std::make_shared<agent::MemoryStore>(cfg.memory_short_term_window);
    // Environment knowledge base — shared across all agents, persisted to disk
    auto env_kb = std::make_shared<agent::EnvKnowledgeBase>();
    {
        // Load env_knowledge from .md (replaces legacy .tsv)
        std::string env_kb_path = cfg.workspace_dir + "/current/env_knowledge.md";
        if (!agent::utf8_ifstream(env_kb_path).is_open())
            env_kb_path = cfg.workspace_dir + "/env_knowledge.tsv";  // legacy fallback
        agent::utf8_ifstream ef(env_kb_path);
        if (ef.is_open()) {
            std::string data((std::istreambuf_iterator<char>(ef)),
                             std::istreambuf_iterator<char>());
            // Use deserialize_md() which handles both Markdown and legacy TSV
            env_kb->deserialize_md(data);
        }
    }
    // Shared skill registry: persists skills learned across runs
    std::string skills_dir = agent::WorkspaceManager::join(cfg.workspace_dir, "skills");
    auto skills = std::make_shared<agent::SkillRegistry>(skills_dir);

    // Load previous session memory from workspace root (cross-run persistence)
    if(cfg.memory_session_enabled){
        std::string session_path=agent::WorkspaceManager::join(
            cfg.workspace_dir,"session.json");
        try{ session_memory->load_session(session_path); }catch(const std::exception& e){ LOG_WARN("Main","main","init","Failed to load session: " + std::string(e.what())); }
        std::string lt_dir=agent::WorkspaceManager::join(cfg.workspace_dir,"long_term");
        try{ session_memory->load_long_term(lt_dir); }catch(const std::exception& e){ LOG_WARN("Main","main","init","Failed to load long_term: " + std::string(e.what())); }
    }

    agent::ThreadPool pool(static_cast<size_t>(cfg.worker_threads));

    // -- Initialize command system ---------------------------------------------
    agent::cli::CommandSystem cmd_system;
    agent::cli::register_all_commands(cmd_system);

    // Initialize readline with command list
    auto cmd_list = cmd_system.list_commands();
    agent::cli::ReadlineHelper::init(cmd_list);
    agent::cli::ReadlineHelper::set_command_system(&cmd_system);

    // -- Conversation loop -----------------------------------------------------
    std::vector<std::string> history;  // for multi-turn context
    bool first=true;
    // Config hot-reload flag (set by SIGHUP)
    static std::atomic<bool> g_reload_config{false};
#ifndef _WIN32
    // POSIX-safe SIGHUP handler using sigaction (safe for multi-threaded programs)
    {
        struct sigaction sa{};
        sa.sa_handler = [](int){ g_reload_config.store(true); };
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = SA_RESTART;  // restart interrupted syscalls
        sigaction(SIGHUP, &sa, nullptr);
    }
#endif

    // Health check server on localhost:8080/health
    agent::HealthServer health_srv(8080);
    {
        const std::string hmodel = cfg.default_model;
        const std::string hws    = cfg.workspace_dir;
        health_srv.start([hmodel, hws]() -> std::string {
            std::string body = "{";
            body += "\"status\":\"ok\"";
            body += ",\"model\":\"" + hmodel + "\"";
            body += ",\"workspace\":\"" + hws + "\"";
            body += ",\"version\":\"v12.0\"";
            body += "}";
            return body;
        });
    }

    print_utf8("\n=== Three-Layer Agent v2 ===\n");
    print_utf8("Type your goal and press Enter. 'exit' to quit.\n");
    if (!no_workspace) {
        print_utf8("Files:  "+cfg.workspace_dir+"/current/files/\n");
        print_utf8("Logs:   "+cfg.workspace_dir+"/logs/activity.md  (Markdown, append)\n");
        print_utf8("Conv dir: "+cfg.workspace_dir+"/conversations/<conv-id>/\n");
        print_utf8("Exp:    "+cfg.workspace_dir+"/current/memory/EXPERIENCE.md\n");

    }
    print_utf8("Health: http://localhost:8080/health\n");
    print_utf8("Tools: list_dir  read_file  write_file  get_env  echo\n\n");

    // Conversation state: shared across all messages in one session
    static std::string conv_id;
    // Apply --conv <id> if specified at startup
    if (!goal_text.empty() && goal_text.substr(0,17) == "__SWITCH_CONV__:") {
        conv_id   = goal_text.substr(17);
        goal_text = "";  // don't treat as goal
        print_utf8("[Resuming conversation: " + conv_id + "]\n");
    }
    static std::atomic<uint64_t> run_counter{0};
    static std::atomic<uint64_t> msg_counter{0};
    static std::shared_ptr<agent::ExperienceManager> exp_mgr;

    while(true) {
        // -- Get goal ----------------------------------------------------------
        std::string current_goal;
        if(first&&!goal_text.empty()){
            current_goal=goal_text; first=false;
        } else {
            current_goal = agent::cli::ReadlineHelper::read_line("You: ");
            if(current_goal.empty())continue;
        }
        // trim trailing whitespace
        while(!current_goal.empty()&&(current_goal.back()=='\r'||current_goal.back()=='\n'
              ||current_goal.back()==' '))current_goal.pop_back();

        // Config hot-reload check
        if (g_reload_config.exchange(false)) {
            try {
                agent::utf8_ifstream rcf(config_path);
                if (rcf.is_open()) {
                    std::string rjcontent((std::istreambuf_iterator<char>(rcf)),
                                           std::istreambuf_iterator<char>());
                    nlohmann::json rj = nlohmann::json::parse(rjcontent);
                    agent::from_json(rj, cfg);
                    cfg.validate();  // throws on invalid config
                    print_utf8("[Config reloaded from " + config_path + "]\n");
                }
            } catch (const std::exception& re) {
                print_utf8("[Config reload failed: " + std::string(re.what()) + " -- keeping old config]\n");
            }
        }

        // ── Conversation commands ───────────────────────────────────────────
        if (current_goal == "/new") {
            conv_id.clear();  // reset → next iteration generates new conv_id
            run_counter.store(0); msg_counter.store(0);
            history.clear();
            print_utf8("\n[New conversation started. Previous context cleared.]\n\n");
            continue;
        }
        if (current_goal == "/conv") {
            print_utf8("Current conversation: " + conv_id +
                       "  (messages: " + std::to_string(msg_counter.load()) + ")\n");
            print_utf8("Directory: " + cfg.workspace_dir + "/conversations/" + conv_id + "/\n");
            continue;
        }
        if (current_goal == "/convs" || current_goal == "/conversations") {
            // List all past conversations
            std::string conv_dir = cfg.workspace_dir + "/conversations";
            std::vector<std::string> convs;
            try {
                for (auto& e : std::filesystem::directory_iterator(conv_dir)) {
                    if (e.is_directory()) {
                        std::string name = e.path().filename().string();
                        if (name.substr(0,5) == "conv-") {
                            // Try to read first line of CONVERSATION.md for summary
                            std::string md_path = e.path().string() + "/CONVERSATION.md";
                            agent::utf8_ifstream mdf(md_path);
                            std::string first_line;
                            if (mdf.is_open()) {
                                std::getline(mdf, first_line);
                                std::getline(mdf, first_line);  // skip blank
                                std::getline(mdf, first_line);  // first run summary
                            }
                            convs.push_back(name + (first_line.empty() ? "" : "  " + first_line.substr(0, 60)));
                        }
                    }
                }
            } catch(const std::exception& e) {
                LOG_WARN("Main","main","convs","Failed to list conversations: " + std::string(e.what()));
            }
            if (convs.empty()) {
                print_utf8("No past conversations found.\n");
            } else {
                std::sort(convs.begin(), convs.end(), std::greater<>());  // newest first
                print_utf8("Past conversations (newest first):\n");
                for (auto& c : convs)
                    print_utf8("  " + c + "\n");
                print_utf8("\nUse /switch <conv-id> to resume a conversation.\n");
            }
            continue;
        }
        if (current_goal.substr(0, 8) == "/switch ") {
            std::string target = current_goal.substr(8);
            // Trim whitespace
            while (!target.empty() && target.front() == ' ') target.erase(target.begin());
            while (!target.empty() && target.back()  == ' ') target.pop_back();
            std::string target_dir = cfg.workspace_dir + "/conversations/" + target;
            if (!std::filesystem::exists(target_dir)) {
                print_utf8("[Error] Conversation not found: " + target + "\n");
                print_utf8("Use /convs to list available conversations.\n");
            } else {
                // Switch to that conversation — restore its memory
                conv_id = target;
                run_counter.store(0); msg_counter.store(0);
                history.clear();
                exp_mgr.reset();  // will be re-created with new conv paths on next run
                // Load MEMORY.md into session_memory
                std::string mem_path = target_dir + "/MEMORY.md";
                if (cfg.memory_session_enabled) {
                    try {
                        session_memory->load_conversation_md(mem_path);
                        print_utf8("[Switched to conversation: " + target + "]\n");
                        print_utf8("[Memory loaded from: " + mem_path + "]\n\n");
                    } catch (...) {
                        print_utf8("[Switched to: " + target + " (memory load failed)]\n\n");
                    }
                } else {
                    print_utf8("[Switched to conversation: " + target + "]\n\n");
                }
            }
            continue;
        }
        if (current_goal == "/workspace") {
            std::string _wmd = cfg.workspace_dir + "/current/WORKSPACE.md";
            try {
                agent::utf8_ifstream _wf(_wmd);
                std::string _wc((std::istreambuf_iterator<char>(_wf)),
                                 std::istreambuf_iterator<char>());
                print_utf8(_wc.empty() ? "[WORKSPACE.md empty]\n" : _wc+"\n");
            } catch(const std::exception& e) {
                print_utf8("[WORKSPACE.md not found: " + std::string(e.what()) + "]\n");
            }
            continue;
        }
        if (current_goal == "/experience" || current_goal == "/exp") {
            std::string _emd = cfg.workspace_dir + "/current/memory/EXPERIENCE.md";
            try {
                agent::utf8_ifstream _ef(_emd);
                std::string _ec((std::istreambuf_iterator<char>(_ef)),
                                 std::istreambuf_iterator<char>());
                print_utf8(_ec.empty() ? "[No experience yet]\n" : _ec+"\n");
            } catch(const std::exception& e) {
                print_utf8("[EXPERIENCE.md not found: " + std::string(e.what()) + "]\n");
            }
            continue;
        }
        // ── Command system (handles all / commands) ────
        if (!current_goal.empty() && current_goal[0] == '/' && cmd_system.execute(current_goal)) {
            continue;
        }
        // ── End conversation commands ────────────────────────────────────────
        if(current_goal=="exit"||current_goal=="quit"||current_goal=="退出"){
            print_utf8("Goodbye.\n"); break;
        }
        if(current_goal.empty())continue;

        // -- Workspace init for this run ---------------------------------------
        // conv_id: shared across ALL runs in one conversation
        // Changes only when user issues /new to start fresh
        if (conv_id.empty()) {
            auto _cn = std::chrono::system_clock::now();
            auto _ct = std::chrono::system_clock::to_time_t(_cn);
            std::tm _tb{};
        #ifdef _WIN32
            gmtime_s(&_tb, &_ct);
        #else
            gmtime_r(&_ct, &_tb);
        #endif
            char _d[12]; std::strftime(_d,sizeof(_d),"%Y%m%d",&_tb);
            unsigned _r=(unsigned)(std::chrono::steady_clock::now().time_since_epoch().count()&0xFFFFFF);
            char _rb[8]; std::snprintf(_rb,sizeof(_rb),"%06x",_r);
            conv_id = std::string("conv-")+_d+"-"+_rb;
        }
        uint64_t run_num = ++run_counter; ++msg_counter;
        // workspace dir uses conv_id (shared per conversation, not per message)
        std::string run_id = conv_id;
        agent::WorkspacePaths wp;
        bool use_workspace=!no_workspace;

        if(use_workspace){
            try {
             wp=agent::WorkspaceManager::init(cfg.workspace_dir, run_id);
            // ExperienceManager (per conversation, shared across runs)
            if (!exp_mgr && !wp.conv_exp_md.empty()) {
                exp_mgr = std::make_shared<agent::ExperienceManager>(
                    wp.conv_exp_md, wp.experience_md,
                    cfg.prompt_dir + "/skills", run_id);
            }
            // Init WORKSPACE.md on first use
            if (!wp.workspace_md.empty()) {
                agent::utf8_ifstream _wchk(wp.workspace_md);
                if (!_wchk.is_open()) {
                agent::utf8_ofstream _wf(wp.workspace_md);
                    if (_wf.is_open()) {
                        _wf << "# Workspace\n\n"
                            << "| Path | Purpose |\n|------|---------|\n"
                            << "| files/ | Agent-created files |\n"
                            << "| memory/ | Persistent memory |\n"
                            << "| ../logs/agent.log | Activity log (append) |\n\n"
                            << "## Session History\n\n";
                    }
                }
            }
            } catch(const std::exception& e){
                std::cerr<<"[WARN] Workspace init failed: "<<e.what()
                         <<"  --  running without workspace\n";
                use_workspace=false;
            }
        }

        // -- Per-run shared memory (fresh per conversation turn) ---------------
        auto run_memory=std::make_shared<agent::MemoryStore>(cfg.memory_short_term_window);
        // Inject long-term summaries from session memory
        if(cfg.memory_session_enabled){
            auto summaries=session_memory->get_summaries();
            for(auto& s:summaries) run_memory->append_summary(s);
        }

        // -- Build AgentContexts -----------------------------------------------
        // Shared cancel/pause flags (one pair per run; Supervisor controls them)
        auto cancel_flag=std::make_shared<std::atomic<bool>>(false);
        auto pause_flag =std::make_shared<std::atomic<bool>>(false);

        // Worker contexts (own per-worker memory; share bus and flags)
        std::vector<std::shared_ptr<agent::WorkerAgent>> worker_ptrs;
        for(int i=0;i<cfg.worker_threads;++i){
            std::string wid="wkr-"+std::to_string(i+1);
            auto worker_mem=std::make_shared<agent::MemoryStore>(cfg.memory_short_term_window);

            agent::AgentContext wctx;
            if(use_workspace){
                wctx=make_context_full(wid,"Worker",run_id,"mgr-*",
                    wp,bus,worker_mem,cancel_flag,pause_flag);
            } else {
                wctx.agent_id=wid; wctx.layer="Worker"; wctx.run_id=run_id;
                wctx.bus=bus; wctx.memory=worker_mem;
                wctx.cancel_flag=cancel_flag; wctx.pause_flag=pause_flag;
            }
            wctx.skills     =skills;       // shared skill registry
            wctx.session_mem=session_memory;  // cross-run experience
            auto w=std::make_shared<agent::WorkerAgent>(
                wid,worker_client,registry,wkr_system_full,
                cfg.max_atomic_retries,std::move(wctx));
            worker_ptrs.push_back(w);
        }

        // Manager factory  --  each Manager gets a fresh context + shared run_memory
        static std::atomic<int> mgr_counter{0};
        auto mgr_factory=[&](const agent::SubTask&)->std::unique_ptr<agent::IManager>{
            int n=++mgr_counter;
            std::string mid="mgr-"+std::to_string(n);
            agent::AgentContext mctx;
            if(use_workspace){
                mctx=make_context_full(mid,"Manager",run_id,"dir-001",
                    wp,bus,run_memory,cancel_flag,pause_flag);
            } else {
                mctx.agent_id=mid; mctx.layer="Manager"; mctx.run_id=run_id;
                mctx.parent_id="dir-001";
                mctx.bus=bus; mctx.memory=run_memory;
                mctx.cancel_flag=cancel_flag; mctx.pause_flag=pause_flag;
                mctx.session_mem=session_memory;  // cross-run experience
                mctx.env_kb     =env_kb;           // environment knowledge
        if (exp_mgr) mctx.exp_mgr_ptr = exp_mgr;
            }
            return std::make_unique<agent::ManagerAgent>(
                mid,manager_client,worker_ptrs,pool,
                mgr_decompose,mgr_validate,
                available_tools,cfg.max_atomic_retries,std::move(mctx));
        };

        // Director context
        agent::AgentContext dctx;
        if(use_workspace){
            dctx=make_context_full("dir-001","Director",run_id,"",
                wp,bus,run_memory,cancel_flag,pause_flag);
        } else {
            dctx.agent_id="dir-001"; dctx.layer="Director"; dctx.run_id=run_id;
            dctx.bus=bus; dctx.memory=run_memory;
            dctx.cancel_flag=cancel_flag; dctx.pause_flag=pause_flag;
        dctx.budget_usd =cfg.max_cost_per_run_usd;
        dctx.env_kb    =env_kb;  // environment knowledge base
        if (exp_mgr) dctx.exp_mgr_ptr = exp_mgr;
        }

        agent::DirectorAgent director(
            "dir-001",director_client,pool,mgr_factory,
            dir_decompose,dir_review,dir_synth,dir_classify,
            cfg.max_subtask_retries,std::move(dctx));

        // Advisor + Supervisor  --  configured from AgentConfig
        std::unique_ptr<agent::AdvisorAgent> advisor;
        if(cfg.supervisor_advisor_enabled)
            advisor=std::make_unique<agent::AdvisorAgent>("adv-001",supervisor_client);

        agent::SupervisorConfig sup_cfg;
        sup_cfg.poll_interval =std::chrono::milliseconds(cfg.supervisor_poll_interval_ms);
        sup_cfg.stuck_timeout =std::chrono::milliseconds(cfg.supervisor_stuck_timeout_ms);
        sup_cfg.max_fail_count=cfg.supervisor_max_fail_count;

        agent::SupervisorAgent supervisor(
            "sup-001",supervisor_client,director,dir_supervise,
            bus,sup_cfg,cfg.supervisor_max_retries,std::move(advisor));

        // Register all Agent state machines with Supervisor for active monitoring
        for(const auto& w : worker_ptrs) {
            // Workers share cancel/pause flags; register each individually
            // so Supervisor can cancel a specific worker
            // (state machine access requires ctx  --  stored inside WorkerAgent)
            // Future: expose ctx.state via WorkerAgent::state() accessor
            // For now, register the shared flags so Supervisor can cancel all workers
            if (w && w->state()) {
                supervisor.register_context(
                    w->id(), w->state(), cancel_flag, pause_flag);
            }
        }
        // Register Director context (full state + flags)
        // Director's state machine was moved into ctx  --  expose via accessor in future
        supervisor.register_context("dir-001", director.state(), cancel_flag, pause_flag);

        // -- Build full goal with conversation context --------------------------
        std::string full_goal=current_goal;
        if(!history.empty()){
            std::ostringstream ctx;
            ctx<<"[Conversation context:]\n";
            size_t start=history.size()>6?history.size()-6:0;
            for(size_t i=start;i<history.size();i+=2){
                ctx<<"User: "<<history[i]<<"\n";
                if(i+1<history.size()){
                    // Safe UTF-8 truncation: don't cut in middle of multi-byte sequence
                    const std::string& ans = history[i+1];
                    size_t limit = std::min(ans.size(), (size_t)400);
                    // Walk back to find a safe UTF-8 boundary
                    while(limit > 0 && (((unsigned char)ans[limit]) & 0xC0) == 0x80) --limit;
                    ctx<<"Assistant: "<<ans.substr(0,limit)<<"\n";
                }
            }
            ctx<<"\n[Current request:]\n"<<current_goal;
            full_goal=ctx.str();
        }

        // -- Run ----------------------------------------------------------------
        // Wire g_shutdown_requested to cancel_flag so SIGINT/Ctrl+C cancels the run
        cancel_flag->store(false);  // reset for this run
        std::thread shutdown_watcher([&cancel_flag](){
            while (!cancel_flag->load()) {
                if (g_shutdown_requested)
                    cancel_flag->store(true);
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        });

        print_utf8("\n[Thinking...]\n");

        // Progress + Internal-Dialog display thread
        // Drains Progress, ToolCall, and LlmDecision messages for real-time visibility
        std::atomic<bool> progress_done{false};
        std::thread progress_thread([&bus, &progress_done](){
            while (!progress_done.load()) {
                while (auto msg = bus->try_receive("main-progress")) {
                    const auto& pl = msg->payload;
                    if (msg->type == agent::MsgType::Progress) {
                        auto sp = pl.find("\"step\":");
                        if (sp != std::string::npos) {
                            sp += 8;
                            while (sp < pl.size() && (pl[sp]=='"'||pl[sp]==' ')) ++sp;
                            auto ep = pl.find('"', sp);
                            if (ep != std::string::npos) {
                                std::string step = pl.substr(sp, ep-sp);
                                if (!step.empty()) print_utf8("  [" + step + "]\n");
                            }
                        }
                    } else if (msg->type == agent::MsgType::ToolCall) {
                        // Show: [wkr-1] TOOL list_dir(".")
                        auto ag = pl.find("\"agent\":");
                        auto tl = pl.find("\"tool\":");
                        auto in = pl.find("\"input\":");
                        auto out= pl.find("\"output\":");
                        auto ex_str = [&](size_t p2) -> std::string {
                            if (p2 == std::string::npos) return "";
                            auto s = pl.find('"', p2 + pl.find(':', p2) - p2);
                            if (s == std::string::npos) return "";
                            ++s; auto e = pl.find('"', s);
                            return (e != std::string::npos) ? pl.substr(s, e-s) : "";
                        };
                        std::string agent_id = ex_str(ag);
                        std::string tool_nm  = ex_str(tl);
                        std::string inp_v    = ex_str(in);
                        std::string out_v    = ex_str(out);
                        if (!tool_nm.empty()) {
                            std::string line = "  [" + agent_id + "] TOOL " + tool_nm;
                            if (!inp_v.empty()) line += "("" + inp_v + "")";
                            if (!out_v.empty()) line += " → " + out_v;
                            print_utf8(line + "\n");
                        }
                    } else if (msg->type == agent::MsgType::Dialog) {
                        // Show thought/reasoning from Agent
                        auto ag = pl.find("\"agent\":");
                        auto th = pl.find("\"thought\":");
                        auto ex_s = [&](size_t p2) -> std::string {
                            if (p2 == std::string::npos) return "";
                            auto s = pl.find('"', p2 + pl.find(':', p2) - p2);
                            if (s == std::string::npos) return "";
                            ++s; auto e = pl.find('"', s);
                            return (e != std::string::npos) ? pl.substr(s, e-s) : "";
                        };
                        std::string ag_id = ex_s(ag);
                        std::string thought = ex_s(th);
                        if (!thought.empty())
                            print_utf8("  [" + ag_id + "] 💭 " + thought + "\n");
                    } else if (msg->type == agent::MsgType::LlmDecision) {
                        // Show: [wkr-1] LLM → "status":done ...
                        auto ag = pl.find("\"agent\":");
                        auto su = pl.find("\"summary\":");
                        auto ex_str = [&](size_t p2) -> std::string {
                            if (p2 == std::string::npos) return "";
                            auto s = pl.find('"', p2 + pl.find(':', p2) - p2);
                            if (s == std::string::npos) return "";
                            ++s; auto e = pl.find('"', s);
                            return (e != std::string::npos) ? pl.substr(s, e-s) : "";
                        };
                        std::string agent_id = ex_str(ag);
                        std::string summary  = ex_str(su);
                        if (!summary.empty())
                            print_utf8("  [" + agent_id + "] LLM → " + summary + "\n");
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
        });

        agent::FinalResult result=supervisor.run(agent::UserGoal{full_goal});
        progress_done.store(true);
        if (progress_thread.joinable()) progress_thread.join();

        // Stop watcher thread
        cancel_flag->store(true);  // ensure thread exits
        if (shutdown_watcher.joinable()) shutdown_watcher.join();
        cancel_flag->store(false);  // reset for next run

        if(result.status==agent::TaskStatus::Done){
            print_utf8("\nAssistant: "+result.answer+"\n\n");
            // Show token usage if non-zero
            if(result.usage.input_tokens > 0) {
                std::ostringstream cost_str;
                cost_str << "[tokens: " << result.usage.input_tokens << " in";
                if(result.usage.cached_tokens > 0)
                    cost_str << " (" << result.usage.cached_tokens << " cached)";
                cost_str << " / " << result.usage.output_tokens << " out";
                if(result.usage.estimated_cost > 0)
                    cost_str << " / ~$" << std::fixed
                             << std::setprecision(4) << result.usage.estimated_cost;
                cost_str << "]\n";
                print_utf8(cost_str.str());
            }
            history.push_back(current_goal);
            // Store a clean version of the answer (validate UTF-8 for history safety)
            {
                std::string clean_ans;
                clean_ans.reserve(result.answer.size());
                const unsigned char* s = (const unsigned char*)result.answer.c_str();
                size_t i = 0, n = result.answer.size();
                while (i < n) {
                    unsigned char c = s[i];
                    size_t seq = 1;
                    if      ((c & 0x80) == 0x00) seq = 1;      // ASCII
                    else if ((c & 0xE0) == 0xC0) seq = 2;
                    else if ((c & 0xF0) == 0xE0) seq = 3;
                    else if ((c & 0xF8) == 0xF0) seq = 4;
                    else { i++; clean_ans += '?'; continue; }  // invalid lead byte
                    bool valid = (i + seq <= n);
                    for (size_t k = 1; valid && k < seq; ++k)
                        if ((s[i+k] & 0xC0) != 0x80) valid = false;
                    if (valid) {
                        for (size_t k = 0; k < seq; ++k) clean_ans += (char)s[i+k];
                        i += seq;
                    } else {
                        clean_ans += '?'; i++;
                    }
                }
                history.push_back(clean_ans);
            }

            // Push answer into session-level memory
            if(cfg.memory_session_enabled&&!result.answer.empty()){
                session_memory->push_result(run_id, result.answer);
            }

            // Generate long-term summary if enabled
            if(cfg.memory_long_term_enabled&&use_workspace&&!result.answer.empty()){
                std::string lt_dir=agent::WorkspaceManager::join(wp.memory_dir,"long_term");
                auto llm_fn=[&](const std::string& prompt)->std::string{
                    try{ return supervisor_client.complete("You are a concise summarizer.",prompt,"summary"); }
                    catch(const std::exception& e){
                        LOG_WARN("Main","main",run_id,"Summary generation failed: " + std::string(e.what()));
                        return "";
                    }
                };
                std::string summary=session_memory->generate_and_store_summary(
                    current_goal, result.answer, lt_dir, llm_fn);
                if(!summary.empty())
                    LOG_INFO("Main","main",run_id,"long-term summary stored");
            }

            // Persist session memory if enabled
            if(cfg.memory_session_enabled&&use_workspace){
                // Save to MEMORY.md (Markdown, human-readable) + legacy json for compat
                if (!wp.conv_memory_md.empty())
                    session_memory->save_conversation_md(wp.conv_memory_md);
                session_memory->save_session(
                    agent::WorkspaceManager::join(wp.memory_dir,"session.json"));
                // Append this run to MEMORY.md
                if (!wp.conv_memory_md.empty() && !result.answer.empty()) {
                    session_memory->append_run_to_memory_md(
                        wp.conv_memory_md,
                        "run-" + std::to_string(msg_counter.load()),
                        current_goal.substr(0, 80),
                        result.answer.substr(0, std::min(result.answer.size(), size_t(150))));
                }
                // Save env_knowledge as .md
                if(use_workspace && env_kb) {
                    std::string ekb_path = cfg.workspace_dir + "/current/env_knowledge.md";
                    agent::utf8_ofstream ekf(ekb_path);
                    if (ekf.is_open()) ekf << env_kb->serialize();
                }
            }

            // Save FinalResult to workspace
            if(use_workspace){
                try {
                    nlohmann::json jr; to_json(jr,result);
                    agent::utf8_ofstream f(wp.result_json);
                    if(f.is_open())f<<jr.dump(2)<<"\n";
                } catch(const std::exception& e){
                    LOG_WARN("Main","main",run_id,"Failed to save result: " + std::string(e.what()));
                }
            }

            // Cleanup artifacts if requested
            if(use_workspace&&cleanup){
                auto deleted=agent::WorkspaceManager::cleanup_artifacts(wp);
                if(!deleted.empty())
                    std::cerr<<"[Cleanup] Deleted "<<deleted.size()<<" artifact files\n";
            }

            const char* audit=std::getenv("AGENT_DUMP_AUDIT");
            if(audit&&std::string(audit)=="1"){
                nlohmann::json j; to_json(j,result);
                print_utf8("\n[AUDIT TRAIL]\n"+j.dump(2)+"\n");
            }
        } else {
            print_utf8("\n[ERROR] "+result.error+"\n\n");
        }

        if(single_run||(!goal_text.empty()&&first==false&&!IS_STDIN_TTY()))
            break;
    }

    return 0;
}
