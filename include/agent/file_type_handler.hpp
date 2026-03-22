#pragma once
#include <string>
#include <optional>

namespace agent {

class FileTypeHandler {
public:
    static std::optional<std::string> handle_special_file(
        const std::string& path,
        const std::string& action);

    static bool is_pdf(const std::string& path);
    static bool is_binary(const std::string& path);
    static bool is_image(const std::string& path);

private:
    static std::string try_pdf_extraction(const std::string& path);
    static std::string try_image_description(const std::string& path);
    static bool command_exists(const std::string& cmd);
};

} // namespace agent
