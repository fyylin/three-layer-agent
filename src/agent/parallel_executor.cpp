#include "agent/parallel_executor.hpp"

namespace agent {

std::vector<std::string> ParallelToolExecutor::execute(
    const std::vector<ToolCall>& calls,
    ToolInvoker invoker
) {
    std::vector<std::future<std::string>> futures;
    futures.reserve(calls.size());

    for (const auto& call : calls) {
        futures.push_back(std::async(std::launch::async, [&, call]() {
            return invoker(call.name, call.input);
        }));
    }

    std::vector<std::string> results;
    results.reserve(calls.size());
    for (auto& fut : futures) {
        results.push_back(fut.get());
    }
    return results;
}

} // namespace agent
