# Builds the desktop tools (core, avtool, core_tests) and runs the tests. Needs
# CMake 3.20 or later on PATH and a C++11 compiler. CMake uses Visual Studio by
# default; for another compiler, set CMAKE_GENERATOR (e.g. Ninja) and CXX first.
#   .\build_pc.ps1            Release build + tests
#   .\build_pc.ps1 -Config Debug
param([string]$Config = "Release")

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    throw "cmake not found on PATH (winget install Kitware.CMake)"
}

& cmake -S $root -B "$root\build\pc"
if ($LASTEXITCODE) { exit $LASTEXITCODE }

& cmake --build "$root\build\pc" --config $Config
if ($LASTEXITCODE) { exit $LASTEXITCODE }

& ctest --test-dir "$root\build\pc" -C $Config --output-on-failure
exit $LASTEXITCODE
