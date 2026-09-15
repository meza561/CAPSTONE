#include <iostream>
#include <vector>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <string>
#include "database.hpp"

/**
 * 2D Heat Equation Simulation using Finite Difference Method (FDM).
 * Equation: dT/dt = alpha * (d2T/dx2 + d2T/dy2)
 */

class HeatSimulation {
public:
    HeatSimulation(int rows, int cols, double alpha, double dx, double dt) 
        : rows(rows), cols(cols), alpha(alpha), dx(dx), dt(dt) {
        grid.resize(rows, std::vector<double>(cols, 0.0));
        nextGrid.resize(rows, std::vector<double>(cols, 0.0));
    }

    void setBoundary(int r, int c, double temp) {
        if (r >= 0 && r < rows && c >= 0 && c < cols) {
            grid[r][c] = temp;
            nextGrid[r][c] = temp;
        }
    }

    void setInitialTemp(int r, int c, double temp) {
        if (r >= 0 && r < rows && c >= 0 && c < cols) {
            grid[r][c] = temp;
        }
    }

    double step() {
        double maxDiff = 0.0;
        double factor = alpha * dt / (dx * dx);

        for (int i = 1; i < rows - 1; ++i) {
            for (int j = 1; j < cols - 1; ++j) {
                nextGrid[i][j] = grid[i][j] + factor * (
                    grid[i+1][j] + grid[i-1][j] + 
                    grid[i][j+1] + grid[i][j-1] - 4 * grid[i][j]
                );
                maxDiff = std::max(maxDiff, std::abs(nextGrid[i][j] - grid[i][j]));
            }
        }
        grid = nextGrid;
        return maxDiff;
    }

    const std::vector<std::vector<double>>& getGrid() const { return grid; }
    int getRows() const { return rows; }
    int getCols() const { return cols; }

private:
    int rows, cols;
    double alpha, dx, dt;
    std::vector<std::vector<double>> grid;
    std::vector<std::vector<double>> nextGrid;
};

void exportToJSON(const std::string& filename, int step, int rows, int cols, const std::vector<std::vector<double>>& grid) {
    std::ofstream file(filename);
    file << "{\n";
    file << "  \"step\": " << step << ",\n";
    file << "  \"rows\": " << rows << ",\n";
    file << "  \"cols\": " << cols << ",\n";
    file << "  \"data\": [\n";
    for (int i = 0; i < rows; ++i) {
        file << "    [";
        for (int j = 0; j < cols; ++j) {
            file << std::fixed << std::setprecision(4) << grid[i][j] << (j == cols - 1 ? "" : ",");
        }
        file << "]" << (i == rows - 1 ? "" : ",\n");
    }
    file << "\n  ]\n}";
}

int main(int argc, char* argv[]) {
    // Default Parameters
    int ROWS = 20;
    int COLS = 20;
    double ALPHA = 0.01;
    double DX = 1.0;
    double DT = 0.1;
    int MAX_STEPS = 1000;
    double CONVERGENCE_THRESHOLD = 1e-4;
    double topTemp = 100.0;
    double bottomTemp = 0.0;
    double leftTemp = 0.0;
    double rightTemp = 0.0;

    // Simple CLI argument parsing for basic parameters
    if (argc >= 6) {
        ROWS = std::stoi(argv[1]);
        COLS = std::stoi(argv[2]);
        topTemp = std::stod(argv[3]);
        bottomTemp = std::stod(argv[4]);
        leftTemp = std::stod(argv[5]);
        if (argc >= 7) rightTemp = std::stod(argv[6]);
    }

    HeatSimulation sim(ROWS, COLS, ALPHA, DX, DT);
    HeatDatabase db("heat_sim.db");

    if (!db.init()) return 1;

    // Boundary Conditions
    for (int j = 0; j < COLS; ++j) sim.setBoundary(0, j, topTemp);
    for (int j = 0; j < COLS; ++j) sim.setBoundary(ROWS - 1, j, bottomTemp);
    for (int i = 0; i < ROWS; ++i) sim.setBoundary(i, 0, leftTemp);
    for (int i = 0; i < ROWS; ++i) sim.setBoundary(i, COLS - 1, rightTemp);

    std::cout << "Starting Simulation (" << ROWS << "x" << COLS << ")...\n";
    
    int finalStep = 0;
    for (int s = 0; s < MAX_STEPS; ++s) {
        double delta = sim.step();
        db.saveTimestep(s, sim.getGrid());
        finalStep = s;

        if (s % 100 == 0) {
            std::cout << "Step " << s << " | Max Delta T: " << delta << "\n";
        }

        if (delta < CONVERGENCE_THRESHOLD) {
            std::cout << "Converged at step " << s << "\n";
            break;
        }
    }

    exportToJSON("latest_heatmap.json", finalStep, ROWS, COLS, sim.getGrid());
    std::cout << "Simulation complete. Output written to latest_heatmap.json\n";

    return 0;
}
