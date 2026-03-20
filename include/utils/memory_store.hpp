#pragma once
// =============================================================================
// include/utils/memory_store.hpp
//
// Three-tier memory model:
//
//   ShortTerm  --  sliding window of Message objects within one task/subtask.
//               Lives in RAM; never persisted.  Size-bounded.
//
//   Session    --  key-value string store for the current run().
//               Written to workspace/memory/session.json on flush().
//               Used to pass "what we already learned" between subtasks.
//
//   LongTerm   --  append-only summary strings, persisted across runs.
//               LLM generates summaries; stored in workspace/memory/long_term/
//               Loaded on startup; relevant fragments injected into Director.
//
// All methods are thread-safe.
// =============================================================================

#include "agent/api_client.hpp"   // Message struct

#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace agent {

class MemoryStore {
public:
    explicit MemoryStore(size_t short_term_window = 8)
        : window_(short_term_window) {}

    // -- ShortTerm ---------------------------------------------------------

    // Push a Message onto the sliding window.
    // When size exceeds window_, the oldest entry is dropped.
    void push_message(const Message& msg);

    // Push a task result summary (role="assistant").
    void push_result(const std::string& task_id, const std::string& output);

    // Retrieve the last N messages (for LLM context injection).
    [[nodiscard]] std::vector<Message> get_context(size_t max = 8) const;

    [[nodiscard]] bool short_term_empty() const noexcept;
    void clear_short_term();

    // -- Session -----------------------------------------------------------

    void        set(const std::string& key, const std::string& value);
    std::string get(const std::string& key,
                    const std::string& default_val = "") const;
    bool        has(const std::string& key) const;
    void        remove(const std::string& key);

    // Merge another store's session into this one (for result aggregation).
    void merge_session(const MemoryStore& other);

    // -- LongTerm ----------------------------------------------------------

    void append_summary(const std::string& summary);

    // Compact: summarise oldest half of short_term_ when above threshold
    void maybe_compact(float threshold = 0.8f);
    bool needs_compact(float threshold = 0.8f) const noexcept {
        return window_ > 0 &&
               (float)short_term_.size() / (float)window_ >= threshold;
    }
    [[nodiscard]] std::vector<std::string> get_summaries() const;

    // Simple keyword-based relevance filter (no vector DB needed for v2).
    [[nodiscard]] std::string load_relevant(const std::string& query,
                                             size_t max_summaries = 3) const;

    // -- Persistence -------------------------------------------------------

    // Flush session and long-term to disk.  Short-term is never persisted.
    void save_session(const std::string& path) const;
    void load_session(const std::string& path);

    void save_long_term(const std::string& dir) const;
    void load_long_term(const std::string& dir);

    // Conversation-scoped MEMORY.md (Markdown, human-readable)
    void save_conversation_md(const std::string& md_path) const;
    void load_conversation_md(const std::string& md_path);
    void append_run_to_memory_md(const std::string& md_path,
                                  const std::string& run_id,
                                  const std::string& goal,
                                  const std::string& result_summary) const;

    // Generate a summary of the current session using an LLM call.
    // Call this at the end of a run when memory_long_term_enabled=true.
    // The summary is appended to long_term_ and saved.
    // Returns the generated summary string (empty on failure).
    std::string generate_and_store_summary(
        const std::string&  goal,
        const std::string&  answer,
        const std::string&  long_term_dir,
        std::function<std::string(const std::string&)> llm_call) noexcept;

private:
    mutable std::mutex mu_;

    // ShortTerm
    size_t              window_;
    std::deque<Message> short_term_;

    // Session
    std::unordered_map<std::string, std::string> session_;

    // LongTerm
    std::vector<std::string> long_term_;
};

} // namespace agent
