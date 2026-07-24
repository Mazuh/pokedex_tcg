#!/usr/bin/env bash
#
# install.sh — build a prod-optimized binary and suggest a `pokedex` command.
#
# Unlike dev.sh (fast, unoptimized, run-in-place), this does a Release build in
# its own dir, then — following anne's lead — *prints* a shell hint rather than
# editing your rc file for you. Paste the line it prints into your shell config
# and `pokedex` launches the app from anywhere.
#
#   ./install.sh          Release-build, then print the shell-alias hint
#
set -euo pipefail

# Work relative to this script's location, so it runs from any directory.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="$ROOT/build-release"        # kept separate from dev.sh's ./build
BIN="$BUILD/pokedex_tcg"

# --- Build (optimized, no tests) ---------------------------------------------
# A dedicated Release dir so the optimized artifact never mixes with the
# unoptimized ./build that dev.sh uses day-to-day. BUILD_TESTING=OFF skips the
# GoogleTest fetch/compile — nothing to run here, this is a packaging build.
cmake -S "$ROOT" -B "$BUILD" -G Ninja \
    -DCMAKE_PREFIX_PATH="$(brew --prefix qt)" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTING=OFF
cmake --build "$BUILD"

# --- Shell hint (suggest, don't mutate) --------------------------------------
# Detect the user's shell to name the right rc file, then print an alias line
# for them to paste — the same non-invasive pattern anne's `bootstrap` uses.
case "${SHELL:-}" in
    *zsh)  rc="~/.zshrc" ;;
    *bash) rc="~/.bashrc" ;;
    *)     rc="your shell config" ;;
esac

printf '\n\033[32mBuilt:\033[0m %s\n' "$BIN"
printf '\n\033[2mTip: to make \033[0m\033[1mpokedex\033[0m\033[2m available everywhere, add to %s:\033[0m\n' "$rc"
printf '\033[2m  alias pokedex="%s"\033[0m\n' "$BIN"
