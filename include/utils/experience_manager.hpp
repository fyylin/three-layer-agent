#pragma once
// =============================================================================
// include/utils/experience_manager.hpp  --  v1.0
// Persistent experience management with automatic skill promotion.
//
// Concept:
//   - Every successful/failed run appends to conversations/<conv>/experience.md
//   - On session start, loads cross-conv EXPERIENCE.md for context injection
//   - When a pattern appears ≥3 times across different convs with rate ≥ 0.9,
//     it is promoted to prompts/skills/ as a new SKILL.md
// =============================================================================
#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <sstream>
#include <mutex>
#include <optional>
#include <filesystem>
#include <chrono>
#include <ctime>

namespace agent {

namespace fs = std::filesystem;

enum class FailureCategory {
    None,
    SyntaxError,      // Compilation/parsing errors
    LogicError,       // Runtime logic errors
    EnvError,         // Environment/dependency issues
    Timeout,          // Operation timeout
    PermissionDenied, // Access denied
    NotFound,         // File/resource not found
    Unknown
};

struct ExperienceEntry {
    std::string timestamp;
    std::string tool;
    std::string input_pattern;   // first 60 chars of input
    std::string outcome;         // "success" | "failure"
    std::string lesson;          // what worked / what failed
    std::string conv_id;
    FailureCategory failure_category = FailureCategory::None;
};

struct ExperiencePattern {
    std::string key;             // tool + input_pattern hash
    std::vector<std::string> conv_ids;  // distinct convs where seen
    int successes = 0;
    int failures  = 0;
    std::string best_lesson;
    std::map<FailureCategory, int> failure_breakdown;
    [[nodiscard]] double success_rate() const noexcept {
        int total = successes + failures;
        return total > 0 ? (double)successes / total : 0.5;
    }
    [[nodiscard]] bool promotion_eligible() const noexcept {
        // Strict: ≥3 distinct conversations, ≥90% success rate
        return (int)conv_ids.size() >= 3 && success_rate() >= 0.9 && successes >= 3;
    }
};

struct DecompositionStrategy {
    std::string task_type;
    int num_subtasks = 0;
    bool parallel = false;
    double avg_completion_time_ms = 0.0;
    int success_count = 0;
    int total_count = 0;
    [[nodiscard]] double success_rate() const {
        return total_count > 0 ? (double)success_count / total_count : 0.0;
    }
};

class ExperienceManager {
public:
    ExperienceManager(std::string conv_exp_path,
                      std::string global_exp_path,
                      std::string skills_dir,
                      std::string conv_id)
        : conv_exp_(std::move(conv_exp_path))
        , global_exp_(std::move(global_exp_path))
        , skills_dir_(std::move(skills_dir))
        , conv_id_(std::move(conv_id))
    {
        // Restore patterns_ from EXPERIENCE.md on construction
        // This allows skill promotion to work across restarts
        load_patterns();
    }

    // Record an experience after a task completes
    void record(const std::string& tool,
                const std::string& input,
                const std::string& outcome,
                const std::string& lesson,
                FailureCategory category = FailureCategory::None) {
        ExperienceEntry e;
        e.timestamp     = iso_now();
        e.tool          = tool;
        e.input_pattern = input.substr(0, std::min(input.size(), size_t(60)));
        e.outcome       = outcome;
        e.lesson        = lesson;
        e.conv_id       = conv_id_;
        e.failure_category = category;
        append_to_conv(e);
        update_global(e);
        maybe_promote(e);
    }

    static const char* category_name(FailureCategory cat) {
        switch(cat) {
            case FailureCategory::SyntaxError: return "syntax_error";
            case FailureCategory::LogicError: return "logic_error";
            case FailureCategory::EnvError: return "env_error";
            case FailureCategory::Timeout: return "timeout";
            case FailureCategory::PermissionDenied: return "permission_denied";
            case FailureCategory::NotFound: return "not_found";
            case FailureCategory::Unknown: return "unknown";
            default: return "none";
        }
    }

    // Load cross-conversation experience summary for prompt injection
    [[nodiscard]] std::string load_context(size_t max_chars = 800) const {
        if (global_exp_.empty() || !fs::exists(global_exp_)) return "";
        std::ifstream f(global_exp_);
        if (!f.is_open()) return "";
        std::string content((std::istreambuf_iterator<char>(f)),
                             std::istreambuf_iterator<char>());
        if (content.size() > max_chars)
            content = content.substr(0, max_chars) + "\n...(truncated)";
        return content;
    }

    // Check if any patterns are ready for promotion
    [[nodiscard]] std::vector<std::string> promoted_skills() const {
        std::lock_guard<std::mutex> lk(mu_);
        return promoted_;
    }

    // Generate failure mode report
    [[nodiscard]] std::string generate_failure_report() const {
        std::lock_guard<std::mutex> lk(mu_);
        std::ostringstream oss;
        oss << "# Failure Mode Analysis\n\n";

        std::map<FailureCategory, int> global_breakdown;
        for (auto& [k, p] : patterns_) {
            for (auto& [cat, cnt] : p.failure_breakdown)
                global_breakdown[cat] += cnt;
        }

        if (!global_breakdown.empty()) {
            oss << "## Global Failure Distribution\n";
            for (auto& [cat, cnt] : global_breakdown)
                oss << "- " << category_name(cat) << ": " << cnt << "\n";
            oss << "\n";
        }

        oss << "## Top Failing Patterns\n";
        std::vector<std::pair<std::string, const ExperiencePattern*>> sorted;
        for (auto& [k, p] : patterns_)
            if (p.failures > 0) sorted.push_back({k, &p});
        std::sort(sorted.begin(), sorted.end(),
            [](auto& a, auto& b) { return a.second->failures > b.second->failures; });

        for (size_t i = 0; i < std::min(size_t(10), sorted.size()); ++i) {
            auto& [k, p] = sorted[i];
            oss << "- " << k.substr(0, 60) << ": " << p->failures << " failures\n";
        }

        return oss.str();
    }

    void record_decomposition(const std::string& task_type, int num_subtasks,
                             bool parallel, double completion_time_ms, bool success) {
        std::lock_guard<std::mutex> lk(mu_);
        auto& strat = decomp_strategies_[task_type];
        strat.task_type = task_type;
        strat.num_subtasks = num_subtasks;
        strat.parallel = parallel;
        strat.total_count++;
        if (success) strat.success_count++;
        strat.avg_completion_time_ms =
            (strat.avg_completion_time_ms * (strat.total_count - 1) + completion_time_ms) / strat.total_count;
    }

    [[nodiscard]] std::string get_decomposition_advice(const std::string& task_type) const {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = decomp_strategies_.find(task_type);
        if (it == decomp_strategies_.end() || it->second.total_count < 2)
            return "";
        auto& s = it->second;
        std::ostringstream oss;
        oss << "Based on " << s.total_count << " previous attempts:\n"
            << "- Success rate: " << (s.success_rate() * 100) << "%\n"
            << "- Avg subtasks: " << s.num_subtasks << "\n"
            << "- Parallel: " << (s.parallel ? "yes" : "no") << "\n"
            << "- Avg time: " << s.avg_completion_time_ms << "ms";
        return oss.str();
    }

private:
    std::string conv_exp_;
    std::string global_exp_;
    std::string skills_dir_;
    std::string conv_id_;
    mutable std::mutex mu_;
    std::map<std::string, ExperiencePattern> patterns_;
    std::map<std::string, DecompositionStrategy> decomp_strategies_;
    std::vector<std::string> promoted_;

    static std::string iso_now() {
        auto now = std::chrono::system_clock::now();
        auto tt  = std::chrono::system_clock::to_time_t(now);
        std::tm tb{};
#ifdef _WIN32
        gmtime_s(&tb, &tt);
#else
        gmtime_r(&tt, &tb);
#endif
        char buf[24]; std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tb);
        return buf;
    }

    // Restore patterns_ by parsing EXPERIENCE.md (called on construction)
    // Parses lines like: "- Success rate: 85% (17/20)"
    //                    "- Seen in 4 conversation(s)"
    //                    "- Lesson: ..."
    void load_patterns() {
        if (global_exp_.empty() || !fs::exists(global_exp_)) return;
        std::ifstream f(global_exp_);
        if (!f.is_open()) return;
        std::lock_guard<std::mutex> lk(mu_);
        ExperiencePattern* cur = nullptr;
        std::string line;
        while (std::getline(f, line)) {
            // Section header: "## tool_name — input_prefix"
            if (line.size() > 3 && line.substr(0, 3) == "## ") {
                std::string heading = line.substr(3);
                // Reconstruct key from heading (tool|prefix)
                auto dash = heading.find(" â ");  // em dash
                if (dash == std::string::npos) dash = heading.find(" - ");
                if (dash != std::string::npos) {
                    std::string tool   = heading.substr(0, dash);
                    std::string prefix = heading.substr(dash + 3);
                    if (prefix.size() > 3 && prefix.substr(0,3) == "â ")
                        prefix = prefix.substr(3);
                    std::string key = tool + "|" + prefix;
                    cur = &patterns_[key];
                    cur->key = key;
                }
                continue;
            }
            if (!cur) continue;
            // "- Success rate: 85% (17/20)"
            if (line.find("Success rate:") != std::string::npos) {
                auto lp = line.find('(');
                auto sl = line.find('/');
                auto rp = line.find(')');
                if (lp != std::string::npos && sl != std::string::npos && rp != std::string::npos) {
                    try {
                        cur->successes = std::stoi(line.substr(lp+1, sl-lp-1));
                        cur->failures  = std::stoi(line.substr(sl+1, rp-sl-1)) - cur->successes;
                    } catch(...) {}
                }
            }
            // "- Seen in 4 conversation(s)"
            if (line.find("Seen in") != std::string::npos) {
                auto ni = line.find("Seen in ") + 8;
                auto si = line.find(' ', ni);
                try { int n = std::stoi(line.substr(ni, si-ni));
                    for (int i = (int)cur->conv_ids.size(); i < n; ++i)
                        cur->conv_ids.push_back("restored-" + std::to_string(i));
                } catch(...) {}
            }
            // "- Lesson: ..."
            if (line.find("- Lesson: ") != std::string::npos)
                cur->best_lesson = line.substr(line.find("- Lesson: ") + 10);
        }
    }

    // Append Markdown entry to conv-scoped experience.md
    void append_to_conv(const ExperienceEntry& e) const {
        if (conv_exp_.empty()) return;
        std::ofstream f(conv_exp_, std::ios::app);
        if (!f.is_open()) return;
        f << "\n## " << e.timestamp << " — " << e.tool << "\n"
          << "- **Outcome:** " << e.outcome << "\n"
          << "- **Input:** `" << e.input_pattern << "`\n";
        if (e.failure_category != FailureCategory::None)
            f << "- **Category:** " << category_name(e.failure_category) << "\n";
        f << "- **Lesson:** " << e.lesson << "\n";
    }

    // Update global EXPERIENCE.md (cross-conversation)
    void update_global(const ExperienceEntry& e) {
        if (global_exp_.empty()) return;
        std::lock_guard<std::mutex> lk(mu_);
        std::string key = e.tool + "|" + e.input_pattern.substr(0, 20);
        auto& pat = patterns_[key];
        pat.key = key;
        if (std::find(pat.conv_ids.begin(), pat.conv_ids.end(), e.conv_id)
                == pat.conv_ids.end())
            pat.conv_ids.push_back(e.conv_id);
        if (e.outcome == "success") { ++pat.successes; pat.best_lesson = e.lesson; }
        else {
            ++pat.failures;
            if (e.failure_category != FailureCategory::None)
                ++pat.failure_breakdown[e.failure_category];
        }

        // Rewrite EXPERIENCE.md with current patterns
        std::ofstream f(global_exp_);
        if (!f.is_open()) return;
        f << "# Cross-Conversation Experience\n\n"
          << "> Auto-generated. Agents use this to avoid repeating mistakes.\n\n";
        for (auto& [k, p] : patterns_) {
            if (p.successes + p.failures < 2) continue;
            std::string pk_tool = k.substr(0, k.find('|'));
            f << "## " << pk_tool << " — " << k.substr(pk_tool.size()+1,20) << "\n"
              << "- Success rate: " << (int)(p.success_rate()*100) << "%"
              << " (" << p.successes << "/" << (p.successes+p.failures) << ")\n"
              << "- Seen in " << p.conv_ids.size() << " conversation(s)\n";
            if (!p.failure_breakdown.empty()) {
                f << "- Failure breakdown: ";
                bool first = true;
                for (auto& [cat, cnt] : p.failure_breakdown) {
                    if (!first) f << ", ";
                    f << category_name(cat) << "(" << cnt << ")";
                    first = false;
                }
                f << "\n";
            }
            f << "- Lesson: " << p.best_lesson << "\n"
              << (p.promotion_eligible() ? "- **[PROMOTED TO SKILL]**\n" : "") << "\n";
        }
    }

    // If pattern qualifies, promote to prompts/skills/
    void maybe_promote(const ExperienceEntry& e) {
        if (skills_dir_.empty()) return;
        std::lock_guard<std::mutex> lk(mu_);
        std::string key = e.tool + "|" + e.input_pattern.substr(0, 20);
        auto it = patterns_.find(key);
        if (it == patterns_.end()) return;
        if (!it->second.promotion_eligible()) return;

        // Check not already promoted
        std::string skill_name = "exp-" + e.tool;
        for (char& c : skill_name) if (c == '_' || c == ' ') c = '-';
        std::string skill_path = skills_dir_ + "/" + skill_name + ".md";
        if (fs::exists(skill_path)) return;

        // Write SKILL.md
        std::ofstream sf(skill_path);
        if (!sf.is_open()) return;
        sf << "---\n"
           << "name: " << skill_name << "\n"
           << "role: cross-agent-skill\n"
           << "version: 1.0.0\n"
           << "description: Promoted from experience: " << e.tool
           << " — " << it->second.best_lesson.substr(0,80) << "\n"
           << "promoted_from: experience\n"
           << "success_rate: " << (int)(it->second.success_rate()*100) << "%\n"
           << "conversations: " << it->second.conv_ids.size() << "\n"
           << "---\n\n"
           << "# " << e.tool << " Best Practice\n\n"
           << "> Promoted from experience after "
           << it->second.conv_ids.size() << " conversations.\n\n"
           << "## Lesson\n" << it->second.best_lesson << "\n\n"
           << "## When to Apply\n"
           << "Apply when using `" << e.tool << "` with similar inputs.\n";

        promoted_.push_back(skill_name);
    }
};

} // namespace agent
