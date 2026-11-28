#!/bin/bash
# Build universal macOS VST3 and standalone bundles with Apple Clang and JUCE 8.
#
# Builds into build-macos/ so the MinGW build in build/ and its cached
# toolchain stay usable; ./build.sh remains the Windows script.
set -euo pipefail
cd "$(dirname "$0")"

usage() {
    cat <<'EOF'
Usage: bash ./build-macos.sh [build|tests|smoke|clean]
  build   Build the VST3 and standalone app (default).
  tests   Build and run the JUCE-free core checks; JUCE is optional.
  smoke   Build and run the offscreen harness, writing the README screenshots.
  clean   Remove build-macos/ before building (may precede tests or smoke).

Requires macOS, Xcode Command Line Tools, CMake 3.22+, and JUCE 8.0.6.
Set SL_JUCE_DIR to use an existing JUCE checkout.
SL_MACOS_ARCHITECTURES defaults to 'arm64;x86_64'.
MACOSX_DEPLOYMENT_TARGET defaults to 11.0; JOBS defaults to the CPU count.
EOF
}

CLEAN=false
if [ "${1:-}" = clean ]; then
    CLEAN=true
    shift
fi
ACTION="${1:-build}"
case "$ACTION" in
    -h|--help) usage; exit 0 ;;
    build|tests|smoke) ;;
    *) usage >&2; exit 1 ;;
esac
if [ "$#" -gt 1 ]; then
    usage >&2
    exit 1
fi

if [ "$(uname -s)" != Darwin ]; then
    echo "This script requires macOS. Use ./build.sh for Windows." >&2
    exit 1
fi
if ! command -v cmake >/dev/null 2>&1; then
    echo "CMake 3.22+ is required. Install it with: brew install cmake" >&2
    exit 1
fi
if ! xcrun --find clang++ >/dev/null 2>&1; then
    echo "Apple's developer tools are required. Run: xcode-select --install" >&2
    exit 1
fi

ROOT="$PWD"
BUILD_DIR="$ROOT/build-macos"
JUCE_DIR="${SL_JUCE_DIR:-$ROOT/.tools/JUCE-8.0.6}"
if [ "$ACTION" != tests ] && [ ! -f "$JUCE_DIR/CMakeLists.txt" ]; then
    echo "JUCE not found at $JUCE_DIR." >&2
    echo "Clone JUCE 8.0.6 into .tools/JUCE-8.0.6 or set SL_JUCE_DIR." >&2
    exit 1
fi

JOBS="${JOBS:-$(sysctl -n hw.logicalcpu 2>/dev/null || echo 4)}"
if [ "$CLEAN" = true ]; then
    rm -rf "$BUILD_DIR"
fi

# Configure every time so changed JUCE paths and architecture settings take
# effect. The hand landmark model is Windows-only (ONNX Runtime), so CMake
# turns it off here and the build needs no downloads.
cmake -S "$ROOT" -B "$BUILD_DIR" -G "Unix Makefiles" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER="$(xcrun --find clang)" \
    -DCMAKE_CXX_COMPILER="$(xcrun --find clang++)" \
    -DCMAKE_OSX_ARCHITECTURES="${SL_MACOS_ARCHITECTURES:-arm64;x86_64}" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="${MACOSX_DEPLOYMENT_TARGET:-11.0}" \
    -DSL_JUCE_DIR="$JUCE_DIR"

case "$ACTION" in
    tests)
        cmake --build "$BUILD_DIR" --target SightLineTests --parallel "$JOBS"
        "$BUILD_DIR/SightLineTests"
        ;;
    smoke)
        cmake --build "$BUILD_DIR" --target SightLineSmoke --parallel "$JOBS"
        mkdir -p "$ROOT/docs/screenshots"
        SIGHTLINE_NO_CAMERA=1 SIGHTLINE_SNAPSHOT_DIR="$ROOT/docs/screenshots" \
            "$BUILD_DIR/SightLineSmoke"
        ;;
    build)
        cmake --build "$BUILD_DIR" --target SightLine_VST3 SightLine_Standalone --parallel "$JOBS"
        echo
        echo "Built:"
        printf '%s\n' \
            "$BUILD_DIR/SightLine_artefacts/Release/VST3/sightline.vst3" \
            "$BUILD_DIR/SightLine_artefacts/Release/Standalone/sightline.app"
        ;;
esac
