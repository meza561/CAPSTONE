#ifndef REACTION_HPP
#define REACTION_HPP

#include <vector>
#include <cmath>
#include <algorithm>
#include <cstdint>

/**
 * Reaction-diffusion on a 2D grid.
 *
 * Same spatial discretisation as the heat solver - a five-point Laplacian on a
 * uniform grid - with a reaction term added to the update rule. That single
 * addition turns a diffusion solver into a pattern-formation model, which is
 * the connection between heat conduction and morphogenesis.
 *
 *   Fisher-KPP   u_t = D grad^2 u + r u (1 - u)
 *                One species. Logistic growth plus diffusion produces a
 *                travelling front whose asymptotic speed is exactly
 *                c* = 2 sqrt(D r) - a closed form to validate against.
 *
 *   Gray-Scott   u_t = Du grad^2 u - u v^2 + f (1 - u)
 *                v_t = Dv grad^2 v + u v^2 - (f + k) v
 *                Two species. An autocatalytic reaction with unequal
 *                diffusivities: the Turing mechanism. Depending on (f, k) it
 *                settles into spots, stripes, mazes or travelling solitons.
 *
 * Boundaries are zero-flux (Neumann), implemented by mirroring the neighbour
 * across the edge. Dirichlet boundaries would pin the edges to a fixed value
 * and suppress the patterns near them; zero-flux means nothing leaves the
 * domain, which is the physically natural choice here.
 */
class ReactionDiffusion {
public:
    enum class Model { FisherKPP, GrayScott };

    using Grid = std::vector<std::vector<double>>;

    ReactionDiffusion(int rows, int cols, Model model)
        : rows(rows), cols(cols), model(model) {
        u.assign(rows, std::vector<double>(cols, 0.0));
        v.assign(rows, std::vector<double>(cols, 0.0));
        un = u;
        vn = v;
    }

    void setFisher(double D_, double r_) { D = D_; r = r_; }

    void setGrayScott(double Du_, double Dv_, double feed_, double kill_) {
        Du = Du_; Dv = Dv_; feed = feed_; kill = kill_;
    }

    void setDt(double dt_) { dt = dt_; }

    /**
     * Grid spacing. Defaults to 1, the normalisation Gray-Scott is usually
     * quoted in. It matters for Fisher-KPP: the front is only sqrt(D/r) wide,
     * so unless dx is well below that the front is thinner than a cell and
     * the discrete wave outruns the continuum speed.
     */
    void setSpacing(double dx_) { if (dx_ > 0.0) dx = dx_; }
    double spacing() const { return dx; }

    /** Left-edge strip of occupied habitat; the front then travels right. */
    void seedFisher(int stripWidth = 3) {
        for (auto& row : u) std::fill(row.begin(), row.end(), 0.0);
        for (int i = 0; i < rows; ++i)
            for (int j = 0; j < std::min(stripWidth, cols); ++j)
                u[i][j] = 1.0;
    }

    /**
     * Gray-Scott starts from the trivial state u = 1, v = 0, which is stable
     * until perturbed. Seed blobs of v and add a little noise so the
     * instability has something asymmetric to amplify.
     */
    void seedGrayScott(unsigned int seed = 1u, int blobs = 3, double noise = 0.02) {
        for (auto& row : u) std::fill(row.begin(), row.end(), 1.0);
        for (auto& row : v) std::fill(row.begin(), row.end(), 0.0);

        rng = seed ? seed : 1u;
        const int half = std::max(2, std::min(rows, cols) / 16);

        for (int b = 0; b < blobs; ++b) {
            const int ci = (blobs == 1) ? rows / 2
                                        : static_cast<int>(nextUnit() * (rows - 2 * half)) + half;
            const int cj = (blobs == 1) ? cols / 2
                                        : static_cast<int>(nextUnit() * (cols - 2 * half)) + half;
            for (int i = ci - half; i <= ci + half; ++i) {
                for (int j = cj - half; j <= cj + half; ++j) {
                    if (i < 0 || i >= rows || j < 0 || j >= cols) continue;
                    u[i][j] = 0.50;
                    v[i][j] = 0.25;
                }
            }
        }
        for (int i = 0; i < rows; ++i) {
            for (int j = 0; j < cols; ++j) {
                u[i][j] = clamp01(u[i][j] + noise * (nextUnit() - 0.5));
                v[i][j] = clamp01(v[i][j] + noise * (nextUnit() - 0.5));
            }
        }
    }

    /** One explicit step. Returns the largest change in the displayed field. */
    double step() {
        double maxDiff = 0.0;
        for (int i = 0; i < rows; ++i) {
            for (int j = 0; j < cols; ++j) {
                if (model == Model::FisherKPP) {
                    const double uu = u[i][j];
                    un[i][j] = uu + dt * (D * lap(u, i, j) / (dx * dx)
                                          + r * uu * (1.0 - uu));
                    maxDiff = std::max(maxDiff, std::abs(un[i][j] - uu));
                } else {
                    const double uu = u[i][j];
                    const double vv = v[i][j];
                    const double uvv = uu * vv * vv;
                    un[i][j] = uu + dt * (Du * lap(u, i, j) / (dx * dx)
                                          - uvv + feed * (1.0 - uu));
                    vn[i][j] = vv + dt * (Dv * lap(v, i, j) / (dx * dx)
                                          + uvv - (feed + kill) * vv);
                    maxDiff = std::max(maxDiff, std::abs(vn[i][j] - vv));
                }
            }
        }
        std::swap(u, un);
        if (model == Model::GrayScott) std::swap(v, vn);
        return maxDiff;
    }

    /**
     * The field shown to the user: the population for Fisher-KPP, and the
     * activator v for Gray-Scott, which is the species the patterns live in.
     */
    const Grid& field() const { return (model == Model::GrayScott) ? v : u; }
    const Grid& fieldU() const { return u; }

    /**
     * Physical x where the Fisher front crosses u = 1/2 along the middle row,
     * found by linear interpolation between the bracketing cells. Returns -1
     * if the front has left the domain or has not formed.
     */
    double frontPosition(double level = 0.5) const {
        const int i = rows / 2;
        for (int j = 0; j + 1 < cols; ++j) {
            const double a = u[i][j], b = u[i][j + 1];
            if ((a >= level && b < level)) {
                const double t = (a - level) / (a - b);
                return (j + t) * dx;
            }
        }
        return -1.0;
    }

    int getRows() const { return rows; }
    int getCols() const { return cols; }

private:
    static double clamp01(double x) { return x < 0.0 ? 0.0 : (x > 1.0 ? 1.0 : x); }

    /** Five-point Laplacian with zero-flux edges (neighbour mirrored inward). */
    double lap(const Grid& f, int i, int j) const {
        const int im = (i > 0) ? i - 1 : std::min(1, rows - 1);
        const int ip = (i < rows - 1) ? i + 1 : std::max(0, rows - 2);
        const int jm = (j > 0) ? j - 1 : std::min(1, cols - 1);
        const int jp = (j < cols - 1) ? j + 1 : std::max(0, cols - 2);
        return f[im][j] + f[ip][j] + f[i][jm] + f[i][jp] - 4.0 * f[i][j];
    }

    /** Small deterministic PRNG so a given seed always reproduces a run. */
    double nextUnit() {
        rng = rng * 1664525u + 1013904223u;
        return static_cast<double>((rng >> 8) & 0xFFFFFFu) / static_cast<double>(0x1000000u);
    }

    int rows, cols;
    Model model;
    double dt = 1.0;
    double dx = 1.0;
    double D = 0.2, r = 1.0;                       // Fisher-KPP
    double Du = 0.16, Dv = 0.08, feed = 0.035, kill = 0.065;  // Gray-Scott
    Grid u, v, un, vn;
    std::uint32_t rng = 1u;
};

#endif // REACTION_HPP
