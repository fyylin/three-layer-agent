#include "indexer/simple_indexer.hpp"
#include <sstream>
#include <algorithm>

namespace indexer {

SymbolInfo SimpleIndexer::find_definition(const std::string& symbol,
                                          const std::string& root_dir) {
    SymbolInfo info;
    info.name = symbol;

    // Search for function/class definitions
    // Pattern: "type symbol(" or "class symbol" or "struct symbol"
    std::string cmd = "cd \"" + root_dir + "\" && grep -rn --include=\"*.cpp\" --include=\"*.hpp\" --include=\"*.h\" "
                      "-E \"(class|struct|enum|void|int|bool|auto|std::)\\s+" + symbol + "\\s*[\\(\\{]\" . 2>/dev/null || true";

    std::string output = runner_(cmd);
    auto locations = parse_grep_output(output);

    if (!locations.empty()) {
        info.definition = locations[0];
        info.type = "function";
    }

    return info;
}

std::vector<SymbolLocation> SimpleIndexer::find_references(const std::string& symbol,
                                                           const std::string& root_dir) {
    // Find all occurrences of symbol
    std::string cmd = "cd \"" + root_dir + "\" && grep -rn --include=\"*.cpp\" --include=\"*.hpp\" --include=\"*.h\" "
                      "\"" + symbol + "\" . 2>/dev/null || true";

    std::string output = runner_(cmd);
    return parse_grep_output(output);
}

std::vector<SymbolLocation> SimpleIndexer::parse_grep_output(const std::string& output) {
    std::vector<SymbolLocation> locations;
    std::istringstream stream(output);
    std::string line;

    while (std::getline(stream, line)) {
        if (line.empty()) continue;

        // Parse: ./path/file.cpp:123:code context
        auto colon1 = line.find(':');
        if (colon1 == std::string::npos) continue;

        auto colon2 = line.find(':', colon1 + 1);
        if (colon2 == std::string::npos) continue;

        SymbolLocation loc;
        loc.file = line.substr(0, colon1);

        // Remove leading "./"
        if (loc.file.size() > 2 && loc.file.substr(0, 2) == "./")
            loc.file = loc.file.substr(2);

        try {
            loc.line = std::stoi(line.substr(colon1 + 1, colon2 - colon1 - 1));
        } catch (...) {
            continue;
        }

        loc.context = line.substr(colon2 + 1);
        // Trim leading whitespace
        loc.context.erase(0, loc.context.find_first_not_of(" \t"));

        locations.push_back(loc);
    }

    return locations;
}

} // namespace indexer
