#ifndef SIMULATION_HPP
#define SIMULATION_HPP

#include <vector>
#include <cmath>
#include <algorithm>
#include <utility>

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

    // Stable evaluation of sinh(k*a)/sinh(k*b). Computing the two sinh values
    // separately overflows to inf/inf = NaN once k*b exceeds ~710, which caps
    // how many series terms can be used.
    static double sinhRatio(double k, double a, double b) {
        if (b <= 0.0) return 0.0;
        const double ka = k * a;
        const double kb = k * b;
        if (kb > 30.0) {
            return std::exp(ka - kb) * (1.0 - std::exp(-2.0 * ka)) /
                                       (1.0 - std::exp(-2.0 * kb));
        }
        return std::sinh(ka) / std::sinh(kb);
    }

    // Analytical steady-state (Laplace) solution on a rectangle.
    //
    // The solution for arbitrary Dirichlet data is the SUPERPOSITION of four
    // single-edge problems, one per boundary. The previous implementation
    // summed only the top-edge series and then blended it 70/30 with a flat
    // average of all four edge temperatures. That blend does not satisfy
    // Laplace's equation: it happened to land on the right answer for a single
    // hot edge (where the series and the average coincide) and was wrong
    // everywhere else - e.g. top=bottom=100 with cold sides gave 32.5 at the
    // centre instead of the exact 50.
    void solveAnalytical(double topTemp, double bottomTemp, double leftTemp, double rightTemp) {
        const double L = cols - 1;
        const double W = rows - 1;
        const int N_TERMS = 199;   // odd harmonics only

        for (int i = 0; i < rows; ++i) {
            for (int j = 0; j < cols; ++j) {
                const double x = j;
                const double y = (rows - 1) - i;   // y = 0 at the bottom row

                double temp = 0.0;
                for (int n = 1; n <= N_TERMS; n += 2) {
                    const double c = 4.0 / (M_PI * n);

                    // Top and bottom edges: expand along x, decay along y.
                    const double kx = n * M_PI / L;
                    const double sx = std::sin(kx * x);
                    temp += c * topTemp    * sx * sinhRatio(kx, y,     W);
                    temp += c * bottomTemp * sx * sinhRatio(kx, W - y, W);

                    // Left and right edges: expand along y, decay along x.
                    const double ky = n * M_PI / W;
                    const double sy = std::sin(ky * y);
                    temp += c * rightTemp * sy * sinhRatio(ky, x,     L);
                    temp += c * leftTemp  * sy * sinhRatio(ky, L - x, L);
                }

                grid[i][j] = temp;

                // Pin the boundaries: a truncated series rings (Gibbs) exactly
                // at the edges, and corners are genuinely discontinuous.
                if (i == 0)        grid[i][j] = topTemp;
                if (i == rows - 1) grid[i][j] = bottomTemp;
                if (j == 0)        grid[i][j] = leftTemp;
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

#endif // SIMULATION_HPP
