#ifndef WAVE_HPP
#define WAVE_HPP

#include <vector>
#include <cmath>
#include <algorithm>

/**
 * 2D wave equation on a uniform grid.
 *
 *     u_tt = c^2 (u_xx + u_yy)
 *
 * Second order in time, which is the structural difference from the heat and
 * reaction-diffusion solvers. Those are first order in time, so one grid holds
 * the whole state and a step maps u^n to u^(n+1). Here the state is a
 * displacement *and* a velocity, so the natural discretisation - a central
 * difference in time, the "leapfrog" scheme -
 *
 *     u^(n+1) = 2 u^n - u^(n-1) + nu^2 lap(u^n),      nu = c dt / dx
 *
 * reads two time levels back. Hence uPrev and u are both kept explicitly
 * rather than one grid plus a scratch buffer.
 *
 * The scheme is second-order accurate in both space and time and, unlike
 * forward Euler on the heat equation, it is non-dissipative: it conserves a
 * discrete energy rather than damping toward a steady state. A wave run has no
 * steady state to converge to, which is why nothing here reports a convergence
 * delta the way HeatSimulation::step does.
 *
 * Starting it takes one special step. Leapfrog needs u^(-1), which does not
 * exist, so the first step comes from a Taylor expansion about t = 0 using the
 * initial velocity (zero here - the membrane is released from rest):
 *
 *     u^1 = u^0 + dt v^0 + (1/2) nu^2 lap(u^0)
 *
 * The factor of 1/2 is load-bearing. Taking a full leapfrog step with
 * u^(-1) = u^0 instead injects twice the correct initial acceleration and
 * drops the scheme to first-order in time, which shows up as a steadily
 * accumulating phase error against the analytical standing mode.
 *
 * Stability is the CFL condition. In 2D the explicit scheme is stable only for
 *
 *     nu = c dt / dx <= 1 / sqrt(2)
 *
 * - stricter than the 1D limit of 1, because the five-point Laplacian's
 * largest eigenvalue grows with dimension. Above it the solution blows up
 * exponentially, so step() refuses to run rather than filling the database
 * with overflow.
 *
 * Boundaries:
 *
 *   Fixed  Dirichlet, amplitude pinned to zero - a clamped membrane, like a
 *          drumhead fastened at its rim. Waves reflect with inverted sign.
 *   Free   Neumann (zero normal slope), implemented by mirroring the
 *          neighbour across the edge exactly as ReactionDiffusion does for
 *          zero-flux. Waves reflect with the same sign.
 */
class WaveSimulation {
public:
    enum class Boundary { Fixed, Free };

    using Grid = std::vector<std::vector<double>>;

    WaveSimulation(int rows, int cols, double c, double dx, double dt)
        : rows(rows), cols(cols), c(c), dx(dx), dt(dt) {
        u.assign(rows, std::vector<double>(cols, 0.0));
        uPrev = u;
        uNext = u;
    }

    void setBoundaryCondition(Boundary b) { boundary = b; }

    /** Courant number nu = c dt / dx. */
    double courant() const { return (dx > 0.0) ? c * dt / dx : 0.0; }

    /** The 2D explicit limit, 1/sqrt(2) - not the 1D value of 1. */
    static double cflLimit() { return 1.0 / std::sqrt(2.0); }

    /**
     * Whether a step may be taken at all. The tiny slack lets a run sit
     * exactly on the limit, which is where a test that pins nu = 1/sqrt(2)
     * would otherwise fall foul of floating-point rounding.
     */
    bool cflSatisfied() const { return courant() <= cflLimit() * (1.0 + 1e-12); }

    /** Initial displacement of one cell. Fixed edges stay clamped regardless. */
    void setDisplacement(int i, int j, double a) {
        if (i < 0 || i >= rows || j < 0 || j >= cols) return;
        u[i][j] = a;
        uPrev[i][j] = a;          // released from rest: u^(-1) = u^0
    }

    /**
     * Displace a rectangle (inclusive) - the plucked-membrane initial
     * condition. Unlike a heat source this is not pinned: it is released at
     * t = 0 and the dynamics take over immediately.
     */
    void pluck(int i0, int j0, int i1, int j1, double amplitude) {
        for (int i = std::max(0, std::min(i0, i1)); i <= std::min(rows - 1, std::max(i0, i1)); ++i)
            for (int j = std::max(0, std::min(j0, j1)); j <= std::min(cols - 1, std::max(j0, j1)); ++j)
                setDisplacement(i, j, amplitude);
        clampFixedEdges();
    }

    /**
     * Smooth centred pluck. A hard-edged rectangle is rich in high spatial
     * frequencies, and those are exactly the modes the five-point Laplacian
     * resolves worst, so a raw rectangle disperses into grid-scale ringing
     * that looks like a solver bug. A raised cosine is band-limited enough to
     * propagate as a recognisable wave.
     *
     * @param radius  in cells; <= 0 scales with the domain.
     */
    void pluckSmooth(int ci, int cj, int radius, double amplitude) {
        if (radius <= 0) radius = std::max(2, std::min(rows, cols) / 8);
        for (int i = 0; i < rows; ++i) {
            for (int j = 0; j < cols; ++j) {
                const double r = std::hypot(static_cast<double>(i - ci),
                                            static_cast<double>(j - cj));
                if (r > radius) continue;
                const double w = 0.5 * (1.0 + std::cos(M_PI * r / radius));
                setDisplacement(i, j, amplitude * w);
            }
        }
        clampFixedEdges();
    }

    /** Single-cell impulse: the sharpest excitation the grid can represent. */
    void impulse(int i, int j, double amplitude) {
        setDisplacement(i, j, amplitude);
        clampFixedEdges();
    }

    /**
     * Advance one step. Returns false without touching the field if the CFL
     * condition is violated, so a caller that ignores the return value gets a
     * frozen field rather than an exponential blow-up.
     */
    bool step() {
        if (!cflSatisfied()) return false;

        const double nu2 = courant() * courant();
        // The first step has no u^(-1) to difference against; see the class
        // comment for why this is half the leapfrog coefficient.
        const double accel = started ? nu2 : 0.5 * nu2;

        for (int i = 0; i < rows; ++i) {
            for (int j = 0; j < cols; ++j) {
                if (boundary == Boundary::Fixed && onEdge(i, j)) {
                    uNext[i][j] = 0.0;
                    continue;
                }
                const double base = started ? (2.0 * u[i][j] - uPrev[i][j]) : u[i][j];
                uNext[i][j] = base + accel * lap(u, i, j);
            }
        }

        std::swap(uPrev, u);
        std::swap(u, uNext);
        started = true;
        return true;
    }

    const Grid& field() const { return u; }

    double maxAbsAmplitude() const {
        double m = 0.0;
        for (const auto& row : u)
            for (double v : row) m = std::max(m, std::abs(v));
        return m;
    }

private:
    bool onEdge(int i, int j) const {
        return i == 0 || i == rows - 1 || j == 0 || j == cols - 1;
    }

    void clampFixedEdges() {
        if (boundary != Boundary::Fixed) return;
        for (int i = 0; i < rows; ++i)
            for (int j = 0; j < cols; ++j)
                if (onEdge(i, j)) { u[i][j] = 0.0; uPrev[i][j] = 0.0; }
    }

    /**
     * Five-point Laplacian times dx^2 (the dx^2 is folded into nu^2 by the
     * caller). On a free edge the missing outward neighbour is mirrored from
     * just inside, which is the zero-normal-slope condition; on a fixed edge
     * this is never reached, since those cells are written directly.
     */
    double lap(const Grid& f, int i, int j) const {
        const int im = (i > 0) ? i - 1 : std::min(1, rows - 1);
        const int ip = (i < rows - 1) ? i + 1 : std::max(0, rows - 2);
        const int jm = (j > 0) ? j - 1 : std::min(1, cols - 1);
        const int jp = (j < cols - 1) ? j + 1 : std::max(0, cols - 2);
        return f[im][j] + f[ip][j] + f[i][jm] + f[i][jp] - 4.0 * f[i][j];
    }

    int rows, cols;
    double c, dx, dt;
    Boundary boundary = Boundary::Fixed;
    bool started = false;
    Grid u, uPrev, uNext;
};

#endif // WAVE_HPP
