// =============================================================================
// src/utils/memory_store.cpp
// =============================================================================
#include "utils/memory_store.hpp"
#include <mutex>
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <algorithm>

namespace agent {

// -- ShortTerm -----------------------------------------------------------------

void MemoryStore::push_message(const Message& msg) {
    std::lock_guard<std::mutex> lk(mu_);
    short_term_.push_back(msg);
    while (short_term_.size() > window_)
        short_term_.pop_front();
}

void MemoryStore::push_result(const std::string& task_id,
                               const std::string& output) {
    push_message(Message{
        "assistant",
        "[" + task_id + "] " + output.substr(0, 500)
    });
}

std::vector<Message> MemoryStore::get_context(size_t max) const {
    std::lock_guard<std::mutex> lk(mu_);
    size_t start = (short_term_.size() > max)
                 ? short_term_.size() - max : 0;
    return std::vector<Message>(short_term_.begin() + (ptrdiff_t)start,
                                short_term_.end());
}

bool MemoryStore::short_term_empty() const noexcept {
    std::lock_guard<std::mutex> lk(mu_);
    return short_term_.empty();
}

void MemoryStore::clear_short_term() {
    std::lock_guard<std::mutex> lk(mu_);
    short_term_.clear();
}

// -- Session -------------------------------------------------------------------

void MemoryStore::set(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lk(mu_);
    session_[key] = value;
}

std::string MemoryStore::get(const std::string& key,
                              const std::string& default_val) const {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = session_.find(key);
    return (it != session_.end()) ? it->second : default_val;
}

bool MemoryStore::has(const std::string& key) const {
    std::lock_guard<std::mutex> lk(mu_);
    return session_.count(key) > 0;
}

void MemoryStore::remove(const std::string& key) {
    std::lock_guard<std::mutex> lk(mu_);
    session_.erase(key);
}

void MemoryStore::merge_session(const MemoryStore& other) {
    // Lock both in address order to avoid deadlock
    std::unique_lock<std::mutex> lk1(mu_, std::defer_lock);
    std::unique_lock<std::mutex> lk2(other.mu_, std::defer_lock);
    std::lock(lk1, lk2);
    for (auto& [k, v] : other.session_)
        session_[k] = v;
}

// -- LongTerm ------------------------------------------------------------------

void MemoryStore::append_summary(const std::string& summary) {
    std::lock_guard<std::mutex> lk(mu_);
    long_term_.push_back(summary);
}

std::vector<std::string> MemoryStore::get_summaries() const {
    std::lock_guard<std::mutex> lk(mu_);
    return long_term_;
}

std::string MemoryStore::load_relevant(const std::string& query,
                                        size_t max_summaries) const {
    std::lock_guard<std::mutex> lk(mu_);
    if (long_term_.empty()) return "";

    // Simple keyword scoring: count query word overlaps
    auto words = [](const std::string& s) {
        std::vector<std::string> ws;
        std::istringstream iss(s);
        std::string w;
        while (iss >> w) ws.push_back(w);
        return ws;
    };
    auto qwords = words(query);

    std::vector<std::pair<int,size_t>> scored;
    for (size_t i = 0; i < long_term_.size(); ++i) {
        int score = 0;
        auto swords = words(long_term_[i]);
        for (auto& qw : qwords)
            for (auto& sw : swords)
                if (sw.find(qw) != std::string::npos) ++score;
        scored.push_back({score, i});
    }
    std::sort(scored.begin(), scored.end(),
              [](auto& a, auto& b){ return a.first > b.first; });

    std::string result;
    size_t count = std::min(max_summaries, scored.size());
    for (size_t i = 0; i < count; ++i) {
        if (!result.empty()) result += "\n---\n";
        result += long_term_[scored[i].second];
    }
    return result;
}

// -- Persistence ---------------------------------------------------------------

void MemoryStore::save_session(const std::string& path) const {
    std::lock_guard<std::mutex> lk(mu_);
    nlohmann::json j = nlohmann::json::object();
    for (auto& [k, v] : session_) j[k] = v;
    std::ofstream f(path);
    if (f.is_open()) f << j.dump(2) << "\n";
}

void MemoryStore::load_session(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return;
    std::ostringstream ss; ss << f.rdbuf();
    try {
        auto j = nlohmann::json::parse(ss.str());
        std::lock_guard<std::mutex> lk(mu_);
        for (auto& [k, v] : j.items())
            session_[k] = v.get<std::string>();
    } catch (...) {}
}

void MemoryStore::save_long_term(const std::string& dir) const {
    std::lock_guard<std::mutex> lk(mu_);
    for (size_t i = 0; i < long_term_.size(); ++i) {
        // Save as .md (also write legacy .txt for backward compat during transition)
        std::string md_path  = dir + "/summary_" + std::to_string(i) + ".md";
        std::ofstream f(md_path);
        if (f.is_open()) {
            f << "# Memory Summary " << i << "\n\n" << long_term_[i] << "\n";
        }
    }
}

void MemoryStore::load_long_term(const std::string& dir) {
    // Load .md files (fall back to .txt for legacy compatibility)
    for (int i = 0; ; ++i) {
        std::string md_path  = dir + "/summary_" + std::to_string(i) + ".md";
        std::string txt_path = dir + "/summary_" + std::to_string(i) + ".txt";
        std::ifstream f(md_path);
        if (!f.is_open()) f.open(txt_path);   // legacy fallback
        if (!f.is_open()) break;
        std::ostringstream ss; ss << f.rdbuf();
        std::string content = ss.str();
        // Strip Markdown heading if present
        if (content.size() > 2 && content.substr(0,2) == "# ") {
            auto nl = content.find("\n\n");
            if (nl != std::string::npos) content = content.substr(nl + 2);
        }
        std::lock_guard<std::mutex> lk(mu_);
        long_term_.push_back(content);
    }
}


// -- maybe_compact ------------------------------------------------------------
// Lightweight context compaction: when short_term_ fills up, keep the most
// recent half and prepend a condensed summary of the discarded half.
void MemoryStore::maybe_compact(float threshold) {
    if (!needs_compact(threshold)) return;

    size_t half = short_term_.size() / 2;
    if (half == 0) return;

    // Build a one-line summary of the discarded messages
    std::string summary = "[Compacted context: ";
    size_t n = 0;
    for (size_t i = 0; i < half; ++i) {
        const auto& m = short_term_[i];
        if (m.role == "assistant" && !m.content.empty()) {
            // Keep first 80 chars of each assistant turn
            summary += m.content.substr(0, 80);
            if (m.content.size() > 80) summary += "...";
            if (++n >= 3) break;  // max 3 assistant turns in summary
        }
    }
    summary += "]";

    // Discard the oldest half, replace with summary message
    short_term_.erase(short_term_.begin(),
                      short_term_.begin() + (std::ptrdiff_t)half);
    short_term_.insert(short_term_.begin(),
                       Message{"system", summary});
}



// ---------------------------------------------------------------------------
// Conversation-scoped MEMORY.md (Markdown, human-readable, append)
// ---------------------------------------------------------------------------

void MemoryStore::save_conversation_md(const std::string& md_path) const {
    std::lock_guard<std::mutex> lk(mu_);
    std::ofstream f(md_path);
    if (!f.is_open()) return;
    f << "# Conversation Memory\n\n"
      << "> Auto-maintained by the agent system. You can read and edit this.\n\n";
    if (!long_term_.empty()) {
        f << "## Long-term Summaries\n\n";
        for (auto& s : long_term_) f << "- " << s << "\n";
        f << "\n";
    }
    if (!short_term_.empty()) {
        f << "## Recent Context\n\n";
        for (auto& m : short_term_)
            f << "**" << m.role << ":** " << m.content.substr(0, 200) << "\n\n";
    }
}

void MemoryStore::load_conversation_md(const std::string& md_path) {
    // Load long-term summaries from MEMORY.md bullet points
    std::ifstream f(md_path);
    if (!f.is_open()) return;
    std::lock_guard<std::mutex> lk(mu_);
    bool in_lt = false;
    std::string line;
    while (std::getline(f, line)) {
        if (line.find("## Long-term") != std::string::npos) { in_lt = true; continue; }
        if (line.size() > 2 && line[0] == '#') { in_lt = false; }
        if (in_lt && line.size() > 2 && line.substr(0,2) == "- ")
            long_term_.push_back(line.substr(2));
    }
}

void MemoryStore::append_run_to_memory_md(const std::string& md_path,
                                           const std::string& run_id,
                                           const std::string& goal,
                                           const std::string& result_summary) const {
    std::ofstream f(md_path, std::ios::app);
    if (!f.is_open()) return;
    f << "\n## Run " << run_id << "\n"
      << "**Goal:** " << goal.substr(0, 100) << "\n"
      << "**Result:** " << result_summary.substr(0, 200) << "\n";
}

} // namespace agent


std::string agent::MemoryStore::generate_and_store_summary(
        const std::string& goal,
        const std::string& answer,
        const std::string& long_term_dir,
        std::function<std::string(const std::string&)> llm_call) noexcept {
    try {
        std::ostringstream prompt;
        prompt << "Summarize this AI agent session in 2-3 sentences for future reference.\n\n"
               << "Goal: " << goal.substr(0, 200) << "\n\n"
               << "Result: " << answer.substr(0, 500) << "\n\n"
               << "Write a concise summary that captures: what was asked, what was done, "
               << "and any important findings. No JSON, plain text only.";

        std::string summary = llm_call(prompt.str());
        if (summary.empty()) return "";

        append_summary(summary);
        save_long_term(long_term_dir);
        return summary;
    } catch (...) {
        return "";
    }
}
