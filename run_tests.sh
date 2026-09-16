#!/bin/bash
# Build and run the physics regression tests. Exits non-zero on failure.
set -e
cd "$(dirname "$0")"

CMAKE="$(command -v cmake || true)"
if [ -z "$CMAKE" ]; then
    for c in /Applications/CLion*.app/Contents/bin/cmake/mac/*/bin/cmake \
             "$HOME"/Applications/CLion*.app/Contents/bin/cmake/mac/*/bin/cmake; do
        if [ -x "$c" ]; then CMAKE="$c"; break; fi
    done
fi

if [ -n "$CMAKE" ]; then
    "$CMAKE" -S . -B build
    "$CMAKE" --build build --target heat_tests
    ./build/heat_tests
else
    CXX="${CXX:-$(command -v clang++ || command -v g++ || command -v c++ || true)}"
    if [ -z "$CXX" ]; then
        echo "Error: no C++ compiler found. Run: xcode-select --install"
        exit 1
    fi
    echo "cmake not found - compiling tests directly..."
    "$CXX" -std=c++17 -O2 tests.cpp -o heat_tests.new
    mv -f heat_tests.new heat_tests
    ./heat_tests
fi
