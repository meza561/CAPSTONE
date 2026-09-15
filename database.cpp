#include "database.hpp"
#include <iostream>

HeatDatabase::HeatDatabase(const std::string& dbName)
    : db(nullptr), insertStmt(nullptr), dbName(dbName), inTransaction(false) {}

HeatDatabase::~HeatDatabase() {
    if (inTransaction) {
        sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
    }
    if (insertStmt) sqlite3_finalize(insertStmt);
    if (db) sqlite3_close(db);
}

int HeatDatabase::pragmaInt(const char* pragma) {
    sqlite3_stmt* stmt = nullptr;
    int value = -1;
    if (sqlite3_prepare_v2(db, pragma, -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) value = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return value;
}

bool HeatDatabase::exec(const char* sql) {
    char* errMsg = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &errMsg) != SQLITE_OK) {
        std::cerr << "SQL error [" << sql << "]: "
                  << (errMsg ? errMsg : "unknown") << std::endl;
        sqlite3_free(errMsg);
        return false;
    }
    return true;
}

bool HeatDatabase::init() {
    if (sqlite3_open(dbName.c_str(), &db) != SQLITE_OK) {
        std::cerr << "Can't open database: " << sqlite3_errmsg(db) << std::endl;
        return false;
    }

    // MUST come first: switching the journal mode writes the database header,
    // and auto_vacuum can no longer be changed once that has happened. This is
    // what lets endRun() hand free pages back to the filesystem instead of
    // letting the file ratchet permanently upward.
    exec("PRAGMA auto_vacuum=INCREMENTAL;");

    // Write-ahead logging with relaxed (but still crash-safe) syncing. The
    // default rollback journal fsyncs on every commit, which dominated runtime
    // when each timestep was its own transaction.
    exec("PRAGMA journal_mode=WAL;");
    exec("PRAGMA synchronous=NORMAL;");
    exec("PRAGMA temp_store=MEMORY;");
    exec("PRAGMA cache_size=-65536;");   // 64 MB page cache

    // WITHOUT ROWID: (step, x, y) is the entire key, so there is no reason to
    // carry a separate rowid B-tree plus a unique index over the same columns.
    // This roughly halves the on-disk footprint per sample.
    const char* sql = "CREATE TABLE IF NOT EXISTS HeatMap ("
                      "step INTEGER NOT NULL, "
                      "x INTEGER NOT NULL, "
                      "y INTEGER NOT NULL, "
                      "temp REAL NOT NULL, "
                      "PRIMARY KEY (step, x, y)) WITHOUT ROWID;";
    if (!exec(sql)) return false;

    // Prepared once and reused for every sample of every timestep.
    const char* ins = "INSERT OR REPLACE INTO HeatMap (step, x, y, temp) VALUES (?, ?, ?, ?);";
    if (sqlite3_prepare_v2(db, ins, -1, &insertStmt, nullptr) != SQLITE_OK) {
        std::cerr << "Failed to prepare insert: " << sqlite3_errmsg(db) << std::endl;
        return false;
    }
    return true;
}

bool HeatDatabase::beginRun() {
    // Only the most recent run is ever served to the UI (the frontend keeps its
    // own run history in localStorage), so previous runs are dropped rather
    // than accumulated. Without this the table grows without bound, and
    // same-sized re-runs collide on the primary key.
    if (!exec("DELETE FROM HeatMap;")) return false;
    if (!exec("BEGIN TRANSACTION;")) return false;
    inTransaction = true;
    return true;
}

bool HeatDatabase::saveTimestep(int step, const std::vector<std::vector<double>>& grid) {
    if (!insertStmt) return false;

    for (size_t i = 0; i < grid.size(); ++i) {
        for (size_t j = 0; j < grid[i].size(); ++j) {
            sqlite3_bind_int(insertStmt, 1, step);
            sqlite3_bind_int(insertStmt, 2, static_cast<int>(i));
            sqlite3_bind_int(insertStmt, 3, static_cast<int>(j));
            sqlite3_bind_double(insertStmt, 4, grid[i][j]);

            if (sqlite3_step(insertStmt) != SQLITE_DONE) {
                std::cerr << "Insert failed at step " << step << " ("
                          << i << "," << j << "): "
                          << sqlite3_errmsg(db) << std::endl;
                sqlite3_reset(insertStmt);
                return false;
            }
            sqlite3_reset(insertStmt);
        }
    }
    return true;
}

bool HeatDatabase::endRun() {
    if (!inTransaction) return true;
    bool ok = exec("COMMIT;");
    inTransaction = false;

    // Release the pages freed by the DELETE in beginRun(). A no-op on a
    // database created before auto_vacuum was enabled.
    exec("PRAGMA incremental_vacuum;");

    // Fallback for such legacy files: once the file is mostly free space, a
    // full VACUUM is the only way to return it. Cheap, because it only runs
    // when the file is already known to be mostly empty.
    int freelist = pragmaInt("PRAGMA freelist_count;");
    int pages    = pragmaInt("PRAGMA page_count;");
    if (pages > 0 && freelist > pages / 2) {
        exec("VACUUM;");
    }

    // Fold the WAL back into the main file so the on-disk size reflects the
    // current run only.
    exec("PRAGMA wal_checkpoint(TRUNCATE);");
    return ok;
}
