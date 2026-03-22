#pragma once
#include <string>
#include <vector>

namespace agent {

struct ContextEntry {
    std::string key;
    std::string value;
    int turn_index;
};

class ConversationContext {
public:
    void add(const std::string& key, const std::string& value);
    std::string get(const std::string& key) const;
    std::string build_summary(int recent_turns = 3) const;
    void clear_old(int keep_recent = 5);

private:
    std::vector<ContextEntry> entries_;
    int current_turn_ = 0;
};

} // namespace agent
