# Heat Simulation Web App

A self-sufficient web application for simulating the 2D Heat Equation using the Finite Difference Method (FDM).

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

## API Endpoints
- `GET /`: Serves the frontend.
- `POST /run`: Executes a simulation.
  - **Payload**: `{ "rows": 20, "cols": 20, "top": 100, "bottom": 0, "left": 0, "right": 0 }`
  - **Response**: JSON containing simulation status and the final grid data.

## Project Structure
- `main.cpp`: C++ Simulation logic.
- `server.py`: Flask API server.
- `web/`: Frontend assets (HTML, CSS, JS).
- `start.sh`: Unified launch script.
