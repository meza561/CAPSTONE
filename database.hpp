#ifndef DATABASE_HPP
#define DATABASE_HPP

#include <sqlite3.h>
#include <string>
#include <vector>

struct HeatPoint {
    int x, y;
    double temp;
};

/**
 * Persistence layer for simulation timesteps.
 *
 * Usage is scoped to a single run:
 *     db.init();
 *     db.beginRun();          // clears the previous run, opens one transaction
 *     db.saveTimestep(...);   // called many times
 *     db.endRun();            // commits once and compacts the file
 */
class HeatDatabase {
public:
    HeatDatabase(const std::string& dbName);
    ~HeatDatabase();

    bool init();
    bool beginRun();
    bool saveTimestep(int step, const std::vector<std::vector<double>>& grid);
    bool endRun();

private:
    bool exec(const char* sql);
    int pragmaInt(const char* pragma);

    sqlite3* db;
    sqlite3_stmt* insertStmt;
    std::string dbName;
    bool inTransaction;
};

#endif
