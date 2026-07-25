#pragma once

#include <string>

#include "core/storage/database.h"

// Forward-declared so sqlite3.h never leaks through this header: the prepared
// statement stays a private implementation detail, mirroring Database.
struct sqlite3_stmt;

namespace pokedex {

// STORAGE — RAII owner of a single prepared statement, the primitive every
// repository builds its queries on. Construction prepares the SQL; the
// destructor finalizes it. Parameters are bound (never string-concatenated), so
// user text can never be misread as SQL. Like Database, it owns a raw handle and
// is therefore non-copyable and non-movable.
//
// Bind indices are 1-based (SQLite's convention); column indices are 0-based.
class Statement {
public:
    // Prepare `sql` against `db`. Throws StorageError on a prepare failure.
    Statement(Database& db, const std::string& sql);
    ~Statement();

    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    Statement(Statement&&) = delete;
    Statement& operator=(Statement&&) = delete;

    void bindText(int index, const std::string& value);
    void bindInt(int index, int value);
    // 64-bit integer bind, used for money stored as integer minor units (cents) —
    // a card price in cents can exceed a 32-bit int for high-value cards.
    void bindInt64(int index, long long value);
    void bindNull(int index);

    // Advance to the next row. Returns true when a row is available
    // (SQLITE_ROW), false when the statement is done (SQLITE_DONE). Throws
    // StorageError on any other result.
    bool step();

    std::string columnText(int column);
    int columnInt(int column);
    long long columnInt64(int column);
    bool columnIsNull(int column);

private:
    sqlite3* db_;  // borrowed; owned by Database, used only for error messages
    sqlite3_stmt* stmt_ = nullptr;
};

}  // namespace pokedex
