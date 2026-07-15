#pragma once

#include <vector>

#include "core/domain/types.h"
#include "core/domain/wishlist.h"

namespace pokedex {

class Database;

// STORAGE — persistence for the Wishlist root, backed by the wishlist and
// wishlist_source tables (the domain's std::set<std::string> sources normalized
// into rows). Pure storage: it neither mints ids nor reads the clock.
//
// This slice exposes only what the binder guide needs: a write primitive (so the
// status pipeline is unit-testable) and the read that resolves the "Wished" case.
class WishlistRepository {
public:
    explicit WishlistRepository(Database& db) : db_(db) {}

    // Insert a wishlist row for a Pokémon plus one wishlist_source row per source.
    // Throws StorageError (e.g. on a duplicate pokemon_dex_num).
    void add(const Wishlist& wishlist);

    // The dex numbers of Pokémon that have at least one wishlist source — the
    // "Wished" case, which the CollectionStatus docstring defines as ">=1 wishlist
    // source" (a row with no sources does not count).
    std::vector<PokemonDexNum> wishedDexNums();

private:
    Database& db_;
};

}  // namespace pokedex
