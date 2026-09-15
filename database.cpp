#include "database.hpp"
#include <iostream>

HeatDatabase::HeatDatabase(const std::string& dbName) : dbName(dbName), db(nullptr) {}

HeatDatabase::~HeatDatabase() {
    if (db) sqlite3_close(db);
}

bool HeatDatabase::init() {
    if (sqlite3_open(dbName.c_str(), &db) != SQLITE_OK) {
        std::cerr << "Can't open database: " << sqlite3_errmsg(db) << std::endl;
        return false;
    }

    const char* sql = "CREATE TABLE IF NOT EXISTS HeatMap ("
                      "step INTEGER, "
                      "x INTEGER, "
                      "y INTEGER, "
                      "temp REAL, "
                      "PRIMARY KEY (step, x, y));";
    
    char* errMsg = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &errMsg) != SQLITE_OK) {
        std::cerr << "SQL error: " << errMsg << std::endl;
        sqlite3_free(errMsg);
        return false;
    }
    return true;
}

bool HeatDatabase::saveTimestep(int step, const std::vector<std::vector<double>>& grid) {
    sqlite3_exec(db, "BEGIN TRANSACTION", nullptr, nullptr, nullptr);
    
    const char* sql = "INSERT INTO HeatMap (step, x, y, temp) VALUES (?, ?, ?, ?);";
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);

    for (size_t i = 0; i < grid.size(); ++i) {
        for (size_t j = 0; j < grid[i].size(); ++j) {
            sqlite3_bind_int(stmt, 1, step);
            sqlite3_bind_int(stmt, 2, i);
            sqlite3_bind_int(stmt, 3, j);
            sqlite3_bind_double(stmt, 4, grid[i][j]);
            sqlite3_step(stmt);
            sqlite3_reset(stmt);
        }
    }

    sqlite3_finalize(stmt);
    sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr);
    return true;
}
