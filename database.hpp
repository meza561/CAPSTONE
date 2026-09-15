#ifndef DATABASE_HPP
#define DATABASE_HPP

#include <sqlite3.h>
#include <string>
#include <vector>

struct HeatPoint {
    int x, y;
    double temp;
};

class HeatDatabase {
public:
    HeatDatabase(const std::string& dbName);
    ~HeatDatabase();

    bool init();
    bool saveTimestep(int step, const std::vector<std::vector<double>>& grid);
    
private:
    sqlite3* db;
    std::string dbName;
};

#endif
