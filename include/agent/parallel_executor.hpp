#pragma once
#include <string>
#include <vector>
#include <future>
#include <functional>

namespace agent {

struct ToolCall {
    std::string name;
    std::string input;
};

class ParallelToolExecutor {
public:
    using ToolInvoker = std::function<std::string(const std::string&, const std::string&)>;

    static std::vector<std::string> execute(
        const std::vector<ToolCall>& calls,
        ToolInvoker invoker
    );
};

} // namespace agent
