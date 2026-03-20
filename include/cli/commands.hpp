#pragma once
// =============================================================================
// include/cli/commands.hpp  --  Conversation commands and CLI help text
// Used by main.cpp to keep the main loop readable.
// =============================================================================
#include <string>

namespace agent::cli {

// Help text shown by /help command
inline const char* kHelpText = R"(
Commands:
  /new              — Start a new conversation (context cleared)
  /conv             — Show current conversation ID and message count
  /convs            — List all past conversations
  /switch <conv-id> — Resume a past conversation (restores memory)
  /workspace        — Display workspace/current/WORKSPACE.md
  /experience       — Display cross-conversation EXPERIENCE.md
  /help, /?         — Show this help

Tips:
  • Type your goal naturally in Chinese or English
  • Use /new to switch topics without losing old logs
  • Run with --list-prompts to see all loaded prompt files
  • Run with --list-skills to see available cross-agent skills
  • Check workspace/logs/activity.md for full session history

Conversation directories: workspace/conversations/<conv-id>/
  MEMORY.md       — conversation memory (Markdown, readable)
  runs.md         — all messages in this conversation
  experience.md   — lessons learned in this conversation
)";

// Returns true if goal is a conversation command (handled inline)
inline bool is_conv_command(const std::string& goal) {
    return goal == "/new" || goal == "/conv" || goal == "/convs" ||
           goal == "/conversations" ||
           goal == "/workspace" || goal == "/experience" ||
           goal == "/exp" || goal == "/help" || goal == "/?" ||
           (goal.size() > 8 && goal.substr(0,8) == "/switch ");
}

} // namespace agent::cli
