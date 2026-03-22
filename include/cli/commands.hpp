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
  /set [key=value]  — View or modify global settings
  /status           — Show system status and statistics
  /stats            — Show task statistics and performance metrics
  /profile          — Show performance profiling information
  /bench [n]        — Run performance benchmark
  /reset            — Reset all settings to defaults
  /retry [max=N]    — Configure retry strategy
  /preset <name>    — Load preset configuration (debug/production/quality/fast)
  /model [opts]     — View or switch model (supports per-agent config)
  /provider [act]   — Configure multi-provider support
  /config [mode]    — Configuration wizard (quick/advanced/show)
  /clear [target]   — Clear stats/memory/cache/all
  /log [action]     — View or manage logs
  /export           — Export current configuration
  /save [file]      — Save configuration to file
  /load [file]      — Load configuration from file
  /history [n]      — Show command history
  /alias [name cmd] — Manage command aliases
  /watch [interval] — Start monitoring mode
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
    if (goal.empty() || goal[0] != '/') return false;
    return goal == "/new" || goal == "/conv" || goal == "/convs" ||
           goal == "/conversations" || goal == "/workspace" ||
           goal == "/experience" || goal == "/exp" || goal == "/help" ||
           goal == "/?" || goal == "/set" || goal == "/status" ||
           goal == "/reset" || goal == "/retry" || goal == "/stats" ||
           goal == "/clear" || goal == "/preset" || goal == "/model" ||
           goal == "/log" || goal == "/export" || goal == "/bench" ||
           goal == "/profile" || goal == "/history" || goal == "/alias" ||
           goal == "/watch" || goal == "/save" || goal == "/load" ||
           goal.substr(0, std::min(size_t(8), goal.size())) == "/switch " ||
           goal.substr(0, std::min(size_t(5), goal.size())) == "/set " ||
           goal.substr(0, std::min(size_t(7), goal.size())) == "/clear " ||
           goal.substr(0, std::min(size_t(8), goal.size())) == "/preset " ||
           goal.substr(0, std::min(size_t(7), goal.size())) == "/model " ||
           goal.substr(0, std::min(size_t(5), goal.size())) == "/log " ||
           goal.substr(0, std::min(size_t(7), goal.size())) == "/bench " ||
           goal.substr(0, std::min(size_t(9), goal.size())) == "/history " ||
           goal.substr(0, std::min(size_t(7), goal.size())) == "/alias " ||
           goal.substr(0, std::min(size_t(7), goal.size())) == "/watch " ||
           goal.substr(0, std::min(size_t(6), goal.size())) == "/save " ||
           goal.substr(0, std::min(size_t(6), goal.size())) == "/load ";
}

} // namespace agent::cli
