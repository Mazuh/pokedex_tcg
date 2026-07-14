#pragma once

namespace pokedex {

// COLLECTION — the subjective grading of a physical CardCopy, from the standard
// TCG scale. Declared best-to-worst.
enum class CardCondition {
    NearMint,          // NM
    LightlyPlayed,     // LP
    ModeratelyPlayed,  // MP
    HeavilyPlayed,     // HP
    Damaged,
};

}  // namespace pokedex
