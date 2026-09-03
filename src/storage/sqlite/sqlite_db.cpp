#include "storage/sqlite/sqlite_db.hpp"

#include <stdexcept>

namespace remin::storage {

namespace {
constexpr const char* kSchema = R"SQL(
CREATE TABLE IF NOT EXISTS workspaces (
    id TEXT PRIMARY KEY,
    name TEXT NOT NULL,
    working_directory TEXT NOT NULL DEFAULT '',
    created_at TEXT NOT NULL,
    last_saved_at TEXT NOT NULL,
    json TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS snapshots (
    id TEXT NOT NULL,
    workspace_id TEXT NOT NULL,
    timestamp TEXT NOT NULL,
    revision INTEGER NOT NULL,
    size_bytes INTEGER NOT NULL DEFAULT 0,
    state_json TEXT NOT NULL,
    PRIMARY KEY (workspace_id, id)
) WITHOUT ROWID;

CREATE TABLE IF NOT EXISTS scrollbacks (
    pane_id TEXT PRIMARY KEY,
    content TEXT NOT NULL,
    updated_at TEXT NOT NULL
);
)SQL";
}

SqliteDb::SqliteDb(std::string path) : path_(std::move(path)) {}

SqliteDb::~SqliteDb() {
    if (db_) sqlite3_close(db_);
}

bool SqliteDb::initialize() {
    std::lock_guard<std::recursive_mutex> lk(mtx_);
    const int rc = sqlite3_open(path_.c_str(), &db_);
    if (rc != SQLITE_OK) {
        err_ = db_ ? sqlite3_errmsg(db_) : "unknown sqlite error";
        if (db_) sqlite3_close(db_);
        db_ = nullptr;
        return false;
    }
    // WAL for crash-safety + better concurrency.
    sqlite3_exec(db_, "PRAGMA journal_mode=WAL; PRAGMA foreign_keys=ON;", nullptr, nullptr, nullptr);
    char* errmsg = nullptr;
    if (sqlite3_exec(db_, kSchema, nullptr, nullptr, &errmsg) != SQLITE_OK) {
        err_ = errmsg ? errmsg : "schema error";
        if (errmsg) sqlite3_free(errmsg);
        return false;
    }
    return true;
}

} // namespace remin::storage
