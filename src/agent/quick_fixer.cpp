#include "agent/quick_fixer.hpp"
#include <algorithm>

namespace agent {

std::string QuickFixer::try_quick_fix(const std::string& error) {
    if (is_path_error(error)) {
        return suggest_path_fix(error);
    }

    if (is_tool_mismatch(error)) {
        return suggest_tool_fix(error);
    }

    return "";
}

bool QuickFixer::is_path_error(const std::string& error) {
    return error.find("not found") != std::string::npos ||
           error.find("does not exist") != std::string::npos ||
           error.find("No such file") != std::string::npos;
}

bool QuickFixer::is_tool_mismatch(const std::string& error) {
    return error.find("Binary file") != std::string::npos ||
           error.find("PDF file") != std::string::npos ||
           error.find("cannot display") != std::string::npos;
}

std::string QuickFixer::suggest_path_fix(const std::string& error) {
    return "[Quick fix] Check if file path is correct. Use list_dir to verify location.";
}

std::string QuickFixer::suggest_tool_fix(const std::string& error) {
    if (error.find("PDF") != std::string::npos) {
        return "[Quick fix] PDF detected. Use specialized extraction or ask user to provide text.";
    }
    return "[Quick fix] File type mismatch. Consider alternative approach.";
}

} // namespace agent
