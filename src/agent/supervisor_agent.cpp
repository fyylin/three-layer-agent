// =============================================================================
// src/agent/supervisor_agent.cpp   --   v2: full monitoring + advisor + escalation
// =============================================================================
#include "agent/supervisor_agent.hpp"
#include "utils/logger.hpp"
#include "utils/json_utils.hpp"
#include <nlohmann/json.hpp>
#include <sstream>
#include <unordered_map>
#include <chrono>

namespace agent {

static const char* kLayer = "Supervisor";

static const char* kDefaultPrompt =
    "You are a Supervisor AI evaluating whether a task result satisfies the user's goal.\n\n"
    "CRITICAL OUTPUT RULE:\n"
    "Respond with ONLY: {\"satisfied\": true/false, \"note\": \"<reason if false>\"}\n\n"
    "Be lenient: if the result substantially addresses the goal, satisfied=true.\n"
    "Only mark false if the result is clearly wrong, empty, or completely off-topic.";

SupervisorAgent::SupervisorAgent(std::string                   id,
                                  ApiClient&                    client,
                                  DirectorAgent&                director,
                                  std::string                   system_prompt,
                                  std::shared_ptr<MessageBus>   bus,
                                  SupervisorConfig              config,
                                  int                           max_retries,
                                  std::unique_ptr<AdvisorAgent> advisor)
    : id_(std::move(id))
    , client_(client)
    , director_(director)
    , system_prompt_(system_prompt.empty() ? kDefaultPrompt : std::move(system_prompt))
    , bus_(std::move(bus))
    , config_(config)
    , max_retries_(max_retries)
    , advisor_(std::move(advisor))
{}

SupervisorAgent::~SupervisorAgent() {
    stop_monitor_.store(true);
    if (monitor_thread_.joinable()) monitor_thread_.join();
}

// -----------------------------------------------------------------------------
// Registration
// -----------------------------------------------------------------------------

void SupervisorAgent::register_context(
        const std::string&                 agent_id,
        std::shared_ptr<StateMachine>      state,
        std::shared_ptr<std::atomic<bool>> cancel_flag,
        std::shared_ptr<std::atomic<bool>> pause_flag) {
    std::lock_guard<std::mutex> lk(reg_mu_);
    registry_[agent_id] = {std::move(state), std::move(cancel_flag), std::move(pause_flag)};
}

// -----------------------------------------------------------------------------
// Manual control
// -----------------------------------------------------------------------------

void SupervisorAgent::cancel_agent(const std::string& agent_id) {
    std::lock_guard<std::mutex> lk(reg_mu_);
    auto it = registry_.find(agent_id);
    if (it == registry_.end()) return;
    if (it->second.cancel_flag)
        it->second.cancel_flag->store(true, std::memory_order_relaxed);
    if (it->second.state)
        it->second.state->transition(AgentState::Cancelled);
    LOG_INFO(kLayer, id_, "sup", "cancelled agent: " + agent_id);
}

void SupervisorAgent::pause_agent(const std::string& agent_id) {
    std::lock_guard<std::mutex> lk(reg_mu_);
    auto it = registry_.find(agent_id);
    if (it == registry_.end()) return;
    if (it->second.pause_flag)
        it->second.pause_flag->store(true, std::memory_order_relaxed);
    if (it->second.state)
        it->second.state->transition(AgentState::Paused);
    LOG_INFO(kLayer, id_, "sup", "paused agent: " + agent_id);
}

void SupervisorAgent::resume_agent(const std::string& agent_id) {
    std::lock_guard<std::mutex> lk(reg_mu_);
    auto it = registry_.find(agent_id);
    if (it == registry_.end()) return;
    if (it->second.pause_flag)
        it->second.pause_flag->store(false, std::memory_order_relaxed);
    if (it->second.state)
        it->second.state->transition(AgentState::Running, "resumed");
    LOG_INFO(kLayer, id_, "sup", "resumed agent: " + agent_id);
}

// -----------------------------------------------------------------------------
// Monitor thread
// -----------------------------------------------------------------------------

void SupervisorAgent::monitor_loop() {
    // Per-agent stuck detection counters (local to this loop)
    std::unordered_map<std::string, int> stuck_counts;
    std::unordered_map<std::string, int64_t> last_seen_ms;

    while (!stop_monitor_.load()) {
        std::this_thread::sleep_for(config_.poll_interval);
        if (stop_monitor_.load()) break;

        // Snapshot all registered states
        std::vector<std::pair<std::string, StateInfo>> snapshots;
        {
            std::lock_guard<std::mutex> lk(reg_mu_);
            for (auto& [aid, ctrl] : registry_) {
                if (ctrl.state) {
                    snapshots.push_back({aid, ctrl.state->snapshot()});
                } else {
                    // No state machine  --  still register for cancel/pause control
                    StateInfo dummy;
                    dummy.agent_id = aid;
                    dummy.state = AgentState::Running;
                    dummy.last_event_ms = 0;
                    snapshots.push_back({aid, dummy});
                }
            }
        }

        int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        int running = 0, failed_total = 0, total = (int)snapshots.size();
        for (auto& [aid, info] : snapshots) {
            if (info.state == AgentState::Running || info.last_event_ms == 0) {
                ++running;
                int64_t last = last_seen_ms.count(aid) ? last_seen_ms[aid] : now_ms;
                int64_t idle_ms = (info.last_event_ms > 0)
                    ? (now_ms - info.last_event_ms)
                    : (now_ms - last);

                if (idle_ms > config_.stuck_timeout.count()) {
                    stuck_counts[aid]++;
                    handle_stuck_agent(aid, info, stuck_counts[aid]);
                } else {
                    stuck_counts[aid] = 0;  // reset on activity
                }

                if (info.fail_count > config_.max_fail_count)
                    handle_high_fail_rate(aid, info);

                last_seen_ms[aid] = now_ms;
            }
            if (info.state == AgentState::Failed) ++failed_total;
        }

        // Global fail rate check
        if (total > 0 && failed_total > 0) {
            double rate = (double)failed_total / (double)total;
            if (rate >= config_.global_fail_rate_threshold && failed_total >= 2) {
                LOG_WARN(kLayer, id_, "sup",
                    "global fail rate " + std::to_string((int)(rate*100)) +
                    "%  --  advisor standby");
            }
        }

        // Drain incoming messages — includes event-driven signals (D1)
        if (bus_) {
            while (auto msg = bus_->try_receive(id_)) {
                if (msg->type == MsgType::Progress) {
                    LOG_DEBUG(kLayer, id_, msg->from_id,
                        "progress: " + msg->payload.substr(0,60));
                } else if (msg->type == MsgType::SubReport) {
                    LOG_INFO(kLayer, id_, msg->from_id,
                        "sub-report: " + msg->payload.substr(0,80));
                } else if (msg->type == MsgType::StateChange) {
                    LOG_DEBUG(kLayer, id_, msg->from_id,
                        "state: " + msg->payload.substr(0,60));

                // ── D1: Event-driven immediate responses ──
                } else if (msg->type == MsgType::ToolFailed) {
                    // Immediately inject a corrective hint to the failing agent
                    LOG_WARN(kLayer, id_, msg->from_id,
                        "ToolFailed event: " + msg->payload.substr(0,80));
                    // Parse tool name from payload for targeted advice
                    std::string hint;
                    auto tp = msg->payload.find("\"tool\":");
                    if (tp != std::string::npos) {
                        tp += 8;
                        auto qs = msg->payload.find('"', tp);
                        auto qe = msg->payload.find('"', qs+1);
                        if (qs!=std::string::npos && qe!=std::string::npos) {
                            std::string tool = msg->payload.substr(qs+1, qe-qs-1);
                            hint = "Tool '" + tool + "' failed. "
                                   "Try an alternative: if read_file fails use run_command; "
                                   "if path unknown use get_current_dir or list_dir first. "
                                   "Do NOT retry the same approach that just failed.";
                        }
                    }
                    if (!hint.empty())
                        inject_correction(msg->from_id, hint);

                } else if (msg->type == MsgType::GivingUp) {
                    LOG_WARN(kLayer, id_, msg->from_id,
                        "GivingUp event: " + msg->payload.substr(0,80));
                    // Note: GivingUp means LLM declared impossible — don't retry,
                    // let the normal flow complete with the honest failure

                } else if (msg->type == MsgType::SlowWarning) {
                    LOG_WARN(kLayer, id_, msg->from_id,
                        "SlowWarning: " + msg->payload.substr(0,60));
                    inject_correction(msg->from_id,
                        "You are taking too long. Return your best partial result now. "
                        "Use status=done with what you have rather than continuing to retry.");
                }
            }
        }
    }
}

void SupervisorAgent::handle_stuck_agent(const std::string& agent_id,
                                          const StateInfo&   info,
                                          int                stuck_count) {
    (void)info;
    if (stuck_count == 1) {
        // First detection: inject advisory correction
        std::string note =
            "You are taking unusually long. Please simplify your current approach: "
            "use a direct tool call instead of reasoning, reduce step count, "
            "or return your best partial result immediately.";
        inject_correction(agent_id, note);
        LOG_WARN(kLayer, id_, "sup",
            "stuck agent " + agent_id + " (count=" +
            std::to_string(stuck_count) + ")  --  correction sent");

        // Also consult advisor if available
        if (advisor_ && bus_) {
            std::vector<std::string> hist{"Agent stuck: no state change for > " +
                std::to_string(config_.stuck_timeout.count()/1000) + "s"};
            AdviceResult adv = advisor_->advise(
                "Agent " + agent_id + " is stuck during task execution",
                hist,
                "Agent is running but not making progress");
            if (!adv.recommended.empty()) {
                inject_correction(agent_id,
                    "Advisor recommendation: " + adv.recommended);
            }
        }
    } else if (stuck_count >= 4) {
        // 4+ detections without progress: cancel (4 × poll_interval wait already happened)
        // Threshold raised from 2→4 to allow legitimate long LLM calls (file reading etc.)
        LOG_ERROR(kLayer, id_, "sup",
            "agent " + agent_id + " stuck " +
            std::to_string(stuck_count) + " intervals  --  cancelling");
        cancel_agent(agent_id);
    } else if (stuck_count == 3) {
        // Third detection: stronger warning, but don't cancel yet
        inject_correction(agent_id,
            "URGENT: You have been stuck for a long time. "
            "Return your best partial result NOW using status=done. "
            "Do not wait for more tool output — use what you already have.");
        LOG_WARN(kLayer, id_, "sup",
            "agent " + agent_id + " stuck " + std::to_string(stuck_count) +
            " intervals  --  urgent correction sent, will cancel next interval");
    }
}

void SupervisorAgent::handle_high_fail_rate(const std::string& agent_id,
                                             const StateInfo&   info) {
    std::string note =
        "Agent " + agent_id + " has failed " +
        std::to_string(info.fail_count) + " times. " +
        "Use a simpler, more direct approach. "
        "For file tasks: use list_dir with input=\"Desktop\" or run_command. "
        "Return partial results rather than failing.";
    inject_correction(agent_id, note);
    LOG_WARN(kLayer, id_, "sup",
        "high fail rate on " + agent_id +
        " (fail_count=" + std::to_string(info.fail_count) + ")  --  correction sent");
}

void SupervisorAgent::inject_correction(const std::string& agent_id,
                                         const std::string& note) {
    if (!bus_) return;
    // Escape note for JSON
    std::string escaped;
    escaped.reserve(note.size());
    for (char c : note) {
        if      (c == '"')  escaped += "\\\"";
        else if (c == '\\') escaped += "\\\\";
        else if (c == '\n') escaped += "\\n";
        else                escaped += c;
    }
    bus_->send(id_, agent_id, MsgType::Correct,
               "{\"note\":\"" + escaped + "\"}");
    LOG_INFO(kLayer, id_, "sup",
        "correction -> " + agent_id + ": " + note.substr(0, 80));
}

// -----------------------------------------------------------------------------
// Advisor consultation
// -----------------------------------------------------------------------------

std::string SupervisorAgent::consult_advisor(
        const UserGoal&                 goal,
        const std::vector<std::string>& error_history) const noexcept {
    // Build rich context with all available tools
    std::string context =
        "Available tools (12 total):\n"
        "  Filesystem: list_dir, stat_file, find_files, read_file, write_file, delete_file\n"
        "  System API: get_env, get_sysinfo, get_process_list, get_current_dir\n"
        "  Shell:      run_command (sandboxed)\n"
        "  Utility:    echo\n\n"
        "Key constraints:\n"
        "  - list_dir input=\"Desktop\" lists the user's desktop (NOT <HOME>/Desktop)\n"
        "  - run_command can execute any shell command and returns stdout+stderr\n"
        "  - Tasks run IN PARALLEL  --  a step cannot use another step's output\n"
        "  - For desktop/file tasks: use ONE step with list_dir or run_command\n"
        "  - If a path is unknown: use get_env with USERPROFILE or HOME first, OR\n"
        "    use run_command with 'dir %USERPROFILE%\\Desktop' (Windows) / 'ls ~/Desktop' (Linux)";

    if (!advisor_) {
        // Fallback: generic correction without LLM analysis
        return goal.description +
               "\n\n[Supervisor correction after " +
               std::to_string(error_history.size()) + " failures]: " +
               (error_history.empty() ? "unknown error" : error_history.back()) +
               "\n" + context;
    }

    LOG_INFO(kLayer, id_, "sup",
        "consulting advisor after " +
        std::to_string(error_history.size()) + " failure(s)");

    AdviceResult advice = advisor_->advise(
        goal.description, error_history, context);

    std::ostringstream result;
    if (!advice.refined_goal.empty() &&
        advice.refined_goal != goal.description) {
        result << advice.refined_goal;
    } else {
        result << goal.description;
    }

    result << "\n\n[Advisor analysis after "
           << error_history.size() << " failure(s)]\n"
           << "Root cause: " << advice.root_cause << "\n"
           << "Fix: " << advice.recommended;
    if (advice.strategies.size() > 1)
        result << "\nAlternative: " << advice.strategies[1];

    return result.str();
}

// -----------------------------------------------------------------------------
// Main run
// -----------------------------------------------------------------------------

FinalResult SupervisorAgent::run(const UserGoal& goal) noexcept {
    LOG_INFO(kLayer, id_, "sup",
        "supervising: " + goal.description.substr(0, 80));

    error_history_.clear();

    // Start monitor thread if any agents registered
    stop_monitor_.store(false);
    if (!registry_.empty())
        monitor_thread_ = std::thread([this] { monitor_loop(); });

    FinalResult result;

    for (int attempt = 0; attempt <= max_retries_; ++attempt) {
        UserGoal current_goal = goal;

        if (attempt > 0) {
            if (!error_history_.empty() &&
                (int)error_history_.size() >= kAdvisorThreshold) {
                current_goal.description =
                    consult_advisor(goal, error_history_);
                LOG_INFO(kLayer, id_, "sup",
                    "retry " + std::to_string(attempt) +
                    " with advisor-refined goal");
            } else if (!result.error.empty()) {
                current_goal.description =
                    goal.description +
                    "\n\n[Supervisor retry " + std::to_string(attempt) +
                    "]: " + result.error;
            }
        }

        result = director_.run(current_goal);

        if (result.status != TaskStatus::Done && !result.error.empty())
            error_history_.push_back(result.error);

        std::string issue = detect_structural_issues(result);
        if (!issue.empty()) {
            LOG_WARN(kLayer, id_, "sup", "structural: " + issue);
            error_history_.push_back(issue);
            if (attempt < max_retries_) { result.error = issue; continue; }
            LOG_ERROR(kLayer, id_, "sup", "persists after all attempts: " + issue);
            break;
        }

        if (result.status == TaskStatus::Done) {
            auto [satisfied, note] = evaluate(goal, result);
            if (satisfied) {
                LOG_INFO(kLayer, id_, "sup", "result approved");
                break;
            }
            LOG_WARN(kLayer, id_, "sup", "not satisfactory: " + note);
            error_history_.push_back("Quality: " + note);
            if (attempt < max_retries_) {
                result.error  = note;
                result.status = TaskStatus::Failed;
                continue;
            }
            result.status = TaskStatus::Done;  // best effort
            break;
        }

        if (attempt < max_retries_) continue;
    }

    stop_monitor_.store(true);
    if (monitor_thread_.joinable()) monitor_thread_.join();
    return result;
}

// -----------------------------------------------------------------------------
// Quality gate evaluation
// -----------------------------------------------------------------------------

std::pair<bool, std::string> SupervisorAgent::evaluate(
        const UserGoal& goal, const FinalResult& result) const noexcept {
    try {
        std::ostringstream msg;
        msg << "## User goal\n" << goal.description << "\n\n"
            << "## Agent answer\n" << result.answer << "\n\n"
            << "## Tool evidence\n";
        if (result.sub_reports.empty()) {
            msg << "(no subtask reports)\n";
        } else {
            for (const auto& report : result.sub_reports) {
                msg << "- " << report.subtask_id
                    << " status=" << status_to_string(report.status);
                if (!report.summary.empty())
                    msg << " summary=" << report.summary.substr(0, 240);
                if (!report.issues.empty())
                    msg << " issues=" << report.issues.substr(0, 240);
                msg << "\n";
                size_t shown = 0;
                for (const auto& atomic : report.results) {
                    if (shown++ >= 3) break;
                    msg << "  * " << atomic.task_id
                        << " status=" << status_to_string(atomic.status);
                    if (!atomic.output.empty())
                        msg << " output=" << atomic.output.substr(0, 160);
                    if (!atomic.error.empty())
                        msg << " error=" << atomic.error.substr(0, 160);
                    msg << "\n";
                }
            }
        }
        msg << "\nDoes the answer substantially address the goal and remain consistent with the tool evidence?\n"
            << "Respond ONLY: "
               "{\"satisfied\":true/false,\"note\":\"<reason if false>\"}";
        std::string    out = client_.complete(system_prompt_, msg.str(), "sup-eval");
        nlohmann::json j   = parse_llm_json(out);
        bool satisfied = true; std::string note;
        if (j.contains("satisfied")) j["satisfied"].get_to(satisfied);
        if (j.contains("note"))      j["note"].get_to(note);
        return {satisfied, note};
    } catch (const std::exception& e) {
        std::string issue = detect_structural_issues(result);
        LOG_WARN(kLayer, id_, "sup",
            std::string("eval failed, using structural fallback: ") + e.what());
        if (!issue.empty()) return {false, issue};
        return {true, ""};
    }
}

std::string SupervisorAgent::detect_structural_issues(
        const FinalResult& result) const noexcept {
    if (result.status == TaskStatus::Failed) return "";
    if (result.answer.empty()) return "answer is empty";
    if (!result.sub_reports.empty()) {
        int failed = 0, total = (int)result.sub_reports.size();
        for (auto& r : result.sub_reports)
            if (r.status == TaskStatus::Failed) ++failed;
        if (failed == total)
            return "all " + std::to_string(total) + " subtasks failed";
    }
    return "";
}

} // namespace agent
