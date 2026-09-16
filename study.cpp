// Convergence and stability study for the 2D heat solver.
//
// Links the same HeatSimulation used by the web application, so what is
// measured here is exactly what the app runs.
//
// Studies performed:
//   1. Spatial convergence  - FDM steady state vs the exact analytical
//      solution under grid refinement. Expect O(dx^2).
//   2. Temporal convergence - transient solution at a fixed physical time vs
//      a fine-dt reference. Forward Euler is O(dt).
//   3. Stability            - sweep the diffusion number F across the
//      theoretical limit F <= 1/4 and watch max|T| diverge above it.
//   4. Sanity checks        - mean-value property, rotational symmetry and
//      the discrete maximum principle.
//
// Outputs study_results.json plus one CSV per study.

#include "simulation.hpp"

#include <iostream>
#include <fstream>
#include <iomanip>
#include <vector>
#include <string>
#include <sstream>
#include <cmath>
#include <algorithm>
#include <chrono>

using Grid = std::vector<std::vector<double>>;

namespace {

constexpr double ALPHA = 1.0;          // unit diffusivity; F carries the scaling
constexpr double F_STABLE_LIMIT = 0.25; // explicit 2D FDM limit

struct Norms { double l2; double linf; };

// RMS (grid-independent) L2 and max-norm over interior points only. The
// boundaries are imposed identically in both solutions, so including them
// would dilute the error with exact zeros.
Norms compareInterior(const Grid& a, const Grid& b) {
    const int rows = static_cast<int>(a.size());
    const int cols = static_cast<int>(a[0].size());
    double sum = 0.0, linf = 0.0;
    long long n = 0;
    for (int i = 1; i < rows - 1; ++i) {
        for (int j = 1; j < cols - 1; ++j) {
            const double d = std::fabs(a[i][j] - b[i][j]);
            sum += d * d;
            linf = std::max(linf, d);
            ++n;
        }
    }
    return { n ? std::sqrt(sum / static_cast<double>(n)) : 0.0, linf };
}

double maxAbs(const Grid& g) {
    double m = 0.0;
    for (const auto& row : g)
        for (double v : row)
            m = std::max(m, std::fabs(v));
    return m;
}

bool allFinite(const Grid& g) {
    for (const auto& row : g)
        for (double v : row)
            if (!std::isfinite(v)) return false;
    return true;
}

void applyBoundaries(HeatSimulation& sim, int N,
                     double top, double bottom, double left, double right) {
    for (int j = 0; j < N; ++j) sim.setBoundary(0, j, top);
    for (int j = 0; j < N; ++j) sim.setBoundary(N - 1, j, bottom);
    for (int i = 0; i < N; ++i) sim.setBoundary(i, 0, left);
    for (int i = 0; i < N; ++i) sim.setBoundary(i, N - 1, right);
}

struct SteadyResult { Grid grid; long long steps; bool converged; };

// Iterate the explicit scheme until the per-step change falls below tol.
SteadyResult runSteady(int N, double F,
                       double top, double bottom, double left, double right,
                       double tol, long long maxSteps,
                       HeatSimulation::Method m = HeatSimulation::Method::Explicit) {
    const double dx = 1.0 / (N - 1);
    const double dt = F * dx * dx / ALPHA;
    HeatSimulation sim(N, N, ALPHA, dx, dt);
    sim.setMethod(m);
    applyBoundaries(sim, N, top, bottom, left, right);

    long long s = 0;
    bool converged = false;
    for (; s < maxSteps; ++s) {
        if (sim.step() < tol) { ++s; converged = true; break; }
    }
    return { sim.getGrid(), s, converged };
}

// Advance exactly `steps` steps, i.e. to physical time steps*dt.
Grid runToTime(int N, double F, long long steps,
               double top, double bottom, double left, double right,
               HeatSimulation::Method m = HeatSimulation::Method::Explicit) {
    const double dx = 1.0 / (N - 1);
    const double dt = F * dx * dx / ALPHA;
    HeatSimulation sim(N, N, ALPHA, dx, dt);
    sim.setMethod(m);
    applyBoundaries(sim, N, top, bottom, left, right);
    for (long long s = 0; s < steps; ++s) sim.step();
    return sim.getGrid();
}

// A manufactured solution that is smooth everywhere, including the corners:
//     u(x,y) = A sin(pi x / L) sinh(pi y / L) / sinh(pi W / L)
// This is harmonic (it is the n=1 term of the Fourier series), vanishes on the
// left, right and bottom edges, and equals A sin(pi x / L) on the top - which
// goes to zero at both top corners, so the boundary data is continuous.
Grid exactSmooth(int N, double A) {
    Grid g(N, std::vector<double>(N, 0.0));
    const double L = N - 1, W = N - 1;
    const double k = M_PI / L;
    for (int i = 0; i < N; ++i) {
        for (int j = 0; j < N; ++j) {
            const double x = j;
            const double y = (N - 1) - i;
            g[i][j] = A * std::sin(k * x) * HeatSimulation::sinhRatio(k, y, W);
        }
    }
    return g;
}

// FDM steady state for that same manufactured boundary data.
SteadyResult runSteadySmooth(int N, double F, double A, double tol, long long maxSteps) {
    const double dx = 1.0 / (N - 1);
    const double dt = F * dx * dx / ALPHA;
    const double L = N - 1;
    HeatSimulation sim(N, N, ALPHA, dx, dt);
    for (int j = 0; j < N; ++j) sim.setBoundary(N - 1, j, 0.0);
    for (int i = 0; i < N; ++i) sim.setBoundary(i, 0, 0.0);
    for (int i = 0; i < N; ++i) sim.setBoundary(i, N - 1, 0.0);
    for (int j = 0; j < N; ++j) sim.setBoundary(0, j, A * std::sin(M_PI * j / L));

    long long s = 0;
    bool converged = false;
    for (; s < maxSteps; ++s) {
        if (sim.step() < tol) { ++s; converged = true; break; }
    }
    return { sim.getGrid(), s, converged };
}

Grid analytical(int N, double top, double bottom, double left, double right) {
    HeatSimulation sim(N, N, ALPHA, 1.0, 0.1);
    sim.solveAnalytical(top, bottom, left, right);
    return sim.getGrid();
}

// Least-squares slope of log(y) against log(x): the observed order.
double fitOrder(const std::vector<double>& xs, const std::vector<double>& ys) {
    const size_t n = xs.size();
    if (n < 2) return 0.0;
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    for (size_t i = 0; i < n; ++i) {
        const double lx = std::log(xs[i]);
        const double ly = std::log(ys[i]);
        sx += lx; sy += ly; sxx += lx * lx; sxy += lx * ly;
    }
    const double d = n * sxx - sx * sx;
    return (d == 0.0) ? 0.0 : (n * sxy - sx * sy) / d;
}

std::string num(double v) {
    if (!std::isfinite(v)) return "null";
    std::ostringstream os;
    os << std::setprecision(10) << v;
    return os.str();
}

} // namespace

int main() {
    std::ofstream json("study_results.json");
    json << "{\n";

    // ---------------------------------------------------------------- 1. space
    // Two cases are run. The manufactured problem has boundary data that is
    // continuous at the corners, so the exact solution is smooth and the
    // scheme can exhibit its true second-order accuracy. The constant-edge
    // problem is discontinuous at the two top corners; the exact solution is
    // singular there, which caps the attainable order no matter how fine the
    // grid. Reporting both is the honest result and explains the difference.
    struct SpatialCase { const char* key; const char* label; bool smooth; };
    const std::vector<SpatialCase> cases = {
        {"smooth", "manufactured: top = 100 sin(pi x/L), smooth at corners", true},
        {"discontinuous", "constant: top = 100, sides 0 (corner singularities)", false}
    };
    const std::vector<int> grids = {11, 21, 41, 81, 161};

    std::ofstream csvS("study_spatial.csv");
    csvS << "case,N,dx,steps,converged,l2,linf\n";
    json << "  \"spatial\": {\n    \"F\": 0.2, \"grids\": [";
    for (size_t i = 0; i < grids.size(); ++i)
        json << grids[i] << (i + 1 < grids.size() ? "," : "");
    json << "],\n    \"cases\": {\n";

    for (size_t ci = 0; ci < cases.size(); ++ci) {
        const SpatialCase& C = cases[ci];
        std::cout << "\n=== 1" << static_cast<char>('a' + ci)
                  << ". SPATIAL CONVERGENCE - " << C.label << " ===\n\n";
        std::cout << "     N        dx        steps          L2 err       Linf err   order(L2)\n";

        std::vector<double> dxs, l2s, linfs;
        json << "      \"" << C.key << "\": {\"label\": \"" << C.label << "\", \"points\": [\n";

        for (size_t k = 0; k < grids.size(); ++k) {
            const int N = grids[k];
            const double dx = 1.0 / (N - 1);
            const SteadyResult r = C.smooth
                ? runSteadySmooth(N, 0.2, 100.0, 1e-10, 5000000LL)
                : runSteady(N, 0.2, 100, 0, 0, 0, 1e-10, 5000000LL);
            const Grid exact = C.smooth ? exactSmooth(N, 100.0)
                                        : analytical(N, 100, 0, 0, 0);
            const Norms e = compareInterior(r.grid, exact);

            dxs.push_back(dx); l2s.push_back(e.l2); linfs.push_back(e.linf);
            double order = std::nan("");
            if (k > 0) order = std::log(l2s[k-1] / l2s[k]) / std::log(dxs[k-1] / dxs[k]);

            std::cout << std::setw(6) << N << std::setw(10) << std::fixed
                      << std::setprecision(5) << dx
                      << std::setw(13) << r.steps
                      << std::scientific << std::setprecision(4)
                      << std::setw(16) << e.l2 << std::setw(15) << e.linf;
            if (k > 0) std::cout << std::fixed << std::setprecision(3) << std::setw(12) << order;
            else       std::cout << std::setw(12) << "-";
            if (!r.converged) std::cout << "  (NOT CONVERGED)";
            std::cout << "\n";

            csvS << C.key << "," << N << "," << dx << "," << r.steps << ","
                 << (r.converged ? 1 : 0) << "," << e.l2 << "," << e.linf << "\n";
            json << "        {\"N\": " << N << ", \"dx\": " << num(dx)
                 << ", \"steps\": " << r.steps
                 << ", \"l2\": " << num(e.l2) << ", \"linf\": " << num(e.linf) << "}"
                 << (k + 1 < grids.size() ? "," : "") << "\n";
        }

        const double oL2 = fitOrder(dxs, l2s);
        const double oLinf = fitOrder(dxs, linfs);
        std::cout << "\n  fitted order: L2 = " << std::fixed << std::setprecision(3) << oL2
                  << " , Linf = " << oLinf << "   (theory: 2 for smooth data)\n";
        json << "        ], \"fitted_order_l2\": " << num(oL2)
             << ", \"fitted_order_linf\": " << num(oLinf) << "}"
             << (ci + 1 < cases.size() ? "," : "") << "\n";
    }
    json << "    },\n    \"theoretical_order\": 2\n  },\n";

    // ----------------------------------------------------------------- 2. time
    // Temporal order for each scheme. Forward and backward Euler are both
    // O(dt); Crank-Nicolson is O(dt^2). Every scheme is measured against a
    // reference computed with ITS OWN discretisation at a 256x smaller step,
    // so what is isolated is each scheme's own time-stepping error.
    std::cout << "\n=== 2. TEMPORAL CONVERGENCE (fixed t*, vs each scheme's fine-dt reference) ===\n";
    const int Nt = 41;
    const double dxt = 1.0 / (Nt - 1);
    const double F0 = 0.2;
    const long long steps0 = 400;
    const double dt0 = F0 * dxt * dxt / ALPHA;
    const double tStar = steps0 * dt0;
    std::cout << "grid " << Nt << "x" << Nt << " ; t* = " << std::scientific
              << std::setprecision(4) << tStar << " s\n";

    struct SchemeCase {
        const char* key; const char* label;
        HeatSimulation::Method m; int theory;
    };
    const std::vector<SchemeCase> schemes = {
        {"explicit",       "explicit (forward Euler)", HeatSimulation::Method::Explicit,      1},
        {"backward_euler", "backward Euler (ADI)",     HeatSimulation::Method::BackwardEuler, 1},
        {"crank_nicolson", "Crank-Nicolson (ADI)",     HeatSimulation::Method::CrankNicolson, 2},
    };

    std::ofstream csvT("study_temporal.csv");
    csvT << "scheme,F,dt,steps,l2,linf\n";
    json << "  \"temporal\": {\n    \"N\": " << Nt << ", \"t_star\": " << num(tStar)
         << ", \"reference_F\": " << num(F0 / 256.0) << ",\n    \"schemes\": {\n";

    const int LEVELS = 5;
    for (size_t si = 0; si < schemes.size(); ++si) {
        const SchemeCase& S = schemes[si];
        std::cout << "\n  " << S.label << "\n";
        std::cout << "        F              dt        steps          L2 err       Linf err   order(L2)\n";

        const Grid ref = runToTime(Nt, F0 / 256.0, steps0 * 256, 100, 0, 0, 0, S.m);
        std::vector<double> dts, tl2s;
        json << "      \"" << S.key << "\": {\"label\": \"" << S.label
             << "\", \"theoretical_order\": " << S.theory << ", \"points\": [\n";

        for (int k = 0; k < LEVELS; ++k) {
            const double F = F0 / std::pow(2.0, k);
            const long long steps = steps0 * (1LL << k);
            const double dt = F * dxt * dxt / ALPHA;
            const Grid g = runToTime(Nt, F, steps, 100, 0, 0, 0, S.m);
            const Norms e = compareInterior(g, ref);

            dts.push_back(dt); tl2s.push_back(e.l2);
            double order = std::nan("");
            if (k > 0) order = std::log(tl2s[k-1] / tl2s[k]) / std::log(dts[k-1] / dts[k]);

            std::cout << std::scientific << std::setprecision(4)
                      << std::setw(10) << F << std::setw(16) << dt
                      << std::setw(13) << steps
                      << std::setw(16) << e.l2 << std::setw(15) << e.linf;
            if (k > 0) std::cout << std::fixed << std::setprecision(3) << std::setw(12) << order;
            else       std::cout << std::setw(12) << "-";
            std::cout << "\n";

            csvT << S.key << "," << F << "," << dt << "," << steps << ","
                 << e.l2 << "," << e.linf << "\n";
            json << "        {\"F\": " << num(F) << ", \"dt\": " << num(dt)
                 << ", \"steps\": " << steps
                 << ", \"l2\": " << num(e.l2) << ", \"linf\": " << num(e.linf) << "}"
                 << (k + 1 < LEVELS ? "," : "") << "\n";
        }
        const double o = fitOrder(dts, tl2s);
        std::cout << "    fitted order = " << std::fixed << std::setprecision(3) << o
                  << "   (theory: " << S.theory << ")\n";
        json << "        ], \"fitted_order_l2\": " << num(o) << "}"
             << (si + 1 < schemes.size() ? "," : "") << "\n";
    }
    json << "    }\n  },\n";

    // ------------------------------------------------------------- 2b. cost
    // Accuracy is only half the argument: the implicit schemes cost more per
    // step but are not bound by F <= 1/4, so they can take far fewer of them.
    std::cout << "\n=== 2b. COST TO STEADY STATE (41x41, exact centre = 25) ===\n\n";
    std::cout << "  scheme                          F     steps     seconds     centre      error\n";
    const Grid exactCost = analytical(Nt, 100, 0, 0, 0);
    std::ofstream csvC("study_cost.csv");
    csvC << "scheme,F,steps,seconds,centre,error\n";
    json << "  \"cost\": {\"N\": " << Nt << ", \"exact_centre\": 25.0, \"runs\": [\n";

    struct CostCase { const char* key; const char* label; HeatSimulation::Method m; double F; };
    const std::vector<CostCase> costCases = {
        {"explicit",       "explicit (forward Euler)", HeatSimulation::Method::Explicit,      0.2},
        {"backward_euler", "backward Euler (ADI)",     HeatSimulation::Method::BackwardEuler, 5.0},
        {"crank_nicolson", "Crank-Nicolson (ADI)",     HeatSimulation::Method::CrankNicolson, 5.0},
        {"crank_nicolson_f50", "Crank-Nicolson, F=50", HeatSimulation::Method::CrankNicolson, 50.0},
    };
    for (size_t ci = 0; ci < costCases.size(); ++ci) {
        const CostCase& C = costCases[ci];
        const auto t0 = std::chrono::steady_clock::now();
        const SteadyResult r = runSteady(Nt, C.F, 100, 0, 0, 0, 1e-8, 2000000LL, C.m);
        const auto t1 = std::chrono::steady_clock::now();
        const double secs = std::chrono::duration<double>(t1 - t0).count();
        const double centre = r.grid[Nt/2][Nt/2];
        const double err = std::fabs(centre - exactCost[Nt/2][Nt/2]);

        std::cout << "  " << std::left << std::setw(28) << C.label << std::right
                  << std::fixed << std::setprecision(1) << std::setw(7) << C.F
                  << std::setw(10) << r.steps
                  << std::setprecision(3) << std::setw(12) << secs
                  << std::setprecision(4) << std::setw(11) << centre
                  << std::scientific << std::setprecision(2) << std::setw(11) << err << "\n";

        csvC << C.key << "," << C.F << "," << r.steps << "," << secs << ","
             << centre << "," << err << "\n";
        json << "      {\"key\": \"" << C.key << "\", \"label\": \"" << C.label
             << "\", \"F\": " << num(C.F) << ", \"steps\": " << r.steps
             << ", \"seconds\": " << num(secs) << ", \"centre\": " << num(centre)
             << ", \"error\": " << num(err) << "}"
             << (ci + 1 < costCases.size() ? "," : "") << "\n";
    }
    json << "    ]\n  },\n";

    // ------------------------------------------------------------ 3. stability
    std::cout << "\n=== 3. STABILITY SWEEP (theoretical limit F <= 0.25) ===\n\n";
    std::cout << "        F     steps run        max|T| at end     verdict\n";

    const std::vector<double> Fs = {0.05, 0.15, 0.20, 0.25, 0.26, 0.30, 0.50};
    const int Ns = 21;
    const long long stabSteps = 400;
    std::ofstream csvB("study_stability.csv");
    csvB << "F,step,max_abs_T\n";
    json << "  \"stability\": {\n    \"N\": " << Ns << ", \"limit\": " << F_STABLE_LIMIT
         << ", \"steps\": " << stabSteps << ",\n    \"runs\": [\n";

    for (size_t k = 0; k < Fs.size(); ++k) {
        const double F = Fs[k];
        const double dx = 1.0 / (Ns - 1);
        const double dt = F * dx * dx / ALPHA;
        HeatSimulation sim(Ns, Ns, ALPHA, dx, dt);
        applyBoundaries(sim, Ns, 100, 0, 0, 0);

        std::vector<std::pair<long long,double>> series;
        bool blewUp = false;
        long long ranTo = stabSteps;
        for (long long s = 0; s <= stabSteps; ++s) {
            if (s % 10 == 0) {
                const double m = maxAbs(sim.getGrid());
                series.emplace_back(s, m);
                csvB << F << "," << s << "," << m << "\n";
                if (!std::isfinite(m) || m > 1e12) { blewUp = true; ranTo = s; break; }
            }
            if (s < stabSteps) sim.step();
        }
        const double finalMax = series.empty() ? 0.0 : series.back().second;
        const bool finite = allFinite(sim.getGrid());
        // The boundary itself is 100, so anything materially above that is growth.
        const bool unstable = blewUp || !finite || finalMax > 100.0 * 1.0001;

        std::cout << std::fixed << std::setprecision(3) << std::setw(10) << F
                  << std::setw(14) << ranTo
                  << std::scientific << std::setprecision(4) << std::setw(21) << finalMax
                  << "     " << (unstable ? "DIVERGES" : "bounded")
                  << (F <= F_STABLE_LIMIT ? "   (F <= 0.25)" : "   (F > 0.25)") << "\n";

        json << "      {\"F\": " << num(F) << ", \"unstable\": " << (unstable ? "true" : "false")
             << ", \"final_max\": " << num(finalMax) << ", \"series\": [";
        for (size_t i = 0; i < series.size(); ++i) {
            json << "[" << series[i].first << "," << num(series[i].second) << "]"
                 << (i + 1 < series.size() ? "," : "");
        }
        json << "]}" << (k + 1 < Fs.size() ? "," : "") << "\n";
    }
    json << "    ]\n  },\n";

    // --------------------------------------------------------------- 4. sanity
    std::cout << "\n=== 4. SANITY CHECKS ===\n\n";
    json << "  \"sanity\": {\n";

    // (a) mean-value property at the centre of a square plate
    const int Nc = 41;
    const double t_ = 50, b_ = 25, l_ = 75, r_ = 10;
    const double expectedCentre = (t_ + b_ + l_ + r_) / 4.0;
    const Grid exactC = analytical(Nc, t_, b_, l_, r_);
    const SteadyResult fdmC = runSteady(Nc, 0.2, t_, b_, l_, r_, 1e-10, 5000000LL);
    const double aC = exactC[Nc/2][Nc/2];
    const double fC = fdmC.grid[Nc/2][Nc/2];
    std::cout << "  mean-value property   centre should equal " << std::fixed
              << std::setprecision(4) << expectedCentre << "\n"
              << "      analytical = " << aC << "   (err " << std::scientific
              << std::fabs(aC - expectedCentre) << ")\n"
              << "      FDM        = " << std::fixed << fC << "   (err " << std::scientific
              << std::fabs(fC - expectedCentre) << ")\n\n";
    json << "    \"mean_value\": {\"expected\": " << num(expectedCentre)
         << ", \"analytical\": " << num(aC) << ", \"fdm\": " << num(fC) << "},\n";

    // (b) rotational symmetry: hot top vs hot left, rotated
    const int Nr = 41;
    const Grid gTop  = analytical(Nr, 100, 0, 0, 0);
    const Grid gLeft = analytical(Nr, 0, 0, 100, 0);
    // Interior only: at a corner two edges disagree, and which one wins is a
    // tie-break in how boundaries are applied, not a property of the solution.
    double symErr = 0.0;
    for (int i = 1; i < Nr - 1; ++i)
        for (int j = 1; j < Nr - 1; ++j)
            symErr = std::max(symErr, std::fabs(gLeft[i][j] - gTop[j][Nr - 1 - i]));
    std::cout << "  rotational symmetry   max|rot(hot-top) - hot-left| = "
              << std::scientific << std::setprecision(4) << symErr << "\n\n";
    json << "    \"symmetry_max_error\": " << num(symErr) << ",\n";

    // (c) discrete maximum principle
    const Grid gm = fdmC.grid;
    double interiorMax = -1e300, interiorMin = 1e300;
    for (int i = 1; i < Nc - 1; ++i)
        for (int j = 1; j < Nc - 1; ++j) {
            interiorMax = std::max(interiorMax, gm[i][j]);
            interiorMin = std::min(interiorMin, gm[i][j]);
        }
    const double bMax = std::max(std::max(t_, b_), std::max(l_, r_));
    const double bMin = std::min(std::min(t_, b_), std::min(l_, r_));
    const bool holds = (interiorMax <= bMax + 1e-9) && (interiorMin >= bMin - 1e-9);
    std::cout << "  maximum principle     interior range [" << std::fixed << std::setprecision(4)
              << interiorMin << ", " << interiorMax << "]  within boundary range ["
              << bMin << ", " << bMax << "] -> " << (holds ? "HOLDS" : "VIOLATED") << "\n";
    json << "    \"maximum_principle\": {\"interior_min\": " << num(interiorMin)
         << ", \"interior_max\": " << num(interiorMax)
         << ", \"boundary_min\": " << num(bMin) << ", \"boundary_max\": " << num(bMax)
         << ", \"holds\": " << (holds ? "true" : "false") << "}\n";

    json << "  }\n}\n";
    json.close();

    std::cout << "\nWrote study_results.json, study_spatial.csv, study_temporal.csv, study_stability.csv\n";
    return 0;
}
