#pragma once

#include <optional>
#include <vector>

#include "core/domain/types.h"
#include "core/domain/wishlist.h"

namespace pokedex {

class Database;

// STORAGE — persistence for the Wishlist root, backed by the wishlist and
// wishlist_source tables (the domain's std::set<std::string> sources normalized
// into rows). Pure storage: it neither mints ids nor reads the clock — the app
// supplies the dex number and both audit stamps.
class WishlistRepository {
public:
    explicit WishlistRepository(Database& db) : db_(db) {}

    // Upsert a Pokémon's wishlist: insert the parent row when new (keeping the
    // caller's insertedAt), otherwise bump only updatedAt, then replace its whole
    // source set with `wishlist.sources`. One logical unit across several
    // statements, so it runs in a transaction. Throws StorageError on failure.
    void save(const Wishlist& wishlist);

    // A single Pokémon's wishlist, or nullopt when it has no wishlist row. The
    // returned sources may be empty (a parent row can outlive its last source).
    std::optional<Wishlist> find(PokemonDexNum pokemonDexNum);

    // Every wishlist that still has at least one source, in dex-number order —
    // the source-of-truth behind the unscoped wishlist section. Source-less
    // parent rows are skipped (they render nothing).
    std::vector<Wishlist> listAll();

    // Delete a Pokémon's wishlist row; its source rows cascade away. A no-op when
    // no such row exists.
    void remove(PokemonDexNum pokemonDexNum);

    // The dex numbers of Pokémon that have at least one wishlist source — the
    // "Wished" case, which the CollectionStatus docstring defines as ">=1 wishlist
    // source" (a row with no sources does not count).
    std::vector<PokemonDexNum> wishedDexNums();

private:
    Database& db_;
};

}  // namespace pokedex
