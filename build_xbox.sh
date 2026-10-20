#!/bin/bash
# Builds the console app (build/xbox/AvatarThriftStore.xex) with xsdk, on Linux or in WSL (build_xbox.ps1 runs it
# there from Windows).
#   ./build_xbox.sh [<path of another xsdk checkout>]
# Uses the xsdk submodule (third_party/xsdk; git submodule update --init) unless another checkout is named. Needs
# xsdk's toolchain in ~/xsdk-gcc (third_party/xsdk/tools/toolchain/build.sh), and CMake, Ninja and Python 3.
set -euo pipefail

root="$(cd "$(dirname "$0")" && pwd)"
xsdk="${1:-$root/third_party/xsdk}"
if [ ! -f "$xsdk/Makefile" ]; then
	if [ $# -eq 0 ]; then
		echo "the xsdk submodule is missing: run git submodule update --init" >&2
	else
		echo "no xsdk checkout in $xsdk" >&2
	fi
	exit 1
fi
xsdk="$(cd "$xsdk" && pwd)"

make -s -C "$xsdk" build/libxsdk.a
cmake -S "$root/app" -B "$root/build/xbox" -G Ninja -DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_TOOLCHAIN_FILE="$xsdk/cmake/xsdk.cmake"
cmake --build "$root/build/xbox"

echo "Built $root/build/xbox/AvatarThriftStore.xex"
