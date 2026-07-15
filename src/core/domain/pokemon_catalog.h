#pragma once

#include <span>

#include "core/domain/pokemon.h"

namespace pokedex {

// CATALOG — the full National Pokédex table (#1–1025, Bulbasaur … Pecharunt),
// one entry per dex number, base species only. This is the authoritative-but-
// fixed reference data described in pokemon.h: it ships as a compile-time
// constant with no persistence and no repository. The collection side refers to
// a species by PokemonDexNum; this table is what those numbers resolve against
// (autocomplete today, image/card API lookups later).
//
// Entries are sorted by dexNumber, contiguous over 1..1025 with no gaps, and
// have unique dexNumbers and unique names. Region is derived from the species'
// generation. Alternate forms (regional, Mega/Gigantamax, gendered, etc.) are
// intentionally excluded — they share a base dex number and the model has no
// slot for them yet.
std::span<const Pokemon> pokemonCatalog();

}  // namespace pokedex
