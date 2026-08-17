#pragma once

#include <array>
#include <span>
#include <unordered_map>
#include <vector>

#include "core/domain/pokemon.h"
#include "core/domain/region.h"
#include "core/domain/types.h"

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

// How far the collection has come in one region: how many of its species are
// "captured" out of how many exist, plus the copies backing that. A recomputed
// fold of the browse list, stored nowhere.
//
// `cards` counts only Owned copies that DEPICT one of those species, so it is
// legitimately smaller than the row count in My Cards: a Trainer/Energy card
// depicts no species, and so belongs to no region and to no figure here.
struct RegionProgress {
    Region region;
    int capturedSpecies;  // species in this region with >=1 Owned copy
    int totalSpecies;     // catalog species in this region
    int cards;            // Owned, species-tied copies of those species
};

// The same three figures for the whole collection — the headline above a
// per-region breakdown. It carries no Region, being the fold of all of them.
struct CollectionProgress {
    int capturedSpecies;
    int totalSpecies;
    int cards;
};

// APP — per-region capture progress, one row per region in the canonical kRegions
// order (a std::array, so "every region reports exactly once" is a property of the
// type rather than a promise a caller has to trust). A species counts as captured
// when it has at least one Owned copy — `ownedCount > 0`, which is exactly the
// predicate behind the browser's Owned column, so the figures can never contradict
// the rows beneath them.
//
// Pass the WHOLE browse list from listAll — never a search-filtered subset. The
// denominators are the catalog's per-region species counts *because* every catalog
// species is present; a filtered span would report a complete-looking "95 of 95"
// for whatever happened to be on screen.
std::array<RegionProgress, kRegions.size()> regionProgress(
    std::span<const PokemonBrowseEntry> entries);

// APP — the whole-collection headline, folded from the region rows rather than
// recounted from the entries: the total sits above the breakdown on screen, so
// deriving it from those very rows makes agreement structural instead of merely
// likely.
CollectionProgress totalProgress(std::span<const RegionProgress> regions);

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

    // Same pairing, but from a caller-supplied dex → owned-count map instead of
    // re-querying. For a caller that already materialized the owned copies (the
    // browser view also needs the copies themselves, for the detail panel's copy
    // mode) and so can derive the counts without a second scan of card_copy — the
    // map must use the same predicate as ownedCountsByDexNum (Owned, species-tied).
    // A species absent from the map reports ownedCount 0.
    std::vector<PokemonBrowseEntry> listAll(
        const std::unordered_map<PokemonDexNum, int>& ownedCounts) const;

private:
    CardCopyRepository& copies_;
};

}  // namespace pokedex
