#!/bin/bash
set -e

# Heat Simulation Startup Script

cd "$(dirname "$0")"

# 1. Build the C++ backend
echo "Building C++ Backend..."
cmake -S . -B build
cmake --build build

# server.py invokes ./heat_sim from the project root, so publish the fresh
# build there instead of leaving a stale binary behind.
cp build/heat_sim ./heat_sim

# 2. Prepare the Python environment
VENV=".venv"
if [ ! -d "$VENV" ]; then
    echo "Creating virtual environment..."
    python3 -m venv "$VENV"
fi

echo "Installing dependencies..."
"$VENV/bin/pip" install -r requirements.txt --quiet

# 3. Launch the Python Flask server in the background
echo "Launching Web Server on http://127.0.0.1:5000..."
# Use the venv interpreter; a bare python3 does not have Flask installed.
nohup "$VENV/bin/python" server.py > server.log 2>&1 &
SERVER_PID=$!

echo "Server started with PID $SERVER_PID. Logs are in server.log"
echo "Opening browser..."

# 4. Open the web application in the default browser
sleep 1
open http://127.0.0.1:5000

echo "System is live. To stop the server, run: kill $SERVER_PID"
