#pragma once

#include <array>

namespace pokedex {

// CATALOG — a territory a Pokémon originates from. Authoritative-but-fixed
// reference data with no persistence: it ships as a compile-time enum, never a
// stored record.
enum class Region {
    Kanto,
    Johto,
    Hoenn,
    Sinnoh,
    Unova,
    Kalos,
    Alola,
    Galar,
    Paldea,
};

// The complete enumeration, in canonical order — the single source anything that
// needs "every region" iterates (storage codecs, the GUI region picker). Adding
// a Region enumerator without extending this array is a compile error (size
// mismatch), so the lists can't silently drift.
inline constexpr std::array<Region, 9> kRegions{
    Region::Kanto, Region::Johto, Region::Hoenn, Region::Sinnoh, Region::Unova,
    Region::Kalos, Region::Alola, Region::Galar, Region::Paldea,
};

}  // namespace pokedex
