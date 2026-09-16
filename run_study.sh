#!/bin/bash
# Build and run the convergence / stability study, then render the figures.
set -e
cd "$(dirname "$0")"

echo "Building study tool..."
CMAKE="$(command -v cmake || true)"
if [ -z "$CMAKE" ]; then
    for c in /Applications/CLion*.app/Contents/bin/cmake/mac/*/bin/cmake \
             "$HOME"/Applications/CLion*.app/Contents/bin/cmake/mac/*/bin/cmake; do
        if [ -x "$c" ]; then CMAKE="$c"; break; fi
    done
fi

if [ -n "$CMAKE" ]; then
    "$CMAKE" -S . -B build
    "$CMAKE" --build build --target heat_study
    cp build/heat_study ./heat_study.new
    mv -f ./heat_study.new ./heat_study
else
    CXX="${CXX:-$(command -v clang++ || command -v g++ || command -v c++ || true)}"
    if [ -z "$CXX" ]; then
        echo "Error: no C++ compiler found. Run: xcode-select --install"
        exit 1
    fi
    "$CXX" -std=c++17 -O2 study.cpp -o heat_study.new
    mv -f heat_study.new heat_study
fi
if command -v codesign >/dev/null 2>&1; then
    codesign --force --sign - ./heat_study >/dev/null 2>&1 || true
fi

echo
./heat_study

# Render the report figures if matplotlib is available.
if [ -d ".venv" ]; then VENV=".venv"; elif [ -d "venv" ]; then VENV="venv"; else VENV=""; fi
if [ -n "$VENV" ] && "$VENV/bin/python" -c "import matplotlib" 2>/dev/null; then
    echo
    echo "Rendering figures..."
    "$VENV/bin/python" make_figures.py
else
    echo
    echo "Skipping figures (matplotlib not installed)."
    echo "  To enable: ${VENV:-venv}/bin/pip install -r requirements-study.txt"
fi
