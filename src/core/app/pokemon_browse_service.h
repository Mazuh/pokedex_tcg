#pragma once

#include <vector>

#include "core/domain/pokemon.h"

namespace pokedex {

class CardCopyRepository;

// One row of the unscoped Pokédex browser: a catalog species paired with how
// many copies of it the user owns. This is a recomputed projection, not stored
// data — the catalog side is fixed reference data and the count is derived from
// the collection — so it lives here in app/ rather than as a domain type.
struct PokemonBrowseEntry {
    Pokemon pokemon;
    int ownedCount;
};

// APP — builds the unscoped Pokédex browse list: every species in the National
// Pokédex catalog, each with its owned-copy count. The catalog analogue of
// BinderGuideService — it reads the source-of-truth copies and the compile-time
// catalog, then recomputes each row and stores nothing.
class PokemonBrowseService {
public:
    explicit PokemonBrowseService(CardCopyRepository& copies) : copies_(copies) {}

    // One entry per catalog species, in dex-number order (1..N), covering the
    // whole National Pokédex regardless of ownership. A species with no Owned
    // copy reports ownedCount 0.
    std::vector<PokemonBrowseEntry> listAll();

private:
    CardCopyRepository& copies_;
};

}  // namespace pokedex
