#pragma once
#include <string>
#include <unordered_map>

namespace agent {

class QuickFixer {
public:
    // Detect common error patterns and return direct fix
    static std::string try_quick_fix(const std::string& error);

private:
    static bool is_path_error(const std::string& error);
    static bool is_tool_mismatch(const std::string& error);
    static std::string suggest_path_fix(const std::string& error);
    static std::string suggest_tool_fix(const std::string& error);
};

} // namespace agent
