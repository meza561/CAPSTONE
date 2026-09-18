#!/bin/bash
# Build the app, start the server, and run the Playwright end-to-end suite
# against it headless. Exits non-zero on failure.
set -e
cd "$(dirname "$0")"

echo "Building the web app backend..."
CMAKE="$(command -v cmake || true)"
if [ -z "$CMAKE" ]; then
    for c in /Applications/CLion*.app/Contents/bin/cmake/mac/*/bin/cmake \
             "$HOME"/Applications/CLion*.app/Contents/bin/cmake/mac/*/bin/cmake; do
        if [ -x "$c" ]; then CMAKE="$c"; break; fi
    done
fi

if [ -n "$CMAKE" ]; then
    "$CMAKE" -S . -B build
    "$CMAKE" --build build --target heat_sim
    cp build/heat_sim ./heat_sim.new
    mv -f ./heat_sim.new ./heat_sim
else
    CXX="${CXX:-$(command -v clang++ || command -v g++ || command -v c++ || true)}"
    if [ -z "$CXX" ]; then
        echo "Error: no C++ compiler found. Run: xcode-select --install"
        exit 1
    fi
    echo "cmake not found - compiling directly..."
    "$CXX" -std=c++17 -O2 main.cpp database.cpp -lsqlite3 -o heat_sim.new
    mv -f heat_sim.new heat_sim
fi
if command -v codesign >/dev/null 2>&1; then
    codesign --force --sign - ./heat_sim >/dev/null 2>&1 || true
fi

# Python environment for server.py, same convention start.sh uses.
if [ -d ".venv" ]; then
    VENV=".venv"
elif [ -d "venv" ]; then
    VENV="venv"
else
    VENV=".venv"
    echo "Creating virtual environment..."
    python3 -m venv "$VENV"
fi
echo "Installing server dependencies..."
"$VENV/bin/pip" install -r requirements.txt --quiet

# The suite talks to a real server; stop anything already holding that port
# first (same logic start.sh uses), and always stop the instance we start
# before this script exits. Override with PORT= if 5000 is already in use by
# something this script should not touch (e.g. a dev instance you started
# yourself outside of this run).
PIDFILE=".server.pid"
PORT="${PORT:-5000}"
if [ "$PORT" = "5000" ] && [ -f "$PIDFILE" ] && kill -0 "$(cat "$PIDFILE")" 2>/dev/null; then
    echo "Stopping previous server (PID $(cat "$PIDFILE"))..."
    kill "$(cat "$PIDFILE")" 2>/dev/null || true
    sleep 1
fi
if command -v lsof >/dev/null 2>&1 && lsof -ti "tcp:$PORT" -sTCP:LISTEN >/dev/null 2>&1; then
    echo "Error: port $PORT is already in use by:"
    lsof -i "tcp:$PORT" -sTCP:LISTEN
    echo "Set PORT=<other port> to run the suite against a different one."
    exit 1
fi

echo "Starting server on http://127.0.0.1:$PORT ..."
PORT="$PORT" "$VENV/bin/python" server.py > server.log 2>&1 &
SERVER_PID=$!
# Only the default pidfile is start.sh's - don't let a PORT-overridden run
# clobber a tracked instance it didn't start and won't be the one stopping.
[ "$PORT" = "5000" ] && echo "$SERVER_PID" > "$PIDFILE"

cleanup() {
    if kill -0 "$SERVER_PID" 2>/dev/null; then
        echo "Stopping test server (PID $SERVER_PID)..."
        kill "$SERVER_PID" 2>/dev/null || true
    fi
    [ "$PORT" = "5000" ] && rm -f "$PIDFILE"
}
trap cleanup EXIT

echo "Waiting for the server to answer..."
for _ in $(seq 1 30); do
    if curl -sf "http://127.0.0.1:$PORT/" >/dev/null 2>&1; then
        break
    fi
    sleep 0.5
done
if ! curl -sf "http://127.0.0.1:$PORT/" >/dev/null 2>&1; then
    echo "Error: server did not come up. See server.log:"
    cat server.log
    exit 1
fi

echo "Running the Playwright suite..."
cd tests/ui
npm ci
npx playwright install --with-deps chromium
UI_BASE_URL="http://127.0.0.1:$PORT" npx playwright test
