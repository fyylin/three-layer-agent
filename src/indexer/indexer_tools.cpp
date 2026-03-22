#include "indexer/indexer_tools.hpp"
#include "utils/utf8_fstream.hpp"
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

    agent::utf8_ifstream f(file);
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

std::string tool_analyze_crash(const std::string& input) {
    std::ostringstream out;
    std::istringstream in(input);
    std::string line;

    while (getline(in, line)) {
        size_t pos = line.find(".cpp:");
        if (pos == std::string::npos) pos = line.find(".hpp:");
        if (pos != std::string::npos) {
            size_t start = line.rfind('/', pos);
            if (start == std::string::npos) start = line.rfind('\\', pos);
            if (start != std::string::npos) {
                out << line.substr(start + 1) << "\n";
            }
        }
    }
    return out.str().empty() ? "No file locations found in stack trace" : out.str();
}

std::string tool_run_tests(const std::string& input) {
    return agent::tool_run_command("cd " + input + " && ctest --output-on-failure");
}

std::string tool_create_fix_branch(const std::string& input) {
    return agent::tool_run_command("git checkout -b " + input);
}

std::string tool_scan_complexity(const std::string& input) {
    auto result = agent::tool_run_command("cd " + input + " && grep -rn \"for\\|while\\|if\" --include=\"*.cpp\" | wc -l");
    return "Complex functions found: " + result;
}

std::string tool_find_duplicates(const std::string& input) {
    return agent::tool_run_command("cd " + input + " && grep -rn \"TODO\\|FIXME\" --include=\"*.cpp\" --include=\"*.hpp\"");
}

std::string tool_generate_report(const std::string& input) {
    std::ostringstream out;
    out << "=== Tech Debt Report ===\n";
    out << tool_scan_complexity(input) << "\n";
    out << tool_find_duplicates(input);
    return out.str();
}

void register_indexer_tools(agent::ToolRegistry& registry) {
    registry.register_tool("find_definition", tool_find_definition);
    registry.register_tool("find_references", tool_find_references);
    registry.register_tool("explain_code", tool_explain_code);
    registry.register_tool("find_callers", tool_find_callers);
    registry.register_tool("find_implementations", tool_find_implementations);
    registry.register_tool("analyze_crash", tool_analyze_crash);
    registry.register_tool("run_tests", tool_run_tests);
    registry.register_tool("create_fix_branch", tool_create_fix_branch);
    registry.register_tool("scan_complexity", tool_scan_complexity);
    registry.register_tool("find_duplicates", tool_find_duplicates);
    registry.register_tool("generate_report", tool_generate_report);
}

} // namespace indexer
