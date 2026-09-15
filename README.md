# Heat Spread Simulation

C++ simulation of 2D heat dispersion on a flat plate using the Finite Difference Method (FDM).

## Requirements
- C++17 compiler (g++)
- SQLite3 library (`libsqlite3-dev` on Ubuntu, `sqlite` via Homebrew on macOS)

## Build Instructions
Instead of CMake, build directly using `g++`:

```bash
g++ -std=c++17 main.cpp database.cpp -lsqlite3 -o heat_sim
```

## Running the Simulation
You can run the simulation with optional boundary temperature parameters:
```bash
./heat_sim <rows> <cols> <top_temp> <bottom_temp> <left_temp> <right_temp>
```
Example:
```bash
./heat_sim 20 20 100 0 0 0
```
If no arguments are provided, it defaults to a 20x20 grid with a hot top edge (100°C) and cold others.

## Web Frontend
A web-based visualizer is located in the `/web` folder. 
To run it:
1. Run the simulation to generate `latest_heatmap.json`.
2. Start a local server in the `/web` directory:
   ```bash
   cd web
   python3 -m http.server 8000
   ```
3. Open `http://localhost:8000` in your browser.

## Implementation Details
- **Method**: Explicit Forward-Time Central-Space (FTCS) scheme.
- **Stability**: The simulation assumes $\alpha \Delta t / \Delta x^2 \le 0.25$ for stability.
- **Output**: The final state is exported to `latest_heatmap.json` for the web frontend.
