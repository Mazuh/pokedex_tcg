#pragma once

#include <string>

namespace pokedex {

// APP — a random RFC 4122 version-4 UUID as a 36-char string. Ids are minted in
// the app layer (never the domain or storage), and randomness — unlike the clock —
// is fine to draw here: an id only needs to be unique, not reproducible. Shared by
// every root's service so there is one id scheme across the app.
std::string newUuidV4();

}  // namespace pokedex
