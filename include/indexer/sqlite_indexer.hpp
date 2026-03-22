#pragma once
#ifdef HAVE_SQLITE3
#include "indexer/code_index.hpp"
#include <sqlite3.h>
#include <memory>

namespace indexer {

class SQLiteIndexer : public CodeIndex {
public:
    explicit SQLiteIndexer(const std::string& db_path);
    ~SQLiteIndexer() override;

    SymbolInfo find_definition(const std::string& symbol, const std::string& root_dir) override;
    std::vector<SymbolLocation> find_references(const std::string& symbol, const std::string& root_dir) override;

    void index_file(const std::string& file_path);
    void clear_index();

private:
    sqlite3* db_ = nullptr;
    void init_schema();
};

} // namespace indexer
#endif // HAVE_SQLITE3
