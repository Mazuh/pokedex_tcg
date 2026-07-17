#pragma once

#include <chrono>
#include <string>

namespace pokedex {

// Fundamental type aliases shared across the domain layer.
//
// Every reference across entities is by id/value (never by pointer): a
// CardCopy stores a CardBinderId, not a CardBinder&. This keeps each entity
// independently constructible and testable today, and serializable later.

// National Pokédex number — the id that sequentially identifies each species
// across the whole franchise. Evolutions get distinct numbers, though some
// alternate forms share one. Unique per Pokémon in the catalog; the natural key
// the collection side uses to refer to a species.
using PokemonDexNum = int;

// Synthetic identity for a physical CardCopy. Copies are otherwise
// indistinguishable (same print, no natural identifier), so the app mints one.
using CardCopyId = std::string;

// Identity for a CardBinder.
using CardBinderId = std::string;

// A point in time, always interpreted as UTC. Audit stamps (insertedAt /
// updatedAt) use this. The domain never reads the clock itself — the app layer
// supplies the current time — so domain logic stays pure and testable.
using Timestamp = std::chrono::system_clock::time_point;

}  // namespace pokedex
