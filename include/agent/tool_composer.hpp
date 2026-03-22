#pragma once
#include <string>

namespace agent {

class ToolComposer {
public:
    // Try external commands when built-in tools fail
    static std::string try_pdf_extraction(const std::string& path);
    static std::string try_python_script(const std::string& script);
    static bool command_available(const std::string& cmd);
};

} // namespace agent
