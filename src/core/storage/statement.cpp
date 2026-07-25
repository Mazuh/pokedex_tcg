#include "core/storage/statement.h"

#include <sqlite3.h>

namespace pokedex {

Statement::Statement(Database& db, const std::string& sql) : db_(db.handle()) {
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt_, nullptr) != SQLITE_OK) {
        throw StorageError(sqlite3_errmsg(db_));
    }
}

Statement::~Statement() { sqlite3_finalize(stmt_); }

void Statement::bindText(int index, const std::string& value) {
    // SQLITE_TRANSIENT: SQLite copies the bytes, so `value` need not outlive the
    // bind — safe even for temporaries passed by the caller.
    if (sqlite3_bind_text(stmt_, index, value.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK) {
        throw StorageError(sqlite3_errmsg(db_));
    }
}

void Statement::bindInt(int index, int value) {
    if (sqlite3_bind_int(stmt_, index, value) != SQLITE_OK) {
        throw StorageError(sqlite3_errmsg(db_));
    }
}

void Statement::bindInt64(int index, long long value) {
    if (sqlite3_bind_int64(stmt_, index, value) != SQLITE_OK) {
        throw StorageError(sqlite3_errmsg(db_));
    }
}

void Statement::bindNull(int index) {
    if (sqlite3_bind_null(stmt_, index) != SQLITE_OK) {
        throw StorageError(sqlite3_errmsg(db_));
    }
}

bool Statement::step() {
    const int rc = sqlite3_step(stmt_);
    if (rc == SQLITE_ROW) {
        return true;
    }
    if (rc == SQLITE_DONE) {
        return false;
    }
    throw StorageError(sqlite3_errmsg(db_));
}

std::string Statement::columnText(int column) {
    // A NULL column reads back as a null pointer; treat it as the empty string so
    // callers that guard NULL with columnIsNull() stay in charge of the mapping.
    const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt_, column));
    return text != nullptr ? std::string(text) : std::string();
}

int Statement::columnInt(int column) { return sqlite3_column_int(stmt_, column); }

long long Statement::columnInt64(int column) { return sqlite3_column_int64(stmt_, column); }

bool Statement::columnIsNull(int column) {
    return sqlite3_column_type(stmt_, column) == SQLITE_NULL;
}

}  // namespace pokedex
