#!/bin/bash
set -e

# Heat Simulation Startup Script

cd "$(dirname "$0")"

# 1. Build the C++ backend
echo "Building C++ Backend..."
# Locate cmake: PATH first, then CLion's bundled copy. CLion ships its own,
# which is why building in the IDE works when a plain shell cannot find it.
CMAKE="$(command -v cmake || true)"
if [ -z "$CMAKE" ]; then
    for c in /Applications/CLion*.app/Contents/bin/cmake/mac/*/bin/cmake \
             "$HOME"/Applications/CLion*.app/Contents/bin/cmake/mac/*/bin/cmake; do
        if [ -x "$c" ]; then CMAKE="$c"; echo "Using CLion's bundled cmake."; break; fi
    done
fi

if [ -n "$CMAKE" ]; then
    "$CMAKE" -S . -B build
    "$CMAKE" --build build --target heat_sim
    # server.py invokes ./heat_sim from the project root, so publish the fresh
    # build there. Replace it ATOMICALLY: macOS caches a binary's code
    # signature against its inode, and copying over the file in place leaves a
    # stale signature, so the next exec dies with SIGKILL ("exit status -9").
    # Renaming a new file over the old one gives it a fresh inode.
    cp build/heat_sim ./heat_sim.new
    mv -f ./heat_sim.new ./heat_sim
else
    # No cmake anywhere. This project is two translation units linked against
    # libsqlite3, so compile it directly rather than making cmake a hard
    # requirement just to run the app.
    echo "cmake not found - compiling directly..."
    CXX="${CXX:-$(command -v clang++ || command -v g++ || command -v c++ || true)}"
    if [ -z "$CXX" ]; then
        echo "Error: no C++ compiler found. Install the Xcode command line tools:"
        echo "    xcode-select --install"
        exit 1
    fi
    # Compile to a temp name and rename over the old binary (see the note
    # above about inode-cached code signatures on macOS).
    "$CXX" -std=c++17 -O2 main.cpp database.cpp -lsqlite3 -o heat_sim.new
    mv -f heat_sim.new heat_sim
fi

# Apple Silicon refuses to run a binary whose signature does not match.
# Re-sign ad-hoc after replacing it; harmless everywhere else.
if command -v codesign >/dev/null 2>&1; then
    codesign --force --sign - ./heat_sim >/dev/null 2>&1 || true
fi

# 2. Prepare the Python environment
# Use whichever virtualenv this clone already has: the two checkouts of this
# project do not agree on the name (.venv vs venv).
if [ -d ".venv" ]; then
    VENV=".venv"
elif [ -d "venv" ]; then
    VENV="venv"
else
    VENV=".venv"
    echo "Creating virtual environment..."
    python3 -m venv "$VENV"
fi

echo "Installing dependencies..."
"$VENV/bin/pip" install -r requirements.txt --quiet

# 3. Launch the Python Flask server in the background
# Stop a previous instance first. Otherwise it keeps holding port 5000 and
# silently serves stale code while the new server dies with "Address already
# in use" - the browser still answers, so the app looks broken for no reason.
PIDFILE=".server.pid"
if [ -f "$PIDFILE" ] && kill -0 "$(cat "$PIDFILE")" 2>/dev/null; then
    echo "Stopping previous server (PID $(cat "$PIDFILE"))..."
    kill "$(cat "$PIDFILE")" 2>/dev/null || true
    sleep 1
fi

# Refuse to start if anything else still holds the port, instead of failing
# quietly in the background. On macOS this is often AirPlay Receiver.
if command -v lsof >/dev/null 2>&1 && lsof -ti tcp:5000 -sTCP:LISTEN >/dev/null 2>&1; then
    echo "Error: port 5000 is already in use by:"
    lsof -i tcp:5000 -sTCP:LISTEN
    echo
    echo "Stop it with:  lsof -ti tcp:5000 -sTCP:LISTEN | xargs kill"
    echo "Or disable AirPlay Receiver: System Settings > General > AirDrop & Handoff."
    exit 1
fi

echo "Launching Web Server on http://127.0.0.1:5000..."
# Use the venv interpreter; a bare python3 does not have Flask installed.
nohup "$VENV/bin/python" server.py > server.log 2>&1 &
SERVER_PID=$!
echo "$SERVER_PID" > "$PIDFILE"

echo "Server started with PID $SERVER_PID. Logs are in server.log"
echo "Opening browser..."

# 4. Open the web application in the default browser
sleep 1
open http://127.0.0.1:5000

echo "System is live. To stop the server, run: kill $SERVER_PID"
