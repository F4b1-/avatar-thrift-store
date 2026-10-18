# Builds the console app (build\xbox\AvatarThriftStore.xex) with xsdk in WSL Debian.
#   .\build_xbox.ps1 [-Xsdk <WSL path of another xsdk checkout>]
# Uses the xsdk submodule (third_party\xsdk; git submodule update --init) unless -Xsdk names another one. Needs
# xsdk's toolchain in ~/xsdk-gcc (third_party/xsdk/tools/toolchain/build.sh), and CMake, Ninja and Python 3 in
# Debian.
param(
    [string]$Xsdk = ""
)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
# C:\Users\... -> /mnt/c/Users/...
$wslRoot = "/mnt/" + $root.Substring(0, 1).ToLower() + ($root.Substring(2) -replace '\\', '/')
if (-not $Xsdk) {
    if (-not (Test-Path "$root\third_party\xsdk\Makefile")) {
        throw "the xsdk submodule is missing: run git submodule update --init"
    }
    $Xsdk = "$wslRoot/third_party/xsdk"
}

$script = @"
set -e
cd $Xsdk && make -s build/libxsdk.a
cmake -S '$wslRoot/app' -B '$wslRoot/build/xbox' -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=`$(cd $Xsdk && pwd)/cmake/xsdk.cmake
cmake --build '$wslRoot/build/xbox'
"@
# A checkout with Windows line endings (Git's default on Windows) would pass
# carriage returns to bash, which then fails on the first line.
$script = $script -replace "`r", ""

& wsl.exe -d Debian --exec bash -c $script
if ($LASTEXITCODE) { throw "xsdk build failed" }

Write-Host "Built $root\build\xbox\AvatarThriftStore.xex"
