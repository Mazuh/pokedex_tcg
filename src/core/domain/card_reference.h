#pragma once

#include <compare>
#include <string>

namespace pokedex {

// Value object — the printed identity of a card: e.g. expansionCode "MEW",
// language "EN", collectorNumber "151/165", setName "151".
//
// A Pokémon Card is a Trading Card Game printing of a species (with its own
// rarity, skills, illustration, and variation); the same species is represented
// by many different cards. The reference is the text on the card — a set code +
// language + collector number, printed bottom-left next to the copyright — that
// names which printing it is and can be used to infer rarity.
//
// `setName` is the human set name (e.g. "Obsidian Flames", "McDonald's Collection
// 2019"). It matters because some sets print NO set code (`expansionCode` is then
// blank) yet reuse the same collector numbers across years — the set name is the
// only thing that tells those printings apart. Every real card belongs to a named
// set, but in this model all reference fields are optional (a copy can be recorded
// with as little as a collector number); the card picker fills them in to
// encourage complete data.
//
// Immutable and compared by value. It is embedded in a CardCopy to record which
// printing that copy is. Ordering is defaulted so copies can be grouped by
// printing (map/set key).
struct CardReference {
    std::string expansionCode;
    std::string language;
    std::string collectorNumber;
    std::string setName;

    auto operator<=>(const CardReference&) const = default;
};

}  // namespace pokedex
