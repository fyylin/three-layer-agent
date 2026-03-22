#include "indexer/simple_indexer.hpp"
#include "utils/tool_set.hpp"
#include <iostream>

int main() {
    auto runner = [](const std::string& cmd) -> std::string {
        std::cout << "[CMD] " << cmd << "\n";
        std::string result = agent::tool_run_command(cmd);
        std::cout << "[OUT] " << (result.empty() ? "(empty)" : result.substr(0, 100)) << "\n";
        return result;
    };

    indexer::SimpleIndexer idx(runner);
    std::string root = ".";

    std::cout << "=== Indexing Current Project ===\n\n";

    // Test 1: Find WorkerAgent class definition
    std::cout << "1. Finding 'WorkerAgent' definition:\n";
    auto info = idx.find_definition("WorkerAgent", root);
    if (!info.definition.file.empty()) {
        std::cout << "   " << info.definition.file << ":" << info.definition.line
                  << " - " << info.definition.context << "\n";
    } else {
        std::cout << "   Not found\n";
    }

    // Test 2: Find ToolRegistry references
    std::cout << "\n2. Finding 'ToolRegistry' references:\n";
    auto refs = idx.find_references("ToolRegistry", root);
    int count = 0;
    for (const auto& ref : refs) {
        if (count++ < 5) {
            std::cout << "   " << ref.file << ":" << ref.line << "\n";
        }
    }
    std::cout << "   Total: " << refs.size() << " references\n";

    // Test 3: Find callers of register_tool
    std::cout << "\n3. Finding callers of 'register_tool':\n";
    auto callers = idx.find_references("register_tool(", root);
    count = 0;
    for (const auto& c : callers) {
        if (count++ < 5) {
            std::cout << "   " << c.file << ":" << c.line << "\n";
        }
    }
    std::cout << "   Total: " << callers.size() << " calls\n";

    std::cout << "\n=== Indexing Complete ===\n";
    return 0;
}
