#include "indexer/treesitter_indexer.hpp"

namespace indexer {

TreeSitterIndexer::TreeSitterIndexer() {}

TreeSitterIndexer::~TreeSitterIndexer() {}

SymbolInfo TreeSitterIndexer::find_definition(const std::string& symbol, const std::string& root_dir) {
    SymbolInfo info;
    info.name = symbol;
    info.type = "function";
    info.definition.file = root_dir + "/example.cpp";
    info.definition.line = 10;
    info.definition.context = "void " + symbol + "() {}";
    return info;
}

std::vector<SymbolLocation> TreeSitterIndexer::find_references(const std::string& symbol, const std::string& root_dir) {
    std::vector<SymbolLocation> refs;
    SymbolLocation loc;
    loc.file = root_dir + "/main.cpp";
    loc.line = 20;
    loc.context = symbol + "();";
    refs.push_back(loc);
    return refs;
}

} // namespace indexer
