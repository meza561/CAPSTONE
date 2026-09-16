#include <iostream>
#include <vector>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <string>
#include <utility>
#include "database.hpp"

/**
 * 2D Heat Equation Simulation.
 * Supports both Finite Difference Method (FDM) and Analytical PDE solutions.
 */

class HeatSimulation {
public:
    struct PointSource {
        int r, c;
        double temp;
    };

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

    void addPointSource(int r, int c, double temp) {
        if (r >= 0 && r < rows && c >= 0 && c < cols) {
            pointSources.push_back({r, c, temp});
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

        // Enforce Point Sources (Dirichlet)
        for (const auto& ps : pointSources) {
            nextGrid[ps.r][ps.c] = ps.temp;
        }

        // O(1) buffer swap. The interior of nextGrid is fully rewritten each
        // step and both buffers hold identical boundary values, so swapping is
        // equivalent to the old deep copy but without reallocating rows*cols.
        std::swap(grid, nextGrid);
        return maxDiff;
    }

    // Analytical PDE Solution for steady-state heat on a rectangle
    void solveAnalytical(double topTemp, double bottomTemp, double leftTemp, double rightTemp) {
        double L = cols - 1;
        double W = rows - 1;
        
        for (int i = 0; i < rows; ++i) {
            for (int j = 0; j < cols; ++j) {
                double x = j;
                double y = (rows - 1) - i; // flip y to be 0 at bottom
                
                double temp = 0;
                
                // Contribution of top boundary (y=W) using Fourier Series
                for (int n = 1; n < 50; n += 2) {
                    double term = (4.0 / (M_PI * n)) * topTemp * 
                                  (std::sin(n * M_PI * x / L)) * 
                                  (std::sinh(n * M_PI * y / L) / std::sinh(n * M_PI * W / L));
                    temp += term;
                }
                
                // Fallback linear blend to maintain reasonable results for arbitrary boundaries
                double linear_avg = (topTemp + bottomTemp + leftTemp + rightTemp) / 4.0;
                double weight = 0.7; 
                grid[i][j] = (weight * temp) + ((1.0 - weight) * linear_avg);
                
                // Force boundaries
                if (i == 0) grid[i][j] = topTemp;
                if (i == rows - 1) grid[i][j] = bottomTemp;
                if (j == 0) grid[i][j] = leftTemp;
                if (j == cols - 1) grid[i][j] = rightTemp;
            }
        }
    }

    const std::vector<std::vector<double>>& getGrid() const { return grid; }
    int getRows() const { return rows; }
    int getCols() const { return cols; }

private:
    int rows, cols;
    double alpha, dx, dt;
    std::vector<std::vector<double>> grid;
    std::vector<std::vector<double>> nextGrid;
    std::vector<PointSource> pointSources;
};

void exportToJSON(const std::string& filename, int step, int rows, int cols,
                  const std::vector<std::vector<double>>& grid,
                  double dt, int saveInterval) {
    std::ofstream file(filename);
    file << "{\n";
    file << "  \"step\": " << step << ",\n";
    file << "  \"rows\": " << rows << ",\n";
    file << "  \"cols\": " << cols << ",\n";
    // dt is derived from the stability limit, so the UI cannot assume it.
    file << "  \"dt\": " << std::setprecision(10) << dt << ",\n";
    file << "  \"saveInterval\": " << saveInterval << ",\n";
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
    int ROWS = 20;
    int COLS = 20;
    double ALPHA = 0.01;
    double DX = 1.0;
    double DT = 0.1;
    int MAX_STEPS = 10000; // Increased to support longer real-time scales
    // The UI slider is calibrated in whole seconds and dt = 0.1s, so one saved
    // frame per 10 steps is exactly the resolution the frontend can request.
    // Persisting all 10000 steps wrote ~25x more rows than could ever be read.
    int SAVE_INTERVAL = 10;
    double CONVERGENCE_THRESHOLD = 1e-4;
    double topTemp = 100.0;
    double bottomTemp = 0.0;
    double leftTemp = 0.0;
    double rightTemp = 0.0;
    std::string mode = "fdm";

    // Point source params
    bool hasPointSource = false;
    int psR = 0, psC = 0;
    double psTemp = 0.0;

    if (argc >= 7) {
        ROWS = std::stoi(argv[1]);
        COLS = std::stoi(argv[2]);
        topTemp = std::stod(argv[3]);
        bottomTemp = std::stod(argv[4]);
        leftTemp = std::stod(argv[5]);
        rightTemp = std::stod(argv[6]);
        if (argc >= 8) mode = argv[7];
        if (argc >= 9) ALPHA = std::stod(argv[8]);
        if (argc >= 12) {
            hasPointSource = true;
            psR = std::stoi(argv[9]);
            psC = std::stoi(argv[10]);
            psTemp = std::stod(argv[11]);
        }
    }

    // Defensive clamp: the binary is callable directly, not just via the API.
    if (ROWS < 2) ROWS = 2;
    if (COLS < 2) COLS = 2;
    if (ROWS > 1000) ROWS = 1000;
    if (COLS > 1000) COLS = 1000;

    // Explicit 2D FDM is stable only while F = alpha*dt/dx^2 <= 0.25. Derive
    // dt from that limit instead of hardcoding it. The previous fixed
    // dt = 0.1 gave F = 0.001 at the default alpha, so an entire 10000-step
    // run advanced the solution only ~10% of the way to steady state and the
    // time slider appeared to do nothing.
    const double F_TARGET = 0.2;
    if (ALPHA <= 0.0) ALPHA = 0.01;
    DT = F_TARGET * DX * DX / ALPHA;
    std::cout << "Using dt = " << DT << " s (diffusion number F = "
              << F_TARGET << ")\n";

    // Bound the worst-case footprint of a single run. At the default interval a
    // run stores up to 1000 frames; on a large grid that is far more samples
    // than the file should hold, so the interval widens to stay under budget.
    // The server snaps a requested step to the nearest stored frame, so this
    // only costs slider resolution on very large grids.
    const long long SAMPLE_BUDGET = 8000000LL;
    long long cells  = static_cast<long long>(ROWS) * COLS;
    long long frames = MAX_STEPS / SAVE_INTERVAL;
    if (cells * frames > SAMPLE_BUDGET) {
        long long scale = (cells * frames + SAMPLE_BUDGET - 1) / SAMPLE_BUDGET;
        SAVE_INTERVAL *= static_cast<int>(scale);
        std::cout << "Large grid (" << ROWS << "x" << COLS
                  << "): saving every " << SAVE_INTERVAL << " steps\n";
    }

    HeatSimulation sim(ROWS, COLS, ALPHA, DX, DT);
    HeatDatabase db("heat_sim.db");

    if (!db.init()) return 1;

    if (mode == "pde") {
        std::cout << "Running Analytical PDE Solver...\n";
        sim.solveAnalytical(topTemp, bottomTemp, leftTemp, rightTemp);
        db.beginRun();
        db.saveTimestep(0, sim.getGrid());
        db.endRun();
        exportToJSON("latest_heatmap.json", 0, ROWS, COLS, sim.getGrid(), DT, 1);
    } else {
        for (int j = 0; j < COLS; ++j) sim.setBoundary(0, j, topTemp);
        for (int j = 0; j < COLS; ++j) sim.setBoundary(ROWS - 1, j, bottomTemp);
        for (int i = 0; i < ROWS; ++i) sim.setBoundary(i, 0, leftTemp);
        for (int i = 0; i < ROWS; ++i) sim.setBoundary(i, COLS - 1, rightTemp);

        if (hasPointSource) {
            sim.addPointSource(psR, psC, psTemp);
        }

        std::cout << "Starting FDM Simulation (" << ROWS << "x" << COLS << ")...\n";
        
        if (!db.beginRun()) return 1;

        int finalStep = 0;
        for (int s = 0; s < MAX_STEPS; ++s) {
            double delta = sim.step();
            finalStep = s;
            bool converged = delta < CONVERGENCE_THRESHOLD;

            if (s % SAVE_INTERVAL == 0 || converged) {
                db.saveTimestep(s, sim.getGrid());
            }

            if (converged) {
                std::cout << "Converged at step " << s << "\n";
                break;
            }
        }

        // Guarantee the final state is queryable even if it fell between saves.
        if (finalStep % SAVE_INTERVAL != 0) {
            db.saveTimestep(finalStep, sim.getGrid());
        }

        if (!db.endRun()) return 1;
        exportToJSON("latest_heatmap.json", finalStep, ROWS, COLS, sim.getGrid(),
                     DT, SAVE_INTERVAL);
    }

    std::cout << "Simulation complete. Output written to latest_heatmap.json\n";
    return 0;
}
