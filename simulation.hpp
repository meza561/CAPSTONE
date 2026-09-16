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
    /**
     * Time integration scheme.
     *
     *   Explicit       forward Euler. O(dt), and only stable for
     *                  F = alpha*dt/dx^2 <= 1/4.
     *   BackwardEuler  implicit, via Douglas-Rachford ADI. O(dt),
     *                  unconditionally stable.
     *   CrankNicolson  implicit, via Peaceman-Rachford ADI. O(dt^2),
     *                  unconditionally stable.
     *
     * Both implicit schemes split the 2D solve into two sets of tridiagonal
     * systems (one per grid line) solved with the Thomas algorithm, so a step
     * stays O(rows*cols) rather than requiring a full 2D matrix solve.
     */
    enum class Method { Explicit, BackwardEuler, CrankNicolson };

    struct PointSource {
        int r, c;
        double temp;
    };

    HeatSimulation(int rows, int cols, double alpha, double dx, double dt) 
        : rows(rows), cols(cols), alpha(alpha), dx(dx), dt(dt) {
        grid.resize(rows, std::vector<double>(cols, 0.0));
        nextGrid.resize(rows, std::vector<double>(cols, 0.0));
    }

    void setMethod(Method m) { method = m; }
    Method getMethod() const { return method; }

    void setBoundary(int r, int c, double temp) {
        if (r >= 0 && r < rows && c >= 0 && c < cols) {
            grid[r][c] = temp;
            nextGrid[r][c] = temp;
            pinnedDirty = true;
        }
    }

    void addPointSource(int r, int c, double temp) {
        if (r >= 0 && r < rows && c >= 0 && c < cols) {
            pointSources.push_back({r, c, temp});
            grid[r][c] = temp;
            pinnedDirty = true;
        }
    }

    /** Advance one step with the selected scheme; returns max |change|. */
    double step() {
        switch (method) {
            case Method::BackwardEuler: return stepADI(false);
            case Method::CrankNicolson: return stepADI(true);
            default:                    return stepExplicit();
        }
    }

    double stepExplicit() {
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
    // Cells whose value is imposed rather than solved for: the Dirichlet
    // boundary ring plus any interior heat source. In the tridiagonal systems
    // these become identity rows, which is what keeps a source pinned through
    // an implicit solve.
    void rebuildPinned() {
        pinned.assign(rows, std::vector<char>(cols, 0));
        for (int j = 0; j < cols; ++j) { pinned[0][j] = 1; pinned[rows - 1][j] = 1; }
        for (int i = 0; i < rows; ++i) { pinned[i][0] = 1; pinned[i][cols - 1] = 1; }
        for (const auto& ps : pointSources) pinned[ps.r][ps.c] = 1;
        pinnedDirty = false;
    }

    /** Thomas algorithm: O(n) solve of a tridiagonal system. */
    static void thomas(const std::vector<double>& a, const std::vector<double>& b,
                       const std::vector<double>& c, const std::vector<double>& d,
                       std::vector<double>& x,
                       std::vector<double>& cp, std::vector<double>& dp) {
        const int n = static_cast<int>(d.size());
        cp[0] = c[0] / b[0];
        dp[0] = d[0] / b[0];
        for (int i = 1; i < n; ++i) {
            const double m = b[i] - a[i] * cp[i - 1];
            cp[i] = c[i] / m;
            dp[i] = (d[i] - a[i] * dp[i - 1]) / m;
        }
        x[n - 1] = dp[n - 1];
        for (int i = n - 2; i >= 0; --i) x[i] = dp[i] - cp[i] * x[i + 1];
    }

    /**
     * One implicit step by alternating direction.
     *
     * Crank-Nicolson (Peaceman-Rachford), with rc = r/2:
     *     (I - rc*dxx) u*   = (I + rc*dyy) u^n
     *     (I - rc*dyy) u^n+1 = (I + rc*dxx) u*
     *
     * Backward Euler (Douglas-Rachford), with rc = r:
     *     (I - rc*dxx) u*   = (I + rc*dyy) u^n
     *     (I - rc*dyy) u^n+1 = u* - rc*dyy u^n
     *
     * Every line is swept, including the boundary lines: pinned cells solve as
     * identity rows, so their values carry through untouched.
     */
    double stepADI(bool crankNicolson) {
        if (pinnedDirty) rebuildPinned();

        const double r  = alpha * dt / (dx * dx);
        const double rc = crankNicolson ? 0.5 * r : r;
        const double diag = 1.0 + 2.0 * rc;

        adiMid.assign(rows, std::vector<double>(cols, 0.0));
        adiOut.assign(rows, std::vector<double>(cols, 0.0));

        const int nmax = std::max(rows, cols);
        std::vector<double> a(nmax), b(nmax), c(nmax), d(nmax), x(nmax), cp(nmax), dp(nmax);

        // --- sweep 1: implicit along x, one system per row ---
        a.resize(cols); b.resize(cols); c.resize(cols);
        d.resize(cols); x.resize(cols); cp.resize(cols); dp.resize(cols);
        for (int i = 0; i < rows; ++i) {
            for (int j = 0; j < cols; ++j) {
                if (pinned[i][j]) {
                    a[j] = 0.0; b[j] = 1.0; c[j] = 0.0;
                    d[j] = grid[i][j];
                } else {
                    a[j] = -rc; b[j] = diag; c[j] = -rc;
                    d[j] = grid[i][j]
                         + rc * (grid[i - 1][j] - 2.0 * grid[i][j] + grid[i + 1][j]);
                }
            }
            thomas(a, b, c, d, x, cp, dp);
            for (int j = 0; j < cols; ++j) adiMid[i][j] = x[j];
        }

        // --- sweep 2: implicit along y, one system per column ---
        a.resize(rows); b.resize(rows); c.resize(rows);
        d.resize(rows); x.resize(rows); cp.resize(rows); dp.resize(rows);
        for (int j = 0; j < cols; ++j) {
            for (int i = 0; i < rows; ++i) {
                if (pinned[i][j]) {
                    a[i] = 0.0; b[i] = 1.0; c[i] = 0.0;
                    d[i] = grid[i][j];
                } else {
                    a[i] = -rc; b[i] = diag; c[i] = -rc;
                    d[i] = crankNicolson
                        ? adiMid[i][j] + rc * (adiMid[i][j - 1] - 2.0 * adiMid[i][j]
                                               + adiMid[i][j + 1])
                        : adiMid[i][j] - rc * (grid[i - 1][j] - 2.0 * grid[i][j]
                                               + grid[i + 1][j]);
                }
            }
            thomas(a, b, c, d, x, cp, dp);
            for (int i = 0; i < rows; ++i) adiOut[i][j] = x[i];
        }

        double maxDiff = 0.0;
        for (int i = 1; i < rows - 1; ++i) {
            for (int j = 1; j < cols - 1; ++j) {
                maxDiff = std::max(maxDiff, std::abs(adiOut[i][j] - grid[i][j]));
            }
        }
        std::swap(grid, adiOut);
        return maxDiff;
    }

    int rows, cols;
    double alpha, dx, dt;
    Method method = Method::Explicit;
    std::vector<std::vector<double>> grid;
    std::vector<std::vector<double>> nextGrid;
    std::vector<std::vector<double>> adiMid;
    std::vector<std::vector<double>> adiOut;
    std::vector<std::vector<char>> pinned;
    bool pinnedDirty = true;
    std::vector<PointSource> pointSources;
};

#endif // SIMULATION_HPP
