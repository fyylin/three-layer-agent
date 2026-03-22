#include "cli/command_system.hpp"
#include <iostream>
#include <sstream>
#include <algorithm>

namespace agent::cli {

GlobalSettings g_settings;

void CommandSystem::register_command(const Command& cmd) {
    commands_[cmd.name] = cmd;
}

bool CommandSystem::execute(const std::string& input) {
    if (input.empty() || input[0] != '/') return false;

    std::istringstream iss(input.substr(1));
    std::string cmd_name;
    iss >> cmd_name;

    auto it = commands_.find(cmd_name);
    if (it == commands_.end()) {
        std::cout << "Unknown command: /" << cmd_name << "\nType /help for available commands.\n";
        return true;
    }

    std::map<std::string, std::string> args;
    std::string rest;
    std::getline(iss, rest);

    size_t pos = 0;
    while (pos < rest.size()) {
        while (pos < rest.size() && std::isspace(rest[pos])) ++pos;
        if (pos >= rest.size()) break;

        size_t eq = rest.find('=', pos);
        if (eq == std::string::npos) break;

        std::string key = rest.substr(pos, eq - pos);
        pos = eq + 1;

        std::string value;
        if (pos < rest.size() && (rest[pos] == '"' || rest[pos] == '\'')) {
            char quote = rest[pos++];
            size_t end = rest.find(quote, pos);
            if (end == std::string::npos) {
                std::cout << "Error: Unclosed quote in parameter value\n";
                return true;
            }
            value = rest.substr(pos, end - pos);
            pos = end + 1;
        } else {
            size_t end = rest.find(' ', pos);
            value = rest.substr(pos, end - pos);
            pos = (end == std::string::npos) ? rest.size() : end;
        }
        args[key] = value;
    }

    it->second.handler(args);
    return true;
}

std::vector<std::string> CommandSystem::get_completions(const std::string& prefix) const {
    std::vector<std::string> results;
    for (const auto& [name, cmd] : commands_) {
        if (name.find(prefix) == 0) {
            results.push_back("/" + name);
        }
    }
    return results;
}

void CommandSystem::show_help(const std::string& cmd_name) const {
    if (cmd_name.empty()) {
        std::cout << "\n=== Available Commands ===\n\n";
        for (const auto& [name, cmd] : commands_) {
            std::cout << "  /" << name << " - " << cmd.description << "\n";
        }
        std::cout << "\nType /help <command> for detailed options.\n\n";
    } else {
        auto it = commands_.find(cmd_name);
        if (it == commands_.end()) {
            std::cout << "Unknown command: " << cmd_name << "\n";
            return;
        }
        const auto& cmd = it->second;
        std::cout << "\n/" << cmd.name << " - " << cmd.description << "\n\n";
        if (!cmd.options.empty()) {
            std::cout << "Options:\n";
            for (const auto& opt : cmd.options) {
                std::cout << "  " << opt.name << " - " << opt.description;
                if (!opt.default_value.empty()) {
                    std::cout << " (default: " << opt.default_value << ")";
                }
                if (!opt.choices.empty()) {
                    std::cout << "\n    Choices: ";
                    for (size_t i = 0; i < opt.choices.size(); ++i) {
                        if (i > 0) std::cout << ", ";
                        std::cout << opt.choices[i];
                    }
                }
                std::cout << "\n";
            }
        }
        std::cout << "\n";
    }
}

std::vector<std::string> CommandSystem::list_commands() const {
    std::vector<std::string> result;
    for (const auto& [name, _] : commands_) {
        result.push_back(name);
    }
    return result;
}

const Command* CommandSystem::get_command(const std::string& name) const {
    auto it = commands_.find(name);
    return it != commands_.end() ? &it->second : nullptr;
}

} // namespace agent::cli
