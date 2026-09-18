# Heat Simulation Web App

A self-sufficient web application for simulating the 2D Heat Equation using the
Finite Difference Method (FDM) and the analytical solution of Laplace's
equation.

## Architecture
- **Backend**: C++ simulation engine for high-performance computation, wrapped in a Python Flask API.
- **Frontend**: Dynamic HTML5/JS visualization with a heatmap renderer and simulation history. Hovering the heatmap reports the temperature and (x, y) under the cursor.
- **Data**: SQLite stores the timesteps of the current run; JSON carries results to the frontend; LocalStorage keeps the run history.

## Getting Started

### Prerequisites
- GCC/Clang (C++17)
- Python 3.x
- SQLite3
- OpenMP *(optional — enables parallel step loops; `brew install libomp` on macOS)*
- Node.js *(optional — only to run the frontend end-to-end tests)*
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

### Time-marching schemes (FDM)
The boundaries are held fixed (Dirichlet), so the interior evolves in time
toward steady state. Three time integrators are available:

| Scheme | Accuracy | Stability | Cost per step |
|---|---|---|---|
| Explicit (forward Euler) | O(dt) | only for F <= 1/4 | cheapest |
| Backward Euler (ADI) | O(dt) | unconditional | two tridiagonal sweeps |
| Crank-Nicolson (ADI) | **O(dt^2)** | unconditional | two tridiagonal sweeps |

Both implicit schemes use **alternating direction implicit** splitting, so a 2D
step reduces to one tridiagonal system per grid line, solved with the Thomas
algorithm in O(rows x cols) rather than requiring a full 2D matrix solve.
Backward Euler uses the Douglas-Rachford split, Crank-Nicolson the
Peaceman-Rachford split. Dirichlet boundaries and pinned interior heat sources
enter the systems as identity rows, which is what holds a source fixed through
an implicit solve.

Because the implicit schemes are unconditionally stable, F is a free parameter
for them and is exposed in the UI: values of 50 or 500 remain bounded where the
explicit scheme diverges above 0.25.

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

### Wave equation (leapfrog)
A different PDE rather than another integrator for the same one: the wave
equation is **second order in time**, so it oscillates instead of relaxing to a
steady state.

$$u_{tt} = c^2 \left( u_{xx} + u_{yy} \right)$$

Time stepping is the central difference (leapfrog)

$$u^{n+1} = 2u^n - u^{n-1} + \nu^2 \nabla_h^2 u^n, \qquad \nu = \frac{c \, \Delta t}{\Delta x}$$

which reads **two** time levels back, so the solver keeps the previous and
current displacement grids explicitly - the structural difference from the heat
and reaction-diffusion classes, which need only one. The scheme is second-order
in space and time and non-dissipative: it conserves a discrete energy rather
than damping, so a clamped membrane rings indefinitely.

Starting it needs one special step, since $u^{-1}$ does not exist. Released
from rest, a Taylor expansion about $t = 0$ gives

$$u^1 = u^0 + \tfrac{1}{2} \nu^2 \nabla_h^2 u^0$$

The factor of $\tfrac{1}{2}$ is load-bearing: a full leapfrog step at $t = 0$
injects twice the correct initial acceleration and quietly drops the scheme to
first order. The test suite pins this by measuring the observed order, which
comes out at 2.00.

| Property | Value |
|---|---|
| Stability | CFL: $\nu = c\,\Delta t/\Delta x \le 1/\sqrt{2}$ |
| Accuracy | O($\Delta t^2$), O($\Delta x^2$) |
| Edges | fixed (clamped, inverted reflection) or free (Neumann, same-sign) |
| Initial condition | centred raised-cosine pluck, or a single-cell impulse |

The $1/\sqrt{2}$ limit is **stricter than the 1D value of 1**, because the
five-point Laplacian's largest eigenvalue grows with dimension; the solver
refuses to step above it rather than filling the database with overflow. As
with $\alpha$ for heat, $\Delta t$ is derived from the stability limit
($\nu = 0.5$), so $c$ sets the physical time the run spans rather than the
distance travelled per frame.

Validation is against the analytical standing mode of a clamped square,

$$u(x,y,t) = \sin\!\left(\frac{m\pi x}{L_x}\right)\sin\!\left(\frac{n\pi y}{L_y}\right)\cos(\omega t),
\qquad \omega = c\pi\sqrt{(m/L_x)^2 + (n/L_y)^2}$$

with $m = n = 1$, where the domain centre is the antinode and the centre value
is exactly $\cos(\omega t)$. The suite tracks it over two full periods and
holds the error below 2e-3.

Amplitude is **signed**, unlike temperature or concentration, so the UI paints
wave runs with a diverging blue-white-red map on a scale held symmetric about
zero - white is zero displacement - and the solver reports the range it spanned
over the whole run in its JSON so the colours do not rescale when the timeline
is scrubbed back toward the louder opening frames.

## Temporal Scaling

The explicit FDM scheme is stable only while the diffusion number

$$F = \frac{\alpha \, \Delta t}{\Delta x^2} \le 0.25$$

so $\Delta t$ is **derived from that limit** ($F = 0.2$) rather than fixed:

$$\Delta t = \frac{0.2 \, \Delta x^2}{\alpha}$$

Real elapsed time is $t = \text{step} \times \Delta t$. The solver reports
`dt` and `saveInterval` in its JSON output, and the time slider steps through
the frames that were actually stored, from $t = 0$ to the run's convergence
time. Because
$\Delta t$ scales inversely with $\alpha$, a lower diffusivity correctly takes
*longer* in physical time: $\alpha = 0.01$ spans ~15,300 s while
$\alpha = 1.0$ spans ~153 s.

### Storage
Timesteps are saved every `saveInterval` steps (matching the slider's
resolution) and each run clears the previous one, so the database reflects only
the current run. The interval widens automatically on large grids to bound the
file size.

## Heterogeneous Media and Insulated Edges

Diffusivity can vary per cell, so the governing equation is the divergence form

$$u_t = \nabla \cdot (\alpha(x,y) \nabla u)$$

not $\alpha \nabla^2 u$ - the two are only equivalent when $\alpha$ is
constant, and the second gets the flux wrong across a material interface.
Conductivity at a cell face is the **harmonic mean** of the two adjacent cells,
which is the standard treatment for a discontinuous coefficient: it reproduces
series resistance exactly, where an arithmetic mean does not. The test suite
checks a two-layer composite against the closed-form series-resistance profile
and matches it to ~1e-9.

Each edge can independently be Dirichlet (held at a temperature) or **zero-flux
/ insulated** (Neumann). An insulated edge is not pinned: its cells are unknowns
like any other, and the absent outward neighbour simply contributes no flux.
With every edge insulated, total energy is conserved to machine precision -
another exact check in the suite.

Both features work with all three time schemes; the implicit ADI sweeps carry
the variable face conductivities into their tridiagonal coefficients.

## Reaction-Diffusion

The same five-point Laplacian with a reaction term added becomes a
pattern-formation model - the link between heat conduction and morphogenesis.
Edges are zero-flux (Neumann) rather than Dirichlet, so patterns are not
suppressed at the boundary, and the fields seed themselves.

### Fisher-KPP

$$u_t = D \nabla^2 u + r\,u(1-u)$$

One species: diffusion plus logistic growth, producing a travelling population
front. Its asymptotic speed is exactly $c^* = 2\sqrt{Dr}$, which makes it a
validation case rather than just a picture - see the table below.

The front is only $\sqrt{D/r}$ wide, so the solver picks $\Delta x$ at a
quarter of that; at $\Delta x = 1$ the front is thinner than one cell and the
discrete wave does not travel at the continuum speed.

### Gray-Scott

$$u_t = D_u \nabla^2 u - uv^2 + f(1-u), \qquad
  v_t = D_v \nabla^2 v + uv^2 - (f+k)v$$

Two species with unequal diffusivities - the Turing mechanism. The display
shows the activator $v$. Named regimes are provided as presets (spots, stripes,
mazes, coral, solitons, bubbles) because most $(f, k)$ pairs give a blank field;
editing either value switches the dropdown to Custom.

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

**Temporal convergence.** At a fixed physical time on a fixed grid, each scheme
measured against a reference computed with its own discretisation at a 256x
smaller step:

| Scheme | Measured order | Theory |
|---|---|---|
| Explicit | 1.02 | 1 |
| Backward Euler | 1.02 | 1 |
| Crank-Nicolson | **2.00** | 2 |

At equal dt, Crank-Nicolson's error is roughly 2000x smaller than either
first-order scheme.

**Cost to steady state** (41x41 grid, all converging to the same exact centre
temperature of 25 C):

| Scheme | F | Steps |
|---|---|---|
| Explicit | 0.2 | 6,529 |
| Backward Euler | 5 | 324 |
| Crank-Nicolson | 5 | 315 |

Roughly 20x fewer steps. Each implicit step costs more, so wall-clock times are
comparable at this grid size; the step-count advantage grows with the grid,
since the explicit limit forces dt down as dx^2.

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

## Testing

Three suites, one per layer, all three run in CI on every push
(`.github/workflows/ci.yml`):

```bash
./run_tests.sh                            # physics (C++)
venv/bin/python -m pytest tests/api -q    # Flask API (pytest, ~0.03s)
./run_ui_tests.sh                         # frontend end to end (Playwright)
```

### Physics

36 assertions covering every value theory fixes independently of the code:
exact analytical temperatures, the mean-value property, rotational symmetry,
the discrete maximum principle, spatial and temporal convergence orders, the
F = 1/4 stability threshold, unconditional stability of the implicit schemes at
F = 500, series resistance through a composite slab, energy conservation in a
sealed domain, the Fisher-KPP wave speed, and Gray-Scott pattern formation with
a dead-regime negative control. Exits non-zero on failure.

This is cheap insurance: two silent correctness bugs got through during
development - an analytical solver that was wrong for every configuration
except a single hot edge, and a convergence test that could never fire when a
heat source was present. Both would have been caught by these assertions.

### API

```bash
venv/bin/pip install -r requirements-test.txt   # once
venv/bin/python -m pytest tests/api -q
```

36 tests over the Flask layer in isolation, using Flask's test client: the
clamping of client-supplied values, `/frames`, `GET /run?time=` snapping down
to the nearest stored frame, and `POST /run`'s error branches with
`subprocess.run` mocked - a solver that times out, one that is missing, one
that exits non-zero. It also pins the argv the API builds, including the
bottom-left `(x, y)` to `[row][col]` flip. No server and no compiled binary,
so the whole suite runs in hundredths of a second.

### Frontend

```bash
./run_ui_tests.sh          # PORT=5577 ./run_ui_tests.sh to avoid a running dev server
```

Builds `heat_sim`, starts a server, runs 14 Playwright tests against it
headless in Chromium, and stops the server again. Covers every simulation
mode, the time slider indexing frames that actually exist (via `/frames`), the
legend gradient agreeing with what the canvas paints, the hover readout
landing on the right cell of a non-square grid, history round-tripping
materials and insulated edges, a full six-mode run with no console errors, and
horizontal overflow at phone width.

The last of those has already earned its place: it caught a `box-sizing` bug
that made the History and Validation panels 66px wider than their container -
invisible in the slack of a desktop layout, and a broken page at 420px.

## Parallel Scaling

The step loops are parallelised with OpenMP: rows of the explicit update write
only their own row, and each ADI grid line is an independent tridiagonal solve,
so neither needs synchronisation. Serial and parallel runs produce
**bit-identical** output.

OpenMP is optional - the pragmas compile away without it, which matters because
Apple's clang does not ship it. To enable it on macOS:

```bash
brew install libomp
```

Measured on an Apple Silicon Mac with 10 hardware threads (`./run_study.sh`
detects the count for whatever machine it runs on). The figures below are one
sweep taken under `OMP_SCHEDULE=guided`; the default `static` schedule landed
within about ten percent of them at every point compared, which is the same
order as the run-to-run variation (see the scheduling note at the end):

| Grid | Scheme | 2 | 4 | 8 | 10 threads |
|---|---|---|---|---|---|
| 256 | explicit | 1.44x | 1.89x | 1.26x | 1.07x |
| 256 | Crank-Nicolson | 1.84x | 3.09x | 2.47x | 2.00x |
| 512 | explicit | 1.76x | 2.76x | 2.78x | 2.78x |
| 512 | Crank-Nicolson | 1.97x | 3.75x | 4.61x | **4.70x** |

Three things in that table are worth more than the headline number.

**Speedup peaks at four threads** and does not recover: both 256 cases fall
back sharply after it, the 512 explicit case plateaus, and only Crank-Nicolson
at 512 keeps climbing all the way to 10 threads. Two effects combine. The
machine has heterogeneous cores - performance and efficiency - so once the
thread count exceeds the performance-core count, evenly divided work lands on
slower cores and the fast ones wait at the barrier. On top of that a
five-point stencil moves a lot of memory per arithmetic operation, so
bandwidth saturates early. More threads stop helping well before the core
count is exhausted.

**Crank-Nicolson scales better than the explicit scheme** - 4.70x against
2.78x at 512 with 10 threads. The implicit step does more arithmetic per byte
moved (a tridiagonal solve rather than a single stencil pass), so it is further
from the bandwidth limit and has more headroom to parallelise. The cheaper
kernel is the harder one to speed up, which is the opposite of the intuition
that cheap work parallelises well.

**Bigger grids scale better** - 512 beats 256 everywhere - because there is more
work per thread to amortise the fork/join overhead. At 256 with 10 threads the
explicit scheme manages only 1.07x: each thread gets so little work that the
per-step barrier costs about as much as the work itself.

The cleanest single result is the 512 explicit row. It measures 0.0156, 0.0155
and 0.0155 seconds at 4, 8 and 10 threads - **identical to three digits**. Once
memory bandwidth is saturated, additional cores do not merely help less, they
do nothing at all. The implicit scheme keeps climbing past that point because
it performs more arithmetic per byte moved.

The loops use `schedule(runtime)`, so scheduling can be compared without
recompiling:

```bash
OMP_SCHEDULE=guided ./run_study.sh      # or dynamic,4 - default is static
```

Measured, `guided` does **not** help here. Against `static` it was within noise
on the 512 grid (4.61x vs 4.25x at 8 threads) and slightly worse on 256 (2.47x
vs 2.68x). The plausible reason a dynamic schedule would help - fast cores
taking extra chunks instead of idling at the barrier - is not what limits this
workload; memory bandwidth is. Output is bit-identical under every schedule, so
the comparison is safe to run.

Run-to-run variation is a few percent, so take a median of several runs before
quoting any figure.

## Deployment

This needs a host that can **run a process and write files**: every simulation
spawns the compiled solver and writes a per-run SQLite database. Cloudflare
Pages and Workers cannot do that - point them at this app and the page loads
while every Run returns 404. Use a container host with Cloudflare in front for
TLS and CDN.

```bash
# Any container host; Fly.io is the shortest path from Dockerfile to a domain.
fly launch --no-deploy --name heat-sim
fly secrets set SITE_URL=https://noahmeza.com \
                ALLOWED_ORIGIN=https://noahmeza.com \
                ANALYTICS_SITE=noahmeza          # GoatCounter site code
fly deploy
fly certs add noahmeza.com     # then point the Cloudflare DNS record here
```

Cloudflare in front of it: proxy the record (orange cloud) and set SSL/TLS mode
to **Full (strict)**, so the hop from Cloudflare to the origin is encrypted too.
Flexible mode leaves that hop in plaintext while the browser still shows a lock.

Or without a container, on any box that has Python and a C++ compiler:

```bash
cp .env.example .env          # then edit
FLASK_ENV=production SITE_URL=https://noahmeza.com \
  gunicorn -w 1 --threads 4 -b 0.0.0.0:5000 wsgi:app
```

**One worker, deliberately.** The solver writes to a single `heat_sim.db` and a
single `latest_heatmap.json`, and what stops two simultaneous runs interleaving
is `SIM_LOCK` — a lock inside one process. A second worker brings its own lock
and corrupts both files. Threads are fine, since `SIM_LOCK` serialises the
simulations while frame reads stay responsive. Scaling past one process means
moving that shared state out of the filesystem first.

`FLASK_ENV=production` turns on forced HTTPS, HSTS and the `/run` rate limit;
anything else leaves them off so `http://localhost` works. Behind a
TLS-terminating proxy the app trusts one hop of `X-Forwarded-*`. Environment
variables are documented in `.env.example`.

| Route | Purpose |
|---|---|
| `/robots.txt` | Allows the app, excludes the API routes, points at the sitemap. |
| `/sitemap.xml` | The one indexable page, at `SITE_URL`. |
| `/og-image.png` | 1200x630 social card, regenerate with `make_og_image.py`. |
| `/privacy.html`, `/terms.html` | Static legal pages, linked from the footer. |

The Validation tab needs `study_results.json`, which `./run_study.sh` generates
and which is not in the image; without it that tab shows a "not generated yet"
message and everything else works.

## API Endpoints
- `GET /`: Serves the frontend.
- `POST /run`: Executes a simulation.
  - **Payload**: `{ "rows": 20, "cols": 20, "top": 100, "bottom": 0, "left": 0, "right": 0, "mode": "fdm", "alpha": 0.01 }`
    - `mode`: `fdm` (explicit), `be` (backward Euler), `cn` (Crank-Nicolson), `pde` (analytical), `fisher`, `gray-scott`, `wave`
    - `insulated`: `{"top": false, "bottom": true, ...}` - zero-flux edges
    - `materials`: `[{"x0": 0, "y0": 28, "x1": 60, "y1": 32, "alpha": 0.0005}]` - diffusivity rectangles in bottom-left coordinates
    - reaction-diffusion: `rdSteps`, plus `rdD`/`rdR` for Fisher-KPP or `Du`/`Dv`/`feed`/`kill` for Gray-Scott
    - wave: `waveC` (speed), `waveBoundary` (`fixed`/`free`), `waveSteps`, `waveIC` (`pluck`/`impulse`)
    - `F`: diffusion number, implicit schemes only; explicit is pinned at 0.2
    - optional heat sources: `"hasPointSource": true, "sources": [{"x": 50, "y": 50, "temp": 1000}, {"x": 20, "y": 80, "temp": 500}]`
      (coordinates are bottom-left origin; out-of-range values are clamped to the grid.
      The older single-source form `psR`/`psC`/`psTemp` is still accepted.)
    - grid dimensions, temperatures and alpha are clamped to sane ranges
  - **Response**: `{ "status", "output", "data": { "step", "rows", "cols", "dt", "saveInterval", "min", "max", "data" } }`
    (`min`/`max` are the range the field spanned over the run, so a signed field can be scaled without assuming 0-100)
  - On failure, `message` carries the solver's actual stderr.
- `GET /run?time=X`: Retrieves the stored state at timestep X, snapping down to the nearest saved frame.
- `GET /frames`: Lists the step numbers stored for the current run, so the timeline can address exact frames instead of guessing them from the save interval.
- `GET /study`: Returns the convergence/stability results, or `{"status": "missing", "message": ...}` (still 200) if `./run_study.sh` has not been run.

**Fisher-KPP front speed.** Measured against the exact $c^* = 2\sqrt{Dr}$ for
$D = 0.2$, $r = 1$ (so $c^* = 0.89443$), over successive time windows:

| Window | Measured c | Relative error |
|---|---|---|
| [10, 20] | 0.83488 | 6.66% |
| [20, 40] | 0.86375 | 3.43% |
| [40, 80] | 0.87625 | 2.03% |
| [80, 160] | 0.88196 | 1.39% |
| [160, 320] | **0.88470** | **1.09%** |

The speed converges monotonically toward $c^*$ **from below**, which is the
expected behaviour: Bramson's result gives $c(t) \sim c^* - 3/(2\lambda t)$
with $\lambda = \sqrt{r/D}$, an algebraic $1/t$ correction, so any finite-time
measurement sits below $c^*$ and closes slowly.

The study also checks rotational symmetry (exact to machine precision) and the
discrete maximum principle (no interior extremum).

## Project Structure
- `simulation.hpp`: the heat solver (explicit, backward Euler, Crank-Nicolson, analytical), shared by the app and the study.
- `reaction.hpp`: reaction-diffusion (Fisher-KPP, Gray-Scott).
- `tests.cpp`: physics regression tests.
- `run_tests.sh`: builds and runs them.
- `tests/api/`: pytest suite for the Flask layer (test client; no server, no binary).
- `tests/ui/`: Playwright end-to-end suite for the frontend, plus the `/study` fixture it serves.
- `run_ui_tests.sh`: builds the binary, starts a server, runs that suite, stops the server.
- `.github/workflows/ci.yml`: runs all three suites on every push.
- `main.cpp`: CLI and JSON export for the web application.
- `database.cpp` / `database.hpp`: SQLite persistence for timesteps.
- `study.cpp`: convergence and stability study.
- `server.py`: Flask API server.
- `web/`: Frontend assets (HTML, CSS, JS; `validation.js` renders the study charts).
- `make_figures.py`: renders the report figures from the study output.
- `start.sh`: Unified launch script.
- `run_study.sh`: builds and runs the study, then renders figures.
