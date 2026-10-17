# Builds the PC targets (core, avtool, core_tests) with the CMake bundled in
# Visual Studio and runs the tests.
#   .\build_pc.ps1            Release build + tests
#   .\build_pc.ps1 -Config Debug
param([string]$Config = "Release")

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw "Visual Studio with C++ tools not found" }

$cmake = "$vs\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$ctest = "$vs\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe"

& $cmake -S $root -B "$root\build\pc" -A x64
if ($LASTEXITCODE) { exit $LASTEXITCODE }

& $cmake --build "$root\build\pc" --config $Config
if ($LASTEXITCODE) { exit $LASTEXITCODE }

& $ctest --test-dir "$root\build\pc" -C $Config --output-on-failure
exit $LASTEXITCODE
