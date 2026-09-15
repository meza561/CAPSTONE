# Heat Spread Simulation

C++ simulation of 2D heat dispersion on a flat plate using the Finite Difference Method (FDM), with SQLite for temporal data persistence.

## Requirements
- C++17 compiler
- SQLite3 library (`libsqlite3-dev` on Ubuntu, `sqlite` via Homebrew on macOS)
- CMake 3.10+

## Build Instructions
```bash
mkdir build && cd build
cmake ..
make
./heat_sim
```

## Implementation Details
- **Method**: Explicit Forward-Time Central-Space (FTCS) scheme.
- **Stability**: The simulation assumes $\alpha \Delta t / \Delta x^2 \le 0.25$ for stability.
- **Storage**: Every timestep is recorded in `heat_sim.db` for post-simulation analysis.
- **Convergence**: Tracking the maximum difference between iterations ($\Delta T_{max}$) to determine steady state.
