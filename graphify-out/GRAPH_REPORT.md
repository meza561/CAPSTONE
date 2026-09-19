# Graph Report - CAPSTONE  (2026-09-19)

## Corpus Check
- Corpus is ~47,848 words - fits in a single context window. You may not need a graph.

## Summary
- 549 nodes · 956 edges · 37 communities (30 shown, 7 thin omitted)
- Extraction: 87% EXTRACTED · 13% INFERRED · 0% AMBIGUOUS · INFERRED: 127 edges (avg confidence: 0.82)
- Token cost: 193,688 input · 0 output

## Community Hubs (Navigation)
- Heat Solver Core
- CLI Entry and Options
- Playwright UI Suite
- Reaction-Diffusion Model
- Frontend App Script
- Flask API Layer
- Wave Equation Model
- Figure Rendering Scripts
- Flask API Test Suite
- Node Tooling Dependencies
- SQLite Persistence
- CMake Build and OpenMP
- Run Endpoint Argv Tests
- Single-Writer Architecture
- Explicit and Wave Numerics
- Input Clamping and Validation
- Playwright Package Config
- Social Preview Card
- Pytest Fixtures
- Subprocess Failure Branches
- Validation Tab Charts
- Study Results Plumbing
- Implicit ADI Schemes
- Coordinate Flip and Payload
- Rate Limiting and Hardening
- JSON Error Handlers
- Pattern-Formation Physics
- Run File Retention Sweep
- API Test CI Job
- Browser-Local Run History
- UI Test Runner Script
- Claude GitHub Workflows
- Study Runner Script
- Physics Test Runner Script
- App Launch Script
- Static Page CSP Details
- Cookieless Analytics

## God Nodes (most connected - your core abstractions)
1. `HeatSimulation` - 55 edges
2. `ReactionDiffusion` - 34 edges
3. `WaveSimulation` - 28 edges
4. `main()` - 21 edges
5. `section()` - 16 edges
6. `main()` - 16 edges
7. `HeatDatabase` - 14 edges
8. `check()` - 14 edges
9. `runSimulation()` - 14 edges
10. `main()` - 13 edges

## Surprising Connections (you probably didn't know these)
- `Per-run database named by random id, deleted after 24h retention` --semantically_similar_to--> `Persistence contract: each run clears the previous`  [AMBIGUOUS] [semantically similar]
  web/privacy.html → CLAUDE.md
- `testGrayScott()` --references--> `ReactionDiffusion`  [INFERRED]
  tests.cpp → reaction.hpp
- `analytical()` --references--> `HeatSimulation`  [INFERRED]
  study.cpp → simulation.hpp
- `main()` --references--> `HeatSimulation`  [INFERRED]
  study.cpp → simulation.hpp
- `CI job: Frontend end-to-end tests` --references--> `study_results.json Playwright fixture and its gitignore negation`  [INFERRED]
  .github/workflows/ci.yml → CLAUDE.md

## Import Cycles
- None detected.

## Hyperedges (group relationships)
- **Three CI jobs cover three application layers** — _github_workflows_ci_physics_tests, _github_workflows_ci_api_tests, _github_workflows_ci_ui_tests, claude_three_tier_single_writer [EXTRACTED 1.00]
- **Three binaries compile the same solver headers** — cmakelists_heat_sim, cmakelists_heat_study, cmakelists_heat_tests, claude_simulation_hpp_core [EXTRACTED 1.00]
- **Wave scheme: leapfrog, Taylor start step and the 2D CFL limit** — readme_wave_leapfrog, readme_taylor_half_step, readme_cfl_limit, readme_signed_amplitude_diverging_map [EXTRACTED 1.00]
- **Simulation modes advertised on the social preview card** — web_og_image_explicit_mode, web_og_image_backward_euler_mode, web_og_image_crank_nicolson_mode, web_og_image_reaction_diffusion_mode [EXTRACTED 1.00]

## Communities (37 total, 7 thin omitted)

### Community 0 - "Heat Solver Core"
Cohesion: 0.07
Nodes (57): cstdio, M, Method, HeatSimulation, adiMid, adiOut, alpha, alphaField (+49 more)

### Community 1 - "CLI Entry and Options"
Cohesion: 0.06
Nodes (50): algorithm, chrono, cmath, cstdint, HeatPoint, temp, x, y (+42 more)

### Community 2 - "Playwright UI Suite"
Cohesion: 0.08
Nodes (34): ref_path, @playwright/test, { defineConfig }, { runSimulation }, { test, expect }, canvasPixel(), DEFAULTS, { expect } (+26 more)

### Community 3 - "Reaction-Diffusion Model"
Cohesion: 0.08
Nodes (27): main(), Model, Grid, ReactionDiffusion, cols, D, dt, Du (+19 more)

### Community 4 - "Frontend App Script"
Cohesion: 0.16
Nodes (27): cacheLimit(), drawHeatmap(), fetchFrame(), fmtTime(), fmtValue(), gridLimits(), heatColor(), loadFrameList() (+19 more)

### Community 5 - "Flask API Layer"
Cohesion: 0.10
Nodes (27): flask, flask_cors, flask_limiter, flask_limiter_util, flask_talisman, json, re, route (+19 more)

### Community 6 - "Wave Equation Model"
Cohesion: 0.12
Nodes (14): Boundary, testWaveBoundaries(), Grid, WaveSimulation, boundary, c, cols, dt (+6 more)

### Community 7 - "Figure Rendering Scripts"
Cohesion: 0.15
Nodes (24): caption(), fig_cost(), fig_scaling(), fig_spatial(), fig_stability(), fig_temporal(), guide(), main() (+16 more)

### Community 8 - "Flask API Test Suite"
Cohesion: 0.09
Nodes (8): Write frames as {step: [[temp, ...], ...]} into one run's database. Same schema…, seed_db(), Unit tests for the Flask layer in server.py. These cover the API in isolation -…, test_frames_lists_sorted_distinct_steps(), test_get_run_falls_back_to_the_earliest_frame(), test_get_run_snaps_down_to_the_nearest_stored_frame(), test_two_runs_do_not_share_frames(), types

### Community 9 - "Node Tooling Dependencies"
Cohesion: 0.12
Nodes (16): allowScripts, tesseract.js, tree-sitter-c, tree-sitter-cpp, tree-sitter-go, tree-sitter-java, tree-sitter-javascript, tree-sitter-lua (+8 more)

### Community 10 - "SQLite Persistence"
Cohesion: 0.20
Nodes (15): string, vector, HeatDatabase, beginRun, db, dbName, endRun, exec (+7 more)

### Community 11 - "CMake Build and OpenMP"
Cohesion: 0.22
Nodes (14): Binary smoke test (centre = 25 C), CI job: Physics regression tests, Ad-hoc codesign after in-place binary copy, Parallel and serial runs must stay bit-identical, simulation.hpp as the shared solver core, heat_sim target (app binary), heat_study target (convergence study), heat_tests target (physics regression) (+6 more)

### Community 12 - "Run Endpoint Argv Tests"
Cohesion: 0.21
Nodes (13): fake_run(), positional(), argv with the --flags removed, matching extractOptions in main.cpp., A subprocess.run stand-in that records argv instead of executing. It writes the…, test_post_run_accepts_the_legacy_single_source_payload(), test_post_run_builds_fisher_arguments(), test_post_run_builds_gray_scott_arguments(), test_post_run_clamps_an_out_of_range_source() (+5 more)

### Community 13 - "Single-Writer Architecture"
Cohesion: 0.17
Nodes (12): CI job: Frontend end-to-end tests, Persistence contract: each run clears the previous, SIM_LOCK run serialization, Three-tier single-writer architecture, Playwright workers: 1 (single-writer app), GET /frames stored-step listing, Container host requirement (process + writable files), One gunicorn worker, deliberately (+4 more)

### Community 14 - "Explicit and Wave Numerics"
Cohesion: 0.18
Nodes (11): UI assertions compare the app's own outputs, not a reimplementation, Analytical Laplace solution by four-edge Fourier superposition, 2D CFL limit nu <= 1/sqrt(2), Diffusion number F and dt derived from it, Explicit forward-Euler FDM scheme, Signed amplitude painted with a run-wide symmetric diverging map, Spatial convergence study (order 2.03 smooth / 1.04 discontinuous corners), Half-coefficient Taylor start step (+3 more)

### Community 15 - "Input Clamping and Validation"
Cohesion: 0.20
Nodes (11): limit, parametrize, _clamp(), _clamp_float(), Coerce a client-supplied dimension into a sane range., Coerce a client-supplied float into a sane range., run_simulation(), test_clamp() (+3 more)

### Community 16 - "Playwright Package Config"
Cohesion: 0.22
Nodes (8): description, devDependencies, @playwright/test, name, private, scripts, test, version

### Community 17 - "Social Preview Card"
Cohesion: 0.28
Nodes (9): Backward Euler solver mode, Heat Simulation Visualizer OG Card, Crank-Nicolson solver mode, Explicit solver mode, Blue-green-red heat colormap legend, Heat Simulation Visualizer (project brand), Reaction-diffusion mode, Social preview card design (1200x630 blurred-field backdrop) (+1 more)

### Community 18 - "Pytest Fixtures"
Cohesion: 0.25
Nodes (7): fixture, pytest, sqlite3, sys, client(), Flask test client, with the CWD moved to an empty tmp dir. server.py addresses…, uuid

### Community 19 - "Subprocess Failure Branches"
Cohesion: 0.25
Nodes (5): test_post_run_falls_back_to_the_exit_status(), test_post_run_reports_a_missing_binary(), test_post_run_reports_a_timeout(), boom(), test_post_run_surfaces_solver_stderr()

### Community 20 - "Validation Tab Charts"
Cohesion: 0.39
Nodes (5): chart(), decadeTicks(), hideTip(), showTip(), tooltip()

### Community 21 - "Study Results Plumbing"
Cohesion: 0.38
Nodes (7): study_results.json Playwright fixture and its gitignore negation, GET /study validation results endpoint, box-sizing overflow bug caught by the narrow-viewport test, Stability sweep pinning the F = 1/4 threshold, matplotlib (opt-in study dependency), Validation tab chart scaffolding, No-warranty / discretisation-error disclaimer

### Community 22 - "Implicit ADI Schemes"
Cohesion: 0.29
Nodes (7): Backward Euler (Douglas-Rachford ADI), Cost-to-steady-state comparison (~20x fewer implicit steps), Crank-Nicolson (Peaceman-Rachford ADI), Divergence-form diffusion with harmonic-mean face conductivity, Temporal convergence study (CN measured 2.00), Thomas algorithm tridiagonal solve per grid line, Material region editor

### Community 23 - "Coordinate Flip and Payload"
Cohesion: 0.33
Nodes (6): Bottom-left to [row][col] coordinate flip at the API boundary, POST /run endpoint payload contract, Pinned heat sources in bottom-left coordinates, Heat source editor with (x, y, C) quick-add, Method select (seven simulation modes), Docked Run button and status line

### Community 24 - "Rate Limiting and Hardening"
Cohesion: 0.40
Nodes (5): FLASK_ENV=production hardening switch, flask-limiter dependency, flask-talisman dependency, In-memory per-IP rate limit counters, Fair-use per-IP rate limiting on runs

### Community 25 - "JSON Error Handlers"
Cohesion: 0.50
Nodes (4): errorhandler, not_found(), rate_limited(), JSON, not Flask's HTML error page: every caller here speaks JSON.

### Community 26 - "Pattern-Formation Physics"
Cohesion: 0.50
Nodes (4): Bramson's 1/t correction to the Fisher-KPP front speed, Fisher-KPP travelling front, Gray-Scott Turing patterns, Zero-flux (Neumann) insulated edges

### Community 27 - "Run File Retention Sweep"
Cohesion: 0.50
Nodes (4): Drop run files past the retention window. Without this runs/ grows for as long…, sweep_old_runs(), test_sweep_is_quiet_when_there_is_nothing_to_sweep(), test_sweep_removes_only_expired_runs()

### Community 28 - "API Test CI Job"
Cohesion: 0.67
Nodes (3): CI job: Flask API tests, API test fixture chdirs into a tmp dir, pytest (opt-in test dependency)

### Community 29 - "Browser-Local Run History"
Cohesion: 0.67
Nodes (3): Run history kept in browser localStorage, History tab table, Run history stays in the visitor's localStorage

## Ambiguous Edges - Review These
- `Persistence contract: each run clears the previous` → `Per-run database named by random id, deleted after 24h retention`  [AMBIGUOUS]
  web/privacy.html · relation: semantically_similar_to

## Knowledge Gaps
- **132 isolated node(s):** `x`, `y`, `temp`, `db`, `insertStmt` (+127 more)
  These have ≤1 connection - possible missing edges or undocumented components. (Counts symbols only; 229 node(s) total have ≤1 connection when file, concept and rationale nodes are included.)
- **7 thin communities (<3 nodes) omitted from report** — run `graphify query` to explore isolated nodes.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **What is the exact relationship between `Persistence contract: each run clears the previous` and `Per-run database named by random id, deleted after 24h retention`?**
  _Edge tagged AMBIGUOUS (relation: semantically_similar_to) - confidence is low._
- **Why does `HeatSimulation` connect `Heat Solver Core` to `CLI Entry and Options`, `Reaction-Diffusion Model`?**
  _High betweenness centrality (0.086) - this node is a cross-community bridge._
- **Why does `ReactionDiffusion` connect `Reaction-Diffusion Model` to `Heat Solver Core`, `CLI Entry and Options`?**
  _High betweenness centrality (0.050) - this node is a cross-community bridge._
- **Why does `WaveSimulation` connect `Wave Equation Model` to `CLI Entry and Options`?**
  _High betweenness centrality (0.049) - this node is a cross-community bridge._
- **Are the 13 inferred relationships involving `HeatSimulation` (e.g. with `analytical()` and `main()`) actually correct?**
  _`HeatSimulation` has 13 INFERRED edges - model-reasoned connections that need verification._
- **Are the 2 inferred relationships involving `ReactionDiffusion` (e.g. with `main()` and `testGrayScott()`) actually correct?**
  _`ReactionDiffusion` has 2 INFERRED edges - model-reasoned connections that need verification._
- **Are the 9 inferred relationships involving `main()` (e.g. with `.frontPosition()` and `.seedFisher()`) actually correct?**
  _`main()` has 9 INFERRED edges - model-reasoned connections that need verification._