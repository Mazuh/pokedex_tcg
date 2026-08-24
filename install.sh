#!/usr/bin/env bash
#
# install.sh — build a prod-optimized binary and install the app two ways.
#
# Unlike dev.sh (fast, unoptimized, run-in-place), this does a Release build in
# its own dir and then installs it so it launches from anywhere. On macOS that
# means BOTH entry points (see the install rules in CMakeLists.txt):
#   - /Applications/<app>.app  — so ⌘Space / Launchpad / Finder can find it
#   - /usr/local/bin/pokedex   — a symlink into that bundle, for the terminal
# Elsewhere it's just the `pokedex` binary on PATH.
#
#   ./install.sh          Release-build, then install
#
set -euo pipefail

# Work relative to this script's location, so it runs from any directory.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="$ROOT/build-release"        # kept separate from dev.sh's ./build
PREFIX="/usr/local"                # /usr/local/bin is on PATH by default
# Spotlight indexes this; /usr/local/bin it does not. Overridable so the per-user
# install the CMake comment documents (POKEDEX_MACOS_APP_DIR="$HOME/Applications")
# survives this script, which would otherwise reset the cache to /Applications on
# every run and send the user back to sudo.
APPDIR="${POKEDEX_MACOS_APP_DIR:-/Applications}"

# Everything .app-related below is macOS-only — on Linux there is no bundle, and
# the install rules there are unchanged (a plain `pokedex` binary on PATH).
IS_MACOS=false
[ "$(uname -s)" = "Darwin" ] && IS_MACOS=true

# --- Build (optimized, no tests) ---------------------------------------------
# A dedicated Release dir so the optimized artifact never mixes with the
# unoptimized ./build that dev.sh uses day-to-day. BUILD_TESTING=OFF skips the
# GoogleTest fetch/compile — nothing to run here, this is a packaging build.
CONFIGURE_ARGS=(-DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF)
if $IS_MACOS; then
    # POKEDEX_MACOS_APP_DIR is read only by the if(APPLE) install block; passing it
    # on Linux would just draw an "unused variable" warning, so keep it scoped here.
    CONFIGURE_ARGS+=(-DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
                     -DPOKEDEX_MACOS_APP_DIR="$APPDIR")
fi
cmake -S "$ROOT" -B "$BUILD" -G Ninja "${CONFIGURE_ARGS[@]}"
cmake --build "$BUILD"

# --- Install (needs sudo to write under /usr/local and /Applications) --------
# cmake --install honors the install rules: on macOS the .app into /Applications
# plus a `pokedex` symlink at $PREFIX/bin; elsewhere the plain binary renamed to
# $PREFIX/bin/pokedex. Both destinations require root, so run the install step
# with sudo (only this step — the build above stays as you).
echo "Installing (sudo required)..."
if sudo cmake --install "$BUILD" --prefix "$PREFIX"; then
    if $IS_MACOS; then
        # Ask the SYMLINK where the bundle is, rather than guessing at its name: CMake owns
        # the app name (POKEDEX_APP_NAME) and the install directory, and the link it just
        # wrote points into exactly the bundle this run installed. Scanning $APPDIR by
        # bundle id instead would be ambiguous the moment a rename leaves two of ours there.
        APP=""
        LINK_TARGET="$(readlink "$PREFIX/bin/pokedex" 2>/dev/null || true)"
        case "$LINK_TARGET" in
            *.app/*) APP="${LINK_TARGET%%.app/*}.app" ;;
        esac

        # Tell LaunchServices about the bundle NOW. Copying into /Applications gets it
        # noticed eventually, but registering explicitly is what makes ⌘Space find it on
        # this run rather than whenever the indexer next wakes up.
        LSREGISTER="/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister"
        if [ -x "$LSREGISTER" ] && [ -d "$APP" ]; then
            "$LSREGISTER" -f "$APP" || true
        else
            printf '\033[33mNote:\033[0m could not register the app with LaunchServices; ⌘Space may take a while to find it.\n'
        fi

        # Sweep away OUR superseded bundles, in both places one can be left behind:
        #   - $PREFIX/bin, where every install before this one put the bundle (Spotlight
        #     can't see it there, and `pokedex` would point at whichever won last);
        #   - $APPDIR, where a future rename would leave the old name sitting beside the
        #     new one — install(DIRECTORY) only ever ADDS — giving LaunchServices two
        #     registrations for one app and ⌘Space a stale one to offer.
        # Matched by bundle id and never by name (that is the whole point — the name is
        # what changes), skipping the bundle we just installed.
        for stale in "$PREFIX"/bin/*.app "$APPDIR"/*.app; do
            [ -d "$stale" ] || continue
            [ "$stale" = "$APP" ] && continue
            id="$(defaults read "$stale/Contents/Info" CFBundleIdentifier 2>/dev/null || true)"
            [ "$id" = "com.mazuh.pokedex-tcg" ] || continue
            printf '\033[2mRemoving superseded install: %s\033[0m\n' "$stale"
            sudo rm -rf "$stale"
        done

        # The install itself succeeded even if the readlink above came up empty, so name
        # the folder rather than printing a blank path.
        printf '\n\033[32mInstalled:\033[0m %s\n' "${APP:-$APPDIR}"
        printf '\033[32mInstalled:\033[0m %s/bin/pokedex\n' "$PREFIX"
        printf '\033[2mPress \033[0m\033[1m⌘Space\033[0m\033[2m and type "Pok" to launch it, or run \033[0m\033[1mpokedex\033[0m\033[2m from any directory.\033[0m\n'
    else
        printf '\n\033[32mInstalled:\033[0m %s/bin/pokedex\n' "$PREFIX"
        printf '\033[2mRun \033[0m\033[1mpokedex\033[0m\033[2m from any directory to launch the app.\033[0m\n'
    fi
    exit 0
fi

# --- Fallback (no sudo / prompt cancelled) -----------------------------------
# The build already succeeded, so don't leave the user empty-handed if the
# privileged install can't run. Fall back to the old non-invasive alias hint
# pointing at the built binary — they paste one line and still get `pokedex`.
# macOS builds a .app bundle; point the alias at its inner binary (running that path
# still resolves the bundle, so camera permission works). Elsewhere it's a plain binary.
# Globbed, not hardcoded: CMake owns the app name ("Pokédex TCG by Mazuh.app" — see the
# if(APPLE) block in CMakeLists.txt), so look inside Contents/MacOS for the binary rather
# than deriving one name from the other.
BIN=""
for bundle in "$BUILD"/*.app; do
    [ -d "$bundle" ] || continue
    for exe in "$bundle"/Contents/MacOS/*; do
        [ -x "$exe" ] && [ -f "$exe" ] && BIN="$exe"
    done
done
[ -n "$BIN" ] || BIN="$BUILD/pokedex_tcg"
case "${SHELL:-}" in
    *zsh)  rc="~/.zshrc" ;;
    *bash) rc="~/.bashrc" ;;
    *)     rc="your shell config" ;;
esac

printf '\n\033[33mInstall to %s skipped.\033[0m The binary is built at:\n  %s\n' "$PREFIX/bin" "$BIN"
printf '\n\033[2mTo get a \033[0m\033[1mpokedex\033[0m\033[2m command without sudo, add to %s:\033[0m\n' "$rc"
printf '\033[2m  alias pokedex="%s"\033[0m\n' "$BIN"
