#include "cli/readline_helper.hpp"
#include "cli/command_system.hpp"
#include <iostream>
#include <algorithm>

#ifdef _WIN32
#include <conio.h>
#include <windows.h>
#else
#include <readline/readline.h>
#include <readline/history.h>
#endif

namespace agent::cli {

std::vector<std::string> ReadlineHelper::commands_;
CommandSystem* ReadlineHelper::cmd_system_ = nullptr;

#ifdef _WIN32
// Windows implementation with UTF-8 support
std::string ReadlineHelper::read_line(const char* prompt) {
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    // Show command hints before input
    if (cmd_system_ && !commands_.empty()) {
        std::cout << "\n可用命令: ";
        int shown = 0;
        for (const auto& cmd : commands_) {
            if (shown++ >= 8) { std::cout << "..."; break; }
            std::cout << "/" << cmd << " ";
        }
        std::cout << "\n";
    }

    std::cout << prompt << std::flush;

    WCHAR wbuf[4096] = {};
    DWORD nr = 0;
    ReadConsoleW(hIn, wbuf, 4095, &nr, nullptr);

    int needed = WideCharToMultiByte(CP_UTF8, 0, wbuf, nr, nullptr, 0, nullptr, nullptr);
    std::string input(needed, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wbuf, nr, &input[0], needed, nullptr, nullptr);

    while (!input.empty() && (input.back() == '\r' || input.back() == '\n')) {
        input.pop_back();
    }

    return input;
}

void ReadlineHelper::add_history(const std::string&) {
    // Already handled in read_line
}

#else
// Unix implementation with readline
char* command_generator(const char* text, int state) {
    static size_t list_index, len;
    if (!state) {
        list_index = 0;
        len = strlen(text);
    }

    while (list_index < ReadlineHelper::commands_.size()) {
        const auto& cmd = ReadlineHelper::commands_[list_index++];
        if (cmd.compare(0, len, text) == 0) {
            return strdup(("/" + cmd).c_str());
        }
    }
    return nullptr;
}

char** command_completion(const char* text, int start, int) {
    if (start == 0 && text[0] == '/') {
        return rl_completion_matches(text + 1, command_generator);
    }
    return nullptr;
}

std::string ReadlineHelper::read_line(const char* prompt) {
    char* line = readline(prompt);
    if (!line) return "";
    std::string result(line);
    free(line);
    return result;
}

void ReadlineHelper::add_history(const std::string& line) {
    if (!line.empty()) {
        ::add_history(line.c_str());
    }
}
#endif

void ReadlineHelper::init(const std::vector<std::string>& commands) {
    commands_ = commands;
#ifndef _WIN32
    rl_attempted_completion_function = command_completion;
#endif
}

void ReadlineHelper::set_command_system(CommandSystem* sys) {
    cmd_system_ = sys;
}

} // namespace agent::cli
