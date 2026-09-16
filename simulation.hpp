#ifndef SIMULATION_HPP
#define SIMULATION_HPP

#include <vector>
#include <cmath>
#include <algorithm>
#include <utility>

// Optional: the pragmas below are ignored when OpenMP is unavailable, which is
// the default for Apple's clang. Nothing here depends on it being present.
#ifdef _OPENMP
#include <omp.h>
#endif

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

    /**
     * Per-cell diffusivity. With a non-uniform alpha the governing equation is
     * the divergence form
     *
     *     du/dt = div( alpha(x,y) grad u )
     *
     * not alpha * lap(u): the latter is only equivalent when alpha is
     * constant, and gets the flux wrong across a material interface.
     * Conductivity at a face is the HARMONIC mean of the two adjacent cells,
     * which is the standard treatment for a discontinuous coefficient - it
     * reproduces series resistance exactly, where an arithmetic mean does not.
     */
    void setAlphaField(const std::vector<std::vector<double>>& f) {
        if (static_cast<int>(f.size()) != rows) return;
        alphaField = f;
        heterogeneous = true;
    }

    void setAlphaAt(int i, int j, double a) {
        if (i < 0 || i >= rows || j < 0 || j >= cols || a <= 0.0) return;
        ensureAlphaField();
        alphaField[i][j] = a;
        heterogeneous = true;
    }

    /** Fill an axis-aligned rectangle (inclusive) with a diffusivity. */
    void setAlphaRegion(int i0, int j0, int i1, int j1, double a) {
        if (a <= 0.0) return;
        ensureAlphaField();
        for (int i = std::max(0, std::min(i0, i1)); i <= std::min(rows - 1, std::max(i0, i1)); ++i)
            for (int j = std::max(0, std::min(j0, j1)); j <= std::min(cols - 1, std::max(j0, j1)); ++j)
                alphaField[i][j] = a;
        heterogeneous = true;
    }

    /**
     * Zero-flux (insulated) edges. An insulated edge is not pinned: its cells
     * are unknowns like any other, and the missing outward neighbour simply
     * contributes no flux. A Dirichlet edge stays pinned at whatever
     * setBoundary wrote there.
     */
    void setInsulatedEdges(bool top, bool bottom, bool left, bool right) {
        insTop = top; insBottom = bottom; insLeft = left; insRight = right;
        pinnedDirty = true;
    }

    bool anyInsulated() const { return insTop || insBottom || insLeft || insRight; }

    /** Sum over all cells - conserved exactly when every edge is insulated. */
    double totalEnergy() const {
        double sum = 0.0;
        for (const auto& row : grid)
            for (double vv : row) sum += vv;
        return sum;
    }

    /** Set a cell's value. On an insulated edge this is an initial condition
     *  rather than a boundary condition, since such cells are not pinned. */
    void setInitial(int r, int c, double value) { setBoundary(r, c, value); }

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
        if (pinnedDirty) rebuildPinned();
        const double coef = dt / (dx * dx);
        double maxDiff = 0.0;

        // Every cell is visited, not just the interior: an insulated edge cell
        // is an unknown too. A neighbour outside the grid contributes no flux,
        // which IS the zero-flux condition - nothing special-cased.
        //
        // Rows are independent: each writes only its own row of nextGrid and
        // reads only grid, so this parallelises without any synchronisation.
        #ifdef _OPENMP
        #pragma omp parallel for schedule(static) reduction(max:maxDiff)
        #endif
        for (int i = 0; i < rows; ++i) {
            for (int j = 0; j < cols; ++j) {
                if (pinned[i][j]) { nextGrid[i][j] = grid[i][j]; continue; }
                const double c = grid[i][j];
                double flux = 0.0;
                if (i > 0)        flux += faceAlpha(i, j, i - 1, j) * (grid[i - 1][j] - c);
                if (i < rows - 1) flux += faceAlpha(i, j, i + 1, j) * (grid[i + 1][j] - c);
                if (j > 0)        flux += faceAlpha(i, j, i, j - 1) * (grid[i][j - 1] - c);
                if (j < cols - 1) flux += faceAlpha(i, j, i, j + 1) * (grid[i][j + 1] - c);
                nextGrid[i][j] = c + coef * flux;
                maxDiff = std::max(maxDiff, std::abs(nextGrid[i][j] - c));
            }
        }
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
        if (!insTop)    for (int j = 0; j < cols; ++j) pinned[0][j] = 1;
        if (!insBottom) for (int j = 0; j < cols; ++j) pinned[rows - 1][j] = 1;
        if (!insLeft)   for (int i = 0; i < rows; ++i) pinned[i][0] = 1;
        if (!insRight)  for (int i = 0; i < rows; ++i) pinned[i][cols - 1] = 1;
        for (const auto& ps : pointSources) pinned[ps.r][ps.c] = 1;
        pinnedDirty = false;
    }

    void ensureAlphaField() {
        if (alphaField.empty())
            alphaField.assign(rows, std::vector<double>(cols, alpha));
    }

    /** Harmonic mean of the two cell diffusivities: the face conductivity. */
    double faceAlpha(int i1, int j1, int i2, int j2) const {
        if (!heterogeneous) return alpha;
        const double a = alphaField[i1][j1];
        const double b = alphaField[i2][j2];
        const double sum = a + b;
        return (sum > 0.0) ? (2.0 * a * b / sum) : 0.0;
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

        // alpha now lives in the face conductivities rather than in r, so the
        // tridiagonal coefficients vary from cell to cell.
        const double rr = dt / (dx * dx);
        const double rc = crankNicolson ? 0.5 * rr : rr;

        adiMid.assign(rows, std::vector<double>(cols, 0.0));
        adiOut.assign(rows, std::vector<double>(cols, 0.0));

        // Each grid line is an independent tridiagonal system, so the sweeps
        // parallelise cleanly. The Thomas scratch has to be per-thread, hence
        // the explicit parallel region rather than a bare parallel-for.
        #ifdef _OPENMP
        #pragma omp parallel
        #endif
        {
        std::vector<double> a(cols), b(cols), c(cols), d(cols), x(cols), cp(cols), dp(cols);

        // --- sweep 1: implicit along x, one system per row ---
        #ifdef _OPENMP
        #pragma omp for schedule(static)
        #endif
        for (int i = 0; i < rows; ++i) {
            for (int j = 0; j < cols; ++j) {
                if (pinned[i][j]) {
                    a[j] = 0.0; b[j] = 1.0; c[j] = 0.0;
                    d[j] = grid[i][j];
                    continue;
                }
                const double aW = (j > 0)        ? faceAlpha(i, j, i, j - 1) : 0.0;
                const double aE = (j < cols - 1) ? faceAlpha(i, j, i, j + 1) : 0.0;
                const double aN = (i > 0)        ? faceAlpha(i, j, i - 1, j) : 0.0;
                const double aS = (i < rows - 1) ? faceAlpha(i, j, i + 1, j) : 0.0;

                a[j] = -rc * aW;
                b[j] = 1.0 + rc * (aW + aE);
                c[j] = -rc * aE;

                double yFlux = 0.0;
                if (i > 0)        yFlux += aN * (grid[i - 1][j] - grid[i][j]);
                if (i < rows - 1) yFlux += aS * (grid[i + 1][j] - grid[i][j]);
                d[j] = grid[i][j] + rc * yFlux;
            }
            thomas(a, b, c, d, x, cp, dp);
            for (int j = 0; j < cols; ++j) adiMid[i][j] = x[j];
        }

        } // end parallel region for sweep 1

        #ifdef _OPENMP
        #pragma omp parallel
        #endif
        {
        std::vector<double> a(rows), b(rows), c(rows), d(rows), x(rows), cp(rows), dp(rows);

        // --- sweep 2: implicit along y, one system per column ---
        #ifdef _OPENMP
        #pragma omp for schedule(static)
        #endif
        for (int j = 0; j < cols; ++j) {
            for (int i = 0; i < rows; ++i) {
                if (pinned[i][j]) {
                    a[i] = 0.0; b[i] = 1.0; c[i] = 0.0;
                    d[i] = grid[i][j];
                    continue;
                }
                const double aW = (j > 0)        ? faceAlpha(i, j, i, j - 1) : 0.0;
                const double aE = (j < cols - 1) ? faceAlpha(i, j, i, j + 1) : 0.0;
                const double aN = (i > 0)        ? faceAlpha(i, j, i - 1, j) : 0.0;
                const double aS = (i < rows - 1) ? faceAlpha(i, j, i + 1, j) : 0.0;

                a[i] = -rc * aN;
                b[i] = 1.0 + rc * (aN + aS);
                c[i] = -rc * aS;

                if (crankNicolson) {
                    double xFlux = 0.0;
                    if (j > 0)        xFlux += aW * (adiMid[i][j - 1] - adiMid[i][j]);
                    if (j < cols - 1) xFlux += aE * (adiMid[i][j + 1] - adiMid[i][j]);
                    d[i] = adiMid[i][j] + rc * xFlux;
                } else {
                    double yFlux = 0.0;
                    if (i > 0)        yFlux += aN * (grid[i - 1][j] - grid[i][j]);
                    if (i < rows - 1) yFlux += aS * (grid[i + 1][j] - grid[i][j]);
                    d[i] = adiMid[i][j] - rc * yFlux;
                }
            }
            thomas(a, b, c, d, x, cp, dp);
            for (int i = 0; i < rows; ++i) adiOut[i][j] = x[i];
        }
        } // end parallel region for sweep 2

        double maxDiff = 0.0;
        #ifdef _OPENMP
        #pragma omp parallel for schedule(static) reduction(max:maxDiff)
        #endif
        for (int i = 0; i < rows; ++i) {
            for (int j = 0; j < cols; ++j) {
                maxDiff = std::max(maxDiff, std::abs(adiOut[i][j] - grid[i][j]));
            }
        }
        std::swap(grid, adiOut);
        return maxDiff;
    }

    int rows, cols;
    double alpha, dx, dt;
    Method method = Method::Explicit;
    bool heterogeneous = false;
    bool insTop = false, insBottom = false, insLeft = false, insRight = false;
    std::vector<std::vector<double>> alphaField;
    std::vector<std::vector<double>> grid;
    std::vector<std::vector<double>> nextGrid;
    std::vector<std::vector<double>> adiMid;
    std::vector<std::vector<double>> adiOut;
    std::vector<std::vector<char>> pinned;
    bool pinnedDirty = true;
    std::vector<PointSource> pointSources;
};

#endif // SIMULATION_HPP
