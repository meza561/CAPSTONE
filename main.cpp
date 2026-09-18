#include <iostream>
#include <vector>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <string>
#include <utility>
#include <algorithm>
#include <sstream>
#include <filesystem>
#include <system_error>
#include <limits>
#include "database.hpp"
#include "simulation.hpp"
#include "reaction.hpp"
#include "wave.hpp"

/**
 * @param rangeMin,rangeMax  the field's range over the whole run, when the
 *        caller tracked it. NaN means "take it from this grid", which is right
 *        for the heat modes: they march toward a steady state bracketed by the
 *        boundary data, so the final frame already spans the run. A wave does
 *        the opposite - it peaks at t = 0 and spreads out - so its final frame
 *        understates the range, and the frontend would rescale mid-timeline.
 */
void exportToJSON(const std::string& filename, int step, int rows, int cols,
                  const std::vector<std::vector<double>>& grid,
                  double dt, int saveInterval,
                  const std::string& scheme, double diffusionNumber,
                  double rangeMin = std::numeric_limits<double>::quiet_NaN(),
                  double rangeMax = std::numeric_limits<double>::quiet_NaN()) {
    std::ofstream file(filename);
    file << "{\n";
    file << "  \"step\": " << step << ",\n";
    file << "  \"rows\": " << rows << ",\n";
    file << "  \"cols\": " << cols << ",\n";
    // dt is derived from the stability limit, so the UI cannot assume it.
    file << "  \"dt\": " << std::setprecision(10) << dt << ",\n";
    file << "  \"saveInterval\": " << saveInterval << ",\n";
    file << "  \"scheme\": \"" << scheme << "\",\n";
    file << "  \"F\": " << diffusionNumber << ",\n";
    // The field's actual range. The heat modes happen to run between the
    // boundary temperatures, but wave amplitude is signed and centred on zero,
    // so a consumer cannot assume a 0-100 scale and read it correctly. Stating
    // the measured range lets the frontend pick a symmetric diverging scale
    // for a signed field without having to know which mode produced it.
    double dataMin = grid.empty() ? 0.0 : grid[0][0];
    double dataMax = dataMin;
    for (const auto& row : grid)
        for (double v : row) { dataMin = std::min(dataMin, v); dataMax = std::max(dataMax, v); }
    if (std::isfinite(rangeMin) && std::isfinite(rangeMax)) {
        dataMin = std::min(dataMin, rangeMin);
        dataMax = std::max(dataMax, rangeMax);
    }
    file << "  \"min\": " << std::setprecision(10) << dataMin << ",\n";
    file << "  \"max\": " << std::setprecision(10) << dataMax << ",\n";
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

namespace {

struct MaterialRegion { int r0, c0, r1, c1; double alpha; };

struct Options {
    bool insTop = false, insBottom = false, insLeft = false, insRight = false;
    std::vector<MaterialRegion> materials;
    std::string runId;
};

/** A run id has to be safe to drop straight into a path. Exactly the 32
 *  lowercase hex characters of a uuid4, so no separators and nothing that can
 *  climb out of runs/. */
bool validRunId(const std::string& id) {
    if (id.size() != 32) return false;
    for (char c : id) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}

/**
 * Pull `--key=value` options out of argv and return the remaining positional
 * arguments. Keeping these flag-style avoids adding more positional slots to
 * an already long fixed layout, and leaves the existing ordering untouched -
 * which matters for --run-id in particular, since the reaction-diffusion modes
 * already read their own parameters from the trailing positional slots.
 *
 *   --insulate=tblr                      edges held at zero flux
 *   --material=r0,c0,r1,c1,alpha         repeatable; a rectangle of diffusivity
 *   --run-id=<32 hex>                    write runs/<id>.{db,json} instead of
 *                                        the shared heat_sim.db
 */
std::vector<std::string> extractOptions(int argc, char* argv[], Options& opt) {
    std::vector<std::string> positional;
    for (int i = 0; i < argc; ++i) {
        const std::string a = argv[i];
        if (a.rfind("--run-id=", 0) == 0) {
            opt.runId = a.substr(9);
        } else if (a.rfind("--insulate=", 0) == 0) {
            const std::string e = a.substr(11);
            opt.insTop    = e.find('t') != std::string::npos;
            opt.insBottom = e.find('b') != std::string::npos;
            opt.insLeft   = e.find('l') != std::string::npos;
            opt.insRight  = e.find('r') != std::string::npos;
        } else if (a.rfind("--material=", 0) == 0) {
            std::string body = a.substr(11);
            for (char& ch : body) if (ch == ',') ch = ' ';
            std::istringstream is(body);
            MaterialRegion mr{};
            if (is >> mr.r0 >> mr.c0 >> mr.r1 >> mr.c1 >> mr.alpha && mr.alpha > 0.0) {
                opt.materials.push_back(mr);
            }
        } else {
            positional.push_back(a);
        }
    }
    return positional;
}

} // namespace

int main(int argcRaw, char* argvRaw[]) {
    Options opt;
    std::vector<std::string> args = extractOptions(argcRaw, argvRaw, opt);

    // Per-run output files. Without --run-id the binary keeps writing the
    // shared pair it always has, so running it by hand (and the CI smoke test)
    // still works; the server always passes one, which is what lets two runs
    // proceed without touching each other's data.
    std::string dbPath = "heat_sim.db";
    std::string jsonPath = "latest_heatmap.json";
    if (!opt.runId.empty()) {
        if (!validRunId(opt.runId)) {
            std::cerr << "Invalid --run-id: expected 32 lowercase hex characters\n";
            return 1;
        }
        std::error_code ec;
        std::filesystem::create_directories("runs", ec);
        if (ec) {
            std::cerr << "Could not create runs/: " << ec.message() << "\n";
            return 1;
        }
        dbPath = "runs/" + opt.runId + ".db";
        jsonPath = "runs/" + opt.runId + ".json";
    }
    const int argc = static_cast<int>(args.size());
    std::vector<char*> argvStore;
    for (auto& s : args) argvStore.push_back(const_cast<char*>(s.c_str()));
    char** argv = argvStore.data();

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
    double F_TARGET = 0.2;

    // Heat sources. Trailing arguments are (row, col, temp) triples, one per
    // source, so any number of them can be passed. The API converts the UI's
    // (x, y) coordinates into row/col before calling.
    struct SourceArg { int r; int c; double t; };
    std::vector<SourceArg> sources;

    if (argc >= 7) {
        ROWS = std::stoi(argv[1]);
        COLS = std::stoi(argv[2]);
        topTemp = std::stod(argv[3]);
        bottomTemp = std::stod(argv[4]);
        leftTemp = std::stod(argv[5]);
        rightTemp = std::stod(argv[6]);
        if (argc >= 8) mode = argv[7];
        if (argc >= 9) ALPHA = std::stod(argv[8]);
        // Diffusion number. The explicit scheme is only stable up to 0.25;
        // the implicit schemes accept any positive value.
        if (argc >= 10) F_TARGET = std::stod(argv[9]);
        // The trailing slots are heat sources only for the heat modes. The
        // other models read their own parameters from the same positions, and
        // wave's boundary type is a word ("fixed"/"free"), so parsing it as a
        // source coordinate throws rather than quietly producing junk.
        if (mode != "wave" && mode != "fisher" && mode != "gray-scott") {
            for (int k = 10; k + 2 < argc; k += 3) {
                sources.push_back({ std::stoi(argv[k]),
                                    std::stoi(argv[k + 1]),
                                    std::stod(argv[k + 2]) });
            }
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
    if (ALPHA <= 0.0) ALPHA = 0.01;
    if (!(F_TARGET > 0.0) || F_TARGET > 1000.0) F_TARGET = 0.2;

    // Map the mode string onto a time-integration scheme.
    HeatSimulation::Method method = HeatSimulation::Method::Explicit;
    std::string methodName = "explicit (forward Euler)";
    if (mode == "be" || mode == "backward-euler") {
        method = HeatSimulation::Method::BackwardEuler;
        methodName = "backward Euler (ADI)";
    } else if (mode == "cn" || mode == "crank-nicolson") {
        method = HeatSimulation::Method::CrankNicolson;
        methodName = "Crank-Nicolson (ADI)";
    }

    const bool isWave = (mode == "wave");
    const bool isReactionDiffusion = (mode == "fisher" || mode == "gray-scott");

    // F is the heat solver's diffusion number. The wave and reaction-diffusion
    // models derive their own step size and ignore it, so warning about it
    // there would be describing a limit that does not apply to the run.
    if (method == HeatSimulation::Method::Explicit && F_TARGET > 0.25
            && !isWave && !isReactionDiffusion) {
        std::cout << "Warning: F = " << F_TARGET
                  << " exceeds the explicit stability limit of 0.25; "
                     "the solution will diverge.\n";
    }

    DT = F_TARGET * DX * DX / ALPHA;
    if (!isReactionDiffusion && !isWave) {
        if (mode != "pde") {
            std::cout << "Scheme: " << methodName << "\n";
        }
        std::cout << "Using dt = " << DT << " s (diffusion number F = "
                  << F_TARGET << ")\n";
    }

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
        if (!isReactionDiffusion && !isWave) {   // those set their own frame budget below
            std::cout << "Large grid (" << ROWS << "x" << COLS
                      << "): saving every " << SAVE_INTERVAL << " steps\n";
        }
    }

    // Judge convergence relative to the problem's temperature scale. A fixed
    // absolute threshold stopped the run ~9% short of equilibrium, and its
    // meaning shifted once dt stopped being a constant.
    {
        double tempScale = std::max({std::fabs(topTemp), std::fabs(bottomTemp),
                                     std::fabs(leftTemp), std::fabs(rightTemp),
                                     1.0});
        for (const auto& s : sources) {
            tempScale = std::max(tempScale, std::fabs(s.t));
        }
        CONVERGENCE_THRESHOLD = 1e-7 * tempScale;
    }

    HeatSimulation sim(ROWS, COLS, ALPHA, DX, DT);
    sim.setMethod(method);
    sim.setInsulatedEdges(opt.insTop, opt.insBottom, opt.insLeft, opt.insRight);
    for (const auto& mr : opt.materials) {
        sim.setAlphaRegion(mr.r0, mr.c0, mr.r1, mr.c1, mr.alpha);
    }
    if (!opt.materials.empty()) {
        std::cout << "Material regions: " << opt.materials.size() << "\n";
    }
    if (sim.anyInsulated()) {
        std::cout << "Insulated edges:"
                  << (opt.insTop ? " top" : "") << (opt.insBottom ? " bottom" : "")
                  << (opt.insLeft ? " left" : "") << (opt.insRight ? " right" : "") << "\n";
    }
    HeatDatabase db(dbPath);

    if (!db.init()) return 1;

    if (mode == "wave") {
        // ---- Wave equation ---------------------------------------------
        // u_tt = c^2 lap(u). Second order in time, so the solver carries two
        // time levels; see wave.hpp. Nothing here converges - a clamped
        // membrane oscillates forever - so the run is a fixed step count
        // rather than a convergence loop.
        const double cWave = (argc > 10) ? std::stod(argv[10]) : 1.0;
        const std::string bcArg = (argc > 11) ? argv[11] : "fixed";
        int waveSteps = (argc > 12) ? std::stoi(argv[12]) : 1200;
        const std::string icArg = (argc > 13) ? argv[13] : "pluck";

        const double c = (cWave > 0.0 && cWave <= 1e3) ? cWave : 1.0;
        const bool freeEdges = (bcArg == "free");
        if (waveSteps < 1) waveSteps = 1;
        if (waveSteps > 200000) waveSteps = 200000;

        // dt comes from the CFL limit, exactly as the heat solver derives dt
        // from F. A Courant number of 0.5 leaves comfortable margin under the
        // 2D limit of 1/sqrt(2) ~ 0.707.
        //
        // Because dt is tied to the limit, dt = nu dx / c shrinks as c grows,
        // so c does not change how far the wave moves per frame - it sets the
        // physical time the run spans, the same way alpha does for heat. The
        // reported dt is what makes the time axis correct.
        const double NU = 0.5;
        const double waveDx = 1.0;
        const double waveDt = NU * waveDx / c;

        WaveSimulation w(ROWS, COLS, c, waveDx, waveDt);
        w.setBoundaryCondition(freeEdges ? WaveSimulation::Boundary::Free
                                         : WaveSimulation::Boundary::Fixed);
        if (icArg == "impulse") {
            w.impulse(ROWS / 2, COLS / 2, 1.0);
        } else {
            // Radius scales with the grid so the pluck stays a feature of the
            // domain rather than of the resolution.
            w.pluckSmooth(ROWS / 2, COLS / 2, std::max(2, std::min(ROWS, COLS) / 8), 1.0);
        }

        std::cout << "Wave: c=" << c << " dt=" << waveDt
                  << " (Courant nu=" << NU << ", 2D limit "
                  << WaveSimulation::cflLimit() << ")\n";
        std::cout << "Edges: " << (freeEdges ? "free (Neumann, same-sign reflection)"
                                             : "fixed (clamped, inverted reflection)") << "\n";

        int saveEvery = std::max(1, waveSteps / 150);
        const long long wCells = static_cast<long long>(ROWS) * COLS;
        const long long W_BUDGET = 3000000LL;
        while ((waveSteps / saveEvery + 1) * wCells > W_BUDGET) saveEvery *= 2;
        std::cout << "Running " << waveSteps << " steps, saving every "
                  << saveEvery << "\n";

        if (!db.beginRun()) return 1;
        double waveMin = 0.0, waveMax = 0.0;
        auto trackRange = [&](const WaveSimulation::Grid& g) {
            for (const auto& row : g)
                for (double v : row) { waveMin = std::min(waveMin, v); waveMax = std::max(waveMax, v); }
        };
        db.saveTimestep(0, w.field());
        trackRange(w.field());
        for (int s = 1; s <= waveSteps; ++s) {
            if (!w.step()) {
                // Unreachable with the derived dt above, but the binary is
                // callable by hand: report it rather than writing a field that
                // silently stopped advancing.
                std::cerr << "CFL violated (nu = " << w.courant() << " > "
                          << WaveSimulation::cflLimit() << "); stopping at step "
                          << s << "\n";
                return 1;
            }
            if (s % saveEvery == 0 || s == waveSteps) {
                db.saveTimestep(s, w.field());
                trackRange(w.field());
            }
        }
        if (!db.endRun()) return 1;

        exportToJSON(jsonPath, waveSteps, ROWS, COLS, w.field(),
                     waveDt, saveEvery, "wave (leapfrog)", F_TARGET,
                     waveMin, waveMax);
        std::cout << "Simulation complete. Output written to " << jsonPath << "\n";
        return 0;

    } else if (mode == "fisher" || mode == "gray-scott") {
        // ---- Reaction-diffusion ----------------------------------------
        // Same five-point Laplacian as the heat solver, plus a reaction term.
        const bool gs = (mode == "gray-scott");
        ReactionDiffusion rd(ROWS, COLS,
            gs ? ReactionDiffusion::Model::GrayScott
               : ReactionDiffusion::Model::FisherKPP);

        double rdDx = 1.0, rdDt = 1.0;
        int rdSteps = 5000;
        std::string rdName;

        if (gs) {
            const double Du = (argc > 10) ? std::stod(argv[10]) : 0.16;
            const double Dv = (argc > 11) ? std::stod(argv[11]) : 0.08;
            const double fd = (argc > 12) ? std::stod(argv[12]) : 0.035;
            const double kl = (argc > 13) ? std::stod(argv[13]) : 0.065;
            if (argc > 14) rdSteps = std::stoi(argv[14]);
            rd.setGrayScott(Du, Dv, fd, kl);
            rd.setSpacing(1.0);          // the normalisation Gray-Scott is quoted in
            rdDt = 1.0;
            rd.seedGrayScott(11u, 0, 0.02);   // 0 = scale seeds with the domain
            if (std::min(ROWS, COLS) < 96) {
                std::cout << "Note: grids below ~96 cells are small relative to the "
                             "pattern wavelength; some regimes may not establish.\n";
            }
            rdName = "Gray-Scott (Turing)";
            std::cout << "Gray-Scott: Du=" << Du << " Dv=" << Dv
                      << " feed=" << fd << " kill=" << kl << "\n";
        } else {
            const double Dd = (argc > 10) ? std::stod(argv[10]) : 0.2;
            const double rr = (argc > 11) ? std::stod(argv[11]) : 1.0;
            if (argc > 12) rdSteps = std::stoi(argv[12]);
            // The travelling front is only sqrt(D/r) wide. Resolve it with a
            // few cells, or the discrete wave does not travel at c* = 2*sqrt(D*r).
            rdDx = std::max(0.02, std::sqrt(Dd / std::max(rr, 1e-9)) / 4.0);
            rdDt = 0.2 * rdDx * rdDx / std::max(Dd, 1e-9);
            rd.setFisher(Dd, rr);
            rd.setSpacing(rdDx);
            rd.seedFisher(std::max(3, static_cast<int>(2.0 / rdDx)));
            rdName = "Fisher-KPP";
            const double cstar = 2.0 * std::sqrt(Dd * rr);
            std::cout << "Fisher-KPP: D=" << Dd << " r=" << rr
                      << " dx=" << rdDx << " c*=" << cstar << "\n";

            // The front would otherwise run off the end and fill the domain,
            // leaving a uniform field that looks like nothing happened. Stop
            // while it is still travelling.
            const double tCross = 0.85 * (COLS * rdDx) / std::max(cstar, 1e-9);
            const int maxUseful = static_cast<int>(tCross / rdDt);
            if (maxUseful > 0 && rdSteps > maxUseful) {
                std::cout << "Front reaches the far edge at step " << maxUseful
                          << "; stopping there.\n";
                rdSteps = maxUseful;
            }
        }
        rd.setDt(rdDt);
        if (rdSteps < 1) rdSteps = 1;
        if (rdSteps > 200000) rdSteps = 200000;

        // Roughly 150 frames is ample for the slider, and each frame costs
        // rows*cols database rows - 500 frames of a 128x128 run is 200 MB.
        int saveEvery = std::max(1, rdSteps / 150);
        const long long rdCells = static_cast<long long>(ROWS) * COLS;
        const long long RD_BUDGET = 3000000LL;
        while ((rdSteps / saveEvery + 1) * rdCells > RD_BUDGET) saveEvery *= 2;
        std::cout << "Running " << rdSteps << " steps, saving every "
                  << saveEvery << "\n";

        if (!db.beginRun()) return 1;
        db.saveTimestep(0, rd.field());
        for (int s = 1; s <= rdSteps; ++s) {
            rd.step();
            if (s % saveEvery == 0 || s == rdSteps) db.saveTimestep(s, rd.field());
        }
        if (!db.endRun()) return 1;

        exportToJSON(jsonPath, rdSteps, ROWS, COLS, rd.field(),
                     rdDt, saveEvery, rdName, F_TARGET);
        std::cout << "Simulation complete. Output written to " << jsonPath << "\n";
        return 0;

    } else if (mode == "pde") {
        std::cout << "Running Analytical PDE Solver...\n";
        sim.solveAnalytical(topTemp, bottomTemp, leftTemp, rightTemp);
        db.beginRun();
        db.saveTimestep(0, sim.getGrid());
        db.endRun();
        exportToJSON(jsonPath, 0, ROWS, COLS, sim.getGrid(), DT, 1,
                     "analytical", F_TARGET);
    } else {
        // An insulated edge carries no imposed temperature - writing one would
        // just be an initial condition that immediately diffuses away.
        if (!opt.insTop)    for (int j = 0; j < COLS; ++j) sim.setBoundary(0, j, topTemp);
        if (!opt.insBottom) for (int j = 0; j < COLS; ++j) sim.setBoundary(ROWS - 1, j, bottomTemp);
        if (!opt.insLeft)   for (int i = 0; i < ROWS; ++i) sim.setBoundary(i, 0, leftTemp);
        if (!opt.insRight)  for (int i = 0; i < ROWS; ++i) sim.setBoundary(i, COLS - 1, rightTemp);

        for (const auto& s : sources) {
            sim.addPointSource(s.r, s.c, s.t);
        }
        if (!sources.empty()) {
            std::cout << "Heat sources: " << sources.size() << "\n";
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
        exportToJSON(jsonPath, finalStep, ROWS, COLS, sim.getGrid(),
                     DT, SAVE_INTERVAL, methodName, F_TARGET);
    }

    std::cout << "Simulation complete. Output written to " << jsonPath << "\n";
    return 0;
}
