#!/usr/bin/env bash
#
# dev.sh — local dev convenience wrapper around the CMake/Ninja workflow.
#
# Auto-configures the build dir on first use (or after you delete it), then
# builds and does whatever you asked. Runnable from anywhere; no need to
# remember the -DCMAKE_PREFIX_PATH incantation.
#
#   ./dev.sh          build the app and run it   (default)
#   ./dev.sh run      same as above
#   ./dev.sh test     build and run the test suite
#   ./dev.sh build    build only
#   ./dev.sh clean    delete the build dir (forces a fresh configure next time)
#
set -euo pipefail

# Work relative to this script's location, so it runs from any directory.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="$ROOT/build"

configure() {
    cmake -S "$ROOT" -B "$BUILD" -G Ninja \
        -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
}

build() {
    # Configure only if it hasn't been done yet (no generated build.ninja).
    [ -f "$BUILD/build.ninja" ] || configure
    cmake --build "$BUILD"
}

cmd="${1:-run}"
case "$cmd" in
    run|"")
        build
        # macOS builds a .app bundle (for camera permission); run its inner binary so the
        # app is recognized as the bundle (NSBundle/TCC) while stdout stays in the terminal
        # for dev logs. Elsewhere it's a plain binary.
        APP="$BUILD/pokedex_tcg.app/Contents/MacOS/pokedex_tcg"
        [ -x "$APP" ] || APP="$BUILD/pokedex_tcg"
        "$APP"
        ;;
    build)
        build
        ;;
    test)
        build
        ctest --test-dir "$BUILD" --output-on-failure
        ;;
    clean)
        rm -rf "$BUILD"
        echo "Removed $BUILD"
        ;;
    *)
        echo "usage: ./dev.sh [run|build|test|clean]" >&2
        exit 2
        ;;
esac
