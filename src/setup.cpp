// =============================================================================
// src/setup.cpp   --   Interactive guided configuration wizard
//
// Windows input strategy:
//   ALL console input goes through ReadConsoleW (Win32 API), then converted
//   to UTF-8.  We NEVER mix ReadConsoleW with std::cin / std::getline on
//   Windows  --  mixing the two causes read_secret() to consume nothing (the
//   CRLF from the previous getline is already buffered) or std::getline to
//   block forever after ReadConsoleW drained the handle.
//
//   Solution: a single win_read_line(bool echo) function handles all input.
//   When echo=false it turns off ENABLE_ECHO_INPUT before ReadConsoleW and
//   restores it after.
//
// POSIX input strategy:
//   Normal lines use std::getline.  Secret input uses tcsetattr(~ECHO) then
//   std::getline, then restores the terminal.
// =============================================================================

#ifndef _WIN32
#  include <termios.h>
#  include <unistd.h>
#endif

#include "setup.hpp"
#include "agent/models.hpp"
#include <nlohmann/json.hpp>

#include <cctype>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif

namespace setup {

// -----------------------------------------------------------------------------
// Color: only when Windows console reports UTF-8 + VT processing OK.
// On Chinese CMD (CP 936) this gracefully degrades to plain ASCII.
// -----------------------------------------------------------------------------
static bool g_color = false;

static void init_console() {
#ifdef _WIN32
    // Try to enable UTF-8 output
    SetConsoleOutputCP(CP_UTF8);

    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (GetConsoleMode(hOut, &mode)) {
        if (SetConsoleMode(hOut, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING))
            g_color = true;   // VT succeeded; code page is now UTF-8
    }
#else
    g_color = true;
#endif
}

static std::string col(const char* code, const std::string& s) {
    if (!g_color) return s;
    return std::string("\033[") + code + "m" + s + "\033[0m";
}
static std::string bold (const std::string& s) { return col("1",    s); }
static std::string green(const std::string& s) { return col("1;32", s); }
static std::string gray (const std::string& s) { return col("2",    s); }
static std::string red  (const std::string& s) { return col("1;31", s); }

// -----------------------------------------------------------------------------
// Low-level line reader  --  the single point of truth for all console input.
// echo=true  → normal visible input
// echo=false → password mode (characters not shown)
// -----------------------------------------------------------------------------
static std::string read_line(bool echo = true) {
#ifdef _WIN32
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    DWORD  old_mode = 0;
    GetConsoleMode(hIn, &old_mode);

    DWORD new_mode = old_mode;
    if (!echo) new_mode &= ~ENABLE_ECHO_INPUT;
    // Always keep ENABLE_LINE_INPUT so ReadConsoleW waits for Enter
    new_mode |= ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT;
    SetConsoleMode(hIn, new_mode);

    // Read one line (up to 2047 wide chars) via Win32  --  bypasses cin buffer
    WCHAR wbuf[2048] = {};
    DWORD nread = 0;
    ReadConsoleW(hIn, wbuf, 2047, &nread, nullptr);

    // Restore mode immediately
    SetConsoleMode(hIn, old_mode);

    if (!echo) std::cout << "\n";   // print newline that was suppressed

    // Strip trailing CR/LF
    while (nread > 0 && (wbuf[nread-1] == L'\r' || wbuf[nread-1] == L'\n'))
        --nread;
    wbuf[nread] = L'\0';

    if (nread == 0) return "";

    // Wide → UTF-8
    int n = WideCharToMultiByte(CP_UTF8, 0, wbuf, (int)nread,
                                nullptr, 0, nullptr, nullptr);
    std::string result;
    if (n > 0) {
        result.resize((size_t)n);
        WideCharToMultiByte(CP_UTF8, 0, wbuf, (int)nread,
                            &result[0], n, nullptr, nullptr);
    }
    // Zero the wide buffer
    SecureZeroMemory(wbuf, sizeof(wbuf));
    return result;

#else
    // POSIX
    if (!echo) {
        termios oldt, newt;
        tcgetattr(STDIN_FILENO, &oldt);
        newt = oldt;
        newt.c_lflag &= ~(tcflag_t)ECHO;
        tcsetattr(STDIN_FILENO, TCSANOW, &newt);
        std::string line;
        std::getline(std::cin, line);
        tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
        std::cout << "\n";
        return line;
    }
    std::string line;
    std::getline(std::cin, line);
    return line;
#endif
}

// -----------------------------------------------------------------------------
// Trim whitespace
// -----------------------------------------------------------------------------
static std::string trim(const std::string& s) {
    auto l = s.find_first_not_of(" \t\r\n");
    auto r = s.find_last_not_of(" \t\r\n");
    return (l == std::string::npos) ? "" : s.substr(l, r - l + 1);
}

// -----------------------------------------------------------------------------
// Banner / step headers  --  pure ASCII, safe in any code page
// -----------------------------------------------------------------------------
static void print_banner() {
    std::cout
        << "\n"
        << "+==================================================+\n"
        << "|   Three-Layer Agent  --  Configuration Wizard    |\n"
        << "+==================================================+\n"
        << "  Press Enter to keep the current value [in brackets].\n"
        << "  Type 'skip' or '-1' to omit optional numeric fields.\n\n";
}

static void print_step(int n, int total, const std::string& name) {
    std::cout << "\n" << bold("[" + std::to_string(n) + "/" +
                              std::to_string(total) + "] " + name) << "\n\n";
}

static void print_header(const std::string& title) {
    std::cout << "\n--- " << bold(title) << " ---\n\n";
}

static void hint(const std::string& s) {
    std::cout << gray("  > " + s) << "\n";
}

// -----------------------------------------------------------------------------
// ask()   --  prompt, optional default (never shown for secret fields), read input
// -----------------------------------------------------------------------------
static std::string ask(const std::string& prompt,
                        const std::string& default_val = "",
                        bool               secret = false) {
    std::cout << "  " << bold(prompt);

    if (!secret && !default_val.empty())
        std::cout << gray(" [" + default_val + "]");
    // For secret fields: never echo the existing value in the prompt
    if (secret && !default_val.empty())
        std::cout << gray(" [****]");   // indicate that a value exists

    std::cout << ": ";
    std::cout.flush();

    std::string line = trim(read_line(!secret ? true : false));
    return line.empty() ? default_val : line;
}

// -----------------------------------------------------------------------------
// ask_bool()
// -----------------------------------------------------------------------------
static bool ask_bool(const std::string& prompt, bool default_yes = true) {
    std::string hint_str = default_yes ? "Y/n" : "y/N";
    std::cout << "  " << bold(prompt) << gray(" [" + hint_str + "]") << ": ";
    std::cout.flush();
    std::string line = trim(read_line());
    if (line.empty()) return default_yes;
    return std::tolower((unsigned char)line[0]) == 'y';
}

// -----------------------------------------------------------------------------
// ask_int()
// -----------------------------------------------------------------------------
static int ask_int(const std::string& prompt, int default_val,
                    int min_val = 0, int max_val = 9999) {
    while (true) {
        std::string s = ask(prompt, std::to_string(default_val));
        try {
            int v = std::stoi(s);
            if (v >= min_val && v <= max_val) return v;
            std::cout << red("  ! Must be between " + std::to_string(min_val) +
                             " and " + std::to_string(max_val)) << "\n";
        } catch (...) {
            std::cout << red("  ! Please enter a whole number") << "\n";
        }
    }
}

// -----------------------------------------------------------------------------
// ask_float()   --  returns -1.0 if user types 'skip'/'-'/'(omit)'/empty-when-default<0
// -----------------------------------------------------------------------------
static double ask_float(const std::string& prompt, double default_val,
                         double min_val, double max_val) {
    std::string def_str = (default_val < 0) ? "(omit)"
                        : std::to_string(default_val).substr(0, 4);
    while (true) {
        std::string s = ask(prompt, def_str);
        if (s == "(omit)" || s == "-" || s == "skip" || s == "") return -1.0;
        try {
            double v = std::stod(s);
            // Allow -1 as explicit "omit" entry
            if (v == -1.0) return -1.0;
            if (v >= min_val && v <= max_val) return v;
            std::cout << red("  ! Must be between " + std::to_string(min_val) +
                             " and " + std::to_string(max_val) +
                             " (or 'skip' to omit)") << "\n";
        } catch (...) {
            std::cout << red("  ! Please enter a number (or 'skip' to omit)") << "\n";
        }
    }
}

// -----------------------------------------------------------------------------
// ask_choice()   --  numbered menu
// -----------------------------------------------------------------------------
static int ask_choice(const std::string& prompt,
                       const std::vector<std::string>& options,
                       int default_idx = 0) {
    for (int i = 0; i < (int)options.size(); ++i) {
        bool def = (i == default_idx);
        std::cout << "  " << (def ? green("*") : " ")
                  << " " << bold(std::to_string(i + 1)) << ". "
                  << options[i] << "\n";
    }
    while (true) {
        std::string s = ask(prompt, std::to_string(default_idx + 1));
        try {
            int v = std::stoi(s) - 1;
            if (v >= 0 && v < (int)options.size()) return v;
        } catch (...) {}
        std::cout << red("  ! Enter a number between 1 and " +
                         std::to_string(options.size())) << "\n";
    }
}

// -----------------------------------------------------------------------------
// Known model lists per provider
// -----------------------------------------------------------------------------
static const std::map<agent::Provider, std::vector<std::string>> kKnownModels = {
    { agent::Provider::Anthropic, {
        "claude-opus-4-5-20251101",
        "claude-sonnet-4-5-20251015",
        "claude-haiku-4-5-20251001",
        "claude-opus-4-20250514",
        "claude-sonnet-4-20250514",
        "(custom)"
    }},
    { agent::Provider::OpenAI, {
        "gpt-4o",
        "gpt-4o-mini",
        "gpt-4-turbo",
        "gpt-3.5-turbo",
        "o1",
        "o1-mini",
        "(custom)"
    }},
    { agent::Provider::Azure,  { "(custom)" }},
    { agent::Provider::Ollama, {
        "llama3.3", "llama3.2", "mistral",
        "gemma2", "qwen2.5", "deepseek-r1",
        "(custom)"
    }},
    { agent::Provider::Custom, { "(custom)" }},
};

static std::string pick_model(agent::Provider provider,
                               const std::string& role_label,
                               const std::string& current_model) {
    auto it = kKnownModels.find(provider);
    if (it == kKnownModels.end())
        return ask("Model ID", current_model);

    const auto& models = it->second;

    // Find current model in list (default to last = custom)
    int def_idx = (int)models.size() - 1;
    for (int i = 0; i < (int)models.size() - 1; ++i)
        if (models[i] == current_model) { def_idx = i; break; }

    std::cout << "\n  " << bold(role_label + " model") << ":\n";
    int choice = ask_choice("Select", models, def_idx);

    if (models[choice] == "(custom)")
        return ask("  Custom model ID", current_model);

    return models[choice];
}

// -----------------------------------------------------------------------------
// Per-layer model spec
// -----------------------------------------------------------------------------
static agent::ModelSpec ask_model_spec(const std::string& layer_name,
                                        agent::Provider    provider,
                                        const std::string& default_model,
                                        int                default_max_tokens) {
    agent::ModelSpec spec;
    std::cout << "\n  " << bold(layer_name)
              << gray(" -- press Enter/No to use the default model") << "\n";

    if (!ask_bool("Override model for " + layer_name + "?", false))
        return spec;

    spec.model = pick_model(provider, layer_name, default_model);

    hint("0=deterministic  0.7=balanced  1+=creative  skip=provider default");
    spec.temperature = ask_float("Temperature", -1.0, 0.0, 2.0);
    if (spec.temperature < 0) spec.temperature = -1.0;

    spec.max_tokens = ask_int("Max tokens", default_max_tokens, 64, 128000);
    return spec;
}

// -----------------------------------------------------------------------------
// Main wizard entry point
// -----------------------------------------------------------------------------
agent::AgentConfig run_wizard(const std::string& existing_config_path) {
    init_console();

    agent::AgentConfig cfg;
    try {
        if (!existing_config_path.empty())
            cfg = agent::AgentConfig::load(existing_config_path);
    } catch (...) { /* start fresh */ }

    constexpr int kSteps = 9;
    print_banner();

    // == Step 1: Provider ================================================
    print_step(1, kSteps, "API Provider");
    std::vector<std::string> providers = {
        "Anthropic    (claude-*  api.anthropic.com)",
        "OpenAI       (gpt-* / o1-*  api.openai.com)",
        "Azure OpenAI (your deployment endpoint)",
        "Ollama       (local  localhost:11434)",
        "Custom       (any OpenAI-compatible endpoint)",
    };
    cfg.provider = static_cast<agent::Provider>(
        ask_choice("Provider", providers, (int)cfg.provider));
    std::cout << green("  OK  Provider set\n");

    // == Step 2: Connection ===============================================
    print_step(2, kSteps, "Connection & Authentication");

    switch (cfg.provider) {
        case agent::Provider::Anthropic:
            hint("Get your key at: https://console.anthropic.com/");
            cfg.api_key = ask("Anthropic API key", cfg.api_key, /*secret=*/true);
            cfg.api_version = ask("anthropic-version header",
                cfg.api_version.empty() ? "2023-06-01" : cfg.api_version);
            if (ask_bool("Use a custom / proxy endpoint?", false))
                cfg.base_url = ask("Base URL",
                    cfg.base_url.empty() ? "https://api.anthropic.com" : cfg.base_url);
            break;

        case agent::Provider::OpenAI:
            hint("Get your key at: https://platform.openai.com/api-keys");
            cfg.api_key      = ask("OpenAI API key", cfg.api_key, true);
            cfg.organization = ask("Organization ID (Enter to skip)", cfg.organization);
            if (ask_bool("Use a custom / proxy endpoint?", false))
                cfg.base_url = ask("Base URL",
                    cfg.base_url.empty() ? "https://api.openai.com" : cfg.base_url);
            break;

        case agent::Provider::Azure:
            hint("URL format: https://{resource}.openai.azure.com/openai/deployments/{name}");
            cfg.base_url    = ask("Deployment URL", cfg.base_url);
            cfg.api_key     = ask("Azure API key", cfg.api_key, true);
            cfg.api_version = ask("API version",
                cfg.api_version.empty() ? "2024-02-01" : cfg.api_version);
            break;

        case agent::Provider::Ollama:
            hint("Make sure Ollama is running:  ollama serve");
            cfg.base_url = ask("Ollama base URL",
                cfg.base_url.empty() ? "http://localhost:11434" : cfg.base_url);
            if (ask_bool("Ollama requires an API key?", false))
                cfg.api_key = ask("API key", cfg.api_key, true);
            break;

        case agent::Provider::Custom:
            hint("Endpoint must support: POST /v1/chat/completions");
            cfg.base_url = ask("Base URL", cfg.base_url);
            hint("Leave empty if no authentication is needed");
            cfg.api_key  = ask("API key", cfg.api_key, true);
            break;
    }
    std::cout << green("  OK  Connection configured\n");

    // == Step 3: Default model ============================================
    print_step(3, kSteps, "Default Model");
    hint("All layers use this model unless overridden in Step 4.");
    cfg.default_model = pick_model(cfg.provider, "Default", cfg.default_model);
    std::cout << green("  OK  Default model: ") << bold(cfg.default_model) << "\n";

    // == Step 4: Per-layer overrides ======================================
    print_step(4, kSteps, "Per-Layer Model Overrides");
    hint("Director (Layer 1): goal decomposition & review  -> strongest model");
    hint("Manager  (Layer 2): task planning & validation   -> balanced model");
    hint("Worker   (Layer 3): execute many atomic tasks    -> fastest/cheapest");
    cfg.director_model = ask_model_spec("Director", cfg.provider,
                                         cfg.default_model, cfg.max_tokens);
    cfg.manager_model  = ask_model_spec("Manager",  cfg.provider,
                                         cfg.default_model, cfg.max_tokens);
    cfg.worker_model   = ask_model_spec("Worker",   cfg.provider,
                                         cfg.default_model, cfg.max_tokens);
    std::cout << green("  OK  Per-layer models configured\n");

    // == Step 5: Generation params ========================================
    print_step(5, kSteps, "Generation Parameters (Global Defaults)");
    cfg.max_tokens  = ask_int("Default max tokens", cfg.max_tokens, 64, 128000);
    hint("0=deterministic  0.7=balanced  1+=creative  skip=provider default");
    cfg.temperature = ask_float("Default temperature", cfg.temperature, 0.0, 2.0);
    if (cfg.temperature < 0) cfg.temperature = -1.0;
    hint("Nucleus sampling 0.0-1.0  skip=provider default");
    cfg.top_p       = ask_float("Default top_p", cfg.top_p, 0.0, 1.0);
    if (cfg.top_p < 0) cfg.top_p = -1.0;
    std::cout << green("  OK  Generation parameters set\n");

    // == Step 6: Agent behaviour ==========================================
    print_step(6, kSteps, "Agent Behaviour");
    cfg.max_network_retries = ask_int("Max HTTP retries",          cfg.max_network_retries, 0, 10);
    cfg.max_subtask_retries = ask_int("Max subtask re-issues",     cfg.max_subtask_retries, 0, 5);
    cfg.max_atomic_retries  = ask_int("Max atomic task retries",   cfg.max_atomic_retries,  0, 10);
    cfg.worker_threads      = ask_int("Worker threads",            cfg.worker_threads,      1, 64);
    cfg.request_timeout     = ask_int("Request timeout (seconds)", cfg.request_timeout,     10, 600);
    cfg.connect_timeout     = ask_int("Connect timeout (seconds)", cfg.connect_timeout,     1, 60);
    std::cout << green("  OK  Agent behaviour configured\n");

    // == Step 7: Logging & paths ==========================================
    print_step(7, kSteps, "Logging & Paths");
    std::vector<std::string> log_opts = {"debug", "info", "warn", "error"};
    int log_def = 1;
    for (int i = 0; i < (int)log_opts.size(); ++i)
        if (log_opts[i] == cfg.log_level) { log_def = i; break; }
    std::cout << "  " << bold("Log level") << ":\n";
    cfg.log_level  = log_opts[ask_choice("Select", log_opts, log_def)];
    cfg.prompt_dir = ask("Prompt directory", cfg.prompt_dir);
    std::cout << "  Prompt structure:\n"
              << "    prompts/base.md          — shared safety rules (all agents)\n"
              << "    prompts/<agent>/SOUL.md  — agent identity & style\n"
              << "    prompts/<agent>/skills/  — agent-specific skills\n"
              << "    prompts/skills/          — cross-agent skills (file_ops, system_ops...)\n"
              << "    prompts/AGENTS.md        — project context (copy to working dir)\n"
              << "  Use --list-prompts to see all loaded prompts.\n"
              << "  Use --list-skills  to see available cross-agent skills.\n";
    std::cout << green("  OK  Logging configured\n");

    // == Step 8: Workspace & Memory ==
    print_step(8, kSteps, "Workspace & Memory");
    hint("Workspace stores run logs, state snapshots and agent artifacts.");
    cfg.workspace_dir = ask("Workspace root directory", cfg.workspace_dir);
    hint("Short-term window: recent messages kept in Worker context.");
    cfg.memory_short_term_window = ask_int("Short-term memory window", cfg.memory_short_term_window, 1, 32);
    cfg.memory_session_enabled   = ask_bool("Enable session memory (persists across turns)?", cfg.memory_session_enabled);
    cfg.memory_long_term_enabled = ask_bool("Enable long-term memory (LLM summarization)?",  cfg.memory_long_term_enabled);
    std::cout << green("  OK  Workspace & memory configured\n");

    // == Step 9: Supervisor & Advisor ==
    print_step(9, kSteps, "Supervisor & Advisor");
    hint("Supervisor monitors all agents and intervenes when problems occur.");
    cfg.supervisor_advisor_enabled = ask_bool("Enable Advisor Agent (LLM root-cause analysis)?", cfg.supervisor_advisor_enabled);
    cfg.supervisor_max_retries     = ask_int("Supervisor max retries (0-5)", cfg.supervisor_max_retries, 0, 5);
    hint("Seconds of inactivity before Supervisor sends correction to stuck agent.");
    cfg.supervisor_stuck_timeout_ms = ask_int("Stuck timeout (seconds)", cfg.supervisor_stuck_timeout_ms / 1000, 30, 3600) * 1000;
    cfg.supervisor_max_fail_count   = ask_int("Max failures before intervention", cfg.supervisor_max_fail_count, 1, 20);
    cfg.supervisor_poll_interval_ms = ask_int("Monitor poll interval (ms)", cfg.supervisor_poll_interval_ms, 500, 30000);
    std::cout << green("  OK  Supervisor & advisor configured\n");

        // == Preview ==========================================================
    print_header("Configuration Preview");
    auto mask_key = [](const std::string& k) -> std::string {
        if (k.empty()) return "(empty)";
        if (k.size() <= 8) return std::string(k.size(), '*');
        return k.substr(0, 4) + "..." + k.substr(k.size() - 4);
    };
    nlohmann::json preview;
    agent::to_json(preview, cfg);
    if (!cfg.api_key.empty())
        preview["api_key"] = mask_key(cfg.api_key);
    std::cout << preview.dump(2) << "\n";

    return cfg;
}

} // namespace setup
