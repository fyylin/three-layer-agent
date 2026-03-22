#pragma once
#include <string>
#include <vector>
#include <map>

namespace agent {

enum class MediaType { Image, Audio, Video, Document };

struct MediaInput {
    MediaType type;
    std::string path;
    std::string mime_type;
    std::vector<unsigned char> data;
};

class MultimodalHandler {
public:
    static std::string encode_base64(const std::vector<unsigned char>& data) {
        static const char* b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        int val = 0, bits = -6;
        for (unsigned char c : data) {
            val = (val << 8) + c;
            bits += 8;
            while (bits >= 0) {
                out.push_back(b64[(val >> bits) & 0x3F]);
                bits -= 6;
            }
        }
        if (bits > -6) out.push_back(b64[((val << 8) >> (bits + 8)) & 0x3F]);
        while (out.size() % 4) out.push_back('=');
        return out;
    }

    static MediaInput load_image(const std::string& path);
    static std::string format_for_llm(const MediaInput& media);
};

} // namespace agent
