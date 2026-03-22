// =============================================================================
// src/agent/worker_agent.cpp  --  v2: AgentContext wired in
// Backward compatible: ctx defaults to AgentContext{} (all nullptrs = no-op)
// =============================================================================
#include "agent/worker_agent.hpp"
#include "agent/file_type_handler.hpp"
#include "utils/experience_manager.hpp"
#include "agent/exceptions.hpp"
#include "agent/reflection.hpp"
#include "agent/skill_registry.hpp"
#include "utils/logger.hpp"
#include "utils/json_utils.hpp"
#include <nlohmann/json.hpp>
#include <sstream>
#include <thread>
#include <chrono>

namespace agent {

static const char* kLayer = "Worker";

WorkerAgent::WorkerAgent(std::string    id,
                         ApiClient&     client,
                         ToolRegistry&  registry,
                         std::string    system_prompt,
                         int            max_atomic_retries,
                         AgentContext   ctx)
    : id_(std::move(id))
    , client_(client)
    , registry_(registry)
    , system_prompt_(std::move(system_prompt))
    , max_retries_(max_atomic_retries)
    , ctx_(std::move(ctx))
{
    // Initialise state machine if context is active
    if (ctx_.state)
        ctx_.state->transition(AgentState::Idle);
    ctx_.log_info("", EvType::AgentCreated,
        "Worker " + id_ + " created");
}

// -- Control helpers -----------------------------------------------------------

bool WorkerAgent::check_control() const noexcept {
    if (ctx_.cancelled()) return false;
    // Honour pause: spin-wait until resumed or cancelled
    if (ctx_.paused()) return ctx_.wait_while_paused();
    return true;
}

void WorkerAgent::report_progress(int pct, const std::string& step,
                                   const std::string& task_id) const noexcept {
    if (!ctx_.bus) return;
    std::string payload = "{\"pct\":" + std::to_string(pct) +
                          ",\"step\":\"" + step + "\"}";
    ctx_.send(ctx_.parent_id, MsgType::Progress, payload);
    ctx_.log_info(task_id, EvType::ProgressReport,
        step + " (" + std::to_string(pct) + "%)");
}

// -- drain_corrections: read supervisor corrections from bus -----------------

std::string WorkerAgent::drain_corrections() const noexcept {
    if (!ctx_.bus) return "";
    std::string note;
    while (auto msg = ctx_.bus->try_receive(id_)) {
        if (msg->type == MsgType::Correct) {
            // Extract "note" field from payload JSON
            auto& pl = msg->payload;
            auto npos = pl.find("\"note\":");
            if (npos != std::string::npos) {
                npos += 7;
                while (npos < pl.size() && (pl[npos]=='"'||pl[npos]==' ')) ++npos;
                auto epos = pl.find('"', npos);
                if (epos != std::string::npos)
                    note += pl.substr(npos, epos - npos) + " ";
            }
        }
    }
    return note;
}

// -- is_agent_giving_up --------------------------------------------------------
// Detects when the LLM's raw output indicates it cannot complete the task,
// as opposed to a JSON parse error. These are legitimate "I give up" signals.
bool WorkerAgent::is_agent_giving_up(const std::string& llm_out) noexcept {
    if (llm_out.empty()) return false;

    // If it looks like a valid JSON response, it's not giving up
    // (even if status=failed, that's a normal failure, not a give-up)
    const std::string& s = llm_out;
    if (s.find("{") != std::string::npos &&
        s.find("\"status\"") != std::string::npos)
        return false;

    // Detect natural-language give-up phrases (multi-lingual)
    std::string lower;
    lower.reserve(s.size());
    for (unsigned char c : s)
        lower.push_back((char)std::tolower(c));

    static const char* GIVE_UP[] = {
        "i cannot", "i can't", "i am unable", "i'm unable",
        "unable to complete", "cannot be completed", "not possible",
        "i don't have access", "i do not have access",
        "no access to", "cannot access the filesystem",
        "as an ai", "as an language model",
        "i apologize, but i cannot",
        nullptr
    };
    for (int i = 0; GIVE_UP[i]; ++i)
        if (lower.find(GIVE_UP[i]) != std::string::npos) return true;

    return false;
}

// -- call_tool_named ----------------------------------------------------------
// Convenience: call a named tool with given input (for ReAct chain calls)
std::string WorkerAgent::call_tool_named(
        const std::string& tool,
        const std::string& input,
        const AtomicTask&  parent_task) const noexcept {
    AtomicTask tmp = parent_task;
    tmp.tool  = tool;
    tmp.input = input;
    return call_tool(tmp);
}

// -- execute -------------------------------------------------------------------

AtomicResult WorkerAgent::execute(const AtomicTask& task) noexcept {
    AtomicResult result;
    result.task_id = task.id;
    result.status  = TaskStatus::Running;

    // State transition: Running/CallingTool
    if (ctx_.state) {
        ctx_.state->transition(AgentState::Running, "CallingTool", task.id);
    }
    LOG_INFO(kLayer, id_, task.id, "starting execution: " + task.description);
    ctx_.log_info(task.id, EvType::TaskStart, task.description);

    // -- Cancellation check before any work -------------------------------
    if (!check_control()) {
        result.status = TaskStatus::Cancelled;
        result.error  = "cancelled before execution";
        if (ctx_.state) ctx_.state->transition(AgentState::Cancelled, "", task.id);
        return result;
    }

    // Compact short-term memory if getting full (context management)
    if (ctx_.memory && ctx_.memory->needs_compact(0.8f))
        ctx_.memory->maybe_compact();

    int react_steps = 0;  // independent ReAct step counter (separate from retry count)

    // C1: Inject input_from predecessor output if available
    if (!task.input_from.empty() && ctx_.session_mem) {
        std::string shared_key = "shared:" + task.input_from;
        std::string predecessor_output = ctx_.session_mem->get(shared_key);
        if (!predecessor_output.empty()) {
            // Prepend predecessor output to this task's input
            const_cast<AtomicTask&>(task).input =
                predecessor_output + "\n" + task.input;
            LOG_INFO(kLayer, id_, task.id,
                "C1: injected predecessor output from " + task.input_from);
        }
    }

    // -- Step 0.5: Extract tool from description if missing --------
    if (task.tool.empty() && !task.description.empty()) {
        std::string desc = task.description;
        auto use_pos = desc.find("Use ");
        if (use_pos != std::string::npos && use_pos < 10) {
            size_t tool_start = use_pos + 4;
            size_t tool_end = desc.find_first_of(" \t.(", tool_start);
            if (tool_end == std::string::npos) tool_end = desc.size();
            std::string extracted_tool = desc.substr(tool_start, tool_end - tool_start);

            if (registry_.has_tool(extracted_tool)) {
                const_cast<AtomicTask&>(task).tool = extracted_tool;
                LOG_INFO(kLayer, id_, task.id,
                    "extracted tool '" + extracted_tool + "' from description");

                auto input_pos = desc.find("Input: ");
                if (input_pos != std::string::npos) {
                    std::string extracted_input = desc.substr(input_pos + 7);
                    while (!extracted_input.empty() &&
                           (extracted_input.front() == '"' || extracted_input.front() == '\'' || extracted_input.front() == ' '))
                        extracted_input = extracted_input.substr(1);
                    while (!extracted_input.empty() &&
                           (extracted_input.back() == '"' || extracted_input.back() == '\'' || extracted_input.back() == ' '))
                        extracted_input.pop_back();
                    const_cast<AtomicTask&>(task).input = extracted_input;
                    LOG_INFO(kLayer, id_, task.id,
                        "extracted input: " + extracted_input.substr(0, 40));
                }
            }
        }
    }

    if (task.tool.empty() && !task.description.empty()) {
        LOG_WARN(kLayer, id_, task.id, "no tool specified, will rely on LLM");
    }

    // -- Step 1: tool call with pre-flight meta-cognition checks --------
    report_progress(10, "tool_call", task.id);

    // Pre-flight 1: detect LLM-generated placeholder paths early
    std::string tool_output;
    if (!task.tool.empty() && !task.input.empty() &&
        is_placeholder_path(task.input)) {
        LOG_WARN(kLayer, id_, task.id,
            "placeholder path in input: " + task.input.substr(0,60));
        ctx_.log_warn(task.id, EvType::ToolCall,
            "placeholder path rejected: " + task.input.substr(0,60));
        tool_output =
            "[Meta-cognition: '" + task.input + "' is a placeholder path. "
            "Use list_dir with input=\"Desktop\" or an absolute path.]";
    }
    // Pre-flight 2: check peer cache to avoid duplicate tool calls
    else if (!task.tool.empty()) {
        std::string cached = check_peer_cache(task.tool, task.input);
        if (!cached.empty()) {
            LOG_INFO(kLayer, id_, task.id,
                "peer cache hit for " + task.tool);
            tool_output = cached;
        }
    }

    // Actual tool call (only if pre-flights did not populate tool_output)
    if (tool_output.empty()) {
        if (!task.tool.empty()) {
            ctx_.log_info(task.id, EvType::ToolCall,
                "invoking " + task.tool,
                "{\"tool\":\"" + task.tool + "\",\"input\":\"" +
                task.input.substr(0, 100) + "\"}"); 
        }
        tool_output = call_tool(task);

        // D1: Broadcast ToolFailed immediately when tool errors out
        // (inline check since is_tool_error lambda is defined later)
        bool _d1_tool_failed = !task.tool.empty() && !tool_output.empty() && (
            tool_output.find("[Tool error:") != std::string::npos ||
            tool_output.find("cannot open") != std::string::npos ||
            tool_output.find("failed:") != std::string::npos);
        if (ctx_.bus && _d1_tool_failed) {
            try {
                nlohmann::json ef;
                ef["agent"] = id_; ef["layer"] = "Worker";
                ef["tool"]  = task.tool;
                ef["error"] = tool_output.size()>100 ? tool_output.substr(0,100)+"..." : tool_output;
                ef["task_id"] = task.id;
                ctx_.bus->broadcast(id_, agent::MsgType::ToolFailed, ef.dump());
            } catch(...) {}
        }
        // Broadcast tool call result for Supervisor visibility
        if (ctx_.bus && !task.tool.empty()) {
            try {
                nlohmann::json dj;
                dj["agent"] = id_; dj["layer"] = "Worker"; dj["event"] = "tool_call";
                dj["task_id"] = task.id; dj["tool"] = task.tool;
                std::string in_s  = task.input.size()>80   ? task.input.substr(0,80)+"..."   : task.input;
                std::string out_s = tool_output.size()>150  ? tool_output.substr(0,150)+"..." : tool_output;
                dj["input"] = in_s; dj["output"] = out_s;
                ctx_.bus->broadcast(id_, agent::MsgType::ToolCall, dj.dump());
            } catch(...) {}
        }
    }

    // -- Autonomous reflection: try to self-heal tool failures before involving LLM --
    // ReflectionEngine decides: switch tools, adjust input, or surface honest error
    // Structured failure detection: check known error prefixes
    // (replaces fragile string matching with a single helper)
    auto is_tool_error = [](const std::string& out) -> bool {
        if (out.empty()) return false;
        return out.find("[Tool error:") != std::string::npos ||
               out.find("[Tool '")      != std::string::npos ||
               out.find("cannot open")  != std::string::npos ||
               out.find("failed:")      != std::string::npos;
    };
    bool tool_failed = is_tool_error(tool_output);

    // Recall past experience with this tool/error type
    std::string experience_hint;
    // Recall experience: session_mem first (cross-run), then run_memory
    if (ctx_.session_mem && !task.tool.empty()) {
        experience_hint = ReflectionEngine::recall_experience(
            *ctx_.session_mem, task.tool + " " + tool_output.substr(0,80));
    }
    if (experience_hint.empty() && ctx_.memory && !task.tool.empty()) {
        experience_hint = ReflectionEngine::recall_experience(
            *ctx_.memory, task.tool + " " + tool_output.substr(0,80));
    }

    // ── TOOL SUCCESS FAST-PATH ─────────────────────────────────────────────
    // When a tool ran and produced output without errors, return immediately.
    // No LLM needed to "reformat" the result — the tool output IS the result.
    // Fixes: parse_failed on large file contents, LLM timeouts on read_file.
    if (!task.tool.empty() && !tool_output.empty()) {
        // Detect tool errors (non-empty error markers mean LLM self-healing needed)
        auto tool_has_error = [&](const std::string& out) -> bool {
            return out.find("[Tool error:")   != std::string::npos ||
                   out.find("[Tool '")        != std::string::npos ||
                   out.find("cannot open")    != std::string::npos ||
                   out.find("path not found") != std::string::npos ||
                   out.find("Access is denied") != std::string::npos;
        };
        // [OUTPUT TRUNCATED is a success (long output), not an error
        bool is_error = tool_has_error(tool_output);

        if (!is_error) {
            AtomicResult fast;
            fast.task_id = task.id;
            fast.status  = TaskStatus::Done;
            fast.output  = tool_output;
            fast.thought    = "Tool " + task.tool + " succeeded (" +
                               std::to_string(tool_output.size()) + " B) -- fast-path, no LLM";
            fast.fast_path  = true;

            LOG_INFO(kLayer, id_, task.id,
                "fast-path done: " + task.tool + " (" +
                std::to_string(tool_output.size()) + " bytes, no LLM needed)");

            // Record experience & update env_kb
            if (ctx_.skills) ctx_.skills->record_tool_outcome(task.tool, true);
            if (ctx_.session_mem)
                ReflectionEngine::record_success(*ctx_.session_mem, task.tool,
                    task.input.substr(0,60),
                    tool_output.substr(0, std::min(tool_output.size(), size_t(60))));
            // Record to ExperienceManager (persistent cross-conversation experience)
            if (ctx_.exp_mgr_ptr) {
                auto* em = static_cast<agent::ExperienceManager*>(ctx_.exp_mgr_ptr.get());
                em->record(task.tool, task.input.substr(0,60), "success",
                    "Tool succeeded: " + tool_output.substr(0, std::min(tool_output.size(), size_t(80))));
            }
            if (ctx_.env_kb) {
                if (task.tool == "get_current_dir")
                    ctx_.env_kb->confirm("cwd", tool_output.substr(0,200));
                else if (task.tool == "read_file" || task.tool == "stat_file")
                    ctx_.env_kb->confirm("file:" + task.input.substr(0,80), "readable");
                else if (task.tool == "list_dir") {
                    std::string key = "dir:" + (task.input.empty() ? "." : task.input.substr(0,50));
                    ctx_.env_kb->confirm(key, tool_output.substr(0,300));
                }
            }
            if (ctx_.bus) {
                try {
                    nlohmann::json dj; dj["agent"]=id_; dj["event"]="fast_path";
                    dj["tool"]=task.tool; dj["bytes"]=std::to_string(tool_output.size())+"B";
                    ctx_.bus->broadcast(id_, agent::MsgType::ToolCall, dj.dump());
                } catch(...) {}
            }
            if (ctx_.state) ctx_.state->transition(AgentState::Done, "", task.id);
            return fast;
        }
    }
    // ── END FAST-PATH ──────────────────────────────────────────────────────

    // Up to 2 autonomous self-fix attempts before escalating to LLM
    for (int fix_attempt = 0; fix_attempt < 2 && tool_failed; ++fix_attempt) {
        auto fix = ReflectionEngine::reflect_tool_failure(
            task.tool, tool_output, task.input,
            task.description, fix_attempt);

        if (!fix || !fix->should_retry) break;  // no known fix -> let LLM handle

        LOG_INFO(kLayer, id_, task.id,
            "self-fix attempt " + std::to_string(fix_attempt+1) +
            ": " + fix->fix_description.substr(0,80));
        ctx_.log_info(task.id, EvType::ToolCall, "self-fix: " + fix->new_tool,
            "{\"fix\":\"" + fix->fix_description.substr(0,60) + "\"}");

        AtomicTask fix_task;
        fix_task.id          = task.id + "-fix" + std::to_string(fix_attempt+1);
        fix_task.tool        = fix->new_tool;
        fix_task.input       = fix->new_input;
        fix_task.description = fix->fix_description;

        std::string fix_out = call_tool(fix_task);
        bool fix_failed = fix_out.find("[Tool error:") != std::string::npos ||
                          fix_out.find("[Tool '") != std::string::npos;

        if (!fix_failed) {
            // Self-fix succeeded -- record experience, update output
            if (ctx_.memory)
                ReflectionEngine::record_success(*ctx_.memory,
                    fix->new_tool, fix->new_input.substr(0,60),
                    fix_out.substr(0,60));
                if (ctx_.session_mem) {
                    ReflectionEngine::record_success(*ctx_.session_mem,
                    fix->new_tool, fix->new_input.substr(0,60),
                    fix_out.substr(0,60));
                }
            tool_output = "[" + task.tool + " failed, " +
                          fix->new_tool + " succeeded:]\n" + fix_out;
            if (!fix->context_note.empty())
                tool_output = fix->context_note + "\n" + tool_output;
            ctx_.log_info(task.id, EvType::ToolResult, "self-fix succeeded");
            tool_failed = false;
            // ── Self-fix fast-path: tool succeeded → return directly, skip LLM ──
            // (same logic as initial fast-path — avoids parse_failed on large output)
            {
                AtomicResult fast;
                fast.task_id   = task.id;
                fast.status    = TaskStatus::Done;
                fast.output    = fix_out;
                fast.fast_path = true;
                fast.thought   = "self-fix succeeded via " + fix_task.tool +
                                 " (" + std::to_string(fix_out.size()) + "B) -- fast-path";
                if (ctx_.memory)
                    ReflectionEngine::record_success(*ctx_.memory, fix_task.tool,
                        fix_task.input.substr(0,60),
                        fix_out.substr(0, std::min(fix_out.size(), size_t(60))));
                if (ctx_.exp_mgr_ptr) {
                    auto* em = static_cast<agent::ExperienceManager*>(ctx_.exp_mgr_ptr.get());
                    em->record(fix_task.tool, fix_task.input.substr(0,60), "success",
                        "self-fix: " + fix_out.substr(0, std::min(fix_out.size(), size_t(60))));
                }
                return fast;
            }
        } else {
            // Fix also failed -- record and try next fix or give up
            if (ctx_.memory)
                ReflectionEngine::record_failure(*ctx_.memory,
                    ReflectionEngine::classify_error(tool_output),
                    task.tool + ":" + task.input.substr(0,40),
                    "tried " + fix->new_tool + " - also failed");
                if (ctx_.session_mem) {
                    ReflectionEngine::record_failure(*ctx_.session_mem,
                    ReflectionEngine::classify_error(tool_output),
                    task.tool + ":" + task.input.substr(0,40),
                    "tried " + fix->new_tool + " - also failed");
                }
            tool_output = "[" + task.tool + " failed: " + tool_output +
                          "]\n[" + fix->new_tool + " also failed: " + fix_out + "]";
            ctx_.log_info(task.id, EvType::ToolResult, "self-fix also failed");
        }
    }

    if (!task.tool.empty()) {
        ctx_.log_info(task.id, EvType::ToolResult,
            "tool returned " + std::to_string(tool_output.size()) + " bytes");
    }

    // -- Step 2: LLM call with retry on parse failure ----------------------
    if (ctx_.state) ctx_.state->transition(AgentState::Running, "WaitingLLM", task.id);
    report_progress(30, "llm_call", task.id);

    // Inject short-term memory context if available
    std::string memory_context;
    if (ctx_.memory && !ctx_.memory->short_term_empty()) {
        auto msgs = ctx_.memory->get_context(4);
        std::ostringstream mc;
        mc << "\n\n## Context from previous steps\n";
        for (auto& m : msgs)
            mc << "[" << m.role << "] " << m.content.substr(0, 300) << "\n";
        memory_context = mc.str();
    }

    std::string format_hint;
    std::string repair_hint;
    for (int attempt = 0; attempt <= max_retries_; ++attempt) {

        if (!check_control()) {
            result.status = TaskStatus::Cancelled;
            result.error  = "cancelled during LLM retry";
            if (ctx_.state) ctx_.state->transition(AgentState::Cancelled, "", task.id);
            return result;
        }

        try {
            if (ctx_.state) ctx_.state->record_call();
            // Drain any supervisor corrections injected via bus
            std::string sup_correction = drain_corrections();
            std::string extra_context = memory_context;
            if (!sup_correction.empty())
                extra_context += "\n\n[Supervisor correction]: " + sup_correction;

            std::string user_msg = build_user_message(task, tool_output, format_hint)
                                 + extra_context;
            // Prepend relevant past experience if available
            if (!experience_hint.empty())
                user_msg = "## Past Experience\n" + experience_hint + "\n\n" + user_msg;

            // On parse_failed retries: prepend repair hint so LLM formats only
            if (attempt > 0 && !repair_hint.empty())
                user_msg = repair_hint + "\n\n" + user_msg;

            ctx_.log_info(task.id, EvType::LlmCall,
                "calling LLM (attempt " + std::to_string(attempt+1) + ")");

            auto t_start = std::chrono::steady_clock::now();
            std::string llm_out;
            client_.complete_stream(system_prompt_, user_msg,
                [&](const std::string& chunk) {
                    llm_out += chunk;
                    if (ctx_.bus) {
                        nlohmann::json cj;
                        cj["agent"] = id_; cj["chunk"] = chunk;
                        ctx_.bus->broadcast(id_, MsgType::Dialog, cj.dump());
                    }
                }, task.id);
            auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - t_start).count();

            // D1: Broadcast SlowWarning if LLM took > 20s
            if (elapsed_ms > 20000 && ctx_.bus) {
                try {
                    nlohmann::json sw;
                    sw["agent"] = id_; sw["elapsed_ms"] = std::to_string(elapsed_ms) + "ms";
                    sw["task_id"] = task.id; sw["attempt"] = std::to_string(attempt);
                    ctx_.bus->broadcast(id_, agent::MsgType::SlowWarning, sw.dump());
                } catch(...) {}
            }

            ctx_.log_info(task.id, EvType::LlmResponse,
                "LLM responded (" + std::to_string(llm_out.size()) + " bytes)");

            // Broadcast LLM decision for Supervisor visibility
            if (ctx_.bus) {
                nlohmann::json dj;
                dj["agent"] = id_; dj["layer"] = "Worker"; dj["event"] = "llm_decision";
                dj["task_id"] = task.id; dj["attempt"] = std::to_string(attempt);
                dj["summary"] = llm_out.substr(0, 150);
                ctx_.bus->broadcast(id_, agent::MsgType::LlmDecision, dj.dump());
            }

            if (ctx_.state) ctx_.state->transition(AgentState::Running, "Parsing", task.id);

            // Early stopping: detect when LLM is giving up (not a parse error)
            // This prevents wasting retries on fundamentally impossible tasks
            if (is_agent_giving_up(llm_out)) {
                LOG_WARN(kLayer, id_, task.id,
                    "LLM indicated task cannot be completed -- early stop");
                ctx_.log_warn(task.id, EvType::ToolCall,
                    "early stop: agent gave up");
                // D1: Broadcast GivingUp for event-driven Supervisor
                if (ctx_.bus) {
                    try {
                        nlohmann::json gj;
                        gj["agent"]="Worker"; gj["task_id"]=task.id;
                        gj["reason"]=llm_out.size()>100 ? llm_out.substr(0,100) : llm_out;
                        ctx_.bus->broadcast(id_, agent::MsgType::GivingUp, gj.dump());
                    } catch(...) {}
                }
                // Return done with the LLM's explanation as output
                result.status = TaskStatus::Done;
                result.output = llm_out.substr(0, 500);
                if (ctx_.state) ctx_.state->transition(AgentState::Done, "", task.id);
                return result;
            }

            // ReAct: JSON-parse based chain detection (not string matching)
            // If LLM returns {"status":"continue","tool":"X","input":"Y"} we chain
            // Uses independent react_steps counter (separate from LLM retry count)
            {
                static const int kMaxReactSteps = 5;
                auto try_react = [&]() -> bool {
                    if (react_steps >= kMaxReactSteps) {
                        LOG_WARN(kLayer, id_, task.id,
                            "ReAct max steps reached (" +
                            std::to_string(kMaxReactSteps) + ")");
                        return false;
                    }
                    // Use proper JSON parse — not string matching
                    AtomicResult tmp;
                    if (!parse_response(llm_out, tmp)) return false;
                    // Running status = LLM wants to continue with another tool
                    if (tmp.status != TaskStatus::Running) return false;
                    // Extract next tool and input from the parsed result
                    // parse_response stores tool in tmp.error and input in tmp.output
                    // when status=running (chain intent)
                    std::string next_tool  = tmp.error;
                    std::string next_input = tmp.output;
                    if (next_tool.empty()) return false;
                    ++react_steps;
                    LOG_INFO(kLayer, id_, task.id,
                        "ReAct step " + std::to_string(react_steps) +
                        ": " + next_tool + "(" + next_input.substr(0,40) + ")");
                    std::string chain_out = call_tool_named(next_tool, next_input, task);
                    // Inject tool result into next LLM context
                    tool_output = chain_out;
                    format_hint = "";
                    return true;
                };
                if (try_react()) continue;
            }

            if (parse_response(llm_out, result)) {
                // Update capability success rate (EMA: rate = 0.9*rate + 0.1*1.0)
                if (ctx_.skills && !task.tool.empty()) {
                    ctx_.skills->record_tool_outcome(task.tool, true);
                }

                // Log thought/reasoning for observability
                if (!result.thought.empty()) {
                    ctx_.log_info(task.id, EvType::LlmResponse,
                        "thought: " + result.thought.substr(0, 120));
                    // Broadcast thought for Supervisor visibility
                    if (ctx_.bus) {
                        nlohmann::json tj;
                        tj["agent"] = id_; tj["event"] = "thought";
                        tj["task_id"] = task.id;
                        tj["thought"] = result.thought.substr(0, 200);
                        ctx_.bus->broadcast(id_, agent::MsgType::Dialog, tj.dump());
                    }
                }
                // Update environment knowledge base with discovered facts
                if (ctx_.env_kb && !task.tool.empty() && !result.output.empty()) {
                    const auto& out = result.output;
                    const auto& inp = task.input;
                    if (task.tool == "get_current_dir") {
                        ctx_.env_kb->confirm("cwd", out.substr(0, 200));
                    } else if (task.tool == "list_dir") {
                        std::string key = "dir:" + (inp.empty() ? "." : inp.substr(0,50));
                        ctx_.env_kb->confirm(key, out.substr(0, 400));
                    } else if (task.tool == "stat_file") {
                        ctx_.env_kb->confirm("file:" + inp.substr(0,80), "exists");
                    } else if (task.tool == "read_file") {
                        ctx_.env_kb->confirm("file:" + inp.substr(0,80), "readable");
                    } else if (task.tool == "get_sysinfo") {
                        ctx_.env_kb->confirm("sysinfo", out.substr(0, 300));
                    }
                }
                // Push successful output to short-term memory + record experience
                if (ctx_.memory) {
                    ctx_.memory->push_result(task.id, result.output);
                    if (!task.tool.empty() && result.status == TaskStatus::Done)
                        ReflectionEngine::record_success(*ctx_.memory,
                            task.tool, task.input.substr(0,60),
                            result.output.substr(0,60));
                if (ctx_.session_mem) {
                    ReflectionEngine::record_success(*ctx_.session_mem,
                            task.tool, task.input.substr(0,60),
                            result.output.substr(0,60));
                }
                }
                // Update capability profile (success)
                if (ctx_.skills && !task.tool.empty())
                    ctx_.skills->update_capability(id_, task.tool, true);

                LOG_INFO(kLayer, id_, task.id,
                    "completed with status: " + status_to_string(result.status));
                ctx_.log_info(task.id, EvType::TaskEnd,
                    status_to_string(result.status));

                report_progress(100, "done", task.id);

                // Report result to Manager via bus
                if (ctx_.bus) {
                    std::string payload =
                        "{\"task_id\":\"" + task.id + "\","
                        "\"status\":\"" + status_to_string(result.status) + "\","
                        "\"output\":\"" + result.output.substr(0, 200) + "\"}";
                    ctx_.send(ctx_.parent_id, MsgType::Result, payload);
                }

                // Broadcast Peer: share tool result with sibling Workers
                if (ctx_.bus && !task.tool.empty() && !tool_output.empty() && !tool_failed) {
                    std::string peer_key = task.tool;
                    for (auto& c : peer_key)
                        if (!std::isalnum((unsigned char)c)) c = '_';
                    // Sanitize input into key suffix
                    std::string inp_key = task.input.substr(0, 40);
                    for (auto& c : inp_key)
                        if (!std::isalnum((unsigned char)c)) c = '_';
                    peer_key += "_" + inp_key;
                    std::string peer_pl = "{\"type\":\"tool_result\","
                        "\"tool\":\"" + task.tool + "\"," 
                        "\"shared_key\":\"" + peer_key + "\"}"; 
                    ctx_.bus->broadcast(id_, MsgType::Peer, peer_pl);
                    // Write to shared/ for persistent cross-Worker access
                    if (!ctx_.workspace.shared_dir.empty()) {
                        try { agent::ResourceManager::write_shared(
                            ctx_.workspace.shared_dir, peer_key+".txt",
                            tool_output, id_); } catch (...) {}
                    }
                }

                if (ctx_.state) ctx_.state->transition(AgentState::Done, "", task.id);
                return result;
            }

            format_hint =
                "\n\n[IMPORTANT] Your previous response could not be parsed as JSON. "
                "You MUST respond with ONLY a JSON object: "
                "{\"status\": \"done\" or \"failed\", \"output\": \"<r>\", "
                "\"error\": \"<empty or error>\"}. No text outside the JSON.";
            LOG_WARN(kLayer, id_, task.id,
                "parse failed, attempt " + std::to_string(attempt + 1));
            ctx_.log_warn(task.id, EvType::ParseFail,
                "parse failed attempt " + std::to_string(attempt+1));
            // Inject repair instruction into next attempt so LLM knows to format only
            repair_hint = "REPAIR: Your last response was not valid JSON.\n"
                          "The tool already ran. DO NOT call any tool again.\n"
                          "Return ONLY this JSON (single line, no markdown):\n"
                          "{\"status\":\"done\",\"tool\":\"<tool>\","
                          "\"output\":\"<first 200 chars of result>\","
                          "\"thought\":\"tool succeeded\"}";
            if (ctx_.state) ctx_.state->record_failure("parse_fail");

        } catch (const ModelException& e) {
            result.status = TaskStatus::Failed;
            result.error  = std::string("API error: ") + e.what();
            LOG_ERROR(kLayer, id_, task.id, result.error);
            ctx_.log_error(task.id, EvType::TaskEnd, result.error);
            if (ctx_.state) {
                ctx_.state->record_failure(result.error);
                ctx_.state->transition(AgentState::Failed, "", task.id);
            }
            if (ctx_.skills && !task.tool.empty())
                ctx_.skills->update_capability(id_, task.tool, false);
            return result;
        } catch (const AgentException& e) {
            if (attempt == max_retries_) {
                result.status = TaskStatus::Failed;
                result.error  = std::string("Agent error: ") + e.what();
                LOG_ERROR(kLayer, id_, task.id, result.error);
                if (ctx_.state) {
                    ctx_.state->record_failure(result.error);
                    ctx_.state->transition(AgentState::Failed, "", task.id);
                }
                return result;
            }
            LOG_WARN(kLayer, id_, task.id,
                std::string("attempt ") + std::to_string(attempt+1) +
                " failed: " + e.what());
            if (ctx_.state) ctx_.state->record_failure(e.what());
        } catch (const std::exception& e) {
            result.status = TaskStatus::Failed;
            result.error  = std::string("Unexpected error: ") + e.what();
            LOG_ERROR(kLayer, id_, task.id, result.error);
            if (ctx_.state) {
                ctx_.state->record_failure(result.error);
                ctx_.state->transition(AgentState::Failed, "", task.id);
            }
            return result;
        }
    }

    result.status = TaskStatus::Failed;
    result.error  = "Exceeded max retries (" + std::to_string(max_retries_) + ")";
    LOG_ERROR(kLayer, id_, task.id, result.error);
    ctx_.log_error(task.id, EvType::TaskEnd, result.error);
    if (ctx_.state) {
        ctx_.state->record_failure(result.error);
        ctx_.state->transition(AgentState::Failed, "", task.id);
    }
    return result;
}

// -- build_user_message --------------------------------------------------------

std::string WorkerAgent::build_user_message(const AtomicTask&  task,
                                             const std::string& tool_output,
                                             const std::string& format_hint) const {
    std::ostringstream ss;
    ss << "## Task\n" << task.description;
    if (!task.input.empty())
        ss << "\n\n## Input\n" << task.input;

    if (!tool_output.empty()) {
        // Detect tool failures and add reflection guidance
        bool is_tool_error =
            tool_output.find("[Tool error:") != std::string::npos ||
            tool_output.find("[Tool '") != std::string::npos ||
            tool_output.find("cannot open") != std::string::npos ||
            tool_output.find("cannot access") != std::string::npos ||
            tool_output.find("path not found") != std::string::npos;

        if (is_tool_error) {
            ss << "\n\n## Tool Attempt Failed\n" << tool_output;
            ss << "\n\n## Reflection Guidance\n"
               << "The tool failed. Think step by step before responding:\n"
               << "1. Is the input path/format correct? Try an alternative if so.\n"
               << "2. Can run_command reach the same resource?\n"
               << "   Windows: type \"path\", dir \"path\", more \"path\"\n"
               << "   Linux:   cat path, ls path\n"
               << "3. Does the resource actually exist? stat_file or run_command can verify.\n"
               << "4. If the resource genuinely does not exist: report that clearly.\n"
               << "Set status=\"done\" and put your findings (success or error) in \"output\".";
        } else {
            ss << "\n\n## Tool Output (" << task.tool << ")\n" << tool_output;
        }
    }

    ss << "\n\n## Response Format\n"
       << "Respond with ONLY a JSON object (no markdown fences, no prose):\n"
       << "{\n"
       << "  \"status\": \"done\" or \"failed\",\n"
       << "  \"output\": \"<your complete result  --  include all findings, errors, or data>\",\n"
       << "  \"error\":  \"<empty string, or brief error note if status is failed>\"\n"
       << "}\n"
       << "RULE: If a tool failed, still set status=\"done\" and explain in \"output\".\n"
       << "status=\"failed\" means you cannot produce ANY output at all.";
    if (!format_hint.empty()) ss << format_hint;
    return ss.str();
}


// -- call_tool -----------------------------------------------------------------

std::string WorkerAgent::call_tool(const AtomicTask& task) const noexcept {
    if (task.tool.empty()) return "";
    if (!registry_.has_tool(task.tool)) {
        LOG_WARN(kLayer, id_, task.id,
            "tool not found: " + task.tool + " -- proceeding without it");
        return "[Tool '" + task.tool + "' is not available]";
    }

    // File type pre-check for read_file
    if (task.tool == "read_file" && !task.input.empty()) {
        if (auto special = FileTypeHandler::handle_special_file(task.input, "read"); special) {
            LOG_INFO(kLayer, id_, task.id, "file type handler: " + *special);
            return *special;
        }
    }

    LOG_DEBUG(kLayer, id_, task.id, "invoking tool: " + task.tool);

    auto start = std::chrono::steady_clock::now();
    bool success = false;
    std::string out;
    FailureCategory fail_cat = FailureCategory::None;

    try {
        out = registry_.invoke(task.tool, task.input, task.id);
        success = true;
    } catch (const ToolException& e) {
        LOG_WARN(kLayer, id_, task.id, std::string("tool failed: ") + e.what());
        out = "[Tool error: " + std::string(e.what()) + "]";
        fail_cat = classify_failure(out);
    } catch (...) {
        out = "[Tool error: unknown exception]";
        fail_cat = FailureCategory::Unknown;
    }

    auto end = std::chrono::steady_clock::now();
    double duration_ms = std::chrono::duration<double, std::milli>(end - start).count();

    // Record stats
    if (ctx_.tool_stats)
        ctx_.tool_stats->record_call(task.tool, success, duration_ms);

    // Record experience
    if (ctx_.exp_mgr_ptr) {
        auto* exp = static_cast<ExperienceManager*>(ctx_.exp_mgr_ptr.get());
        std::string lesson = success ? "Tool succeeded" : out.substr(0, 100);
        exp->record(task.tool, task.input, success ? "success" : "failure", lesson, fail_cat);
    }

    return out;
}

// -- classify_failure ----------------------------------------------------------

FailureCategory WorkerAgent::classify_failure(const std::string& error) const noexcept {
    if (error.find("syntax") != std::string::npos ||
        error.find("parse") != std::string::npos ||
        error.find("compilation") != std::string::npos)
        return FailureCategory::SyntaxError;
    if (error.find("timeout") != std::string::npos ||
        error.find("timed out") != std::string::npos)
        return FailureCategory::Timeout;
    if (error.find("permission") != std::string::npos ||
        error.find("denied") != std::string::npos ||
        error.find("access") != std::string::npos)
        return FailureCategory::PermissionDenied;
    if (error.find("not found") != std::string::npos ||
        error.find("does not exist") != std::string::npos ||
        error.find("no such") != std::string::npos)
        return FailureCategory::NotFound;
    if (error.find("environment") != std::string::npos ||
        error.find("dependency") != std::string::npos ||
        error.find("missing") != std::string::npos)
        return FailureCategory::EnvError;
    if (error.find("logic") != std::string::npos ||
        error.find("runtime") != std::string::npos ||
        error.find("assertion") != std::string::npos)
        return FailureCategory::LogicError;
    return FailureCategory::Unknown;
}

// -- parse_response ------------------------------------------------------------

bool WorkerAgent::parse_response(const std::string& llm_output,
                                  AtomicResult& out) const noexcept {
    try {
        nlohmann::json j = parse_llm_json(llm_output);
        // Extract thought/reasoning (CoT field) if present
        if (j.contains("thought"))
            out.thought = j.at("thought").get<std::string>();
        else if (j.contains("reasoning"))
            out.thought = j.at("reasoning").get<std::string>();
        else if (j.contains("analysis"))
            out.thought = j.at("analysis").get<std::string>();

        // Tolerate missing "status" -- default to "done" if output is present
        if (j.contains("output")) {
            out.output = j.at("output").get<std::string>();
            std::string status_str = j.contains("status")
                ? j.at("status").get<std::string>() : "done";
            out.status = status_from_string(status_str);
            if (j.contains("error")) out.error = j.at("error").get<std::string>();
            return true;
        }
        return false;
    } catch (...) {
        return false;
    }
}

// -- capability & meta-cognition methods ------------------------------------

float WorkerAgent::tool_success_rate(const std::string& tool) const noexcept {
    if (!ctx_.skills) return 0.5f;
    auto cap = ctx_.skills->get_capability(id_);
    if (!cap) return 0.5f;
    return cap->tool_rate(tool);
}

void WorkerAgent::broadcast_capability() const noexcept {
    if (!ctx_.bus || !ctx_.skills) return;
    auto cap = ctx_.skills->get_capability(id_);
    CapabilityProfile dummy;
    dummy.agent_id = id_;
    const CapabilityProfile& prof = cap ? *cap : dummy;
    ctx_.bus->broadcast(id_, MsgType::Capability, prof.to_payload());
}

bool WorkerAgent::is_placeholder_path(const std::string& input) noexcept {
    // Detect LLM-generated placeholder tokens that cannot be valid paths
    static const char* kPlaceholders[] = {
        "<HOME>", "<home>", "$HOME", "$home",
        "<PATH>", "<USERPROFILE>", "%USERPROFILE%",
        "<DESKTOP>", "<USER>", "<USERNAME>",
        "/path/to/", "path/to/file", "<file_path>",
        "<path>", "[path]", "{path}", nullptr
    };
    for (int i = 0; kPlaceholders[i]; ++i)
        if (input.find(kPlaceholders[i]) != std::string::npos) return true;
    return false;
}

std::string WorkerAgent::check_peer_cache(
        const std::string& tool,
        const std::string& input) const noexcept {
    if (ctx_.workspace.shared_dir.empty()) return "";
    try {
        // Build key matching what broadcast_capability uses
        std::string key = tool;
        for (auto& c : key) if (!std::isalnum((unsigned char)c)) c = '_';
        std::string inp_key = input.substr(0, 40);
        for (auto& c : inp_key) if (!std::isalnum((unsigned char)c)) c = '_';
        key += "_" + inp_key;
        auto cached = ResourceManager::read_shared(ctx_.workspace.shared_dir, key + ".txt");
        if (cached && !cached->empty()) {
            ctx_.log_info("cache", EvType::ToolResult,
                "peer cache hit for " + tool + " [" + input.substr(0,40) + "]");
            return *cached;
        }
    } catch (...) {}
    return "";
}

} // namespace agent
