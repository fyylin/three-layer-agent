#pragma once
#include <string>
#include <vector>
#include <functional>

namespace agent::cli {

class TUIInput {
public:
    using CompletionCallback = std::function<std::vector<std::string>(const std::string&)>;

    void set_completion_callback(CompletionCallback cb) { completion_cb_ = std::move(cb); }
    std::string read_line(const char* prompt);

private:
    CompletionCallback completion_cb_;
};

} // namespace agent::cli
