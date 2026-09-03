#pragma once

#include "core/workspace_core.hpp"

#include <sqlite3.h>
#include <string>
#include <mutex>
#include <memory>
#include <optional>

namespace remin::storage {

// Thin RAII wrapper around a SQLite connection. Owns the sqlite3* handle.
// Thread-safe via an internal mutex (SQLite connection is not safe to share
// unguarded across threads).
class SqliteDb {
public:
    explicit SqliteDb(std::string path);
    ~SqliteDb();

    SqliteDb(const SqliteDb&) = delete;
    SqliteDb& operator=(const SqliteDb&) = delete;

    sqlite3* raw() { return db_; }
    std::recursive_mutex& mutex() { return mtx_; }

    // Open (create if missing) and run migrations. Returns false on error.
    bool initialize();
    std::string error_message() const { return err_; }

private:
    std::string path_;
    sqlite3* db_{nullptr};
    std::recursive_mutex mtx_;
    std::string err_;
};

} // namespace remin::storage
