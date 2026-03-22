#include "agent/tool_composer.hpp"
#include <cstdlib>
#include <array>
#include <memory>

#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#endif

namespace agent {

bool ToolComposer::command_available(const std::string& cmd) {
#ifdef _WIN32
    std::string check = "where " + cmd + " >nul 2>&1";
#else
    std::string check = "command -v " + cmd + " >/dev/null 2>&1";
#endif
    return system(check.c_str()) == 0;
}

std::string ToolComposer::try_pdf_extraction(const std::string& path) {
    if (command_available("pdftotext")) {
        std::string cmd = "pdftotext \"" + path + "\" - 2>&1";
        std::array<char, 128> buffer;
        std::string result;
        FILE* pipe = popen(cmd.c_str(), "r");
        if (pipe) {
            while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
                result += buffer.data();
            }
            pclose(pipe);
            return result.empty() ? "[pdftotext returned empty]" : result;
        }
    }

    if (command_available("python")) {
        std::string script = R"(python -c "import sys; import PyPDF2; f=open(sys.argv[1],'rb'); pdf=PyPDF2.PdfReader(f); print(''.join(p.extract_text() for p in pdf.pages[:3]))" ")";
        return try_python_script(script + path + "\"");
    }

    return "[PDF extraction unavailable: install pdftotext or Python PyPDF2]";
}

std::string ToolComposer::try_python_script(const std::string& script) {
    std::array<char, 128> buffer;
    std::string result;
    FILE* pipe = popen(script.c_str(), "r");
    if (pipe) {
        while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
            result += buffer.data();
        }
        pclose(pipe);
    }
    return result.empty() ? "[Script returned empty]" : result;
}

} // namespace agent
