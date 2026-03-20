// =============================================================================
// src/utils/structured_log.cpp
// =============================================================================
#include "utils/structured_log.hpp"
#include "utils/file_lock.hpp"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <mutex>
#include <sstream>
#include <unordered_map>

namespace agent {

// Global log files share a per-path mutex
std::mutex& StructuredLogger::global_mutex(const std::string& path) {
    static std::mutex map_mu;
    static std::unordered_map<std::string, std::mutex> mu_map;
    std::lock_guard<std::mutex> lk(map_mu);
    return mu_map[path];
}

StructuredLogger::StructuredLogger(std::string agent_id,
                                    std::string layer,
                                    std::string run_id,
                                    std::string global_log_path,
                                    std::string agent_log_path)
    : agent_id_(std::move(agent_id))
    , layer_(std::move(layer))
    , run_id_(std::move(run_id))
    , global_path_(std::move(global_log_path))
    , agent_path_(std::move(agent_log_path))
{}

StructuredLogger::~StructuredLogger() { flush(); }

void StructuredLogger::log(const std::string& level,
                            const std::string& task_id,
                            const std::string& event_type,
                            const std::string& message,
                            const std::string& data_json) {
    if (agent_id_.empty()) return;  // NullLogger

    std::string line = build_record(level, task_id, event_type, message, data_json);

    if (!global_path_.empty())
        write_line(global_path_, global_mutex(global_path_), line);

    if (!agent_path_.empty())
        write_line(agent_path_, agent_mu_, line);

    // Also write NDJSON to structured path if configured
    if (!structured_path_.empty())
        write_line(structured_path_, global_mutex(structured_path_), line);
}

void StructuredLogger::info(const std::string& tid, const std::string& ev,
                             const std::string& msg, const std::string& d) {
    log("INFO", tid, ev, msg, d);
}
void StructuredLogger::warn(const std::string& tid, const std::string& ev,
                             const std::string& msg, const std::string& d) {
    log("WARN", tid, ev, msg, d);
}
void StructuredLogger::error(const std::string& tid, const std::string& ev,
                              const std::string& msg, const std::string& d) {
    log("ERROR", tid, ev, msg, d);
}
void StructuredLogger::debug(const std::string& tid, const std::string& ev,
                              const std::string& msg, const std::string& d) {
    log("DEBUG", tid, ev, msg, d);
}

void StructuredLogger::flush() {}  // File appends are flushed per-write

std::string StructuredLogger::iso_now() noexcept {
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

// Escape a string for JSON (minimal  --  handles quotes and backslashes)
static std::string json_esc(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 4);
    for (unsigned char c : s) {
        if      (c == '"')  out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else out += (char)c;
    }
    return out;
}

std::string StructuredLogger::build_record(const std::string& level,
                                            const std::string& task_id,
                                            const std::string& event_type,
                                            const std::string& message,
                                            const std::string& data_json) const noexcept {
    std::ostringstream ss;
    ss << "{"
       << "\"ts\":\"" << iso_now() << "\","
       << "\"level\":\"" << level << "\","
       << "\"layer\":\"" << json_esc(layer_) << "\","
       << "\"agent_id\":\"" << json_esc(agent_id_) << "\","
       << "\"run_id\":\"" << json_esc(run_id_) << "\","
       << "\"span_id\":" << ++span_counter_ << ","
       << "\"task_id\":\"" << json_esc(task_id) << "\","
       << "\"event\":\"" << json_esc(event_type) << "\","
       << "\"message\":\"" << json_esc(message) << "\"";
    if (!data_json.empty())
        ss << ",\"data\":" << data_json;
    ss << "}";
    return ss.str();
}

void StructuredLogger::write_line(const std::string& path,
                                   std::mutex& mu,
                                   const std::string& line) noexcept {
    try {
        std::lock_guard<std::mutex> lk(mu);
        std::ofstream f(path, std::ios::app);
        if (f.is_open()) { f << line << "\n"; }
    } catch (...) {}
}

} // namespace agent
