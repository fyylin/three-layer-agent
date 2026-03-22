#ifdef HAVE_SQLITE3
#include "indexer/sqlite_indexer.hpp"
#include <stdexcept>

namespace indexer {

SQLiteIndexer::SQLiteIndexer(const std::string& db_path) {
    if (sqlite3_open(db_path.c_str(), &db_) != SQLITE_OK) {
        throw std::runtime_error("Failed to open SQLite database");
    }
    init_schema();
}

SQLiteIndexer::~SQLiteIndexer() {
    if (db_) sqlite3_close(db_);
}

void SQLiteIndexer::init_schema() {
    const char* sql = "CREATE TABLE IF NOT EXISTS symbols ("
                      "name TEXT, type TEXT, file TEXT, line INTEGER, context TEXT);"
                      "CREATE INDEX IF NOT EXISTS idx_name ON symbols(name);";
    sqlite3_exec(db_, sql, nullptr, nullptr, nullptr);
}

SymbolInfo SQLiteIndexer::find_definition(const std::string& symbol, const std::string&) {
    SymbolInfo info;
    info.name = symbol;
    const char* sql = "SELECT file, line, context FROM symbols WHERE name=? LIMIT 1";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, symbol.c_str(), -1, SQLITE_STATIC);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            info.definition.file = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            info.definition.line = sqlite3_column_int(stmt, 1);
            info.definition.context = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        }
        sqlite3_finalize(stmt);
    }
    return info;
}

std::vector<SymbolLocation> SQLiteIndexer::find_references(const std::string& symbol, const std::string&) {
    std::vector<SymbolLocation> refs;
    const char* sql = "SELECT file, line, context FROM symbols WHERE name=?";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, symbol.c_str(), -1, SQLITE_STATIC);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            SymbolLocation loc;
            loc.file = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            loc.line = sqlite3_column_int(stmt, 1);
            loc.context = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            refs.push_back(loc);
        }
        sqlite3_finalize(stmt);
    }
    return refs;
}

void SQLiteIndexer::index_file(const std::string& file_path) {
    const char* sql = "INSERT INTO symbols VALUES (?, ?, ?, ?, ?)";
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, "example", -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, "function", -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, file_path.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 4, 1);
    sqlite3_bind_text(stmt, 5, "void example() {}", -1, SQLITE_STATIC);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void SQLiteIndexer::clear_index() {
    sqlite3_exec(db_, "DELETE FROM symbols", nullptr, nullptr, nullptr);
}

} // namespace indexer
#endif // HAVE_SQLITE3
