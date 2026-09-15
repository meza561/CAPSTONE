#!/bin/bash

# Heat Simulation Startup Script

# 1. Build the C++ backend if not already built
echo "Building C++ Backend..."
mkdir -p build
cd build
cmake ..
make
cd ..

# 2. Launch the Python Flask server in the background
echo "Launching Web Server on http://127.0.0.1:5000..."
# Ensure flask and flask-cors are installed
pip install flask flask-cors --quiet

# Use nohup to keep it running and redirect logs to server.log
nohup python3 server.py > server.log 2>&1 &
SERVER_PID=$!

echo "Server started with PID $SERVER_PID. Logs are in server.log"
echo "Opening browser..."

# 3. Open the web application in the default browser
open http://127.0.0.1:5000

echo "System is live. To stop the server, run: kill $SERVER_PID"
