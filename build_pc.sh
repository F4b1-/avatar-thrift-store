#!/bin/bash
# Builds the desktop tools (core, avtool, core_tests) on Linux and runs the tests, as CI's Linux job does. Needs CMake
# 3.20 or later and a C++ compiler (GCC or Clang).
#   ./build_pc.sh            Release build + tests
#   ./build_pc.sh Debug
set -euo pipefail

root="$(cd "$(dirname "$0")" && pwd)"
config="${1:-Release}"
# Apart from build/pc, so a Windows build in the same checkout (WSL) keeps its own.
build="$root/build/linux"

cmake -S "$root" -B "$build" -DCMAKE_BUILD_TYPE="$config" -DCMAKE_CXX_FLAGS="-Wall -Wextra"
cmake --build "$build" --parallel
ctest --test-dir "$build" --output-on-failure
