// Regression tests for the solver's physics.
//
// Every assertion here is a value that theory fixes independently of the code:
// exact analytical temperatures, convergence orders, a stability threshold, a
// conservation law, a closed-form wave speed. Two silent correctness bugs got
// through review during development - an analytical solver that was wrong for
// every case except one, and a convergence test that could never fire when a
// heat source was present. Both would have been caught here.
//
// Exits non-zero if anything fails, so CI can gate on it.

#include "simulation.hpp"
#include "reaction.hpp"
#include "wave.hpp"

#include <cstdio>
#include <string>
#include <vector>
#include <cmath>

namespace {

int failures = 0;
int checks = 0;

void check(bool ok, const std::string& name, const std::string& detail = "") {
    ++checks;
    if (!ok) ++failures;
    std::printf("  [%s] %s%s%s\n", ok ? "PASS" : "FAIL", name.c_str(),
                detail.empty() ? "" : "  -> ", detail.c_str());
}

void near(double got, double want, double tol, const std::string& name) {
    char buf[160];
    std::snprintf(buf, sizeof buf, "got %.6g, want %.6g (tol %.3g)", got, want, tol);
    check(std::fabs(got - want) <= tol, name, buf);
}

void section(const char* title) { std::printf("\n%s\n", title); }

using M = HeatSimulation::Method;
using Grid = std::vector<std::vector<double>>;

HeatSimulation plate(int N, double F, double top, double bottom,
                     double left, double right, M m = M::Explicit) {
    const double dx = 1.0 / (N - 1), alpha = 1.0;
    HeatSimulation s(N, N, alpha, dx, F * dx * dx / alpha);
    s.setMethod(m);
    for (int j = 0; j < N; ++j) { s.setBoundary(0, j, top); s.setBoundary(N - 1, j, bottom); }
    for (int i = 0; i < N; ++i) { s.setBoundary(i, 0, left); s.setBoundary(i, N - 1, right); }
    return s;
}

Grid analytic(int N, double t, double b, double l, double r) {
    HeatSimulation s(N, N, 1.0, 1.0, 0.1);
    s.solveAnalytical(t, b, l, r);
    return s.getGrid();
}

Grid steady(int N, double F, double t, double b, double l, double r, M m,
            double tol = 1e-10, long long cap = 2000000) {
    HeatSimulation s = plate(N, F, t, b, l, r, m);
    for (long long k = 0; k < cap; ++k) if (s.step() < tol) break;
    return s.getGrid();
}

double rms(const Grid& a, const Grid& b) {
    double sum = 0.0; long long n = 0;
    for (size_t i = 1; i + 1 < a.size(); ++i)
        for (size_t j = 1; j + 1 < a[0].size(); ++j) {
            const double d = a[i][j] - b[i][j]; sum += d * d; ++n;
        }
    return std::sqrt(sum / static_cast<double>(n));
}

double fitOrder(const std::vector<double>& xs, const std::vector<double>& ys) {
    const size_t n = xs.size();
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    for (size_t i = 0; i < n; ++i) {
        const double lx = std::log(xs[i]), ly = std::log(ys[i]);
        sx += lx; sy += ly; sxx += lx * lx; sxy += lx * ly;
    }
    return (n * sxy - sx * sy) / (n * sxx - sx * sx);
}

// ---------------------------------------------------------------- tests

void testAnalyticalExact() {
    section("Analytical solution: exact values fixed by theory");
    // At the centre of a square plate the steady temperature is the mean of the
    // four edge temperatures. This is what the original solver got wrong for
    // every configuration except a single hot edge.
    const int N = 41;
    near(analytic(N,100,0,0,0)[N/2][N/2],      25.0, 1e-9, "one hot edge -> 25");
    near(analytic(N,100,100,0,0)[N/2][N/2],    50.0, 1e-9, "two opposite   -> 50");
    near(analytic(N,100,0,100,0)[N/2][N/2],    50.0, 1e-9, "two adjacent   -> 50");
    near(analytic(N,100,100,100,100)[N/2][N/2],100.0,1e-9, "all four = 100 -> 100");
    near(analytic(N,0,0,0,0)[N/2][N/2],         0.0, 1e-12,"all cold       -> 0");
    near(analytic(N,50,25,75,10)[N/2][N/2],    40.0, 1e-9, "mixed          -> 40");

    // Rotating the boundary data must rotate the solution.
    const Grid a = analytic(N,100,0,0,0), b = analytic(N,0,0,100,0);
    double sym = 0.0;
    for (int i = 1; i < N-1; ++i)
        for (int j = 1; j < N-1; ++j)
            sym = std::max(sym, std::fabs(b[i][j] - a[j][N-1-i]));
    near(sym, 0.0, 1e-9, "rotational symmetry");
}

void testSchemesAgree() {
    section("All three time schemes reach the same steady state");
    const int N = 41;
    const double exact = analytic(N,100,0,0,0)[N/2][N/2];
    near(steady(N,0.2,100,0,0,0,M::Explicit)[N/2][N/2],      exact, 1e-3, "explicit");
    near(steady(N,5.0,100,0,0,0,M::BackwardEuler)[N/2][N/2], exact, 1e-3, "backward Euler");
    near(steady(N,5.0,100,0,0,0,M::CrankNicolson)[N/2][N/2], exact, 1e-3, "Crank-Nicolson");
}

void testMaximumPrinciple() {
    section("Discrete maximum principle");
    const int N = 41;
    const Grid g = steady(N,0.2,50,25,75,10,M::Explicit);
    double lo = 1e300, hi = -1e300;
    for (int i = 1; i < N-1; ++i)
        for (int j = 1; j < N-1; ++j) { lo = std::min(lo,g[i][j]); hi = std::max(hi,g[i][j]); }
    check(hi <= 75.0 + 1e-9 && lo >= 10.0 - 1e-9, "no interior extremum",
          "interior [" + std::to_string(lo) + ", " + std::to_string(hi) + "]");
}

void testSpatialOrder() {
    section("Spatial convergence: second order on smooth data");
    // Manufactured solution u = A sin(pi x/L) sinh(pi y/L)/sinh(pi W/L):
    // harmonic, and continuous at the corners, so the scheme can show its
    // true order. With discontinuous corner data it cannot.
    std::vector<double> dxs, errs;
    for (int N : {11, 21, 41, 81}) {
        const double dx = 1.0/(N-1), L = N-1, k = M_PI/L;
        HeatSimulation s(N,N,1.0,dx,0.2*dx*dx);
        for (int j=0;j<N;++j) s.setBoundary(N-1,j,0.0);
        for (int i=0;i<N;++i){ s.setBoundary(i,0,0.0); s.setBoundary(i,N-1,0.0); }
        for (int j=0;j<N;++j) s.setBoundary(0,j,100.0*std::sin(M_PI*j/L));
        for (long long t=0;t<5000000;++t) if (s.step() < 1e-10) break;

        Grid ex(N, std::vector<double>(N,0.0));
        for (int i=0;i<N;++i) for (int j=0;j<N;++j)
            ex[i][j] = 100.0*std::sin(k*j)*HeatSimulation::sinhRatio(k,(N-1)-i,N-1);
        dxs.push_back(dx); errs.push_back(rms(s.getGrid(), ex));
    }
    const double p = fitOrder(dxs, errs);
    check(p > 1.85 && p < 2.15, "observed order approaches 2",
          "order = " + std::to_string(p));
}

void testTemporalOrders() {
    section("Temporal convergence: Euler schemes O(dt), Crank-Nicolson O(dt^2)");
    const int N = 41; const double dx = 1.0/(N-1);
    struct C { M m; const char* name; double lo, hi; };
    for (auto c : std::vector<C>{{M::Explicit,"explicit",0.85,1.25},
                                 {M::BackwardEuler,"backward Euler",0.85,1.25},
                                 {M::CrankNicolson,"Crank-Nicolson",1.85,2.15}}) {
        auto at = [&](double F, long long steps) {
            HeatSimulation s = plate(N,F,100,0,0,0,c.m);
            for (long long k=0;k<steps;++k) s.step();
            return s.getGrid();
        };
        const Grid ref = at(0.2/256.0, 400*256);
        std::vector<double> dts, es;
        for (int k=0;k<4;++k) {
            const double F = 0.2/std::pow(2.0,k);
            dts.push_back(F*dx*dx);
            es.push_back(rms(at(F, 400*(1LL<<k)), ref));
        }
        const double p = fitOrder(dts, es);
        check(p > c.lo && p < c.hi, std::string(c.name) + " order",
              "order = " + std::to_string(p));
    }
}

void testStability() {
    section("Stability: explicit is bound by F <= 1/4, implicit is not");
    auto maxAbsAfter = [](double F, M m, int steps) {
        HeatSimulation s = plate(21,F,100,0,0,0,m);
        for (int k=0;k<steps;++k) s.step();
        double mx = 0.0; bool fin = true;
        for (const auto& row : s.getGrid())
            for (double v : row) { mx = std::max(mx,std::fabs(v)); if (!std::isfinite(v)) fin=false; }
        return fin ? mx : 1e300;
    };
    check(maxAbsAfter(0.25, M::Explicit, 400) <= 100.0001, "explicit bounded at F = 0.25");
    check(maxAbsAfter(0.26, M::Explicit, 400) > 1e3,       "explicit diverges at F = 0.26");
    check(maxAbsAfter(500.0, M::BackwardEuler, 200) <= 100.0001, "backward Euler bounded at F = 500");
    check(maxAbsAfter(500.0, M::CrankNicolson, 200) <= 100.0001, "Crank-Nicolson bounded at F = 500");
}

void testPointSource() {
    section("Heat sources stay pinned through an implicit solve");
    for (auto m : {M::Explicit, M::BackwardEuler, M::CrankNicolson}) {
        HeatSimulation s = plate(41, (m==M::Explicit?0.2:5.0), 0,0,0,0, m);
        s.addPointSource(20,20,500.0);
        for (int k=0;k<500;++k) s.step();
        near(s.getGrid()[20][20], 500.0, 1e-9, "source held at 500");
    }
}

void testComposite() {
    section("Heterogeneous media: exact series resistance");
    // A two-layer slab, insulated top and bottom so it is 1D. Harmonic-mean
    // face conductivity reproduces the series-resistance profile exactly; an
    // arithmetic mean would not.
    for (auto pr : std::vector<std::pair<double,double>>{{1.0,1.0},{1.0,0.1},{1.0,10.0}}) {
        const double a1 = pr.first, a2 = pr.second;
        const int N = 21, C = 81, m = C/2;
        const double T1 = 100.0, T2 = 0.0, dx = 1.0;
        HeatSimulation s(N,C,1.0,dx, 0.2*dx*dx/std::max(a1,a2));
        s.setMethod(M::CrankNicolson);
        s.setInsulatedEdges(true,true,false,false);
        for (int i=0;i<N;++i) { s.setBoundary(i,0,T1); s.setBoundary(i,C-1,T2); }
        s.setAlphaRegion(0,0,N-1,m-1,a1);
        s.setAlphaRegion(0,m,N-1,C-1,a2);
        for (long long k=0;k<400000;++k) if (s.step() < 1e-13) break;

        const double L1 = (m-0.5)*dx, L2 = ((C-1)-(m-0.5))*dx;
        const double q = (T1-T2)/(L1/a1 + L2/a2);
        const double Tface = T1 - q*L1/a1;
        double maxerr = 0.0;
        const auto& g = s.getGrid();
        for (int j=0;j<C;++j) {
            const double x = j*dx;
            const double ex = (x <= L1) ? (T1 - q*x/a1) : (Tface - q*(x-L1)/a2);
            maxerr = std::max(maxerr, std::fabs(g[N/2][j] - ex));
        }
        check(maxerr < 1e-6, "profile matches exact solution (a1=" +
              std::to_string(a1) + ", a2=" + std::to_string(a2) + ")",
              "max err = " + std::to_string(maxerr));
    }
}

void testSymmetricMaterial() {
    section("Four-fold symmetric material leaves the centre at the boundary mean");
    // Summing the four rotations of the single-hot-edge problem gives the
    // uniform all-hot solution. If the domain AND the alpha field are
    // invariant under those rotations, the four centre values are equal, so
    // each must be exactly a quarter of the boundary temperature - whatever
    // the material contrast. This validates heterogeneous media without
    // relying on any 1D formula.
    const int N = 41;
    for (double contrast : {0.001, 0.1, 10.0, 1000.0}) {
        HeatSimulation s = plate(N, 5.0, 100, 0, 0, 0, M::CrankNicolson);
        s.setAlphaRegion(15, 15, 25, 25, contrast);   // symmetric about the centre
        for (long long k = 0; k < 500000; ++k) if (s.step() < 1e-12) break;
        near(s.getGrid()[N/2][N/2], 25.0, 1e-3,
             "centre = 25 with alpha contrast " + std::to_string(contrast));
    }
}

void testEnergyConservation() {
    section("Insulated domain conserves energy");
    const char* names[3] = {"explicit","backward Euler","Crank-Nicolson"};
    M ms[3] = {M::Explicit, M::BackwardEuler, M::CrankNicolson};
    for (int k=0;k<3;++k) {
        const int N = 41; const double dx = 1.0, al = 0.2;
        HeatSimulation s(N,N,al,dx,(k==0?0.2:5.0)*dx*dx/al);
        s.setMethod(ms[k]);
        s.setInsulatedEdges(true,true,true,true);
        for (int i=0;i<N;++i) for (int j=0;j<N;++j)
            s.setBoundary(i,j,(i<N/2 && j<N/2) ? 100.0 : 0.0);
        const double e0 = s.totalEnergy();
        for (int t=0;t<2000;++t) s.step();
        const double drift = std::fabs(s.totalEnergy()-e0)/e0;
        check(drift < 1e-10, std::string(names[k]) + " conserves total energy",
              "relative drift = " + std::to_string(drift));
    }
}

void testFisherSpeed() {
    section("Fisher-KPP front speed approaches c* = 2 sqrt(D r)");
    const double D = 0.2, r = 1.0, dx = 0.1, dt = 0.2*dx*dx/D;
    const double cstar = 2.0*std::sqrt(D*r);
    ReactionDiffusion rd(5, 3000, ReactionDiffusion::Model::FisherKPP);
    rd.setFisher(D,r); rd.setSpacing(dx); rd.setDt(dt); rd.seedFisher(20);

    long long n = static_cast<long long>(20.0/dt);
    for (long long k=0;k<n;++k) rd.step();
    double x0 = rd.frontPosition();
    long long n2 = static_cast<long long>(160.0/dt);
    for (long long k=n;k<n2;++k) rd.step();
    const double c = (rd.frontPosition()-x0)/(160.0-20.0);
    // Convergence is algebraic (Bramson's 1/t correction), so it sits just
    // below c* at any finite time. 5% is comfortably inside that.
    check(c < cstar && std::fabs(c-cstar)/cstar < 0.05,
          "front speed within 5% of c*, approaching from below",
          "c = " + std::to_string(c) + " vs c* = " + std::to_string(cstar));
}

void testGrayScott() {
    section("Gray-Scott patterns form (and a dead regime does not)");
    // 128 square: below ~96 cells the domain is small relative to the pattern
    // wavelength and marginal regimes genuinely fail to establish.
    auto sd = [](double f, double k) {
        ReactionDiffusion rd(128,128, ReactionDiffusion::Model::GrayScott);
        rd.setGrayScott(0.16,0.08,f,k); rd.setDt(1.0); rd.seedGrayScott(7,0,0.02);
        for (int t=0;t<6000;++t) rd.step();
        double s=0, n=0;
        for (const auto& row : rd.field()) for (double v : row) { s+=v; ++n; }
        const double mean=s/n; double var=0;
        for (const auto& row : rd.field()) for (double v : row) var+=(v-mean)*(v-mean);
        return std::sqrt(var/n);
    };
    check(sd(0.035,0.065) > 0.02, "spots regime produces structure");
    check(sd(0.029,0.057) > 0.02, "mazes regime produces structure");
    // Negative control: without this, "it made a pattern" proves nothing.
    check(sd(0.010,0.080) < 1e-6, "dead regime stays uniform");
}

// ---------------------------------------------------------------- wave ----

/**
 * Build the (m, n) standing mode of a clamped square membrane and return the
 * centre amplitude after each step alongside the closed form it should match.
 *
 *   u(x,y,t) = sin(m pi x/Lx) sin(n pi y/Ly) cos(omega t)
 *   omega    = c pi sqrt((m/Lx)^2 + (n/Ly)^2)
 *
 * Released from rest, so the cosine starts at its maximum. For m = n = 1 the
 * centre is the antinode, where the spatial factor is exactly 1 and the centre
 * value is therefore cos(omega t) on the nose - no amplitude fitting needed.
 */
double standingModeError(int N, double nu, double tEnd) {
    const double L = 1.0, c = 1.0;
    const double dx = L / (N - 1), dt = nu * dx / c;
    WaveSimulation w(N, N, c, dx, dt);
    w.setBoundaryCondition(WaveSimulation::Boundary::Fixed);
    for (int i = 0; i < N; ++i)
        for (int j = 0; j < N; ++j)
            w.setDisplacement(i, j, std::sin(M_PI * (j * dx) / L) * std::sin(M_PI * (i * dx) / L));

    const double omega = c * M_PI * std::sqrt(1.0 / (L * L) + 1.0 / (L * L));
    const int ctr = (N - 1) / 2, steps = static_cast<int>(tEnd / dt);
    double worst = 0.0;
    for (int s = 1; s <= steps; ++s) {
        w.step();
        worst = std::max(worst, std::fabs(w.field()[ctr][ctr] - std::cos(omega * s * dt)));
    }
    return worst;
}

void testWaveStandingMode() {
    section("Wave: (1,1) standing mode tracks the closed form");
    // Two full periods (omega = pi sqrt(2), so T ~ 1.41). Phase error is what
    // accumulates in a non-dissipative scheme, so a long window is the honest
    // test - a single step would pass no matter how wrong the frequency is.
    check(standingModeError(81, 0.5, 2.0) < 2e-3,
          "centre amplitude matches cos(omega t) over two periods",
          "max|err| = " + std::to_string(standingModeError(81, 0.5, 2.0)));

    // Refining dx and dt together must quarter the error. This is the check
    // that pins the startup step: taking a full leapfrog step at t = 0 instead
    // of the half-coefficient Taylor step still looks plausible frame by frame
    // but degrades the scheme to first order, halving the error instead.
    const double e1 = standingModeError(41, 0.5, 2.0);
    const double e2 = standingModeError(81, 0.5, 2.0);
    const double e3 = standingModeError(161, 0.5, 2.0);
    const double p = 0.5 * (std::log2(e1 / e2) + std::log2(e2 / e3));
    check(p > 1.85 && p < 2.15, "observed order approaches 2",
          "order = " + std::to_string(p));
}

void testWaveCFL() {
    section("Wave: CFL limit is 1/sqrt(2) in 2D, not 1");
    auto run = [](double nu, int steps) {
        const int N = 61;
        const double dx = 1.0 / (N - 1), c = 1.0, dt = nu * dx / c;
        WaveSimulation w(N, N, c, dx, dt);
        w.setBoundaryCondition(WaveSimulation::Boundary::Fixed);
        w.pluckSmooth(N / 2, N / 2, 6, 1.0);
        bool stepped = true;
        for (int s = 0; s < steps; ++s) if (!w.step()) { stepped = false; break; }
        return std::make_pair(stepped, w.maxAbsAmplitude());
    };
    // At and below the limit the scheme runs and stays bounded - it is
    // non-dissipative, so "bounded" is the right criterion, not "decaying".
    check(run(0.5, 3000).first && run(0.5, 3000).second <= 1.0001,
          "runs and stays bounded at nu = 0.5");
    check(run(WaveSimulation::cflLimit(), 3000).first,
          "runs exactly at nu = 1/sqrt(2)");
    // Above it, the step is refused rather than allowed to overflow. The 1D
    // limit of 1 sits in this range, which is the point of the check.
    check(!run(0.72, 10).first, "refuses to step at nu = 0.72");
    check(!run(1.0, 10).first,  "refuses to step at nu = 1 (the 1D limit)");
}

void testWaveBoundaries() {
    section("Wave: clamped edges stay pinned, free edges do not");
    const int N = 41;
    const double dx = 1.0 / (N - 1), dt = 0.5 * dx;

    WaveSimulation fixed(N, N, 1.0, dx, dt);
    fixed.setBoundaryCondition(WaveSimulation::Boundary::Fixed);
    fixed.pluckSmooth(N / 2, N / 2, 5, 1.0);
    for (int s = 0; s < 400; ++s) fixed.step();
    double edge = 0.0;
    for (int j = 0; j < N; ++j) edge = std::max(edge, std::fabs(fixed.field()[0][j]));
    check(edge == 0.0, "clamped edge holds amplitude 0 exactly");

    WaveSimulation free_(N, N, 1.0, dx, dt);
    free_.setBoundaryCondition(WaveSimulation::Boundary::Free);
    free_.pluckSmooth(N / 2, N / 2, 5, 1.0);
    for (int s = 0; s < 400; ++s) free_.step();
    double fedge = 0.0;
    for (int j = 0; j < N; ++j) fedge = std::max(fedge, std::fabs(free_.field()[0][j]));
    // The wave has had time to cross and reflect, so a free edge must carry
    // amplitude; if it read zero the mirroring would be doing nothing.
    check(fedge > 1e-6, "free edge carries amplitude after the wave reaches it",
          "max|u| on edge = " + std::to_string(fedge));
}

} // namespace

int main() {
    std::printf("Heat solver physics tests\n");
    testAnalyticalExact();
    testSchemesAgree();
    testMaximumPrinciple();
    testSpatialOrder();
    testTemporalOrders();
    testStability();
    testPointSource();
    testComposite();
    testSymmetricMaterial();
    testEnergyConservation();
    testFisherSpeed();
    testGrayScott();
    testWaveStandingMode();
    testWaveCFL();
    testWaveBoundaries();

    std::printf("\n%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
