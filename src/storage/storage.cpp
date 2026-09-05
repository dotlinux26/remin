#include "storage/storage.hpp"
#include "core/serialization.hpp"

#include <sqlite3.h>
#include <memory>
#include <chrono>
#include <string>

namespace remin::storage {

namespace {
std::string now_iso() {
    return remin::core::to_iso8601(std::chrono::system_clock::now());
}
} // namespace

SqliteStorage::SqliteStorage(std::string db_path)
    : db_(std::make_unique<SqliteDb>(std::move(db_path))) {
    ok_ = db_->initialize();
    if (!ok_ && db_) err_ = db_->error_message();
}

SqliteStorage::~SqliteStorage() = default;

bool SqliteStorage::checkpoint(const remin::core::WorkspaceId& ws_id,
                               const remin::core::json& workspace_state,
                               int schema_version,
                               int64_t generation,
                               const std::string& reason,
                               const std::vector<std::pair<remin::core::PaneId, std::string>>& scrollbacks) {
    if (!ok_) return false;

    SqliteDb::Transaction tx = db_->begin_transaction();
    if (!tx.ok()) {
        err_ = db_->error_message();
        return false;
    }

    // 1. Write workspace JSON
    const auto j = workspace_state.dump();
    sqlite3_stmt* stmt = nullptr;
    const char* ws_sql = R"SQL(
        INSERT INTO workspaces (id, name, working_directory, created_at, last_saved_at, json)
        VALUES (?1, ?2, ?3, ?4, ?5, ?6)
        ON CONFLICT(id) DO UPDATE SET
            name = excluded.name,
            working_directory = excluded.working_directory,
            last_saved_at = excluded.last_saved_at,
            json = excluded.json;
    )SQL";
    if (sqlite3_prepare_v2(db_->raw(), ws_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        err_ = sqlite3_errmsg(db_->raw());
        tx.rollback();
        return false;
    }
    sqlite3_bind_text(stmt, 1, ws_id.str().c_str(), -1, SQLITE_TRANSIENT);
    const char* name = workspace_state.value("name", "").c_str();
    sqlite3_bind_text(stmt, 2, name, -1, SQLITE_TRANSIENT);
    const char* wd = workspace_state.value("working_directory", "").c_str();
    sqlite3_bind_text(stmt, 3, wd, -1, SQLITE_TRANSIENT);
    const char* created = workspace_state.value("created_at", "").c_str();
    sqlite3_bind_text(stmt, 4, created, -1, SQLITE_TRANSIENT);
    const auto la = now_iso();
    sqlite3_bind_text(stmt, 5, la.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, j.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        err_ = sqlite3_errmsg(db_->raw());
        sqlite3_finalize(stmt);
        tx.rollback();
        return false;
    }
    sqlite3_finalize(stmt);

    // 2. Write all scrollbacks
    for (const auto& [pane, content] : scrollbacks) {
        stmt = nullptr;
        const char* sb_sql = R"SQL(
            INSERT INTO scrollbacks (pane_id, content, updated_at)
            VALUES (?1, ?2, ?3)
            ON CONFLICT(pane_id) DO UPDATE SET content=excluded.content, updated_at=excluded.updated_at;
        )SQL";
        if (sqlite3_prepare_v2(db_->raw(), sb_sql, -1, &stmt, nullptr) != SQLITE_OK) {
            err_ = sqlite3_errmsg(db_->raw());
            tx.rollback();
            return false;
        }
        sqlite3_bind_text(stmt, 1, pane.str().c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, content.c_str(), -1, SQLITE_TRANSIENT);
        const auto ts = now_iso();
        sqlite3_bind_text(stmt, 3, ts.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) != SQLITE_DONE) {
            err_ = sqlite3_errmsg(db_->raw());
            sqlite3_finalize(stmt);
            tx.rollback();
            return false;
        }
        sqlite3_finalize(stmt);
    }

    // 3. Create snapshot row
    remin::core::SnapshotId snap_id = remin::core::SnapshotId::generate();
    const auto snap_ts = now_iso();
    const auto snap_state = workspace_state.dump();
    const auto snap_size = static_cast<sqlite3_int64>(snap_state.size());
    stmt = nullptr;
    const char* snap_sql = R"SQL(
        INSERT INTO snapshots (id, workspace_id, timestamp, revision, size_bytes, state_json, schema_version, generation, reason)
        VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9);
    )SQL";
    if (sqlite3_prepare_v2(db_->raw(), snap_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        err_ = sqlite3_errmsg(db_->raw());
        tx.rollback();
        return false;
    }
    sqlite3_bind_text(stmt, 1, snap_id.str().c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, ws_id.str().c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, snap_ts.c_str(), -1, SQLITE_TRANSIENT);
    // revision = count of existing snapshots + 1
    int revision = 1;
    {
        sqlite3_stmt* cnt = nullptr;
        sqlite3_prepare_v2(db_->raw(), "SELECT COUNT(*) FROM snapshots WHERE workspace_id=?1;", -1, &cnt, nullptr);
        sqlite3_bind_text(cnt, 1, ws_id.str().c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(cnt) == SQLITE_ROW) {
            revision = sqlite3_column_int(cnt, 0) + 1;
        }
        sqlite3_finalize(cnt);
    }
    sqlite3_bind_text(stmt, 1, snap_id.str().c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, ws_id.str().c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, snap_ts.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, revision);
    sqlite3_bind_int64(stmt, 5, static_cast<sqlite3_int64>(snap_size));
    sqlite3_bind_text(stmt, 6, snap_state.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 7, schema_version);
    sqlite3_bind_int64(stmt, 8, generation);
    sqlite3_bind_text(stmt, 9, reason.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        err_ = sqlite3_errmsg(db_->raw());
        sqlite3_finalize(stmt);
        tx.rollback();
        return false;
    }
    sqlite3_finalize(stmt);

    if (!tx.commit()) {
        err_ = db_->error_message();
        return false;
    }
    return true;
}

std::vector<remin::core::Workspace> SqliteStorage::list_workspaces() {
    std::vector<remin::core::Workspace> out;
    std::lock_guard<std::recursive_mutex> lk(db_->mutex());
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT json FROM workspaces ORDER BY created_at;";
    if (sqlite3_prepare_v2(db_->raw(), sql, -1, &stmt, nullptr) != SQLITE_OK) return out;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char* txt = sqlite3_column_text(stmt, 0);
        if (!txt) continue;
        remin::core::Workspace ws;
        try {
            ws = remin::core::json::parse(reinterpret_cast<const char*>(txt)).get<remin::core::Workspace>();
        } catch (...) {
            continue;
        }
        out.push_back(std::move(ws));
    }
    sqlite3_finalize(stmt);
    return out;
}

std::optional<remin::core::Workspace> SqliteStorage::load_workspace(const remin::core::WorkspaceId& id) {
    std::lock_guard<std::recursive_mutex> lk(db_->mutex());
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT json FROM workspaces WHERE id = ?1;";
    if (sqlite3_prepare_v2(db_->raw(), sql, -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;
    sqlite3_bind_text(stmt, 1, id.str().c_str(), -1, SQLITE_TRANSIENT);
    std::optional<remin::core::Workspace> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char* txt = sqlite3_column_text(stmt, 0);
        if (txt) {
            try {
                result = remin::core::json::parse(reinterpret_cast<const char*>(txt)).get<remin::core::Workspace>();
            } catch (...) {
                result = std::nullopt;
            }
        }
    }
    sqlite3_finalize(stmt);
    return result;
}

void SqliteStorage::save_workspace(const remin::core::Workspace& workspace) {
    std::lock_guard<std::recursive_mutex> lk(db_->mutex());
    const auto j = remin::core::json(workspace).dump();
    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"SQL(
        INSERT INTO workspaces (id, name, working_directory, created_at, last_saved_at, json)
        VALUES (?1, ?2, ?3, ?4, ?5, ?6)
        ON CONFLICT(id) DO UPDATE SET
            name = excluded.name,
            working_directory = excluded.working_directory,
            last_saved_at = excluded.last_saved_at,
            json = excluded.json;
    )SQL";
    if (sqlite3_prepare_v2(db_->raw(), sql, -1, &stmt, nullptr) != SQLITE_OK) return;
    sqlite3_bind_text(stmt, 1, workspace.id.str().c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, workspace.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, workspace.working_directory.c_str(), -1, SQLITE_TRANSIENT);
    const auto ca = remin::core::to_iso8601(workspace.created_at);
    const auto la = remin::core::to_iso8601(workspace.last_saved_at);
    sqlite3_bind_text(stmt, 4, ca.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, la.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, j.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void SqliteStorage::delete_workspace(const remin::core::WorkspaceId& id) {
    std::lock_guard<std::recursive_mutex> lk(db_->mutex());
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_->raw(), "DELETE FROM workspaces WHERE id = ?1;", -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, id.str().c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

std::vector<remin::core::Snapshot> SqliteStorage::list_snapshots(const remin::core::WorkspaceId& id) {
    std::vector<remin::core::Snapshot> out;
    std::lock_guard<std::recursive_mutex> lk(db_->mutex());
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_->raw(),
                       "SELECT id, timestamp, revision, size_bytes FROM snapshots WHERE workspace_id=?1 ORDER BY timestamp;",
                       -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, id.str().c_str(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        remin::core::Snapshot s;
        s.id = remin::core::SnapshotId{reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0))};
        s.revision = sqlite3_column_int(stmt, 2);
        s.size_bytes = static_cast<std::uint64_t>(sqlite3_column_int64(stmt, 3));
        out.push_back(std::move(s));
    }
    sqlite3_finalize(stmt);
    return out;
}

std::optional<remin::core::json> SqliteStorage::load_snapshot(const remin::core::WorkspaceId& id,
                                                              const remin::core::SnapshotId& snap) {
    std::lock_guard<std::recursive_mutex> lk(db_->mutex());
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_->raw(),
                       "SELECT state_json FROM snapshots WHERE workspace_id=?1 AND id=?2;",
                       -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, id.str().c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, snap.str().c_str(), -1, SQLITE_TRANSIENT);
    std::optional<remin::core::json> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char* txt = sqlite3_column_text(stmt, 0);
        if (txt) {
            try {
                result = remin::core::json::parse(reinterpret_cast<const char*>(txt));
            } catch (...) {
                result = std::nullopt;
            }
        }
    }
    sqlite3_finalize(stmt);
    return result;
}

void SqliteStorage::save_snapshot(const remin::core::WorkspaceId& id, const remin::core::Snapshot& snap,
                                  const remin::core::json& state) {
    std::lock_guard<std::recursive_mutex> lk(db_->mutex());
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_->raw(),
                       "INSERT INTO snapshots (id, workspace_id, timestamp, revision, size_bytes, state_json) VALUES (?1,?2,?3,?4,?5,?6);",
                       -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, snap.id.str().c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, id.str().c_str(), -1, SQLITE_TRANSIENT);
    const auto ts = remin::core::to_iso8601(snap.timestamp);
    sqlite3_bind_text(stmt, 3, ts.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, snap.revision);
    sqlite3_bind_int64(stmt, 5, static_cast<sqlite3_int64>(snap.size_bytes));
    const auto ser = state.dump();
    sqlite3_bind_text(stmt, 6, ser.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void SqliteStorage::delete_snapshot(const remin::core::WorkspaceId& id, const remin::core::SnapshotId& snap) {
    std::lock_guard<std::recursive_mutex> lk(db_->mutex());
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_->raw(), "DELETE FROM snapshots WHERE workspace_id=?1 AND id=?2;", -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, id.str().c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, snap.str().c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void SqliteStorage::store_scrollback(const remin::core::PaneId& pane, std::string content) {
    std::lock_guard<std::recursive_mutex> lk(db_->mutex());
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_->raw(),
                       "INSERT INTO scrollbacks (pane_id, content, updated_at) VALUES (?1,?2,?3) "
                       "ON CONFLICT(pane_id) DO UPDATE SET content=excluded.content, updated_at=excluded.updated_at;",
                       -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, pane.str().c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, content.c_str(), -1, SQLITE_TRANSIENT);
    const auto ts = now_iso();
    sqlite3_bind_text(stmt, 3, ts.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

std::string SqliteStorage::load_scrollback(const remin::core::PaneId& pane) {
    std::string out;
    std::lock_guard<std::recursive_mutex> lk(db_->mutex());
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_->raw(), "SELECT content FROM scrollbacks WHERE pane_id=?1;", -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, pane.str().c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char* txt = sqlite3_column_text(stmt, 0);
        if (txt) out.assign(reinterpret_cast<const char*>(txt));
    }
    sqlite3_finalize(stmt);
    return out;
}

} // namespace remin::storage
