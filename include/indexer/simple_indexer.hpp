#pragma once
#include "code_index.hpp"
#include <functional>

namespace indexer {

// Grep-based indexer - no external dependencies
class SimpleIndexer : public CodeIndex {
public:
    using CommandRunner = std::function<std::string(const std::string&)>;

    explicit SimpleIndexer(CommandRunner runner) : runner_(runner) {}

    SymbolInfo find_definition(const std::string& symbol,
                               const std::string& root_dir) override;

    std::vector<SymbolLocation> find_references(const std::string& symbol,
                                                const std::string& root_dir) override;

private:
    CommandRunner runner_;

    std::vector<SymbolLocation> parse_grep_output(const std::string& output);
};

} // namespace indexer
