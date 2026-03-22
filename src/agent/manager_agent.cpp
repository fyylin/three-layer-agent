// =============================================================================
// src/agent/manager_agent.cpp  --  v2: AgentContext wired in
// =============================================================================
#include "agent/manager_agent.hpp"
#ifdef _WIN32
#  include <windows.h>
#endif
#include "agent/exceptions.hpp"
#include "utils/logger.hpp"
#include "utils/json_utils.hpp"
#include <nlohmann/json.hpp>
#include <future>
#include <unordered_map>
#include <sstream>
#include <thread>
#include <chrono>

namespace agent {

static const char* kLayer = "Manager";

ManagerAgent::ManagerAgent(std::string               id,
                           ApiClient&                client,
                           std::vector<std::shared_ptr<WorkerAgent>> workers,
                           ThreadPool&               pool,
                           std::string               decompose_prompt,
                           std::string               validate_prompt,
                           std::vector<std::string>  available_tools,
                           int                       max_atomic_retries,
                           AgentContext              ctx)
    : id_(std::move(id))
    , client_(client)
    , workers_(std::move(workers))
    , pool_(pool)
    , decompose_prompt_(std::move(decompose_prompt))
    , validate_prompt_(std::move(validate_prompt))
    , available_tools_(std::move(available_tools))
    , max_atomic_retries_(max_atomic_retries)
    , ctx_(std::move(ctx))
{
    if (workers_.empty())
        throw std::invalid_argument("ManagerAgent: worker list must not be empty");
    if (ctx_.state)
        ctx_.state->transition(AgentState::Idle);
    ctx_.log_info("", EvType::AgentCreated, "Manager " + id_ + " created");
}

// -- process -------------------------------------------------------------------

SubTaskReport ManagerAgent::process(const SubTask& task) noexcept {
    SubTaskReport report;
    report.subtask_id = task.id;
    report.status     = TaskStatus::Running;

    if (ctx_.state)
        ctx_.state->transition(AgentState::Running, "Decomposing", task.id);
    LOG_INFO(kLayer, id_, task.id, "processing: " + task.description);
    ctx_.log_info(task.id, EvType::TaskStart, task.description);

    try {
        // -- Cancellation check --------------------------------------------
        if (ctx_.cancelled()) {
            report.status = TaskStatus::Failed;
            report.issues = "cancelled";
            return report;
        }

        // -- Drain any Supervisor corrections -----------------------------
        std::string supervisor_note;
        if (ctx_.bus) {
            while (auto msg = ctx_.bus->try_receive(id_)) {
                if (msg->type == MsgType::Correct) {
                    auto& pl = msg->payload;
                    auto npos = pl.find("\"note\":");
                    if (npos != std::string::npos) {
                        npos += 7;
                        while (npos < pl.size() && (pl[npos]=='"'||pl[npos]==' ')) ++npos;
                        auto epos = pl.find('"', npos);
                        if (epos != std::string::npos)
                            supervisor_note += pl.substr(npos, epos - npos) + " ";
                    }
                }
            }
        }

        // -- Step 1: Decompose ---------------------------------------------
        std::vector<AtomicTask> atomic_tasks;
        {
            // Check result cache first (semantic matching)
            if (ctx_.result_cache) {
                std::string cached_summary;
                std::string cache_key = "subtask:" + task.id;
                if (ctx_.result_cache->get(cache_key, cached_summary)) {
                    LOG_INFO(kLayer, id_, task.id, "cache hit, skipping execution");
                    report.status = TaskStatus::Done;
                    report.summary = cached_summary;
                    return report;
                }
            }

            // Check session memory for cached decomposition
            std::string cache_key = "decompose:" + task.id;
            if (ctx_.memory && ctx_.memory->has(cache_key)) {
                LOG_DEBUG(kLayer, id_, task.id, "using cached decomposition");
            }

            std::string format_hint;
            for (int attempt = 0; attempt <= max_atomic_retries_; ++attempt) {
                try {
                    atomic_tasks = decompose(task, format_hint, supervisor_note);
                    break;
                } catch (const ParseException& e) { (void)e;
                    if (attempt == max_atomic_retries_) throw;
                    format_hint =
                        "\n\n[CORRECTION] Your previous response was not valid JSON. "
                        "Return ONLY a JSON array of task objects. No prose, no fences.";
                    LOG_WARN(kLayer, id_, task.id,
                        "decompose parse failed, retry " + std::to_string(attempt+1));
                    ctx_.log_warn(task.id, EvType::ParseFail,
                        "decompose retry " + std::to_string(attempt+1));
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                }
            }
        }

        LOG_INFO(kLayer, id_, task.id,
            "decomposed into " + std::to_string(atomic_tasks.size()) + " atomic tasks");

        if (ctx_.state)
            ctx_.state->transition(AgentState::Running, "Dispatching", task.id);

        // -- Step 2: Dispatch ----------------------------------------------
        report.results = dispatch(std::move(atomic_tasks), task.id);

        if (ctx_.state)
            ctx_.state->transition(AgentState::Running, "Validating", task.id);

        // -- Step 3: Validate ----------------------------------------------
        auto [approved, feedback] = validate(task, report.results);
        report.summary = build_summary(report.results, approved);

        if (approved) {
            report.status = TaskStatus::Done;
            LOG_INFO(kLayer, id_, task.id, "validation passed");
            ctx_.log_info(task.id, EvType::SubReport, "approved");

            // Cache successful summary in session memory
            if (ctx_.memory) {
                ctx_.memory->set("result:" + task.id, report.summary);
                // Accumulate for Director's context
                std::string prior = ctx_.memory->get("completed_subtasks", "");
                ctx_.memory->set("completed_subtasks",
                    prior + (prior.empty() ? "" : "\n") +
                    task.id + ": " + report.summary);
            }

            // Notify Director via bus
            if (ctx_.bus) {
                std::string payload =
                    "{\"subtask_id\":\"" + task.id + "\","
                    "\"status\":\"done\","
                    "\"summary\":\"" + report.summary.substr(0,200) + "\"}";
                ctx_.send(ctx_.parent_id, MsgType::SubReport, payload);
            }

            if (ctx_.state) ctx_.state->transition(AgentState::Done, "", task.id);

            // Cache successful result
            if (ctx_.result_cache && report.status == TaskStatus::Done) {
                std::string cache_key = "subtask:" + task.id;
                ctx_.result_cache->put(cache_key, report.summary);
            }
        } else {
            // Rejected = validated but content doesn't meet criteria (distinct from Failed)
            report.status = TaskStatus::Rejected;
            report.issues = feedback;
            LOG_WARN(kLayer, id_, task.id, "validation failed: " + feedback);
            ctx_.log_warn(task.id, EvType::SubReport, "rejected: " + feedback);
            if (ctx_.state) {
                ctx_.state->record_failure(feedback);
                ctx_.state->transition(AgentState::Failed, "", task.id);
            }
        }

    } catch (const std::exception& e) {
        report.status = TaskStatus::Failed;
        report.issues = std::string("Manager error: ") + e.what();
        LOG_ERROR(kLayer, id_, task.id, report.issues);
        ctx_.log_error(task.id, EvType::TaskEnd, report.issues);
        if (ctx_.state) {
            ctx_.state->record_failure(report.issues);
            ctx_.state->transition(AgentState::Failed, "", task.id);
        }
    } catch (...) {
        report.status = TaskStatus::Failed;
        report.issues = "Manager error: unknown exception";
        LOG_ERROR(kLayer, id_, task.id, report.issues);
        if (ctx_.state) ctx_.state->transition(AgentState::Failed, "", task.id);
    }

    return report;
}

// -- decompose -----------------------------------------------------------------

std::vector<AtomicTask> ManagerAgent::decompose(const SubTask& task,
                                                 const std::string& format_hint,
                                                 const std::string& supervisor_note) const {

    // ── Fast-path: description already specifies tool+input directly ──
    // Format: "Use <tool> to <desc>. Input: <input>" or "Use <tool>(<input>)"
    // This avoids an extra LLM call for L1 single-tool tasks from Director
    {
        std::string desc = task.description;
        // Detect "Use <tool_name> to ... Input: <exact_input>"
        auto use_pos = desc.find("Use ");
        if (use_pos == 0 || (use_pos != std::string::npos && use_pos < 10)) {
            auto input_pos = desc.rfind("Input: ");
            if (input_pos != std::string::npos) {
                // Extract tool name
                size_t tool_start = use_pos + 4;
                size_t tool_end   = desc.find_first_of(" \t(", tool_start);
                if (tool_end == std::string::npos) tool_end = desc.size();
                std::string tool_name = desc.substr(tool_start, tool_end - tool_start);
                // Extract input value (after "Input: ")
                std::string input_val = desc.substr(input_pos + 7);
                // Trim quotes
                while (!input_val.empty() && (input_val.front()=='"'||input_val.front()=='\''||input_val.front()==' '))
                    input_val = input_val.substr(1);
                while (!input_val.empty() && (input_val.back() =='"'||input_val.back() =='\''||input_val.back() ==' '))
                    input_val.pop_back();
                // Validate tool exists
                bool known = false;
                for (auto& t : available_tools_) if (t == tool_name) { known=true; break; }
                // Disable fast-path for write_file: needs structured path\ncontent format
                if (known && !tool_name.empty() && tool_name != "write_file") {
                    LOG_INFO(kLayer, id_, task.id,
                        "fast-path: " + tool_name + "(" + input_val.substr(0,40) + ")");
                    AtomicTask at;
                    at.id        = task.id + "-atomic-1";
                    at.parent_id = task.id;
                    at.description = "Use " + tool_name + ". Input: " + input_val;
                    at.tool      = tool_name;
                    at.input     = input_val;
                    return {at};
                }
            }
        }
    }

    std::string tool_list_str;
    for (size_t i = 0; i < available_tools_.size(); ++i) {
        if (i > 0) tool_list_str += ", ";
        tool_list_str += available_tools_[i];
    }
    if (tool_list_str.empty()) tool_list_str = "(none)";

    // Inject prior completed work from session memory
    std::string prior_context;
    if (ctx_.memory) {
        std::string prior = ctx_.memory->get("completed_subtasks", "");
        if (!prior.empty())
            prior_context = "\n\n## Already completed work (do not redo)\n" + prior;
    }

    // Inject CWD + files_dir context so LLM uses correct paths
    std::string mgr_cwd;
#ifdef _WIN32
    { WCHAR w[MAX_PATH]={}; if(GetCurrentDirectoryW(MAX_PATH,w)){
        int n=WideCharToMultiByte(CP_UTF8,0,w,-1,nullptr,0,nullptr,nullptr);
        if(n>0){mgr_cwd.resize(n); WideCharToMultiByte(CP_UTF8,0,w,-1,&mgr_cwd[0],n,nullptr,nullptr);
        while(!mgr_cwd.empty()&&mgr_cwd.back()=='\0') mgr_cwd.pop_back();}}}
#else
    { const char* c=std::getenv("PWD"); if(c&&*c) mgr_cwd=c; }
#endif

    std::ostringstream user_msg;
    user_msg << "## Environment\n";
    if (!mgr_cwd.empty())
        user_msg << "Working Directory: " << mgr_cwd
                 << "  (use this for file paths, NOT workspace/current)\n";
    if (!ctx_.workspace.files_dir.empty())
        user_msg << "Default Output Dir: " << ctx_.workspace.files_dir
                 << "  (write agent-created files here by default)\n";
    user_msg << "\n";
    user_msg << "## Subtask\n" << task.description << "\n\n"
             << "## Acceptance criteria\n" << task.expected_output << "\n\n";
    if (!task.retry_feedback.empty())
        user_msg << "## Previous attempt feedback (must address)\n"
                 << task.retry_feedback << "\n\n";
    user_msg << "## Available tools\n" << tool_list_str << "\n\n"
             << "## Tool input format (CRITICAL)\n"
             << "- write_file: \"file_path\\n<actual file content>\"\n"
             << "- run_command: \"command with args\"\n"
             << "- list_dir: \"directory_path\"\n"
             << "- read_file: \"file_path\"\n"
             << "- find_files: \"search_dir\\npattern\"\n"
             << "- For write_file: path on first line, then newline, then FULL file content\n"
             << "- Keep input concise: paths and commands only, NOT full code in description\n\n"
             << "## Instructions\n"
             << "Break this subtask into atomic steps. "
                "If the task can be done in ONE step (with or without a tool), use ONE step. "
                "Only split if genuinely necessary. Each step uses AT MOST one tool.\n\n"
             << "## Required output format\n"
             << "Respond with ONLY a JSON array (no prose, no fences):\n"
             << "[\n"
             << "  {\n"
             << "    \"id\": \"" << task.id << "-atomic-1\",\n"
             << "    \"parent_id\": \"" << task.id << "\",\n"
             << "    \"description\": \"<what to do>\",\n"
             << "    \"tool\": \"<tool_name or empty string>\",\n"
             << "    \"input\": \"<tool input following format above>\"\n"
             << "  }\n"
             << "]"
             << format_hint
             << prior_context
             << (supervisor_note.empty() ? "" : "\n\n[Supervisor instruction]: " + supervisor_note);

    ctx_.log_info(task.id, EvType::LlmCall, "decompose LLM call");
    if (ctx_.state) ctx_.state->record_call();

    std::string prompt = decompose_prompt_;
    if (ctx_.prompt_opt) {
        std::string opt = ctx_.prompt_opt->select_best("manager-decompose");
        if (!opt.empty()) prompt = opt;
    }

    std::string llm_out = client_.complete(prompt, user_msg.str(), task.id);

    nlohmann::json j = parse_llm_json(llm_out);
    if (!j.is_array())
        throw ParseException("decompose: expected JSON array", llm_out, task.id, kLayer);

    std::vector<AtomicTask> tasks;
    tasks.reserve(j.size());
    for (size_t i = 0; i < j.size(); ++i) {
        AtomicTask at;
        try {
            from_json(j[i], at);
            if (at.parent_id.empty()) at.parent_id = task.id;
            if (at.id.empty()) at.id = task.id + "-atomic-" + std::to_string(i+1);
        } catch (const std::exception& e) {
            throw ParseException(
                std::string("decompose: malformed task[") + std::to_string(i) + "]: " + e.what(),
                llm_out, task.id, kLayer);
        }
        tasks.push_back(std::move(at));
    }
    if (tasks.empty())
        throw ParseException("decompose: empty task list", llm_out, task.id, kLayer);

    // Validate each atomic task before returning
    for (size_t i = 0; i < tasks.size(); ++i) {
        auto& at = tasks[i];

        // Validation 1: tool must exist in available_tools or be empty
        if (!at.tool.empty()) {
            bool found = false;
            for (auto& t : available_tools_) {
                if (t == at.tool) { found = true; break; }
            }
            if (!found) {
                LOG_WARN(kLayer, id_, task.id,
                    "atomic task " + at.id + " has unknown tool: " + at.tool);
                throw ParseException(
                    "Unknown tool '" + at.tool + "' in atomic task " + at.id,
                    llm_out, task.id, kLayer);
            }
        }

        // Validation 2: forbid obvious placeholder patterns
        if (!at.input.empty()) {
            std::string trimmed = at.input;
            size_t start = trimmed.find_first_not_of(" \t\n\r");
            if (start != std::string::npos) trimmed = trimmed.substr(start, 100);

            bool is_placeholder = false;
            if (trimmed.find("<path>") != std::string::npos ||
                trimmed.find("<file") != std::string::npos ||
                trimmed.find("<directory>") != std::string::npos ||
                trimmed.find("$HOME") == 0 ||
                trimmed.find("%USERPROFILE%") == 0 ||
                (trimmed.size() > 0 && trimmed[0] == '~')) {
                is_placeholder = true;
            }

            if (is_placeholder) {
                LOG_WARN(kLayer, id_, task.id,
                    "atomic task " + at.id + " has placeholder in input: " + at.input.substr(0,60));
                throw ParseException(
                    "Placeholder path in atomic task " + at.id + ": " + at.input.substr(0,60),
                    llm_out, task.id, kLayer);
            }
        }

        // Validation 3: if description mentions "Use <tool>", must have tool field
        if (at.description.find("Use ") == 0 && at.tool.empty()) {
            LOG_WARN(kLayer, id_, task.id,
                "atomic task " + at.id + " description mentions tool but tool field is empty");
            // Try to extract from description
            size_t tool_start = 4; // after "Use "
            size_t tool_end = at.description.find_first_of(" \t.(", tool_start);
            if (tool_end != std::string::npos) {
                std::string extracted_tool = at.description.substr(tool_start, tool_end - tool_start);
                // Validate extracted tool exists
                for (auto& t : available_tools_) {
                    if (t == extracted_tool) {
                        at.tool = extracted_tool;
                        LOG_INFO(kLayer, id_, task.id,
                            "extracted tool '" + extracted_tool + "' from description");
                        break;
                    }
                }
            }
        }
    }

    return tasks;
}

// -- dispatch ------------------------------------------------------------------

std::vector<AtomicResult> ManagerAgent::dispatch(
        std::vector<AtomicTask> tasks,
        const std::string&      subtask_id) {

    // -- Topological sort: tasks with no depends_on run first (parallel),
    //    tasks with depends_on run after their prerequisites complete.
    //    Input data from a predecessor is injected via input_from field.

    // Map task_id -> index for quick lookup
    std::unordered_map<std::string, size_t> idx_map;
    for (size_t i = 0; i < tasks.size(); ++i)
        idx_map[tasks[i].id] = i;

    // Check if any task has dependencies
    bool has_deps = false;
    for (auto& t : tasks)
        if (!t.depends_on.empty()) { has_deps = true; break; }

    // Result store (indexed same as tasks)
    std::vector<AtomicResult> results(tasks.size());
    std::vector<bool>         done(tasks.size(), false);

    if (!has_deps) {
        // Fast path: all independent  --  full parallel (original behavior)
        std::vector<std::future<AtomicResult>> futures;
        futures.reserve(tasks.size());
        for (size_t i = 0; i < tasks.size(); ++i) {
            AtomicTask   tc     = tasks[i];
            auto worker = select_worker(tc.tool, i);
            if (!worker) {
                AtomicResult fail;
                fail.task_id = tc.id;
                fail.status = TaskStatus::Failed;
                fail.error = "No worker available";
                results.push_back(fail);
                continue;
            }
            LOG_DEBUG(kLayer, id_, subtask_id,
                "parallel dispatch " + tc.id + " -> " + worker->id());
            futures.push_back(pool_.submit(
                [worker, tc = std::move(tc)]() mutable {
                    return worker->execute(tc);
                }));
        }
        for (size_t i = 0; i < futures.size(); ++i) {
            AtomicResult res = futures[i].get();
            if (res.status == TaskStatus::Failed) {
                LOG_WARN(kLayer, id_, subtask_id,
                    "task " + res.task_id + " failed -- local retry");
                res = retry_atomic(tasks[i]);
            }
            results[i] = std::move(res);
        }
        return results;
    }

    // DAG path: wave-by-wave topological execution
    LOG_INFO(kLayer, id_, subtask_id, "DAG dispatch: " +
        std::to_string(tasks.size()) + " tasks with dependencies");

    size_t completed = 0;
    int    max_waves = (int)tasks.size() + 1;  // safety bound

    while (completed < tasks.size() && max_waves-- > 0) {
        // Find tasks whose prerequisites are all done
        std::vector<size_t> ready;
        for (size_t i = 0; i < tasks.size(); ++i) {
            if (done[i]) continue;
            bool prereqs_met = true;
            for (auto& dep_id : tasks[i].depends_on) {
                auto it = idx_map.find(dep_id);
                if (it == idx_map.end() || !done[it->second]) {
                    prereqs_met = false; break;
                }
            }
            if (prereqs_met) ready.push_back(i);
        }

        if (ready.empty()) {
            LOG_ERROR(kLayer, id_, subtask_id, "DAG cycle or unresolvable dependency");
            break;
        }

        // Inject input_from outputs before dispatching
        for (size_t i : ready) {
            auto& t = tasks[i];
            if (!t.input_from.empty()) {
                auto src = idx_map.find(t.input_from);
                if (src != idx_map.end() && done[src->second]) {
                    const std::string& pred_output = results[src->second].output;
                    if (!pred_output.empty() && t.input.empty())
                        t.input = pred_output;
                    else if (!pred_output.empty())
                        t.input = pred_output + "\n" + t.input;
                }
            }
        }

        // Submit this wave in parallel
        std::vector<std::future<AtomicResult>> wave_futures;
        std::vector<size_t> valid_indices;
        wave_futures.reserve(ready.size());
        for (size_t i : ready) {
            AtomicTask   tc     = tasks[i];
            auto worker = select_worker(tc.tool, i);
            if (!worker) {
                AtomicResult fail;
                fail.task_id = tc.id;
                fail.status = TaskStatus::Failed;
                fail.error = "No worker available";
                results[i] = fail;
                continue;
            }
            LOG_DEBUG(kLayer, id_, subtask_id,
                "DAG wave dispatch " + tc.id + " -> " + worker->id());
            wave_futures.push_back(pool_.submit(
                [worker, tc = std::move(tc)]() mutable {
                    return worker->execute(tc);
                }));
            valid_indices.push_back(i);
        }

        // Collect wave results
        for (size_t j = 0; j < valid_indices.size(); ++j) {
            size_t       i   = valid_indices[j];
            AtomicResult res = wave_futures[j].get();
            // C1: Store completed result in session_mem for input_from injection
            if (ctx_.session_mem && res.status == TaskStatus::Done
                    && !res.output.empty()) {
                ctx_.session_mem->set("shared:" + res.task_id,
                    res.output.substr(0, 400));
            }
            if (res.status == TaskStatus::Failed) {
                LOG_WARN(kLayer, id_, subtask_id,
                    "DAG task " + res.task_id + " failed -- retry");
                res = retry_atomic(tasks[i]);
            }
            results[i] = std::move(res);
            done[i]    = true;
            ++completed;
        }
    }
    return results;
}

AtomicResult ManagerAgent::retry_atomic(const AtomicTask& task) noexcept {
    for (int attempt = 1; attempt <= max_atomic_retries_; ++attempt) {
        LOG_INFO(kLayer, id_, task.id,
            "retry " + std::to_string(attempt) + "/" + std::to_string(max_atomic_retries_));
        std::this_thread::sleep_for(
            std::chrono::milliseconds(250 * (1 << (attempt - 1))));
        auto worker = select_worker(task.tool, (size_t)attempt);
        if (!worker) continue;
        AtomicResult res = worker->execute(task);
        if (res.status != TaskStatus::Failed) return res;
    }
    AtomicResult fail;
    fail.task_id = task.id;
    fail.status  = TaskStatus::Failed;
    fail.error   = "Exhausted " + std::to_string(max_atomic_retries_) + " local retries";
    return fail;
}

// -- validate ------------------------------------------------------------------

std::pair<bool,std::string> ManagerAgent::validate(
        const SubTask& task,
        const std::vector<AtomicResult>& results) const {

    // Fast-path shortcut: if all results came from tool fast-path (no LLM),
    // they are raw tool output — valid by definition. Skip LLM validation.
    // EXCEPTION: creation tasks (write_file, create, generate) need quality check
    bool all_fast_path = !results.empty() &&
        std::all_of(results.begin(), results.end(),
            [](const AtomicResult& r){ return r.fast_path && r.status == TaskStatus::Done; });
    bool needs_validation = task.description.find("write") != std::string::npos ||
                           task.description.find("create") != std::string::npos ||
                           task.description.find("generate") != std::string::npos;
    if (all_fast_path && !needs_validation) {
        LOG_INFO(kLayer, id_, task.id,
            "validate fast-pass: all results from tool fast-path, no LLM needed");
        return {true, ""};  // raw tool output is trusted, skip LLM
    }
    if (all_fast_path && needs_validation) {
        LOG_INFO(kLayer, id_, task.id,
            "creation task detected: performing quality validation despite fast-path");
    }

    nlohmann::json results_j = nlohmann::json::array();
    for (const auto& r : results) {
        nlohmann::json rj; to_json(rj, r);
        results_j.push_back(std::move(rj));
    }

    std::ostringstream user_msg;
    user_msg << "## Subtask\n" << task.description << "\n\n"
             << "## Acceptance criteria\n" << task.expected_output << "\n\n"
             << "## Execution results\n" << results_j.dump(2) << "\n\n";

    // Enhanced validation for creation tasks
    if (needs_validation) {
        user_msg << "## Quality Check (STRICT)\n"
                 << "REJECT (approved=false) if ANY of these issues exist:\n"
                 << "1. Syntax errors in code (SyntaxError, parse failures)\n"
                 << "2. File contains format markers (---CONTENT---, ---BEGIN---, etc.)\n"
                 << "3. Placeholders remain (TODO, FIXME, <placeholder>, ...)\n"
                 << "4. Missing required components (imports, entry point, etc.)\n"
                 << "5. Invalid config syntax or broken references\n"
                 << "If ANY issue found, set approved=false with specific feedback.\n\n";
    }

    user_msg << "Evaluate. Respond with ONLY:\n"
             << "{\"approved\": true or false, "
             << "\"feedback\": \"<mandatory if false, else empty>\"}";

    try {
        ctx_.log_info(task.id, EvType::LlmCall, "validate LLM call");
        if (ctx_.state) ctx_.state->record_call();

        std::string prompt = validate_prompt_;
        if (ctx_.prompt_opt) {
            std::string opt = ctx_.prompt_opt->select_best("manager-validate");
            if (!opt.empty()) prompt = opt;
        }

        std::string    llm_out  = client_.complete(prompt, user_msg.str(), task.id);
        nlohmann::json j        = parse_llm_json(llm_out);
        bool           approved = j.at("approved").get<bool>();
        std::string    feedback;
        if (j.contains("feedback")) feedback = j.at("feedback").get<std::string>();
        return {approved, feedback};
    } catch (const std::exception& e) {
        LOG_ERROR(kLayer, id_, task.id,
            std::string("validate LLM failed: ") + e.what());
        return {false, std::string("Validation error: ") + e.what()};
    }
}

// -- helpers -------------------------------------------------------------------

std::shared_ptr<WorkerAgent> ManagerAgent::select_worker(const std::string& tool,
                                          size_t hint) const noexcept {
    if (workers_.empty()) return nullptr;
    if (tool.empty() || workers_.size() == 1)
        return workers_[hint % workers_.size()];

    // Pick the Worker with the best historical success rate for this tool
    std::shared_ptr<WorkerAgent> best;
    float        best_rt = -1.0f;
    for (const auto& w : workers_) {
        if (!w) continue;
        float r = w->tool_success_rate(tool);
        if (r > best_rt) { best_rt = r; best = w; }
    }
    // Only use capability-based selection if we have real data (not all 0.5 default)
    if (best && best_rt > 0.5f) return best;
    return workers_[hint % workers_.size()];
}

std::string ManagerAgent::build_summary(const std::vector<AtomicResult>& results,
                                         bool approved) {
    // Build summary that puts ACTUAL TOOL OUTPUT first
    // This is what Director synthesise and Review will see
    std::ostringstream ss;

    for (size_t i = 0; i < results.size(); ++i) {
        const auto& r = results[i];
        if (!r.output.empty())
            ss << r.output;
        if (!r.error.empty() && r.error != "")
            ss << "\n[error: " << r.error << "]";
        if (i + 1 < results.size()) ss << "\n---\n";
    }

    std::string out = ss.str();
    // Trim
    while (!out.empty() && (out.back()=='\n'||out.back()==' ')) out.pop_back();

    // Append status only for Manager's internal tracking (used in logs)
    // Director synthesise will format the output nicely for the user
    if (out.empty()) {
        size_t done = 0, failed = 0;
        for (const auto& r : results) {
            if (r.status == TaskStatus::Done)   ++done;
            if (r.status == TaskStatus::Failed) ++failed;
        }
        out = (approved ? "Tasks completed" : "Tasks failed");
        if (failed > 0) out += " (" + std::to_string(failed) + " failed)";
    }
    return out;
}

} // namespace agent
