#!/usr/bin/env bash
#
# install.sh — build a prod-optimized binary and install a `pokedex` command.
#
# Unlike dev.sh (fast, unoptimized, run-in-place), this does a Release build in
# its own dir, then installs the binary to /usr/local/bin as `pokedex` (the
# CMake install(PROGRAMS ... RENAME) rule) so it launches from any directory —
# no shell alias or PATH edit needed, since /usr/local/bin is already on PATH.
#
#   ./install.sh          Release-build, then install to /usr/local/bin
#
set -euo pipefail

# Work relative to this script's location, so it runs from any directory.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="$ROOT/build-release"        # kept separate from dev.sh's ./build
PREFIX="/usr/local"                # /usr/local/bin is on PATH by default

# --- Build (optimized, no tests) ---------------------------------------------
# A dedicated Release dir so the optimized artifact never mixes with the
# unoptimized ./build that dev.sh uses day-to-day. BUILD_TESTING=OFF skips the
# GoogleTest fetch/compile — nothing to run here, this is a packaging build.
cmake -S "$ROOT" -B "$BUILD" -G Ninja \
    -DCMAKE_PREFIX_PATH="$(brew --prefix qt)" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTING=OFF
cmake --build "$BUILD"

# --- Install (needs sudo to write under /usr/local) --------------------------
# cmake --install honors the install(PROGRAMS ... RENAME pokedex) rule, dropping
# the binary at $PREFIX/bin/pokedex. Writing under /usr/local requires root, so
# run the install step with sudo (only this step — the build above stays as you).
echo "Installing pokedex to $PREFIX/bin (sudo required)..."
if sudo cmake --install "$BUILD" --prefix "$PREFIX"; then
    printf '\n\033[32mInstalled:\033[0m %s/bin/pokedex\n' "$PREFIX"
    printf '\033[2mRun \033[0m\033[1mpokedex\033[0m\033[2m from any directory to launch the app.\033[0m\n'
    exit 0
fi

# --- Fallback (no sudo / prompt cancelled) -----------------------------------
# The build already succeeded, so don't leave the user empty-handed if the
# privileged install can't run. Fall back to the old non-invasive alias hint
# pointing at the built binary — they paste one line and still get `pokedex`.
BIN="$BUILD/pokedex_tcg"
case "${SHELL:-}" in
    *zsh)  rc="~/.zshrc" ;;
    *bash) rc="~/.bashrc" ;;
    *)     rc="your shell config" ;;
esac

printf '\n\033[33mInstall to %s skipped.\033[0m The binary is built at:\n  %s\n' "$PREFIX/bin" "$BIN"
printf '\n\033[2mTo get a \033[0m\033[1mpokedex\033[0m\033[2m command without sudo, add to %s:\033[0m\n' "$rc"
printf '\033[2m  alias pokedex="%s"\033[0m\n' "$BIN"
