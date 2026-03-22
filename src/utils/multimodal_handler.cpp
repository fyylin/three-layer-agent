#include "utils/multimodal_handler.hpp"
#include "utils/utf8_fstream.hpp"
#include <fstream>
#include <sstream>

namespace agent {

static bool ends_with(const std::string& str, const std::string& suffix) {
    return str.size() >= suffix.size() &&
           str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}

MediaInput MultimodalHandler::load_image(const std::string& path) {
    agent::utf8_ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("Cannot open file: " + path);

    std::vector<unsigned char> data((std::istreambuf_iterator<char>(file)),
                                     std::istreambuf_iterator<char>());

    std::string mime = "image/png";
    if (ends_with(path, ".jpg") || ends_with(path, ".jpeg")) mime = "image/jpeg";
    else if (ends_with(path, ".gif")) mime = "image/gif";
    else if (ends_with(path, ".webp")) mime = "image/webp";

    return {MediaType::Image, path, mime, std::move(data)};
}

std::string MultimodalHandler::format_for_llm(const MediaInput& media) {
    std::ostringstream oss;
    oss << "{\"type\":\"" << (media.type == MediaType::Image ? "image" : "unknown")
        << "\",\"source\":{\"type\":\"base64\",\"media_type\":\"" << media.mime_type
        << "\",\"data\":\"" << encode_base64(media.data) << "\"}}";
    return oss.str();
}

} // namespace agent
