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

**Point Source Support:** enabling a "Point Source" holds a chosen cell $(X, Y)$
at a constant temperature, acting as a persistent heat origin.

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
    - optional: `"hasPointSource": true, "psR": 10, "psC": 10, "psTemp": 250`
    - grid dimensions, temperatures and alpha are clamped to sane ranges
  - **Response**: `{ "status", "output", "data": { "step", "rows", "cols", "dt", "saveInterval", "data" } }`
  - On failure, `message` carries the solver's actual stderr.
- `GET /run?time=X`: Retrieves the stored state at timestep X, snapping down to the nearest saved frame.

## Project Structure
- `main.cpp`: C++ simulation logic (FDM + analytical).
- `database.cpp` / `database.hpp`: SQLite persistence for timesteps.
- `server.py`: Flask API server.
- `web/`: Frontend assets (HTML, CSS, JS).
- `start.sh`: Unified launch script.
