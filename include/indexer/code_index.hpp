#pragma once
// Simple code indexing interface - grep-based implementation
#include <string>
#include <vector>

namespace indexer {

struct SymbolLocation {
    std::string file;
    int line;
    std::string context;  // surrounding code
};

struct SymbolInfo {
    std::string name;
    std::string type;  // "function", "class", "variable"
    SymbolLocation definition;
    std::vector<SymbolLocation> references;
};

class CodeIndex {
public:
    virtual ~CodeIndex() = default;

    // Find where symbol is defined
    virtual SymbolInfo find_definition(const std::string& symbol,
                                       const std::string& root_dir) = 0;

    // Find all references to symbol
    virtual std::vector<SymbolLocation> find_references(const std::string& symbol,
                                                        const std::string& root_dir) = 0;
};

} // namespace indexer
