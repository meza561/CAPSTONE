#include <iostream>
#include <vector>
#include <cmath>
#include <fstream>
#include <iomanip>
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
        grid[r][c] = temp;
        nextGrid[r][c] = temp;
    }

    void setInitialTemp(int r, int c, double temp) {
        grid[r][c] = temp;
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

private:
    int rows, cols;
    double alpha, dx, dt;
    std::vector<std::vector<double>> grid;
    std::vector<std::vector<double>> nextGrid;
};

void exportToCSV(int step, const std::vector<std::vector<double>>& grid) {
    std::ofstream file("heatmap_step_" + std::to_string(step) + ".csv");
    for (const auto& row : grid) {
        for (size_t i = 0; i < row.size(); ++i) {
            file << std::fixed << std::setprecision(4) << row[i] << (i == row.size() - 1 ? "" : ",");
        }
        file << "\n";
    }
}

int main() {
    const int ROWS = 20;
    const int COLS = 20;
    const double ALPHA = 0.01; // Thermal diffusivity
    const double DX = 1.0;     // Space step
    const double DT = 0.1;     // Time step
    const int MAX_STEPS = 1000;
    const double CONVERGENCE_THRESHOLD = 1e-4;

    HeatSimulation sim(ROWS, COLS, ALPHA, DX, DT);
    HeatDatabase db("heat_sim.db");

    if (!db.init()) return 1;

    // Boundary Conditions: Top edge hot, others cold
    for (int j = 0; j < COLS; ++j) sim.setBoundary(0, j, 100.0);
    for (int i = 1; i < ROWS; ++i) {
        sim.setBoundary(i, 0, 0.0);
        sim.setBoundary(i, COLS - 1, 0.0);
    }
    for (int j = 0; j < COLS; ++j) sim.setBoundary(ROWS - 1, j, 0.0);

    std::cout << "Starting Simulation...\n";
    std::cout << "Step\tMax Delta T\n";

    for (int s = 0; s < MAX_STEPS; ++s) {
        double delta = sim.step();
        db.saveTimestep(s, sim.getGrid());

        if (s % 100 == 0) {
            std::cout << s << "\t" << delta << "\n";
        }

        if (delta < CONVERGENCE_THRESHOLD) {
            std::cout << "Converged at step " << s << " (Delta T: " << delta << ")\n";
            break;
        }
    }

    exportToCSV(MAX_STEPS, sim.getGrid());
    std::cout << "Simulation complete. Data stored in heat_sim.db and final CSV generated.\n";

    return 0;
}
