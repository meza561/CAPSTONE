# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Commands

```bash
./start.sh          # build heat_sim, set up the venv, launch Flask on :5000, open browser
./run_tests.sh       # build + run heat_tests (36 physics assertions), exits non-zero on failure
./run_study.sh        # build + run heat_study (convergence/stability/scaling), render figures/ via make_figures.py
./run_ui_tests.sh     # build heat_sim, start a server, run the Playwright suite headless, stop the server
.venv/bin/python -m pytest tests/api -q  # Flask API tests: no server, no binary, ~0.03s
```

The scripts create `.venv` (they accept a pre-existing `venv` too, but `.venv` is
what they make), and they install `requirements.txt` only. The other two
requirement files are opt-in, one-time installs kept out of the runtime set so
the Docker image ships neither: `.venv/bin/pip install -r requirements-test.txt`
for pytest, which `tests/api` needs and no script installs for you, and
`requirements-study.txt` for matplotlib, which `run_study.sh` prompts for when
the figures step is skipped. CI installs the test one explicitly.

All four shell scripts prefer CMake (`build/` dir) but fall back to compiling the relevant `.cpp` directly with `clang++`/`g++` if no CMake is found (they search `PATH` then CLion's bundled copy). After a CMake build, the binary is copied over the top-level `./heat_sim` / `./heat_study` (atomically, via rename) because `server.py` and ad-hoc runs invoke it from the project root — and it's re-signed with `codesign --force --sign -` since macOS SIGKILLs a binary whose ad-hoc signature doesn't match its inode after an in-place copy.

`run_ui_tests.sh` binds port 5000 like `start.sh` does, and refuses to start if something else holds it; `PORT=5577 ./run_ui_tests.sh` runs against another port instead of fighting a dev instance (`server.py` reads `PORT`, defaulting to 5000).

There is no separate lint step. The C++ test binary isn't structured for filtering by name, so to run a single physics assertion it's easiest to isolate it in `tests.cpp` and re-run `./run_tests.sh`; the other two suites filter natively (`pytest -k <substring>`, `npx playwright test <file> -g <title>` from `tests/ui/`).

To invoke the simulator directly instead of through the API:
```bash
./heat_sim ROWS COLS top bottom left right [mode] [alpha] [F] [row col temp]...
# mode: fdm (explicit, default) | be (backward Euler) | cn (Crank-Nicolson) | pde (analytical) | fisher | gray-scott
# optional flags (anywhere in argv): --insulate=tblr   --material=r0,c0,r1,c1,alpha (repeatable)
```
Fisher-KPP/Gray-Scott take their own trailing params instead of boundary temps/sources — see `main.cpp`'s argv parsing for the exact positions.

## Architecture

**Three-tier, single-writer.** `server.py` (Flask) shells out to the compiled `./heat_sim` binary per request (`subprocess.run`, 300s timeout) rather than linking it as a library. A single `SIM_LOCK` serializes all runs because the binary always writes to the same `heat_sim.db` and `latest_heatmap.json` — two concurrent requests would interleave writes to both. The lock is held across the read of `latest_heatmap.json` too, so a second request can't swap the file out from under the first's response.

**Coordinate flip happens once, at the API boundary.** The UI and API use bottom-left-origin (x right, y up); the solver uses `[row][col]` with row 0 at the top. `server.py`'s `/run` handler is the only place this conversion occurs (for point sources and material rectangles) — `main.cpp` and `simulation.hpp` are unaware of the UI's coordinate convention.

**`simulation.hpp` is the shared solver core**, linked into `heat_sim` (the app), `heat_study` (validation/scaling), and `heat_tests` (regression suite) — all three targets in `CMakeLists.txt` compile it directly rather than linking a shared library, so a change there affects all three binaries and should be re-validated with `./run_tests.sh` and usually `./run_study.sh`. `reaction.hpp` (Fisher-KPP / Gray-Scott) is a parallel, separate model sharing only the five-point Laplacian idea, not code, with the heat solver.

**Time integration.** Explicit (forward Euler) is conditionally stable (F = alpha·dt/dx² ≤ 0.25); Backward Euler and Crank-Nicolson use ADI (alternating-direction-implicit) splitting — Douglas-Rachford and Peaceman-Rachford respectively — reducing each 2D step to one tridiagonal (Thomas algorithm) solve per grid line, and are unconditionally stable so `F` becomes a free UI parameter for them. `dt` is always derived from `F` and `alpha`, never fixed, because a fixed dt at the default alpha under-resolved the approach to steady state (see `main.cpp` comments). Dirichlet boundaries and pinned point sources enter the implicit systems as identity rows.

**Heterogeneous media** use divergence-form diffusion (`∇·(α∇u)`, not `α∇²u`) with harmonic-mean conductivity at cell faces — required to get flux right across a material discontinuity; this is load-bearing for the composite-slab regression test.

**Persistence contract:** `database.cpp`/`.hpp` wraps SQLite; each run clears the previous one (`beginRun`/`endRun`), so the DB always reflects only the latest simulation. Timesteps are saved every `SAVE_INTERVAL` steps, and the save interval widens automatically on large grids to bound file size under a fixed sample budget — the frontend's `/frames` endpoint lists the actually-stored step numbers so the time slider addresses real frames instead of guessing from the interval, and `/run?time=X` snaps down to the nearest stored frame.

**Parallelism:** step loops use `#pragma omp` with `schedule(runtime)` (default `static`; compare via `OMP_SCHEDULE=guided ./run_study.sh`). OpenMP is optional and compiles away cleanly when absent (Apple's clang doesn't ship it — `CMakeLists.txt` has custom logic to locate Homebrew's `libomp` on macOS since CMake can't find it unaided). Parallel and serial runs must stay bit-identical — rows of the explicit update and each ADI grid line are independent, so no synchronization is needed; preserve this property when touching the step loops.

**Validation is data-driven, not just pass/fail:** `heat_study` writes `study_results.json` + per-study CSVs, which `server.py`'s `/study` endpoint serves and `web/validation.js` renders in the app's Validation tab. `make_figures.py` renders the same data into `figures/*.png`/`.pdf` for the README. If you change solver numerics, re-run `./run_study.sh` and check whether the measured orders/thresholds in `README.md`'s tables still hold.

## Tests

Three suites, three CI jobs (`.github/workflows/ci.yml`), each covering a different layer:

- **`tests.cpp` → `heat_tests`** — the physics. Every assertion is a value theory fixes independently of the code (exact analytical temperatures, convergence orders, the F = 1/4 threshold, conservation laws), so it's the suite to re-run after any change to `simulation.hpp`/`reaction.hpp`.
- **`tests/api/` (pytest)** — the Flask layer in isolation: clamping, `/frames`, `GET /run?time=`'s snap-to-nearest-earlier-frame, and `POST /run`'s error branches with `subprocess.run` mocked. Needs no server and no compiled binary. The fixture `chdir`s into a tmp dir rather than monkeypatching `DB_NAME`, since `server.py` addresses `heat_sim.db` and `latest_heatmap.json` relative to the working directory — keep it that way, or tests start writing to the real database.
- **`tests/ui/` (Playwright, Node)** — the frontend end to end against a real server: each simulation mode, the timeline indexing real stored frames, the legend gradient matching what the canvas paints, hover mapping on a non-square grid, history round-tripping, console errors, and narrow-viewport overflow.

Two constraints in the UI suite that are easy to undo by accident:

- **`workers: 1` is deliberate.** The app is single-writer (one DB file, one `latest_heatmap.json`), so parallel workers race each other's runs — a GET following one test's POST can land after another test's run has already cleared the table.
- **Assertions compare the app's own outputs to each other, not to a reimplementation.** The legend test samples the canvas bitmap and the rendered CSS gradient and checks they agree; duplicating `heatColor()` in the test would just be a second copy free to drift the same way the old hardcoded gradient did.

`study_results.json` is a gitignored `./run_study.sh` output that CI never has, so `responsive.spec.js` serves `tests/ui/fixtures/study_results.json` through `page.route` to get the Validation tab's charts rendered at all (measuring before they load is how a 76px overflow went unnoticed). That fixture needs its `.gitignore` negation to stay in place — the root `study_results.json` pattern is unanchored and otherwise matches it too. `console-errors.spec.js` deliberately doesn't mock it: it waits for whichever state the real server produces, so the "not generated yet" path stays covered.

## Project structure

See `README.md`'s "Project Structure" section and its "API Endpoints" section for the `/run` payload shape — both are current and detailed; don't duplicate them here.
