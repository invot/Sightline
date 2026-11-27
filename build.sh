#!/usr/bin/env bash
# Builds sightline with the bundled MinGW + CMake + JUCE toolchain.
#
#   ./build.sh              configure (if needed) and build everything
#   ./build.sh clean        wipe build/ first
#   ./build.sh tests        build and run the core tests only
#
# Outputs:
#   build/SightLine_artefacts/Release/VST3/sightline.vst3
#   build/SightLine_artefacts/Release/Standalone/sightline.exe

set -euo pipefail
cd "$(dirname "$0")"

ROOT="$PWD"
# GCC and CMake want Windows-style paths on this machine.
ROOT_W="$(cd "$ROOT" && pwd -W 2>/dev/null || echo "$ROOT")"
TOOLS="$ROOT/.tools"
export PATH="$TOOLS/cmake-3.31.6-windows-x86_64/bin:$TOOLS/mingw64/bin:$PATH"
unset RC

# GCC 16 no longer pulls the C headers JUCE assumes, and JUCE 8's UIA text
# provider needs one enum MinGW does not declare. Both are force-included.
# Static runtime: the build tools run at configure time, before any PATH setup a
# host might have, and the plugin is loaded by DAWs that know nothing about MinGW.
export SIGHTLINE_MANIFEST_RC="$ROOT_W/Source/platform/win_manifest.rc"
export CXXFLAGS="-include utility -include cstring -include cstdint -include cstdio -include $ROOT_W/Source/platform/mingw_uia_shim.h -static"

if [ "${1:-}" = "clean" ]; then
    rm -rf build
    shift
fi

CONFIGURE_ARGS=(
    -S "$ROOT" -B "$ROOT/build" -G "MinGW Makefiles"
    -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_C_COMPILER="$TOOLS/mingw64/bin/gcc.exe"
    -DCMAKE_CXX_COMPILER="$TOOLS/mingw64/bin/c++.exe"
    -DCMAKE_RC_COMPILER="$TOOLS/mingw64/bin/windres.exe"
    -DCMAKE_MAKE_PROGRAM="$TOOLS/mingw64/bin/mingw32-make.exe"
    -DCMAKE_CXX_FLAGS="$CXXFLAGS"
)

if [ ! -f build/Makefile ]; then
    cmake "${CONFIGURE_ARGS[@]}"
fi

if [ "${1:-}" = "tests" ]; then
    cmake --build build --target SightLineTests -j"$(nproc)"
    ./build/SightLineTests.exe
else
    cmake --build build -j"$(nproc)"
    echo
    echo "Built:"
    ls -1 build/SightLine_artefacts/Release/VST3/sightline.vst3 2>/dev/null || true
    ls -1 build/SightLine_artefacts/Release/Standalone/sightline.exe 2>/dev/null || true
fi
