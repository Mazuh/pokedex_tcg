# Pokédex TCG by Mazuh

Just another Pokédex, but specifically designed to
manage my physical card collection using local files.

## About the Software

### Use Cases

- ~~As a user, I want to quickly browse any existing Pokémon
by partial name, so I see its region and how many copies I own.~~
- ~~As a user, I want to see a Pokémon's picture~~ so browsing doubles
as tracking what I still need. *(The manual "mark as collected" toggle is
dropped: whether a Pokémon is collected is inferred from the real card copies
you own — the stored entities are the single source of truth — not a separate
flag. The artwork already shows in the Pokémon detail panel, and owned-copy
counts / "My Cards" track what you still need.)*
- ~~As a user, I want to create a binder optionally initialized
with a selected region, so I can initialize a binder already having
all the Pokémon even which I don't own card copies yet but guides
me on the journey to capture them all.~~
- ~~As a user, I want to remove a binder, so I can stop tracking
a physical binder (it doesn't affect the cards containing it).~~
- ~~As a user, I want to choose where my collection workspace is stored,
so that I can use a local directory, iCloud Drive, Dropbox, a NAS, or
another synchronized folder.~~
- ~~As a user, I want to browse any existing cards for each Pokémon,
so I can assign them to a binder or simply say that I own it
(this is an optional part of marking a Pokémon as collected).~~
- ~~As a user, I want to browse within my owned cards, so I can keep
track of my inventory.~~
- ~~As a user, I want to add a free text comment to a card I own, so
I can put maintain historical data similary to what video games do like
when the creature was captured, where, price, from who, special variation,
observed imperfections etc., which help me both to reevaluate value over
time and to possibly investigate frauds in the future.~~
- ~~As a user, I want to remove a card I own with an optional note
to append to the comments, so I can keep auditable track of
what I owned but don't own anymore and still have clear stats even
with removed cards still being reasonably easily discovered.~~
- As a user, I want to hard delete a card that I previously removed,
so I can keep my data clean of things that were inserted by mistake.
- ~~As a user, I want to manage a card wishlist to a Pokémon registry,
so I can manage links or salespeople sources.~~
- ~~As a user, I want to see my entire wishlist unscoped but grouped
by Pokémon, so this can easily guide me on building a shopping cart
in external resselers.~~
- ~~As a new user, I don't want to register accounts or pay for
cloud services, so I can manage my private collection in my
own machine with optional cloud storage backup,
without vendor lockin of paid services, nor proprietary formats,
nor personal data concerns.~~
- ~~As a operational system user, I want my storage kept in an open,
non-proprietary SQLite database (inspectable with the `sqlite3` CLI or
DB Browser for SQLite) alongside a multimedia cache folder, so my data is
accessible and not locked by this app.~~
- ~~As a new user, on first launch I want to choose where my collection
workspace folder lives (a local directory, or one inside iCloud, Dropbox,
or a NAS), so the app can create its SQLite database there and remember it.~~

### Entities and Glossary

- **Pokémon**: Fictional creatures that humans (known as Trainers) can
    catch, train, and battle, usually having powers from nature,
    and each species have a name and region.
- **National Pokédex Number**: sequentially identifies each species
    in the franchise, and even when a Pokémon has multiple evolutions,
    each evolved version has a different numeric id, although some
    forms share the same id.
- **Pokémon Region**: a territory in the fictional universe where
    each Pokémon originate from (like Kanto inspired by Japan,
    and Paldea inspired by Portugal and Spain).
- **Pokédex**: an encyclopedia that records information
    about various Pokémon species captured by a trainer,
    usually within a specific region, and many trainers have
    the personal goal of collecting all Pokémon there
    instead of only battling.
- **National Pokédex**: a Pokédex that records information on
    all Pokémon species discovered across the entire franchise,
    rather than just those native to one specific region.
- **Pokémon Card**: in the Trading Card Game (TCG), each Pokémon can be
    represented differently by many cards, each card has a rarity,
    skills, illustration, reference number and (only detected
    by non verbal analysis) variation.
- **Pokémon Card Reference**: readable in the card, it's a combination
    of set code and collector number (like "MEW EN 151/165",
    where "MEW" is the set expansion code, "EN" is the language,
    "151/165" is the collector number), they're written in the
    left-bottom side of the card, next to the copyright notice, and
    can be used to infer rarity.
- **Card Copy**: each card is printed exactly the same multiple times
    with equal card reference, distributed by official vendors,
    ressellers and other collectors, therefore an individual copy
    is an unique actual print with its own conditions.
- **Card Copy Condition**: standardized but subjective grading
    categories of each card copy based on its physical imperfections,
    options are Near Mint (NM), Lightly Played (LP),
    Moderately Played (MP), Heavily Played (HP) and Damaged.
- **Graded Card Copy**: when a card is worth a lot on collectors market,
    then one of its card copies can have conditions evaluated using
    more objective measures taken by trusted companies and given an
    unique certificate number, so worth mentioning that non-graded
    copies have no standard identification system.
- **Card Binder**: a specialized album used to store, organize,
    and display collectible cards in plastic pockets and pages,
    usually not graded except when it's slab binders.

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
