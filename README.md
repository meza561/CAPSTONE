# Heat Simulation Web App

A self-sufficient web application for simulating the 2D Heat Equation using the
Finite Difference Method (FDM) and the analytical solution of Laplace's
equation.

## Architecture
- **Backend**: C++ simulation engine for high-performance computation, wrapped in a Python Flask API.
- **Frontend**: Dynamic HTML5/JS visualization with a heatmap renderer and simulation history.
- **Data**: SQLite stores the timesteps of the current run; JSON carries results to the frontend; LocalStorage keeps the run history.

## Getting Started

### Prerequisites
- GCC/Clang (C++17)
- Python 3.x
- SQLite3
- CMake *(optional — `start.sh` finds CLion's bundled copy, and falls back to compiling directly if no CMake is installed)*

### Quick Start
```bash
./start.sh
```
This script will:
1. Compile the C++ simulation binary.
2. Create/reuse the virtual environment and install dependencies.
3. Start the Flask web server and open your browser.

It stops any server it previously started, and refuses to start if port 5000 is
already held by something else (on macOS this is often AirPlay Receiver).

## Simulation Modes

### Initial Conditions (FDM)
Uses the Finite Difference Method to iteratively solve the heat equation. The
boundaries are held fixed (Dirichlet), so the interior evolves in time toward
steady state.

**Heat sources.** Any number of cells (up to 32) can be pinned at a fixed
temperature, acting as persistent heat origins. Each is given as $(x, y, C)$ with
the origin at the **bottom-left**: $x$ increases to the right, $y$ increases
upward. The API converts $(x, y)$ into the solver's `[row][col]` indexing, so
$y = 0$ is the bottom row. Sources apply to FDM only; the analytical solver works
from the boundary temperatures alone.

### PDE-driven (Analytical)
Computes the exact steady-state solution of Laplace's equation on a rectangle
by **superposing four single-edge Fourier series**, one per boundary. Each term
uses the standard form

$$u(x,y) = \sum_{n \text{ odd}} \frac{4T}{n\pi} \sin\!\left(\frac{n\pi x}{L}\right)
\frac{\sinh(n\pi y / L)}{\sinh(n\pi W / L)}$$

with the hyperbolic ratio evaluated in exponential form so that high harmonics
do not overflow. Superposition is what makes arbitrary boundary combinations
correct; summing a single edge and blending toward an average does not solve
Laplace's equation.

## Temporal Scaling

The explicit FDM scheme is stable only while the diffusion number

$$F = \frac{\alpha \, \Delta t}{\Delta x^2} \le 0.25$$

so $\Delta t$ is **derived from that limit** ($F = 0.2$) rather than fixed:

$$\Delta t = \frac{0.2 \, \Delta x^2}{\alpha}$$

Real elapsed time is $t = \text{step} \times \Delta t$. The solver reports
`dt` and `saveInterval` in its JSON output, and the time slider spans
$t = 0$ to the run's actual convergence time in real seconds. Because
$\Delta t$ scales inversely with $\alpha$, a lower diffusivity correctly takes
*longer* in physical time: $\alpha = 0.01$ spans ~15,300 s while
$\alpha = 1.0$ spans ~153 s.

### Storage
Timesteps are saved every `saveInterval` steps (matching the slider's
resolution) and each run clears the previous one, so the database reflects only
the current run. The interval widens automatically on large grids to bound the
file size.

## Validation

### Convergence and stability study

```bash
./run_study.sh
```

Builds `heat_study`, which links the same `simulation.hpp` the application uses,
runs four studies, and writes `study_results.json` plus a CSV per study. If
matplotlib is installed (`pip install -r requirements-study.txt`) it also renders
report figures into `figures/`. The results are served at `/study` and displayed
in the app's **Validation** tab.

**Spatial convergence.** The FDM steady state is compared against the exact
solution under grid refinement (N = 11 to 161). Two boundary conditions are run,
and the difference between them is the point:

| Boundary data | Measured order (L2) |
|---|---|
| Manufactured, smooth at the corners | **2.03** |
| Constant top edge, discontinuous corners | **1.04** |

Smooth data recovers the theoretical O(dx^2). With a constant hot edge against
cold sides the exact solution is singular at the two top corners, so the
attainable order is capped near 1 and the max-norm error does not decrease at
all, however fine the grid. Reporting both is the honest result.

**Temporal convergence.** At a fixed physical time on a fixed grid, against a
reference run with a 256x smaller step: measured order **1.02**, matching
forward Euler's O(dt).

**Stability.** Sweeping the diffusion number across the theoretical limit:

| F | Result |
|---|---|
| 0.05, 0.15, 0.20, 0.25 | bounded at the boundary maximum |
| 0.26 | diverges |
| 0.30, 0.50 | diverges (F = 0.5 passes 1e12 within 30 steps) |

The transition sits exactly at F = 1/4, which is why the timestep is derived
from alpha rather than fixed.

### Exact-value checks

At the center of a square plate the steady-state temperature equals the mean of
the four edge temperatures. Both solvers reproduce this:

| Boundaries (T/B/L/R) | Analytical | FDM steady state | Exact |
|---|---|---|---|
| 100 / 0 / 0 / 0 | 25.0000 | 24.9990 | 25 |
| 100 / 100 / 0 / 0 | 50.0000 | 49.9990 | 50 |
| 50 / 25 / 75 / 10 | 40.0000 | 39.9992 | 40 |

The analytical solver matches to machine precision; FDM converges to within
1e-3 of it, which cross-validates the two independent methods.

## API Endpoints
- `GET /`: Serves the frontend.
- `POST /run`: Executes a simulation.
  - **Payload**: `{ "rows": 20, "cols": 20, "top": 100, "bottom": 0, "left": 0, "right": 0, "mode": "fdm", "alpha": 0.01 }`
    - optional heat sources: `"hasPointSource": true, "sources": [{"x": 50, "y": 50, "temp": 1000}, {"x": 20, "y": 80, "temp": 500}]`
      (coordinates are bottom-left origin; out-of-range values are clamped to the grid.
      The older single-source form `psR`/`psC`/`psTemp` is still accepted.)
    - grid dimensions, temperatures and alpha are clamped to sane ranges
  - **Response**: `{ "status", "output", "data": { "step", "rows", "cols", "dt", "saveInterval", "data" } }`
  - On failure, `message` carries the solver's actual stderr.
- `GET /run?time=X`: Retrieves the stored state at timestep X, snapping down to the nearest saved frame.
- `GET /study`: Returns the convergence/stability results, or 404 if `./run_study.sh` has not been run.

The study also checks rotational symmetry (exact to machine precision) and the
discrete maximum principle (no interior extremum).

## Project Structure
- `simulation.hpp`: the solver itself (FDM step + analytical solution), shared by the app and the study.
- `main.cpp`: CLI and JSON export for the web application.
- `database.cpp` / `database.hpp`: SQLite persistence for timesteps.
- `study.cpp`: convergence and stability study.
- `server.py`: Flask API server.
- `web/`: Frontend assets (HTML, CSS, JS; `validation.js` renders the study charts).
- `make_figures.py`: renders the report figures from the study output.
- `start.sh`: Unified launch script.
- `run_study.sh`: builds and runs the study, then renders figures.
