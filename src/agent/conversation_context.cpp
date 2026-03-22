#include "agent/conversation_context.hpp"
#include <algorithm>

namespace agent {

void ConversationContext::add(const std::string& key, const std::string& value) {
    entries_.push_back({key, value, current_turn_++});
}

std::string ConversationContext::get(const std::string& key) const {
    for (auto it = entries_.rbegin(); it != entries_.rend(); ++it) {
        if (it->key == key) return it->value;
    }
    return "";
}

std::string ConversationContext::build_summary(int recent_turns) const {
    std::string summary;
    int min_turn = current_turn_ - recent_turns;

    for (const auto& e : entries_) {
        if (e.turn_index >= min_turn) {
            summary += "[" + e.key + ": " + e.value + "] ";
        }
    }

    return summary.empty() ? "" : "[Context] " + summary;
}

void ConversationContext::clear_old(int keep_recent) {
    int min_turn = current_turn_ - keep_recent;
    entries_.erase(
        std::remove_if(entries_.begin(), entries_.end(),
            [min_turn](const ContextEntry& e) { return e.turn_index < min_turn; }),
        entries_.end());
}

} // namespace agent
