#pragma once
// =============================================================================
// include/utils/logger.hpp   --   cross-platform structured logger
//
// Windows: outputs via WriteConsoleW (UTF-8 → wide) so Chinese characters
//          display correctly regardless of the console code page.
//          Falls back to fputs when stdout is redirected to a file.
// POSIX:   fputs to stderr as before.
// =============================================================================

#include <chrono>
#include <cstdio>
#include <mutex>
#include <sstream>
#include <string>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif

namespace agent {

enum class LogLevel : int { Debug=0, Info=1, Warn=2, Error=3 };

[[nodiscard]] inline LogLevel log_level_from_string(const std::string& s) noexcept {
    if (s=="debug") return LogLevel::Debug;
    if (s=="warn")  return LogLevel::Warn;
    if (s=="error") return LogLevel::Error;
    return LogLevel::Info;
}
[[nodiscard]] inline const char* log_level_to_cstr(LogLevel l) noexcept {
    switch(l){
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO ";
        case LogLevel::Warn:  return "WARN ";
        case LogLevel::Error: return "ERROR";
    }
    return "INFO ";
}

class Logger {
public:
    static Logger& instance() { static Logger inst; return inst; }

    void set_level(LogLevel l)           noexcept { min_level_ = l; }
    void set_level(const std::string& s) noexcept { min_level_ = log_level_from_string(s); }

    void log(LogLevel level,
             const std::string& layer,
             const std::string& agid,
             const std::string& task_id,
             const std::string& message) noexcept {
        if (level < min_level_) return;
        std::string line = format(level, layer, agid, task_id, message);
        std::lock_guard<std::mutex> lk(mu_);
        write_line(line);
    }

    void debug(const std::string& l,const std::string& a,const std::string& t,const std::string& m) noexcept { log(LogLevel::Debug,l,a,t,m); }
    void info (const std::string& l,const std::string& a,const std::string& t,const std::string& m) noexcept { log(LogLevel::Info, l,a,t,m); }
    void warn (const std::string& l,const std::string& a,const std::string& t,const std::string& m) noexcept { log(LogLevel::Warn, l,a,t,m); }
    void error(const std::string& l,const std::string& a,const std::string& t,const std::string& m) noexcept { log(LogLevel::Error,l,a,t,m); }

private:
    Logger() = default;
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    LogLevel   min_level_ = LogLevel::Info;
    std::mutex mu_;

    // Write one log line. On Windows uses WriteConsoleW so UTF-8 Chinese
    // characters are displayed correctly even in CP936 CMD windows.
    static void write_line(const std::string& utf8_line) noexcept {
#ifdef _WIN32
        HANDLE h = GetStdHandle(STD_ERROR_HANDLE);
        // Check if stderr is a real console (not redirected to file)
        DWORD mode = 0;
        if (h != INVALID_HANDLE_VALUE && GetConsoleMode(h, &mode)) {
            // It's a console  --  convert UTF-8 → wide and use WriteConsoleW
            int wlen = MultiByteToWideChar(CP_UTF8, 0,
                utf8_line.c_str(), (int)utf8_line.size(), nullptr, 0);
            if (wlen > 0) {
                std::wstring wline(static_cast<size_t>(wlen), L'\0');
                MultiByteToWideChar(CP_UTF8, 0,
                    utf8_line.c_str(), (int)utf8_line.size(),
                    &wline[0], wlen);
                DWORD written = 0;
                WriteConsoleW(h, wline.c_str(), (DWORD)wline.size(),
                              &written, nullptr);
                return;
            }
        }
        // Redirected to file  --  write raw UTF-8 bytes
        std::fputs(utf8_line.c_str(), stderr);
        std::fflush(stderr);
#else
        std::fputs(utf8_line.c_str(), stderr);
        std::fflush(stderr);
#endif
    }

    static std::string iso8601_now() noexcept {
        using namespace std::chrono;
        auto now = system_clock::now();
        auto tt  = system_clock::to_time_t(now);
        auto ms  = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
        std::tm tm_buf{};
#ifdef _WIN32
        gmtime_s(&tm_buf, &tt);
#else
        gmtime_r(&tt, &tm_buf);
#endif
        char buf[40];
        std::snprintf(buf, sizeof(buf),
            "%04d-%02d-%02dT%02d:%02d:%02d.%03ldZ",
            tm_buf.tm_year+1900, tm_buf.tm_mon+1, tm_buf.tm_mday,
            tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec,
            static_cast<long>(ms.count()));
        return buf;
    }

    static std::string format(LogLevel level,
                              const std::string& layer,
                              const std::string& agid,
                              const std::string& task_id,
                              const std::string& message) noexcept {
        std::ostringstream ss;
        ss << '[' << iso8601_now() << ']'
           << " [" << log_level_to_cstr(level) << ']'
           << " [" << layer   << ']'
           << " [" << agid    << ']'
           << " [" << task_id << "] "
           << message << '\n';
        return ss.str();
    }
};

#define LOG_DEBUG(layer,agid,task,msg) ::agent::Logger::instance().debug((layer),(agid),(task),(msg))
#define LOG_INFO( layer,agid,task,msg) ::agent::Logger::instance().info ((layer),(agid),(task),(msg))
#define LOG_WARN( layer,agid,task,msg) ::agent::Logger::instance().warn ((layer),(agid),(task),(msg))
#define LOG_ERROR(layer,agid,task,msg) ::agent::Logger::instance().error((layer),(agid),(task),(msg))

} // namespace agent
