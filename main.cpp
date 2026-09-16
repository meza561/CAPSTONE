#include <iostream>
#include <vector>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <string>
#include <utility>
#include <algorithm>
#include <sstream>
#include "database.hpp"
#include "simulation.hpp"
#include "reaction.hpp"

void exportToJSON(const std::string& filename, int step, int rows, int cols,
                  const std::vector<std::vector<double>>& grid,
                  double dt, int saveInterval,
                  const std::string& scheme, double diffusionNumber) {
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
};

/**
 * Pull `--key=value` options out of argv and return the remaining positional
 * arguments. Keeping these flag-style avoids adding more positional slots to
 * an already long fixed layout, and leaves the existing ordering untouched.
 *
 *   --insulate=tblr                      edges held at zero flux
 *   --material=r0,c0,r1,c1,alpha         repeatable; a rectangle of diffusivity
 */
std::vector<std::string> extractOptions(int argc, char* argv[], Options& opt) {
    std::vector<std::string> positional;
    for (int i = 0; i < argc; ++i) {
        const std::string a = argv[i];
        if (a.rfind("--insulate=", 0) == 0) {
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
        for (int k = 10; k + 2 < argc; k += 3) {
            sources.push_back({ std::stoi(argv[k]),
                                std::stoi(argv[k + 1]),
                                std::stod(argv[k + 2]) });
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

    if (method == HeatSimulation::Method::Explicit && F_TARGET > 0.25) {
        std::cout << "Warning: F = " << F_TARGET
                  << " exceeds the explicit stability limit of 0.25; "
                     "the solution will diverge.\n";
    }

    DT = F_TARGET * DX * DX / ALPHA;
    const bool isReactionDiffusion = (mode == "fisher" || mode == "gray-scott");
    if (!isReactionDiffusion) {
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
        if (!isReactionDiffusion) {   // RD sets its own frame budget below
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
    HeatDatabase db("heat_sim.db");

    if (!db.init()) return 1;

    if (mode == "fisher" || mode == "gray-scott") {
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

        exportToJSON("latest_heatmap.json", rdSteps, ROWS, COLS, rd.field(),
                     rdDt, saveEvery, rdName, F_TARGET);
        std::cout << "Simulation complete. Output written to latest_heatmap.json\n";
        return 0;

    } else if (mode == "pde") {
        std::cout << "Running Analytical PDE Solver...\n";
        sim.solveAnalytical(topTemp, bottomTemp, leftTemp, rightTemp);
        db.beginRun();
        db.saveTimestep(0, sim.getGrid());
        db.endRun();
        exportToJSON("latest_heatmap.json", 0, ROWS, COLS, sim.getGrid(), DT, 1,
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
        exportToJSON("latest_heatmap.json", finalStep, ROWS, COLS, sim.getGrid(),
                     DT, SAVE_INTERVAL, methodName, F_TARGET);
    }

    std::cout << "Simulation complete. Output written to latest_heatmap.json\n";
    return 0;
}
