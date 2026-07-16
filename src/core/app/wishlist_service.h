#pragma once

#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/domain/pokemon.h"
#include "core/domain/types.h"
#include "core/domain/wishlist.h"

namespace pokedex {

class WishlistRepository;

// APP — raised when a wishlist operation is invalid, e.g. adding a blank source.
class WishlistError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// APP — one row of the unscoped wishlist section: a catalog species paired with
// its wishlist sources. A recomputed projection (the catalog side is fixed
// reference data, the sources are the source of truth), so it lives in app/, not
// as a domain type — the sibling of PokemonBrowseEntry.
struct WishlistEntry {
    Pokemon pokemon;
    std::vector<std::string> sources;  // sorted, as stored
};

// APP — the verbs of the Wishlist root: the manage-sources use cases behind both
// the per-Pokémon editor and the unscoped wishlist section. It owns the clock the
// domain and storage layers deliberately do not; the clock is injectable so tests
// stay deterministic. Sources are free text (a seller name or an "http…" link);
// they are trimmed and a blank one is rejected.
class WishlistService {
public:
    using Clock = std::function<Timestamp()>;

    explicit WishlistService(WishlistRepository& repo, Clock clock = systemClock());

    // A single Pokémon's wishlist, or nullopt when it has none yet.
    std::optional<Wishlist> forPokemon(PokemonDexNum pokemonDexNum);

    // Every wished species with its sources, paired with its catalog entry, in
    // dex-number order — the data behind the unscoped section (the GUI flattens
    // each entry to one row per source).
    std::vector<WishlistEntry> listAll();

    // Add a source to a Pokémon's wishlist, creating the wishlist if needed. The
    // source is trimmed; a blank one throws WishlistError. A duplicate is a no-op
    // (the source set already holds it). Bumps updatedAt (and sets insertedAt when
    // the wishlist is new) to now().
    void addSource(PokemonDexNum pokemonDexNum, std::string source);

    // Replace one existing source string with another (an in-place edit). The new
    // value is trimmed and must be non-blank. Bumps updatedAt to now(). A no-op
    // when the Pokémon has no wishlist, or when oldSource is not one of its
    // sources (a stale edit — never fabricates a new source).
    void editSource(PokemonDexNum pokemonDexNum, const std::string& oldSource,
                    std::string newSource);

    // Remove a source. When it was the last one, the wishlist row is deleted so no
    // sourceless parent lingers. Bumps updatedAt to now() otherwise.
    void removeSource(PokemonDexNum pokemonDexNum, const std::string& source);

    // The default, exposed so callers can wrap/compose it if needed.
    static Clock systemClock();

private:
    WishlistRepository& repo_;
    Clock clock_;
};

}  // namespace pokedex
