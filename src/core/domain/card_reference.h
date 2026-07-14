#pragma once

#include <compare>
#include <string>

namespace pokedex {

// Value object — the printed identity of a card, read straight off it: e.g.
// expansionCode "MEW", language "EN", collectorNumber "151/165".
//
// Immutable and compared by value. It is embedded in a CardCopy to record which
// printing that copy is. Ordering is defaulted so copies can be grouped by
// printing (map/set key).
struct CardReference {
    std::string expansionCode;
    std::string language;
    std::string collectorNumber;

    auto operator<=>(const CardReference&) const = default;
};

}  // namespace pokedex
