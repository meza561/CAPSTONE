# Heat Simulation Web App

A self-sufficient web application for simulating the 2D Heat Equation using the Finite Difference Method (FDM) and Analytical PDE solutions.

## Architecture
- **Backend**: C++ simulation engine for high-performance computation, wrapped in a Python Flask API.
- **Frontend**: Dynamic HTML5/JS visualization with a heatmap renderer and simulation history.
- **Data**: JSON exchange between API and Frontend; LocalStorage for history.

## Getting Started

### Prerequisites
- CMake
- GCC/Clang (C++17)
- Python 3.x
- SQLite3

### Quick Start
Run the provided startup script:
```bash
./start.sh
```
This script will:
1. Compile the C++ simulation binary.
2. Start the Flask web server.
3. Open your browser to the application.

## Simulation Modes

### Initial Conditions (FDM)
Uses the Finite Difference Method to iteratively solve the heat equation. This mode allows for arbitrary boundary temperatures and simulates the time-evolution of the heat spread.

### PDE-driven (Analytical)
Calculates the steady-state solution using the general solution for the heat equation on a rectangular plate. It uses a Fourier series approximation to determine the temperature at any given point $(x, y)$ based on the boundary conditions.

## API Endpoints
- `GET /`: Serves the frontend.
- `POST /run`: Executes a simulation.
  - **Payload**: `{ "rows": 20, "cols": 20, "top": 100, "bottom": 0, "left": 0, "right": 0, "mode": "fdm", "alpha": 0.01 }`
  - **Response**: JSON containing simulation status and the final grid data.

## Project Structure
- `main.cpp`: C++ Simulation logic.
- `server.py`: Flask API server.
- `web/`: Frontend assets (HTML, CSS, JS).
- `start.sh`: Unified launch script.
