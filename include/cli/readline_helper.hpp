#pragma once
#include <string>
#include <vector>

namespace agent::cli {

class CommandSystem;

// Command completion helper
class ReadlineHelper {
public:
    static void init(const std::vector<std::string>& commands);
    static void set_command_system(CommandSystem* sys);
    static std::string read_line(const char* prompt);
    static void add_history(const std::string& line);

private:
    static std::vector<std::string> commands_;
    static CommandSystem* cmd_system_;
};

} // namespace agent::cli
