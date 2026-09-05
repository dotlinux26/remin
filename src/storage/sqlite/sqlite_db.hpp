#pragma once

#include "core/workspace_core.hpp"

#include <sqlite3.h>
#include <string>
#include <mutex>
#include <memory>
#include <optional>
#include <functional>

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

    // --- Transaction support (design §10.2) ---
    // RAII transaction: BEGIN IMMEDIATE on construction, COMMIT on explicit
    // commit() or ROLLBACK on destruction if not committed.
    class Transaction {
    public:
        explicit Transaction(SqliteDb& db) : db_(&db) {
            db_->mutex().lock();
            char* err = nullptr;
            int rc = sqlite3_exec(db_->raw(), "BEGIN IMMEDIATE TRANSACTION;", nullptr, nullptr, &err);
            if (rc != SQLITE_OK) {
                if (err) { db_->err_ = err; sqlite3_free(err); }
                ok_ = false;
            } else {
                ok_ = true;
            }
        }
        ~Transaction() {
            if (ok_ && !committed_) rollback();
            if (db_ && db_->raw()) db_->mutex().unlock();
        }
        Transaction(const Transaction&) = delete;
        Transaction& operator=(const Transaction&) = delete;
        Transaction(Transaction&& other) noexcept : db_(other.db_), ok_(other.ok_), committed_(other.committed_) {
            other.ok_ = false;
            other.db_ = nullptr;
        }
        Transaction& operator=(Transaction&& other) noexcept {
            if (this != &other) {
                if (ok_ && !committed_) rollback();
                if (db_ && db_->raw()) db_->mutex().unlock();
                db_ = other.db_;
                ok_ = other.ok_;
                committed_ = other.committed_;
                other.ok_ = false;
                other.db_ = nullptr;
            }
            return *this;
        }
        [[nodiscard]] bool ok() const { return ok_; }
        // Explicit commit. Returns true on success.
        bool commit() {
            if (!ok_ || committed_) return false;
            char* err = nullptr;
            int rc = sqlite3_exec(db_->raw(), "COMMIT;", nullptr, nullptr, &err);
            if (rc != SQLITE_OK) {
                if (err) { db_->err_ = err; sqlite3_free(err); }
                ok_ = false;
                return false;
            }
            committed_ = true;
            db_->mutex().unlock();
            return true;
        }
        // Explicit rollback.
        void rollback() {
            if (!ok_ || committed_) return;
            sqlite3_exec(db_->raw(), "ROLLBACK;", nullptr, nullptr, nullptr);
            ok_ = false;
            db_->mutex().unlock();
        }
    private:
        SqliteDb* db_ = nullptr;
        bool ok_ = false;
        bool committed_ = false;
    };

    // Begin a new IMMEDIATE transaction. Caller must check Transaction::ok().
    Transaction begin_transaction() { return Transaction(*this); }

private:
    std::string path_;
    sqlite3* db_{nullptr};
    std::recursive_mutex mtx_;
    std::string err_;
};

} // namespace remin::storage
