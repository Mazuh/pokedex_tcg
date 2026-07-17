#pragma once

namespace pokedex {

// COLLECTION — the subjective grading of a physical CardCopy, from the standard
// TCG scale, based on the copy's physical imperfections. Declared best-to-worst.
//
// This is the self-assessed, everyday scale. A valuable copy may instead be
// professionally *graded* — evaluated by a trusted company against more
// objective measures and issued a unique certificate number; non-graded copies
// have no such standard identifier. (This model does not yet store certificate
// numbers; condition is optional on a CardCopy — nullopt means ungraded here.)
enum class CardCondition {
    NearMint,          // NM
    LightlyPlayed,     // LP
    ModeratelyPlayed,  // MP
    HeavilyPlayed,     // HP
    Damaged,
};

}  // namespace pokedex
