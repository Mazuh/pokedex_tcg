# Pokédex TCG by Mazuh

Just another Pokédex, but specifically designed to
manage my physical card collection using local files.

![Pokédex TCG](screenshot.png)

## Features

- Browse the National Pokédex and see each species' collection status.
- Organize your physical cards into binders.
- Record card copies with printing details, condition and ownership.
- Search the pokemontcg.io catalog to autofill a card and its image.
- Keep a wishlist of the cards and sources you're still after.

## Instructions

### OS Requirements

macOS with [Homebrew](https://brew.sh) and Apple Clang (from Xcode Command
Line Tools: `xcode-select --install`).

Install the build dependencies:

```sh
brew install cmake ninja qt
```

- **cmake** — build system generator
- **ninja** — fast build backend
- **qt** — Qt 6 Widgets, the GUI toolkit

Apple Clang provides the C++23 compiler. GoogleTest is fetched automatically
by CMake, so there is nothing to install for it.

### Local Setup

From the project root:

```sh
# Configure (points CMake at the Homebrew Qt install)
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"

# Build the app and the tests
cmake --build build

# Run the visual Hello World
./build/pokedex_tcg

# Run the tests
ctest --test-dir build --output-on-failure
```

**Quick build & run** — a `dev.sh` wrapper handles the CMake/Ninja steps
(auto-configuring on first use) so day-to-day iteration is one command:

```sh
./dev.sh          # build the app and run it (default)
./dev.sh test     # build and run the test suite
./dev.sh build    # build only
./dev.sh clean    # delete the build dir (forces a fresh configure)
```

## Acknowledgments

Huge thanks to these projects for making their data freely available — this
app would not exist without them:

- [**Pokémon TCG API**](https://github.com/PokemonTCG/pokemon-tcg-api)
  ([pokemontcg.io](https://pokemontcg.io)) — card catalog, printings, and images.
- [**PokeAPI/sprites**](https://github.com/PokeAPI/sprites) — the official
  Pokémon artwork (fetched directly from the sprites repo).
- [**lgreski/pokemonData**](https://github.com/lgreski/pokemonData) — the
  National Pokédex species list the built-in catalog was derived from.

Please consider supporting them.

## License and Legal Disclaimer

The original source code of this project
is licensed under the [MIT License](https://github.com/Mazuh/pokedex_tcg/blob/main/LICENSE).

This is an unofficial, non-commercial, fan-made project. It is not affiliated with,
endorsed by, sponsored by, or otherwise associated with
The Pokémon Company, Nintendo, Creatures Inc., or GAME FREAK inc.

This project is provided as source code only. No publicly hosted service or
precompiled release is provided; users are responsible for downloading, configuring,
compiling, and running the software themselves.

Pokémon, the Pokémon Trading Card Game, and all related names, characters, artwork,
images, trademarks, and other intellectual property are the property of their
respective owners. No ownership of such third-party intellectual property is claimed,
and it is not covered by this project's MIT License.
