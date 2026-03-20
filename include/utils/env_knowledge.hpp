#pragma once
// =============================================================================
// include/utils/env_knowledge.hpp  --  Environment Knowledge Base
// Stores discovered environment facts (paths, CWD, file existence)
// so Agents don't need to re-discover them on every request.
// =============================================================================
#include <string>
#include <vector>
#include <mutex>
#include <chrono>
#include <sstream>

namespace agent {

struct EnvFact {
    std::string key;           // e.g. "cwd", "file:BUILD.md", "tool:list_dir"
    std::string value;         // e.g. "E:\\projects\\app"
    float       confidence;    // 0.0 - 1.0; decays on failure, set to 1.0 on success
    int64_t     last_seen_ms;  // Unix ms of last confirmation
    bool        persistent;    // true = save across runs

    EnvFact() : confidence(0.7f), last_seen_ms(0), persistent(true) {}
    EnvFact(std::string k, std::string v, float c=1.0f, bool p=true)
        : key(std::move(k)), value(std::move(v)), confidence(c), persistent(p) {
        auto now = std::chrono::system_clock::now().time_since_epoch();
        last_seen_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    }
};

// Thread-safe environment knowledge base
class EnvKnowledgeBase {
public:
    static constexpr float kDecayOnFailure  = 0.4f;
    static constexpr float kMinConfidence   = 0.2f;
    static constexpr int   kMaxFacts        = 128;

    // Record a confirmed fact (confidence = 1.0)
    void confirm(const std::string& key, const std::string& value,
                 bool persistent = true) {
        std::lock_guard<std::mutex> lk(mu_);
        for (auto& f : facts_) {
            if (f.key == key) {
                f.value = value; f.confidence = 1.0f;
                auto now = std::chrono::system_clock::now().time_since_epoch();
                f.last_seen_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
                f.persistent = persistent;
                return;
            }
        }
        if (facts_.size() >= kMaxFacts)
            facts_.erase(facts_.begin());  // evict oldest
        facts_.emplace_back(key, value, 1.0f, persistent);
    }

    // Decrease confidence when a fact turns out to be wrong
    void invalidate(const std::string& key) {
        std::lock_guard<std::mutex> lk(mu_);
        for (auto it = facts_.begin(); it != facts_.end(); ++it) {
            if (it->key == key) {
                it->confidence -= kDecayOnFailure;
                if (it->confidence < kMinConfidence)
                    facts_.erase(it);
                return;
            }
        }
    }

    // Lookup a fact; returns "" if not found or low confidence
    std::string get(const std::string& key, float min_conf = 0.5f) const {
        std::lock_guard<std::mutex> lk(mu_);
        for (const auto& f : facts_)
            if (f.key == key && f.confidence >= min_conf)
                return f.value;
        return "";
    }

    // Build context string for injection into LLM prompts
    // Returns "## Known Environment\ncwd: E:\\path\nfile:X exists\n..." (max ~300 chars)
    std::string build_context(float min_conf = 0.6f) const {
        std::lock_guard<std::mutex> lk(mu_);
        if (facts_.empty()) return "";
        std::ostringstream out;
        out << "## Known Environment (confirmed facts)\n";
        int count = 0;
        for (const auto& f : facts_) {
            if (f.confidence < min_conf) continue;
            out << f.key << ": " << f.value << "\n";
            if (++count >= 8) break;  // cap at 8 facts (~200 chars)
        }
        return count > 0 ? out.str() : "";
    }

    // Serialize to string (for session persistence)
    std::string serialize() const {
        std::lock_guard<std::mutex> lk(mu_);
        std::ostringstream out;
        // Markdown table format (replaces TSV)
        out << "# Environment Knowledge Base\n\n"
            << "> Auto-maintained. Agents use this to avoid re-discovering paths.\n\n"
            << "| Key | Value | Confidence | Last Seen |\n"
            << "|-----|-------|-----------|-----------|\n";
        for (const auto& f : facts_) {
            if (!f.persistent) continue;
            out << "| " << f.key << " | " << f.value
                << " | " << f.confidence
                << " | " << f.last_seen_ms << " |\n";
        }
        return out.str();
    }

    // Deserialize from Markdown table (also handles legacy TSV)
    void deserialize_md(const std::string& content) {
        std::lock_guard<std::mutex> lk(mu_);
        std::istringstream ss(content);
        std::string line;
        bool in_table = false;
        while (std::getline(ss, line)) {
            // Detect TSV legacy format (no | chars)
            if (line.empty() || line[0] == '#' || line[0] == '>') continue;
            if (line.find("| Key |") != std::string::npos) { in_table = true; continue; }
            if (line.find("|-----|") != std::string::npos) continue;
            if (in_table && line.size() > 2 && line[0] == '|') {
                // Parse: | key | value | confidence | last_seen |
                auto parse_cell = [](const std::string& s, size_t& pos) -> std::string {
                    while (pos < s.size() && (s[pos] == '|' || s[pos] == ' ')) ++pos;
                    size_t start = pos;
                    while (pos < s.size() && s[pos] != '|') ++pos;
                    std::string cell = s.substr(start, pos - start);
                    while (!cell.empty() && cell.back() == ' ') cell.pop_back();
                    return cell;
                };
                size_t pos = 0;
                std::string key   = parse_cell(line, pos);
                std::string val   = parse_cell(line, pos);
                std::string conf  = parse_cell(line, pos);
                std::string ts    = parse_cell(line, pos);
                if (!key.empty() && !val.empty()) {
                    facts_.emplace_back();
                    facts_.back().key = key; facts_.back().value = val;
                    facts_.back().persistent = true;
                    try { facts_.back().confidence = std::stof(conf); }
                    catch(...) { facts_.back().confidence = 0.7f; }
                    try { facts_.back().last_seen_ms = std::stoll(ts); } catch(...) {}
                }
            } else if (!in_table && line.find('\t') != std::string::npos) {
                // Legacy TSV: key\tvalue\tconfidence\tlast_seen
                std::istringstream tss(line);
                std::string key, val, conf, ts;
                std::getline(tss, key, '\t');
                std::getline(tss, val, '\t');
                std::getline(tss, conf, '\t');
                std::getline(tss, ts, '\t');
                if (!key.empty() && !val.empty()) {
                    facts_.emplace_back();
                    facts_.back().key = key; facts_.back().value = val;
                    facts_.back().persistent = true;
                    try { facts_.back().confidence = std::stof(conf); }
                    catch(...) { facts_.back().confidence = 0.7f; }
                    try { facts_.back().last_seen_ms = std::stoll(ts); } catch(...) {}
                }
            }
        }
    }

    // Deserialize from string
    void deserialize(const std::string& data) {
        std::istringstream in(data);
        std::string line;
        std::lock_guard<std::mutex> lk(mu_);
        while (std::getline(in, line)) {
            auto t1 = line.find('\t');
            if (t1 == std::string::npos) continue;
            auto t2 = line.find('\t', t1+1);
            if (t2 == std::string::npos) continue;
            auto t3 = line.find('\t', t2+1);
            std::string key   = line.substr(0, t1);
            std::string value = line.substr(t1+1, t2-t1-1);
            float conf = 0.8f;
            try { conf = std::stof(line.substr(t2+1, t3==std::string::npos ? std::string::npos : t3-t2-1)); } catch(...) {}
            // Slightly reduce confidence on load (facts may be stale)
            facts_.emplace_back(key, value, conf * 0.9f, true);
        }
    }

    bool empty() const { std::lock_guard<std::mutex> lk(mu_); return facts_.empty(); }
    size_t size() const { std::lock_guard<std::mutex> lk(mu_); return facts_.size(); }

private:
    mutable std::mutex    mu_;
    std::vector<EnvFact>  facts_;
};

}  // namespace agent
