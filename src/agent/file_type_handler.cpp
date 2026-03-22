#include "agent/file_type_handler.hpp"
#include "agent/tool_composer.hpp"
#include <algorithm>
#include <cstdlib>

namespace agent {

std::optional<std::string> FileTypeHandler::handle_special_file(
    const std::string& path, const std::string& action) {

    if (is_pdf(path) && (action == "read" || action == "view")) {
        return try_pdf_extraction(path);
    }

    if (is_image(path) && (action == "read" || action == "view")) {
        return try_image_description(path);
    }

    if (is_binary(path) && action == "read") {
        return "Binary file detected. Cannot display as text.";
    }

    return std::nullopt;
}

bool FileTypeHandler::is_pdf(const std::string& path) {
    return path.size() >= 4 &&
           path.substr(path.size() - 4) == ".pdf";
}

bool FileTypeHandler::is_binary(const std::string& path) {
    static const char* bin_exts[] = {
        ".exe", ".dll", ".so", ".dylib", ".bin", ".obj", ".o"
    };
    for (auto ext : bin_exts) {
        if (path.size() >= strlen(ext) &&
            path.substr(path.size() - strlen(ext)) == ext) {
            return true;
        }
    }
    return false;
}

bool FileTypeHandler::is_image(const std::string& path) {
    static const char* img_exts[] = {
        ".png", ".jpg", ".jpeg", ".gif", ".bmp", ".webp"
    };
    for (auto ext : img_exts) {
        if (path.size() >= strlen(ext) &&
            path.substr(path.size() - strlen(ext)) == ext) {
            return true;
        }
    }
    return false;
}

std::string FileTypeHandler::try_pdf_extraction(const std::string& path) {
    std::string result = ToolComposer::try_pdf_extraction(path);

    if (result.find("unavailable") != std::string::npos) {
        return "PDF file detected (" + path + "). "
               "Cannot extract text (pdftotext/PyPDF2 not available). "
               "Suggestion: Open with PDF reader and copy needed content.";
    }

    return result;
}

std::string FileTypeHandler::try_image_description(const std::string& path) {
    return "Image file detected (" + path + "). "
           "Current tools cannot display images. "
           "Suggestion: Open with image viewer or describe what you want to know.";
}

bool FileTypeHandler::command_exists(const std::string& cmd) {
#ifdef _WIN32
    std::string check = "where " + cmd + " >nul 2>&1";
#else
    std::string check = "command -v " + cmd + " >/dev/null 2>&1";
#endif
    return system(check.c_str()) == 0;
}

} // namespace agent
