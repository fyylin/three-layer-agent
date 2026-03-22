// =============================================================================
#include "utils/utf8_fstream.hpp"
// src/agent/director_agent.cpp   --   v2: AgentContext wired in
// =============================================================================
#include "agent/director_agent.hpp"
#include "agent/global_summary.hpp"
#include <cstring>
#include "utils/task_rules.hpp"
#ifdef _WIN32
#  include <windows.h>
#endif
#include "agent/manager_pool.hpp"
#include "agent/exceptions.hpp"
#include "utils/logger.hpp"
#include "utils/json_utils.hpp"
#include <nlohmann/json.hpp>
#include <atomic>
#include <cctype>
#include <future>
#include <optional>
#include <unordered_map>
#include <sstream>
#include <thread>
#include <chrono>
#include <cstdio>
#include <ctime>

namespace agent {

static const char* kLayer = "Director";

// Build workspace + CWD context string for LLM prompt injection
// Called by decompose_goal() and synthesise() to avoid code duplication
static std::string build_ws_context(const WorkspacePaths& ws) {
    std::string info;
    if (!ws.current_dir.empty())
        info = "Workspace: " + ws.current_dir;
    else if (!ws.run_root.empty())
        info = "Working dir: " + ws.run_root;
    if (!ws.files_dir.empty())
        info += "  |  Files: " + ws.files_dir;
#ifdef _WIN32
    {
        WCHAR wcwd[MAX_PATH] = {};
        if (GetCurrentDirectoryW(MAX_PATH, wcwd)) {
            int n = WideCharToMultiByte(CP_UTF8, 0, wcwd, -1, nullptr, 0, nullptr, nullptr);
            if (n > 0) {
                std::string cwd(n, '\0');
                WideCharToMultiByte(CP_UTF8, 0, wcwd, -1, &cwd[0], n, nullptr, nullptr);
                while (!cwd.empty() && cwd.back() == '\0') cwd.pop_back();
                if (!info.empty()) info += "  |  ";
                info += "CWD: " + cwd;
            }
        }
    }
#else
    {
        const char* cwd = std::getenv("PWD");
        if (cwd && *cwd) {
            if (!info.empty()) info += "  |  ";
            info += "CWD: " + std::string(cwd);
        }
    }
#endif
    return info;
}

static std::string iso_now() {
    using namespace std::chrono;
    auto now = system_clock::now();
    auto tt  = system_clock::to_time_t(now);
    auto ms  = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
    std::tm tm_buf{};
#ifdef _WIN32
    gmtime_s(&tm_buf, &tt);
#else
    gmtime_r(&tt, &tm_buf);
#endif
    char buf[40];
    std::snprintf(buf,sizeof(buf),"%04d-%02d-%02dT%02d:%02d:%02d.%03ldZ",
        tm_buf.tm_year+1900,tm_buf.tm_mon+1,tm_buf.tm_mday,
        tm_buf.tm_hour,tm_buf.tm_min,tm_buf.tm_sec,(long)ms.count());
    return buf;
}

DirectorAgent::DirectorAgent(std::string    id,
                             ApiClient&     client,
                             ThreadPool&    pool,
                             ManagerFactory factory,
                             std::string    decompose_prompt,
                             std::string    review_prompt,
                             std::string    synthesise_prompt,
                             std::string    classify_prompt,
                             int            max_subtask_retries,
                             AgentContext   ctx)
    : id_(std::move(id))
    , client_(client)
    , pool_(pool)
    , factory_(std::move(factory))
    , mgr_pool_(factory_, 4)
    , decompose_prompt_(std::move(decompose_prompt))
    , review_prompt_(std::move(review_prompt))
    , synthesise_prompt_(std::move(synthesise_prompt))
    , classify_prompt_(std::move(classify_prompt))
    , max_subtask_retries_(max_subtask_retries)
    , ctx_(std::move(ctx))
{
    if (ctx_.state)
        ctx_.state->transition(AgentState::Idle);
    ctx_.log_info("", EvType::AgentCreated, "Director " + id_ + " created");
}

namespace {

std::string trim_copy(std::string s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

std::string extract_current_request(const std::string& desc, size_t max_len = 0) {
    static const char* REQ_MARKER = "[Current request:]";
    std::string req = desc;
    auto mpos = desc.find(REQ_MARKER);
    if (mpos != std::string::npos) {
        mpos += std::strlen(REQ_MARKER);
        while (mpos < desc.size() &&
               (desc[mpos] == '\n' || desc[mpos] == '\r' || desc[mpos] == ' ')) ++mpos;
        req = desc.substr(mpos);
    }
    if (max_len > 0 && req.size() > max_len) req.resize(max_len);
    return req;
}

std::string ascii_upper_copy(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s)
        out.push_back(static_cast<char>(std::toupper(c)));
    return out;
}

bool has_label_token(const std::string& text, const char* label) {
    size_t pos = text.find(label);
    while (pos != std::string::npos) {
        bool left_ok = (pos == 0) || !std::isalnum(static_cast<unsigned char>(text[pos - 1]));
        size_t end = pos + std::strlen(label);
        bool right_ok = (end >= text.size()) || !std::isalnum(static_cast<unsigned char>(text[end]));
        if (left_ok && right_ok) return true;
        pos = text.find(label, pos + 1);
    }
    return false;
}

bool looks_like_real_path(const std::string& text) {
    for (size_t i = 0; i + 2 < text.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        if (((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) &&
            text[i + 1] == ':' &&
            (text[i + 2] == '\\' || text[i + 2] == '/')) {
            return true;
        }
    }

    static const char* kPathHints[] = {
        "./", "../", "~/", "/tmp/", "/var/", "/home/", "\\\\", nullptr
    };
    for (int i = 0; kPathHints[i]; ++i)
        if (text.find(kPathHints[i]) != std::string::npos) return true;
    return false;
}

bool contains_ascii_keyword(const std::string& text, const char* keyword) {
    return ascii_upper_copy(text).find(ascii_upper_copy(keyword)) != std::string::npos;
}

bool is_creation_subtask(const std::string& text) {
    return contains_ascii_keyword(text, "write_file") ||
           contains_ascii_keyword(text, "create") ||
           contains_ascii_keyword(text, "write ") ||
           text.find(u8"创建") != std::string::npos ||
           text.find(u8"写入") != std::string::npos;
}

bool is_followup_validation_subtask(const std::string& text) {
    return contains_ascii_keyword(text, "verify") ||
           contains_ascii_keyword(text, "validation") ||
           contains_ascii_keyword(text, "check") ||
           contains_ascii_keyword(text, "accessible") ||
           contains_ascii_keyword(text, "exists") ||
           contains_ascii_keyword(text, "syntax") ||
           contains_ascii_keyword(text, "stat_file") ||
           contains_ascii_keyword(text, "list_dir") ||
           text.find(u8"验证") != std::string::npos ||
           text.find(u8"检查") != std::string::npos;
}

bool is_file_creation_request(const std::string& text) {
    return looks_like_real_path(text) && is_creation_subtask(text);
}

std::string extract_real_path_snippet(const std::string& text) {
    for (char quote : std::string{"\"'`"}) {
        size_t pos = 0;
        while ((pos = text.find(quote, pos)) != std::string::npos) {
            size_t end = text.find(quote, pos + 1);
            if (end == std::string::npos) break;
            std::string candidate = trim_copy(text.substr(pos + 1, end - pos - 1));
            if (looks_like_real_path(candidate)) return candidate;
            pos = end + 1;
        }
    }

    for (size_t i = 0; i + 2 < text.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        if ((((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))) &&
            text[i + 1] == ':' &&
            (text[i + 2] == '\\' || text[i + 2] == '/')) {
            size_t end = i + 3;
            while (end < text.size()) {
                char ch = text[end];
                if (ch == '\r' || ch == '\n' || ch == '"' || ch == '\'' || ch == '`') break;
                ++end;
            }
            std::string candidate = trim_copy(text.substr(i, end - i));
            while (!candidate.empty()) {
                char tail = candidate.back();
                if (tail == '.' || tail == ',' || tail == ';') candidate.pop_back();
                else break;
            }
            if (looks_like_real_path(candidate)) return candidate;
        }
    }
    return "";
}

void renumber_subtasks(std::vector<SubTask>& tasks) {
    for (size_t i = 0; i < tasks.size(); ++i)
        tasks[i].id = "subtask-" + std::to_string(i + 1);
}

std::vector<SubTask> compact_file_creation_subtasks(
        const std::string& request,
        std::vector<SubTask> tasks) {
    if (!is_file_creation_request(request) || tasks.size() <= 2) return tasks;

    std::optional<SubTask> creation;
    std::optional<SubTask> validation;
    std::string request_path = extract_real_path_snippet(request);

    for (const auto& task : tasks) {
        if (!creation && is_creation_subtask(task.description))
            creation = task;
        if (!validation &&
            is_followup_validation_subtask(task.description) &&
            looks_like_real_path(task.description)) {
            validation = task;
        }
    }

    if (!creation) return tasks;

    if (!request_path.empty() && !looks_like_real_path(creation->description))
        creation->description += " Target path: " + request_path;

    std::vector<SubTask> compacted;
    compacted.push_back(*creation);

    if (validation.has_value()) {
        compacted.push_back(*validation);
    } else {
        std::string verify_path = request_path.empty()
            ? extract_real_path_snippet(creation->description)
            : request_path;
        SubTask verify;
        verify.description = verify_path.empty()
            ? "Verify the created file exists at the requested path and perform a basic syntax or startup check if possible."
            : "Verify the created file exists at " + verify_path +
              " and perform a basic syntax or startup check if possible.";
        verify.expected_output = verify_path.empty()
            ? "Tool evidence confirms the file exists and passes a lightweight validity check, or reports a concrete limitation."
            : "Tool evidence confirms " + verify_path +
              " exists and passes a lightweight validity check, or reports a concrete limitation.";
        compacted.push_back(std::move(verify));
    }

    renumber_subtasks(compacted);
    return compacted;
}

bool should_dispatch_sequentially(const std::vector<SubTask>& tasks) {
    bool has_creation = false;
    bool has_validation = false;
    for (const auto& task : tasks) {
        if (looks_like_real_path(task.description)) {
            if (is_creation_subtask(task.description)) has_creation = true;
            if (is_followup_validation_subtask(task.description)) has_validation = true;
        }
    }
    return has_creation && has_validation;
}

std::optional<TaskComplexity> parse_complexity_label(const std::string& raw) {
    std::string candidate = trim_copy(raw);
    if (candidate.empty()) return std::nullopt;

    try {
        auto j = nlohmann::json::parse(candidate);
        if (j.is_string()) {
            candidate = j.get<std::string>();
        } else if (j.is_object()) {
            for (const char* key : {"label", "complexity", "route"}) {
                if (j.contains(key) && j.at(key).is_string()) {
                    candidate = j.at(key).get<std::string>();
                    break;
                }
            }
        }
    } catch (...) {
        // Plain-text labels are expected; ignore parse failures.
    }

    candidate = ascii_upper_copy(trim_copy(candidate));
    struct LabelMap { const char* label; TaskComplexity value; };
    static const LabelMap kLabels[] = {
        {"L0", TaskComplexity::L0_Conversational},
        {"L1", TaskComplexity::L1_SingleTool},
        {"L2", TaskComplexity::L2_SingleSubtask},
        {"L3", TaskComplexity::L3_Parallel},
        {"L4", TaskComplexity::L4_Complex},
    };
    for (const auto& item : kLabels)
        if (has_label_token(candidate, item.label)) return item.value;
    return std::nullopt;
}

const char* complexity_to_cstr(TaskComplexity complexity) {
    switch (complexity) {
        case TaskComplexity::L0_Conversational: return "L0";
        case TaskComplexity::L1_SingleTool:     return "L1";
        case TaskComplexity::L2_SingleSubtask:  return "L2";
        case TaskComplexity::L3_Parallel:       return "L3";
        case TaskComplexity::L4_Complex:        return "L4";
    }
    return "L3";
}

struct ApiClientConfigScope {
    ApiClient& client;
    ApiConfig  saved;

    explicit ApiClientConfigScope(ApiClient& c)
        : client(c), saved(c.config()) {}

    ~ApiClientConfigScope() {
        client.reconfigure(saved);
    }
};

} // namespace


// -- assess_complexity --------------------------------------------------------
// Heuristic fallback. LLM classification is preferred; this remains the safety net.

TaskComplexity DirectorAgent::assess_complexity(
        const std::string& desc) noexcept {

    // ── Extract [Current request:] if goal is wrapped with conversation history ──
    std::string req = extract_current_request(desc, 200);

    // Count Unicode code points
    size_t nchars = 0;
    for (unsigned char c : req) if ((c & 0xC0) != 0x80) ++nchars;

    // Lower-case ASCII copy for English keyword matching
    std::string d;
    d.reserve(req.size());
    for (unsigned char c : req) d.push_back((char)std::tolower(c));

    // Any concrete filesystem path is an operational request, not a chat-only turn.
    if (looks_like_real_path(req))
        return TaskComplexity::L3_Parallel;

    // ══════════════════════════════════════════════════════
    // L0: PURE CONVERSATIONAL — no tools needed at all
    // Rule: ONLY when user is greeting OR asking a knowledge
    //       question with no action intent whatsoever.
    //       When in doubt → L3 (tool pipeline).
    //       Better to over-use tools than to fail silently.
    // ══════════════════════════════════════════════════════

    // Very short (≤3 chars): greetings/ack only
    if (nchars <= 3) return TaskComplexity::L0_Conversational;

    // English: explicit knowledge/explanation with NO action verb
    // Must start with these patterns (not just contain them)
    static const char* L0_START_EN[] = {
        "what is ", "what are ", "what's ", "whats ",
        "explain ", "define ", "describe what",
        "how does ", "how do ", "why does ", "why is ",
        "difference between", "compare ", "which is better",
        "tell me about", "can you explain", "help me understand",
        nullptr
    };
    for (int i = 0; L0_START_EN[i]; ++i)
        if (d.rfind(L0_START_EN[i], 0) == 0)   // must START with pattern
            return TaskComplexity::L0_Conversational;

    // English greetings (must be the whole request or start of it)
    static const char* L0_GREET_EN[] = {
        "hello", "hi there", "good morning", "good evening",
        "good afternoon", "how are you", "nice to meet", "hey",
        nullptr
    };
    for (int i = 0; L0_GREET_EN[i]; ++i) {
        size_t pos = d.find(L0_GREET_EN[i]);
        if (pos == 0 || (pos != std::string::npos && pos + std::strlen(L0_GREET_EN[i]) >= nchars))
            return TaskComplexity::L0_Conversational;
    }

    // ── TEXT-EDIT INTENT: "改为/替换/换成" → L0 (conversational) ──────────
    // Prevents: "把命令中的/改为\" from being executed as a shell command.
    // Rule: if request is short (<30 chars) AND contains a text-edit verb → L0
    static const char* CN_EDIT_VERBS[] = {
        "æ¹ä¸º",   // 改为
        "æ¿æ¢",   // 替换
        "æ¢æ",   // 换成
        "ä¿®æ¹æ",  // 修改成
        "æ¹æ",   // 改成
        nullptr
    };
    if (nchars < 40) {
        for (int i = 0; CN_EDIT_VERBS[i]; ++i)
            if (req.find(CN_EDIT_VERBS[i]) != std::string::npos)
                return TaskComplexity::L0_Conversational;
    }

    // Chinese: pure greetings (≤ 6 chars AND starts with 你好/嗨/hey)
    // Chinese action+verb requests (查/看/列/找/读/写/运行) → always L3
    static const char* CN_ACTION_VERBS[] = {
        "\xe6\x9f\xa5",  // 查
        "\xe7\x9c\x8b",  // 看
        "\xe5\x88\x97",  // 列
        "\xe6\x89\xbe",  // 找
        "\xe8\xaf\xbb",  // 读
        "\xe5\x86\x99",  // 写
        "\xe6\x89\x93",  // 打(开)
        "\xe8\xbf\x90",  // 运(行)
        "\xe6\x89\xa7",  // 执(行)
        "\xe5\x88\x9b",  // 创(建)
        "\xe6\x96\xb0\xe5\xbb\xba",  // 新建
        "\xe5\x88\xa0",  // 删(除)
        "\xe7\xa7\xbb",  // 移(动)
        "\xe5\xa4\x8d\xe5\x88\xb6",  // 复制
        "\xe4\xb8\x8b\xe8\xbd\xbd",  // 下载
        "\xe5\xae\x89\xe8\xa3\x85",  // 安装
        "\xe7\xbc\x96\xe8\xaf\x91",  // 编译
        "\xe5\x90\xaf\xe5\x8a\xa8",  // 启动
        "\xe5\x81\x9c\xe6\xad\xa2",  // 停止
        "\xe8\x8e\xb7\xe5\x8f\x96",  // 获取
        "\xe6\x98\xbe\xe7\xa4\xba",  // 显示
        "\xe6\x89\x93\xe5\xbc\x80",  // 打开
        nullptr
    };
    for (int i = 0; CN_ACTION_VERBS[i]; ++i)
        if (req.find(CN_ACTION_VERBS[i]) != std::string::npos)
            return TaskComplexity::L3_Parallel;

    // Chinese pure greetings (≤5 chars, no action verbs above)
    if (nchars <= 5) {
        static const char* CN_GREET[] = {
            "\xe4\xbd\xa0\xe5\xa5\xbd",   // 你好
            "\xe5\x97\xa8",               // 嗨
            "\xe6\x99\xa9\xe4\xb8\x8a\xe5\xa5\xbd",  // 晚上好
            "\xe6\x97\xa9\xe4\xb8\x8a\xe5\xa5\xbd",  // 早上好
            nullptr
        };
        for (int i = 0; CN_GREET[i]; ++i)
            if (req.find(CN_GREET[i]) != std::string::npos)
                return TaskComplexity::L0_Conversational;
    }

    // Chinese knowledge questions (must start with 什么是/怎么/为什么/如何)
    // These are pure Q&A with no action intent
    // CRITICAL: Only match if NO action verbs found above
    static const char* CN_KNOWLEDGE[] = {
        "\xe4\xbb\x80\xe4\xb9\x88\xe6\x98\xaf",              // 什么是
        "\xe6\x80\x8e\xe4\xb9\x88\xe7\x90\x86\xe8\xa7\xa3",  // 怎么理解
        "\xe4\xb8\xba\xe4\xbb\x80\xe4\xb9\x88",              // 为什么
        "\xe8\xa7\xa3\xe9\x87\x8a\xe4\xb8\x80\xe4\xb8\x8b",  // 解释一下
        "\xe4\xbb\x8b\xe7\xbb\x8d\xe4\xb8\x80\xe4\xb8\x8b",  // 介绍一下
        nullptr
    };
    for (int i = 0; CN_KNOWLEDGE[i]; ++i)
        if (req.rfind(CN_KNOWLEDGE[i], 3) != std::string::npos)  // within first 3 bytes = starts with
            return TaskComplexity::L0_Conversational;

    // ══════════════════════════════════════════════════════
    // L1: single-tool operations — skip Manager/review overhead
    // These are direct tool calls with no ambiguity.
    // Pattern: action verb + direct object (no compound structure)
    // ══════════════════════════════════════════════════════

    // English single-tool patterns (must match closely)
    static const char* L1_EN[] = {
        "list ", "ls ", "dir ", "pwd", "show files", "list files",
        "current dir", "current directory", "what directory",
        "system info", "sysinfo", "get sysinfo",
        "process list", "running processes", "list processes",
        "get env", "environment variable",
        nullptr
    };
    for (int i = 0; L1_EN[i]; ++i)
        if (d.find(L1_EN[i]) != std::string::npos &&
            d.size() < 60)  // short request = single tool, long = multi-step
            return TaskComplexity::L1_SingleTool;

    // Chinese single-tool patterns (action verb already sent to L3 above,
    // but catch the most common patterns for direct tool routing)
    // 当前目录 / 上级目录 / 系统信息 / 进程列表
    struct { const char* utf8; const char* tool; } CN_L1[] = {
        {"\xe5\xbd\x93\xe5\x89\x8d\xe7\x9b\xae\xe5\xbd\x95", "get_current_dir"},  // 当前目录
        {"\xe4\xb8\x8a\xe7\xba\xa7\xe7\x9b\xae\xe5\xbd\x95", "list_dir .."},       // 上级目录
        {"\xe7\xb3\xbb\xe7\xbb\x9f\xe4\xbf\xa1\xe6\x81\xaf", "get_sysinfo"},       // 系统信息
        {"\xe8\xbf\x9b\xe7\xa8\x8b\xe5\x88\x97\xe8\xa1\xa8", "get_process_list"},  // 进程列表
        {"\xe5\xbd\x93\xe5\x89\x8d\xe8\xb7\xaf\xe5\xbe\x84", "get_current_dir"},   // 当前路径
        {nullptr, nullptr}
    };
    for (int i = 0; CN_L1[i].utf8; ++i)
        if (req.find(CN_L1[i].utf8) != std::string::npos && nchars < 15)
            return TaskComplexity::L1_SingleTool;

    // ══════════════════════════════════════════════════════
    // L4: explicitly complex multi-step (refactor/migrate/build)
    // ══════════════════════════════════════════════════════
    static const char* L4_KW[] = {
        "refactor", "migrate", "rewrite", "implement a ",
        "build a ", "create a complete", "design a ", "architect ",
        nullptr
    };
    for (int i = 0; L4_KW[i]; ++i)
        if (d.find(L4_KW[i]) != std::string::npos)
            return TaskComplexity::L4_Complex;

    // ══════════════════════════════════════════════════════
    // DEFAULT: L3 standard pipeline
    // Every action/execution/file/system request goes here.
    // Director LLM will figure out what tools to use.
    // ══════════════════════════════════════════════════════
    return TaskComplexity::L3_Parallel;
}

TaskComplexity DirectorAgent::classify_complexity(
        const std::string& description) noexcept {
    TaskComplexity heuristic = assess_complexity(description);
    if (classify_prompt_.empty())
        return heuristic;

    std::string request = trim_copy(extract_current_request(description, 400));
    if (request.empty())
        return heuristic;

    try {
        std::string prompt = classify_prompt_;
        if (ctx_.prompt_opt) {
            std::string opt = ctx_.prompt_opt->select_best("director-classify");
            if (!opt.empty()) prompt = opt;
        }
        if (prompt.empty())
            return heuristic;

        ApiClientConfigScope guard(client_);
        auto cfg = guard.saved;
        if (cfg.max_tokens <= 0 || cfg.max_tokens > 32) cfg.max_tokens = 32;
        cfg.temperature = 0.0;
        cfg.top_p = -1.0;
        client_.reconfigure(cfg);

        ctx_.log_info("classify", EvType::LlmCall, "complexity classify");
        if (ctx_.state) ctx_.state->record_call();
        std::string llm_out = client_.complete(prompt, request, "classify");
        auto parsed = parse_complexity_label(llm_out);
        if (!parsed) {
            LOG_WARN(kLayer, id_, "classify",
                "unrecognised classification output: " + trim_copy(llm_out));
            return heuristic;
        }

        TaskComplexity classified = *parsed;
        if (classified == TaskComplexity::L0_Conversational &&
            heuristic != TaskComplexity::L0_Conversational) {
            LOG_WARN(kLayer, id_, "classify",
                "classifier suggested L0 but heuristic detected actionable intent; using " +
                std::string(complexity_to_cstr(heuristic)));
            return heuristic;
        }

        LOG_INFO(kLayer, id_, "classify",
            "llm=" + std::string(complexity_to_cstr(classified)) +
            ", heuristic=" + std::string(complexity_to_cstr(heuristic)));
        return classified;
    } catch (const std::exception& e) {
        LOG_WARN(kLayer, id_, "classify",
            std::string("classification failed, falling back to heuristic: ") + e.what());
        return heuristic;
    }
}

// -- decompose_goal ------------------------------------------------------------


FinalResult DirectorAgent::run(const UserGoal& goal) noexcept {
    FinalResult result;
    const std::string run_id = make_run_id();

    result.started_at = iso_now();
    client_.reset_usage();

    // Extract context and update global summary
    if (goal.description.find("桌面") != std::string::npos ||
        goal.description.find("Desktop") != std::string::npos) {
        conv_ctx_.add("location", "Desktop");
        if (ctx_.global_summary) {
            ctx_.global_summary->set("current_location", "Desktop", 2);
        }
    }
    if (goal.description.find("这个文件") != std::string::npos ||
        goal.description.find("该文件") != std::string::npos) {
        std::string last_loc = conv_ctx_.get("location");
        if (!last_loc.empty()) {
            conv_ctx_.add("file_context", "file in " + last_loc);
            if (ctx_.global_summary) {
                ctx_.global_summary->set("file_context", "referring to file in " + last_loc, 2);
            }
        }
    }

    if (ctx_.state)
        ctx_.state->transition(AgentState::Running, "Decomposing", run_id);
    LOG_INFO(kLayer, id_, run_id, "starting: " + goal.description);
    ctx_.log_info(run_id, EvType::TaskStart, goal.description.substr(0,120));

    // Inject long-term memory context into goal if available
    UserGoal enriched_goal = goal;
    if (ctx_.memory) {
        std::string relevant = ctx_.memory->load_relevant(goal.description, 2);
        if (!relevant.empty())
            enriched_goal.description = goal.description +
                "\n\n[Relevant past context:]\n" + relevant;
    }

    try {
        // -- Step 0: Complexity assessment  --  route simple tasks directly ----
        TaskComplexity complexity = classify_complexity(enriched_goal.description);

        if (complexity == TaskComplexity::L0_Conversational) {
            // Answer directly without spawning any Manager or Worker
            LOG_INFO(kLayer, id_, run_id, "L0 conversational: direct answer");
            ctx_.log_info(run_id, EvType::LlmCall, "direct conversational response");
            if (ctx_.state) ctx_.state->record_call();
            // Tell the LLM what tools it has so it can answer capability questions correctly
            std::string direct_sys =
                "You are a helpful AI assistant. Answer conversational questions directly.\n"
                "You also have tools: list_dir, read_file, write_file, get_current_dir, "
                "get_sysinfo, run_command, and more.\n"
                "\n"
                "IMPORTANT: This conversation route is for PURE Q&A only. "
                "If the user is asking you to CHECK, LIST, READ, RUN, or DO anything "
                "on their system -- you cannot do that from this route. "
                "Tell them their request will be handled by your tool pipeline. "
                "Do NOT pretend you cannot use tools at all -- you CAN, just not via this route.";
            result.answer = client_.complete(direct_sys, enriched_goal.description, run_id);
            result.status = TaskStatus::Done;
            result.finished_at = iso_now();
            result.usage      = client_.usage(); client_.reset_usage();
            if (ctx_.state) ctx_.state->transition(AgentState::Done, "", run_id);
            return result;
        }

        // -- Step 1 (L1): Direct single-tool execution -------------------
        // For simple tool requests, Director executes directly without Manager/Worker overhead
        if (complexity == TaskComplexity::L1_SingleTool) {
            LOG_INFO(kLayer, id_, run_id, "L1 single-tool: direct execution");
            // Build a minimal subtask and dispatch via a single Manager
            // Extract the actual user request (strip conversation history)
            std::string req_desc = enriched_goal.description;
            {
                static const char* M = "[Current request:]";
                auto mp = req_desc.find(M);
                if (mp != std::string::npos) {
                    mp += std::strlen(M);
                    while (mp < req_desc.size() &&
                           (req_desc[mp]=='\n'||req_desc[mp]=='\r'||req_desc[mp]==' ')) ++mp;
                    req_desc = req_desc.substr(mp);
                }
            }
            SubTask st;
            st.id              = "subtask-1";
            st.description     = req_desc;
            st.expected_output = "Tool execution result OR honest error description";
            st.retry_feedback  = "";
            auto reports = dispatch_managers({st});
            if (!reports.empty() && reports[0].status == TaskStatus::Done) {
                result.status       = TaskStatus::Done;
                result.sub_reports  = reports;
                // Synthesise with the clean request (no history noise)
                try {
                    UserGoal clean_goal; clean_goal.description = req_desc;
                    result.answer = synthesise(clean_goal, reports);
                } catch (...) {
                    result.answer = reports[0].summary;
                }
                result.finished_at  = iso_now();
                result.usage        = client_.usage(); client_.reset_usage();
                if (ctx_.state) ctx_.state->transition(AgentState::Done, "", run_id);
                return result;
            }
            // On failure, fall through to full decompose path
            LOG_WARN(kLayer, id_, run_id, "L1 direct execution failed, falling back to decompose");
        }

        // -- Step 1: Rule-based fast decompose (skip LLM for known patterns) --
        std::vector<SubTask> tasks;
        {
            // Extract clean request for rule matching
            std::string rule_req = enriched_goal.description;
            {
                static const char* RM = "[Current request:]";
                auto rp = rule_req.find(RM);
                if (rp != std::string::npos) {
                    rp += std::strlen(RM);
                    while (rp < rule_req.size() &&
                           (rule_req[rp]=='\n'||rule_req[rp]=='\r'||rule_req[rp]==' ')) ++rp;
                    rule_req = rule_req.substr(rp, 200);
                }
            }
            auto rule_match = apply_task_rules(rule_req);
            if (rule_match.has_value()) {
                tasks = {rule_match.value()};
                LOG_INFO(kLayer, id_, run_id,
                    "rule-engine match: " + rule_match->description.substr(0,60));
            }
        }

        // -- Step 1: LLM Decompose (only if no rule matched) ---------------
        if (tasks.empty()) {
            std::string format_hint;
            for (int attempt = 0; attempt <= max_subtask_retries_; ++attempt) {
                try {
                    tasks = decompose_goal(enriched_goal, format_hint);
                    break;
                } catch (const ParseException& e) { (void)e;
                    if (attempt == max_subtask_retries_) throw;
                    format_hint =
                        "\n\n[CORRECTION] Your previous response was not valid JSON. "
                        "Return ONLY a JSON array of subtask objects. No prose, no fences.";
                    LOG_WARN(kLayer, id_, run_id,
                        "decompose parse failed, retry " + std::to_string(attempt+1));
                    ctx_.log_warn(run_id, EvType::ParseFail,
                        "decompose retry " + std::to_string(attempt+1));
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                }
            }
        } // end if (tasks.empty()) — LLM decompose

        std::string current_request = extract_current_request(enriched_goal.description);
        size_t original_task_count = tasks.size();
        tasks = compact_file_creation_subtasks(current_request, std::move(tasks));
        if (tasks.size() != original_task_count) {
            LOG_INFO(kLayer, id_, run_id,
                "compacted file-creation workflow from " +
                std::to_string(original_task_count) + " to " +
                std::to_string(tasks.size()) + " subtasks");
        }

        LOG_INFO(kLayer, id_, run_id,
            "decomposed into " + std::to_string(tasks.size()) + " subtasks");

        // Save subtask list to workspace
        if (!ctx_.workspace.run_root.empty()) {
            std::string path = WorkspaceManager::join(
                ctx_.workspace.agent_dir(id_), "subtasks.json");
            try {
                nlohmann::json j = nlohmann::json::array();
                for (auto& t : tasks) { nlohmann::json jt; to_json(jt,t); j.push_back(jt); }
                agent::utf8_ofstream f(path);
                if (f.is_open()) f << j.dump(2) << "\n";
            } catch (...) {}
        }

        if (ctx_.state)
            ctx_.state->transition(AgentState::Running, "Dispatching", run_id);

        // -- Step 2: Dispatch managers -------------------------------------
        std::vector<SubTaskReport> reports = dispatch_managers(tasks);
        result.sub_reports = reports;

        // E2: Store successful decomposition as few-shot example for future requests
        if (ctx_.session_mem && !tasks.empty()) {
            bool all_done = std::all_of(reports.begin(), reports.end(),
                [](const SubTaskReport& r){ return r.status == TaskStatus::Done; });
            if (all_done && !goal.description.empty()) {
                std::string req_ex = goal.description;
                static const char* RM2 = "[Current request:]";
                auto rp2 = req_ex.find(RM2);
                if (rp2 != std::string::npos) {
                    rp2 += std::strlen(RM2);
                    while (rp2 < req_ex.size() && req_ex[rp2] <= ' ') ++rp2;
                    req_ex = req_ex.substr(rp2, 40);
                } else req_ex = req_ex.substr(0, 40);
                std::ostringstream ex;
                ex << "{\"goal\":\"" << req_ex << "\",\"tools\":[";
                for (size_t k = 0; k < tasks.size(); ++k) {
                    if (k > 0) ex << ",";
                    std::string d2 = tasks[k].description;
                    auto tp2 = d2.find("Use "); auto te2 = tp2 != std::string::npos
                        ? d2.find_first_of(" (", tp2+4) : std::string::npos;
                    std::string tname = (tp2 != std::string::npos && te2 != std::string::npos)
                        ? d2.substr(tp2+4, te2-tp2-4) : "?";
                    ex << "\"" << tname << "\"";
                }
                ex << "]}";
                ctx_.session_mem->set("fewshot:" + req_ex.substr(0, 20), ex.str());
            }
        }

        // Checkpoint: persist subtask results for --resume support
        if (!ctx_.workspace.run_root.empty()) {
            try {
                nlohmann::json ckpt = nlohmann::json::array();
                for (auto& r : reports) {
                    nlohmann::json jr; to_json(jr, r); ckpt.push_back(jr);
                }
                agent::utf8_ofstream ck_f(ctx_.workspace.run_root + "/checkpoint.json");
                if (ck_f.is_open()) ck_f << ckpt.dump(2) << "\n";
            } catch (...) {}
        }

        if (ctx_.state)
            ctx_.state->transition(AgentState::Running, "Reviewing", run_id);

        // -- Step 3: Review ------------------------------------------------
        auto feedbacks = review(goal, tasks, reports);

        // -- Step 4: Retry rejected (with loop-break on repeated failure) ------
        // Track how many times each subtask has been rejected.
        // If any subtask is rejected more than once, it means the failure is
        // external (file missing, path wrong, etc.)  --  force-approve to stop loop.
        std::unordered_map<std::string,int> rejection_counts;
        for (int round = 0; round < max_subtask_retries_; ++round) {
            bool any_actionable_rejection = false;
            for (const auto& fb : feedbacks) {
                if (!fb.approved) {
                    rejection_counts[fb.subtask_id]++;
                    if (rejection_counts[fb.subtask_id] >= 2) {
                        // Same subtask rejected twice  --  external constraint, accept result
                        LOG_WARN(kLayer, id_, fb.subtask_id,
                            "subtask rejected " +
                            std::to_string(rejection_counts[fb.subtask_id]) +
                            " times  --  external failure, force-approving to stop loop");
                    } else {
                        any_actionable_rejection = true;
                    }
                }
            }
            // Force-approve subtasks rejected 2+ times (external constraint → degrade gracefully)
            int forced = 0;
            for (auto& fb : feedbacks) {
                if (!fb.approved && rejection_counts[fb.subtask_id] >= 2) {
                    fb.approved = true;
                    ++forced;
                }
            }
            if (forced > 0) {
                LOG_WARN(kLayer, id_, run_id,
                    std::to_string(forced) + " subtask(s) force-approved after 2+ rejections "
                    "-- returning best-effort partial result");
                // Inject degradation note into synthesise context via session_mem
                if (ctx_.session_mem)
                    ctx_.session_mem->set("degraded_subtasks", std::to_string(forced));
            }
            if (!any_actionable_rejection) break;
            LOG_INFO(kLayer, id_, run_id, "retry round " + std::to_string(round+1));
            retry_rejected(goal, tasks, reports, feedbacks);
        }
        result.sub_reports = reports;

        if (ctx_.state)
            ctx_.state->transition(AgentState::Running, "Synthesising", run_id);

        // -- Step 5: Synthesise --------------------------------------------
        result.answer     = synthesise(goal, reports);
        result.status     = TaskStatus::Done;
        result.finished_at = iso_now();
        result.usage      = client_.usage(); client_.reset_usage();
        LOG_INFO(kLayer, id_, run_id, "completed successfully");
        ctx_.log_info(run_id, EvType::TaskEnd, "done");

        if (ctx_.state)
            ctx_.state->transition(AgentState::Done, "", run_id);

        // Save FinalResult to workspace
        if (!ctx_.workspace.result_json.empty()) {
            try {
                nlohmann::json jr; to_json(jr, result);
                agent::utf8_ofstream f(ctx_.workspace.result_json);
                if (f.is_open()) f << jr.dump(2) << "\n";
            } catch (...) {}
        }

        // Append session record to WORKSPACE.md
        if (!ctx_.workspace.workspace_md.empty()) {
            try {
                agent::utf8_ofstream wmd(ctx_.workspace.workspace_md, std::ios::app);
                if (wmd.is_open()) {
                    // ISO timestamp
                    auto now = std::chrono::system_clock::now();
                    auto tt  = std::chrono::system_clock::to_time_t(now);
                    std::tm tm_b{};
#ifdef _WIN32
                    gmtime_s(&tm_b, &tt);
#else
                    gmtime_r(&tt, &tm_b);
#endif
                    char ts[32]; std::strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", &tm_b);
                    // Goal summary (first 80 chars)
                    std::string goal_summary = goal.description;
                    static const char* RM = "[Current request:]";
                    auto rp = goal_summary.find(RM);
                    if (rp != std::string::npos) {
                        rp += std::strlen(RM);
                        while (rp < goal_summary.size() && goal_summary[rp] <= ' ') ++rp;
                        goal_summary = goal_summary.substr(rp);
                    }
                    if (goal_summary.size() > 80) goal_summary = goal_summary.substr(0, 80) + "...";
                    wmd << "\n### " << ts << " — " << run_id << "\n";
                    wmd << "**Goal:** " << goal_summary << "\n";
                    wmd << "**Status:** " << status_to_string(result.status) << "\n";
                    if (!result.answer.empty()) {
                        std::string ans_preview = result.answer;
                        if (ans_preview.size() > 120)
                            ans_preview = ans_preview.substr(0, 120) + "...";
                        wmd << "**Summary:** " << ans_preview << "\n";
                    }
                }
            } catch (...) {}
        }

    } catch (const std::exception& e) {
        result.status = TaskStatus::Failed;
        result.error  = std::string("Director error: ") + e.what();
        LOG_ERROR(kLayer, id_, run_id, result.error);
        ctx_.log_error(run_id, EvType::TaskEnd, result.error);
        if (ctx_.state) {
            ctx_.state->record_failure(result.error);
            ctx_.state->transition(AgentState::Failed, "", run_id);
        }
    } catch (...) {
        result.status = TaskStatus::Failed;
        result.error  = "Director error: unknown exception";
        LOG_ERROR(kLayer, id_, run_id, result.error);
        if (ctx_.state) ctx_.state->transition(AgentState::Failed, "", run_id);
    }

    return result;
}


// -- dispatch_managers ---------------------------------------------------------

std::vector<SubTaskReport> DirectorAgent::dispatch_managers(
        const std::vector<SubTask>& tasks) {

    if (should_dispatch_sequentially(tasks)) {
        LOG_INFO(kLayer, id_, "dispatch",
            "detected dependent file workflow; dispatching managers sequentially");
        std::vector<SubTaskReport> reports;
        reports.reserve(tasks.size());
        for (const auto& task : tasks) {
            std::shared_ptr<IManager> mgr;
            try {
                mgr = mgr_pool_.acquire(task);
            } catch (const std::exception& e) {
                LOG_ERROR(kLayer, id_, task.id, std::string("pool acquire failed: ") + e.what());
                SubTaskReport fr;
                fr.subtask_id = task.id;
                fr.status     = TaskStatus::Failed;
                fr.issues     = std::string("ManagerPool::acquire threw: ") + e.what();
                reports.push_back(std::move(fr));
                continue;
            }

            LOG_DEBUG(kLayer, id_, task.id,
                "dispatching to manager " + mgr->id() + " [pooled][sequential]");
            auto report = mgr->process(task);
            mgr_pool_.release(mgr, report.status == TaskStatus::Done);
            if (ctx_.session_mem && report.status == TaskStatus::Done && !report.summary.empty())
                ctx_.session_mem->set("shared:" + report.subtask_id, report.summary.substr(0, 400));
            reports.push_back(std::move(report));
        }
        return reports;
    }

    // Use ManagerPool for persistent, memory-retaining Managers
    // Managers are classified by task type and reused across subtasks
    std::vector<std::future<SubTaskReport>> futures;
    futures.reserve(tasks.size());

    for (const auto& task : tasks) {
        std::shared_ptr<IManager> mgr;
        try {
            mgr = mgr_pool_.acquire(task);
        } catch (const std::exception& e) {
            LOG_ERROR(kLayer, id_, task.id, std::string("pool acquire failed: ") + e.what());
            SubTaskReport fr;
            fr.subtask_id = task.id;
            fr.status     = TaskStatus::Failed;
            fr.issues     = std::string("ManagerPool::acquire threw: ") + e.what();
            futures.push_back(pool_.submit([r=std::move(fr)](){ return r; }));
            continue;
        }
        LOG_DEBUG(kLayer, id_, task.id,
            "dispatching to manager " + mgr->id() + " [pooled]");
        futures.push_back(pool_.submit(
            [this, mgr, t=task]() mutable {
                auto report = mgr->process(t);
                // Return Manager to pool; memory is preserved for next task
                mgr_pool_.release(mgr, report.status == TaskStatus::Done);
                return report;
            }));
    }

    std::vector<SubTaskReport> reports;
    reports.reserve(futures.size());
    for (auto& f : futures) {
        auto rep = f.get();
        // C1: store completed result so input_from Workers can read it
        if (ctx_.session_mem && rep.status == TaskStatus::Done && !rep.summary.empty())
            ctx_.session_mem->set("shared:" + rep.subtask_id, rep.summary.substr(0, 400));
        reports.push_back(std::move(rep));
    }
    return reports;
}


// -- review --------------------------------------------------------------------

std::vector<ReviewFeedback> DirectorAgent::review(
        const UserGoal&                   goal,
        const std::vector<SubTask>&       tasks,
        const std::vector<SubTaskReport>& reports) const {

    nlohmann::json summary = nlohmann::json::array();
    for (size_t i = 0; i < tasks.size() && i < reports.size(); ++i) {
        nlohmann::json entry;
        entry["subtask_id"]      = tasks[i].id;
        entry["description"]     = tasks[i].description;
        entry["expected_output"] = tasks[i].expected_output;
        entry["report_status"]   = status_to_string(reports[i].status);
        entry["report_summary"]  = reports[i].summary;
        entry["issues"]          = reports[i].issues;
        summary.push_back(std::move(entry));
    }

    std::ostringstream user_msg;
    user_msg << "## Original user goal\n" << goal.description << "\n\n"
             << "## Subtask execution summary\n" << summary.dump(2) << "\n\n"
             << "Review each subtask against its acceptance criteria.\n"
             << "Respond with ONLY a JSON array:\n"
             << "[\n"
             << "  {\"subtask_id\":\"subtask-1\",\"approved\":true,\"feedback\":\"\"}\n"
             << "]";

    ctx_.log_info("review", EvType::LlmCall, "review LLM call");
    if (ctx_.state) ctx_.state->record_call();

    std::string prompt = review_prompt_;
    if (ctx_.prompt_opt) {
        std::string opt = ctx_.prompt_opt->select_best("director-review");
        if (!opt.empty()) prompt = opt;
    }

    std::string    llm_out = client_.complete(prompt, user_msg.str(), "review");
    nlohmann::json j       = parse_llm_json(llm_out);

    if (!j.is_array())
        throw ParseException("review: expected array", llm_out, "goal", kLayer);

    std::vector<ReviewFeedback> feedbacks;
    feedbacks.reserve(j.size());
    for (size_t i = 0; i < j.size(); ++i) {
        ReviewFeedback fb;
        try { from_json(j[i], fb); }
        catch (const std::exception& e) {
            throw ParseException(
                std::string("review: malformed[") + std::to_string(i) + "]: " + e.what(),
                llm_out, "goal", kLayer);
        }
        feedbacks.push_back(std::move(fb));
    }

    for (const auto& fb : feedbacks) {
        if (fb.approved)
            LOG_INFO(kLayer, id_, fb.subtask_id, "review: APPROVED");
        else
            LOG_WARN(kLayer, id_, fb.subtask_id, "review: REJECTED  --  " + fb.feedback);
    }
    return feedbacks;
}


// -- retry_rejected ------------------------------------------------------------

void DirectorAgent::retry_rejected(const UserGoal&              goal,
                                   const std::vector<SubTask>&  tasks,
                                   std::vector<SubTaskReport>&  reports,
                                   std::vector<ReviewFeedback>& feedbacks) {
    std::vector<size_t> rejected;
    for (size_t i = 0; i < feedbacks.size(); ++i)
        if (!feedbacks[i].approved) rejected.push_back(i);
    if (rejected.empty()) return;

    std::vector<std::future<SubTaskReport>> futures;
    futures.reserve(rejected.size());

    for (size_t idx : rejected) {
        SubTask revised        = tasks[idx];
        revised.retry_feedback = feedbacks[idx].feedback;

        std::shared_ptr<IManager> mgr;
        try { mgr = factory_(revised); }
        catch (const std::exception& e) {
            LOG_ERROR(kLayer, id_, revised.id, std::string("retry factory: ") + e.what());
            SubTaskReport fr; fr.subtask_id=revised.id; fr.status=TaskStatus::Failed;
            fr.issues=std::string("factory: ")+e.what();
            futures.push_back(pool_.submit([r=std::move(fr)](){ return r; }));
            continue;
        }
        LOG_INFO(kLayer, id_, revised.id,
            "re-issuing to " + mgr->id() + " | feedback: " + revised.retry_feedback);
        futures.push_back(pool_.submit(
            [mgr,t=std::move(revised)]() mutable { return mgr->process(t); }));
    }

    for (size_t fi = 0; fi < futures.size(); ++fi)
        reports[rejected[fi]] = futures[fi].get();

    feedbacks = review(goal, tasks, reports);
}

std::vector<SubTask> DirectorAgent::decompose_goal(const UserGoal&    goal,
                                                    const std::string& format_hint) const {
    std::ostringstream user_msg;

    // Inject global summary (shared across all agents)
    if (ctx_.global_summary) {
        std::string global_ctx = ctx_.global_summary->build_context(5);
        if (!global_ctx.empty()) {
            user_msg << global_ctx << "\n\n";
        }
    }

    // Inject conversation context
    std::string conv_summary = conv_ctx_.build_summary(3);
    if (!conv_summary.empty()) {
        user_msg << conv_summary << "\n\n";
    }

    // Inject workspace + CWD context (via helper to avoid duplication)
    {
        std::string ws_info = build_ws_context(ctx_.workspace);
        if (!ws_info.empty())
            user_msg << "## Context\n" << ws_info << "\n\n";
    }
    // Inject known environment facts to avoid re-discovery
    if (ctx_.env_kb && !ctx_.env_kb->empty()) {
        std::string env_ctx = ctx_.env_kb->build_context(0.6f);
        if (!env_ctx.empty())
            user_msg << env_ctx << "\n";
    }

    // (CWD already injected via build_ws_context above)

    // Inject only the current request (not full conversation history)
    // This follows ADK "Scope by Default" — LLM sees minimum necessary context
    std::string req_for_decompose = goal.description;
    {
        static const char* REQ_M = "[Current request:]";
        auto rp = req_for_decompose.find(REQ_M);
        if (rp != std::string::npos) {
            rp += std::strlen(REQ_M);
            while (rp < req_for_decompose.size() &&
                   (req_for_decompose[rp]=='\n'||req_for_decompose[rp]==' ')) ++rp;
            req_for_decompose = req_for_decompose.substr(rp);
        }
    }
    // E2: Few-shot from past successful decompositions (req_for_decompose now in scope)
    if (ctx_.session_mem) {
        std::string ex_key = "fewshot:" + req_for_decompose.substr(0, 20);
        std::string example = ctx_.session_mem->get(ex_key);
        if (!example.empty())
            user_msg << "## Similar past request\n" << example.substr(0,300) << "\n\n";
    }

    user_msg << "## User goal\n" << req_for_decompose << "\n\n"
             << "## Instructions\n"
             << "Decompose this goal into independent, parallelisable subtasks. "
                "Each subtask must be self-contained with measurable acceptance criteria. "
                "Simple requests (greetings, single questions) need only 1 subtask.\n\n"
             << "IMPORTANT: For creation tasks (write/create/generate files/docs/configs), "
                "add a verification subtask based on available resources:\n"
                "- Code: check dependencies exist, test syntax/run if possible\n"
                "- Docs: verify completeness, check links if applicable\n"
                "- Configs: validate syntax, check required fields\n\n"
             << "## Required output format\n"
             << "Respond with ONLY a JSON array (no prose, no fences):\n"
             << "[\n"
             << "  {\n"
             << "    \"id\": \"subtask-1\",\n"
             << "    \"description\": \"<what to accomplish>\",\n"
             << "    \"expected_output\": \"<measurable acceptance criteria>\",\n"
             << "    \"retry_feedback\": \"\"\n"
             << "  }\n"
             << "]"
             << format_hint;

    ctx_.log_info("decompose", EvType::LlmCall, "decompose_goal LLM call");
    if (ctx_.state) ctx_.state->record_call();

    std::string prompt = decompose_prompt_;
    if (ctx_.prompt_opt) {
        std::string opt = ctx_.prompt_opt->select_best("director-decompose");
        if (!opt.empty()) prompt = opt;
    }

    std::string llm_out = client_.complete(prompt, user_msg.str(), "decompose");

    nlohmann::json j = parse_llm_json(llm_out);
    if (!j.is_array())
        throw ParseException("decompose_goal: expected array", llm_out, "goal", kLayer);

    std::vector<SubTask> tasks;
    tasks.reserve(j.size());
    for (size_t i = 0; i < j.size(); ++i) {
        SubTask t;
        try {
            from_json(j[i], t);
            if (t.id.empty()) t.id = "subtask-" + std::to_string(i+1);
        } catch (const std::exception& e) {
            throw ParseException(
                std::string("decompose_goal: malformed subtask[") + std::to_string(i) + "]: " + e.what(),
                llm_out, "goal", kLayer);
        }
        tasks.push_back(std::move(t));
    }
    if (tasks.empty()) {
        LOG_WARN(kLayer, id_, "decompose",
            "LLM returned empty task list; creating fallback single subtask");
        SubTask fallback;
        fallback.id              = "subtask-1";
        fallback.description     = goal.description;
        fallback.expected_output = "Honest result or error description";
        tasks.push_back(fallback);
    }
    return tasks;
}

// -- synthesise ----------------------------------------------------------------

std::string DirectorAgent::synthesise(
        const UserGoal& goal,
        const std::vector<SubTaskReport>& reports) const {

    // Build context using actual Worker outputs  --  not just metadata/summary strings
    // This ensures the synthesiser LLM has real content to work with
    std::ostringstream results_str;
    // Cap per-report output to prevent context explosion with many Workers
    static const size_t kMaxReportChars = 2000;
    for (const auto& r : reports) {
        results_str << "\n[" << r.subtask_id << " / "
                    << status_to_string(r.status) << "]\n";
        for (const auto& ar : r.results) {
            if (!ar.output.empty()) {
                if (ar.output.size() > kMaxReportChars) {
                    results_str << ar.output.substr(0, kMaxReportChars)
                                << "\n[...truncated " << ar.output.size() << " bytes]";
                } else {
                    results_str << ar.output;
                }
            }
            if (!ar.error.empty() && ar.error != "")
                results_str << "\n[error: " << ar.error << "]";
            results_str << "\n";
        }
    }

    std::ostringstream user_msg;

    // Inject workspace + CWD context (via helper to avoid duplication)
    {
        std::string ws_info = build_ws_context(ctx_.workspace);
        if (!ws_info.empty())
            user_msg << "## Context\n" << ws_info << "\n\n";
    }
    // Inject known environment facts to avoid re-discovery
    if (ctx_.env_kb && !ctx_.env_kb->empty()) {
        std::string env_ctx = ctx_.env_kb->build_context(0.6f);
        if (!env_ctx.empty())
            user_msg << env_ctx << "\n";
    }
    // (CWD already injected via build_ws_context above)

    // Inject only the current request (not full conversation history)
    // This follows ADK "Scope by Default" — LLM sees minimum necessary context
    std::string req_for_decompose = goal.description;
    {
        static const char* REQ_M = "[Current request:]";
        auto rp = req_for_decompose.find(REQ_M);
        if (rp != std::string::npos) {
            rp += std::strlen(REQ_M);
            while (rp < req_for_decompose.size() &&
                   (req_for_decompose[rp]=='\n'||req_for_decompose[rp]==' ')) ++rp;
            req_for_decompose = req_for_decompose.substr(rp);
        }
    }
    user_msg << "## User goal\n" << req_for_decompose << "\n\n"
             << "## Task results\n" << results_str.str() << "\n\n"
             << "Write a clear, helpful answer to the user's goal based on the task results above.\n"
             << "Rules:\n"
             << "- Write in the same language the user used\n"
             << "- If a file/resource was not found: state the specific error concisely\n"
             << "- If a command succeeded: share the actual output\n"
             << "- For file create/write/update requests, explicitly state the exact target path and add one brief verification line based on the tool results\n"
             << "- Do not mention agents, pipeline, subtasks, JSON, or internal mechanics\n"
             << "- Be direct and concise  --  no padding\n"
             << "- Do not invent or guess information not in the results";

    ctx_.log_info("synthesise", EvType::LlmCall, "synthesise LLM call");
    if (ctx_.state) ctx_.state->record_call();
    // Budget guard (if configured)
    if (ctx_.budget_usd > 0 && client_.usage().estimated_cost >= ctx_.budget_usd) {
        LOG_WARN("Director", id_, "synthesise", "budget exceeded, skipping synthesise LLM call");
        return "[Budget limit reached: $" + std::to_string(ctx_.budget_usd) +
               " -- partial results shown above]";
    }

    std::string prompt = synthesise_prompt_;
    if (ctx_.prompt_opt) {
        std::string opt = ctx_.prompt_opt->select_best("director-synthesise");
        if (!opt.empty()) prompt = opt;
    }

    return client_.complete(prompt, user_msg.str(), "synthesise");
}


} // namespace agent


