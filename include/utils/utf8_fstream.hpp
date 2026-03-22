#pragma once
#include <fstream>
#include <string>

#ifdef _WIN32
#include <windows.h>

namespace agent {

inline std::wstring utf8_to_wide(const std::string& utf8) {
    if (utf8.empty()) return {};
    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    if (wlen <= 0) return {};
    std::wstring wide(wlen - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &wide[0], wlen);
    return wide;
}

class utf8_ifstream : public std::ifstream {
public:
    utf8_ifstream() = default;
    explicit utf8_ifstream(const std::string& path, std::ios_base::openmode mode = std::ios_base::in) {
        open(path, mode);
    }
    void open(const std::string& path, std::ios_base::openmode mode = std::ios_base::in) {
        std::ifstream::open(utf8_to_wide(path), mode);
    }
};

class utf8_ofstream : public std::ofstream {
public:
    utf8_ofstream() = default;
    explicit utf8_ofstream(const std::string& path, std::ios_base::openmode mode = std::ios_base::out) {
        open(path, mode);
    }
    void open(const std::string& path, std::ios_base::openmode mode = std::ios_base::out) {
        std::ofstream::open(utf8_to_wide(path), mode);
    }
};

} // namespace agent

#else
// On POSIX, std::fstream already handles UTF-8 correctly
namespace agent {
using utf8_ifstream = std::ifstream;
using utf8_ofstream = std::ofstream;
}
#endif
