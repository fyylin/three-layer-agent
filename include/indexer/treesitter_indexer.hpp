#pragma once
#include "indexer/code_index.hpp"

namespace indexer {

class TreeSitterIndexer : public CodeIndex {
public:
    TreeSitterIndexer();
    ~TreeSitterIndexer() override;

    SymbolInfo find_definition(const std::string& symbol, const std::string& root_dir) override;
    std::vector<SymbolLocation> find_references(const std::string& symbol, const std::string& root_dir) override;

private:
    void* parser_ = nullptr;
};

} // namespace indexer
