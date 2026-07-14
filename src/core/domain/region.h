#pragma once

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

}  // namespace pokedex
