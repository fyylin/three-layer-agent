#include "indexer/indexer_tools.hpp"
#include "indexer/simple_indexer.hpp"
#include "utils/tool_set.hpp"
#include <sstream>
#include <fstream>

namespace indexer {

std::string tool_find_definition(const std::string& input) {
    auto nl = input.find('\n');
    if (nl == std::string::npos) {
        return "Error: input format is 'symbol\\nroot_dir'";
    }

    std::string symbol = input.substr(0, nl);
    std::string root_dir = input.substr(nl + 1);

    // Use run_command as the command runner
    auto runner = [](const std::string& cmd) -> std::string {
        return agent::tool_run_command(cmd);
    };

    SimpleIndexer indexer(runner);
    auto info = indexer.find_definition(symbol, root_dir);

    if (info.definition.file.empty()) {
        return "Not found: " + symbol;
    }

    std::ostringstream out;
    out << info.definition.file << ":" << info.definition.line
        << ": " << info.definition.context;
    return out.str();
}

std::string tool_find_references(const std::string& input) {
    auto nl = input.find('\n');
    if (nl == std::string::npos) {
        return "Error: input format is 'symbol\\nroot_dir'";
    }

    std::string symbol = input.substr(0, nl);
    std::string root_dir = input.substr(nl + 1);

    auto runner = [](const std::string& cmd) -> std::string {
        return agent::tool_run_command(cmd);
    };

    SimpleIndexer indexer(runner);
    auto refs = indexer.find_references(symbol, root_dir);

    if (refs.empty()) {
        return "No references found for: " + symbol;
    }

    std::ostringstream out;
    for (const auto& ref : refs) {
        out << ref.file << ":" << ref.line << ": " << ref.context << "\n";
    }
    return out.str();
}

std::string tool_explain_code(const std::string& input) {
    auto nl1 = input.find('\n');
    auto nl2 = input.find('\n', nl1 + 1);
    if (nl1 == std::string::npos || nl2 == std::string::npos) {
        return "Error: input format is 'file_path\\nstart_line\\nend_line'";
    }

    std::string file = input.substr(0, nl1);
    int start = std::stoi(input.substr(nl1 + 1, nl2 - nl1 - 1));
    int end = std::stoi(input.substr(nl2 + 1));

    std::ifstream f(file);
    if (!f) return "Error: cannot open file";

    std::ostringstream out;
    std::string line;
    int num = 1;
    while (getline(f, line)) {
        if (num >= start && num <= end) {
            out << num << ": " << line << "\n";
        }
        if (num > end) break;
        num++;
    }
    return out.str();
}

std::string tool_find_callers(const std::string& input) {
    auto nl = input.find('\n');
    if (nl == std::string::npos) {
        return "Error: input format is 'function\\nroot_dir'";
    }

    std::string func = input.substr(0, nl);
    std::string root = input.substr(nl + 1);

    auto runner = [](const std::string& cmd) -> std::string {
        return agent::tool_run_command(cmd);
    };

    SimpleIndexer indexer(runner);
    auto refs = indexer.find_references(func + "(", root);

    if (refs.empty()) {
        return "No callers found for: " + func;
    }

    std::ostringstream out;
    for (const auto& ref : refs) {
        out << ref.file << ":" << ref.line << ": " << ref.context << "\n";
    }
    return out.str();
}

std::string tool_find_implementations(const std::string& input) {
    auto nl = input.find('\n');
    if (nl == std::string::npos) {
        return "Error: input format is 'class_name\\nroot_dir'";
    }

    std::string cls = input.substr(0, nl);
    std::string root = input.substr(nl + 1);

    auto runner = [](const std::string& cmd) -> std::string {
        return agent::tool_run_command(cmd);
    };

    SimpleIndexer indexer(runner);
    auto refs = indexer.find_references(": public " + cls, root);

    if (refs.empty()) {
        refs = indexer.find_references(": " + cls, root);
    }

    if (refs.empty()) {
        return "No implementations found for: " + cls;
    }

    std::ostringstream out;
    for (const auto& ref : refs) {
        out << ref.file << ":" << ref.line << ": " << ref.context << "\n";
    }
    return out.str();
}

void register_indexer_tools(agent::ToolRegistry& registry) {
    registry.register_tool("find_definition", tool_find_definition);
    registry.register_tool("find_references", tool_find_references);
    registry.register_tool("explain_code", tool_explain_code);
    registry.register_tool("find_callers", tool_find_callers);
    registry.register_tool("find_implementations", tool_find_implementations);
}

} // namespace indexer
