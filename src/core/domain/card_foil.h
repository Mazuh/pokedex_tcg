#pragma once

namespace pokedex {

// COLLECTION — a physical CardCopy's foil treatment (a.k.a. finish): how the card
// is printed and where the holographic effect appears. Independent of CardRarity —
// e.g. a Double Rare can be Holo while an Ultra Rare is Textured. Optional on a
// CardCopy: a copy may be recorded without one (nullopt).
//
// A categorical list, not a ranking; declared in this order and pinned by a test
// (the GUI's foil picker relies on it). NonHolo is first as the plain default.
enum class CardFoil {
    NonHolo,         // no holographic effect anywhere
    Holo,            // only the artwork window is holographic
    ReverseHolo,     // whole card holographic except the artwork window
    CosmosHolo,      // circular "orb" pattern, common on promos
    MirrorHolo,      // uniform reflective foil (mostly Asian releases)
    CrackedIceHolo,  // cracked-ice pattern, older promos
    ConfettiHolo,    // tiny reflective dots / particles
    CrosshatchHolo,  // crosshatched / grid-like pattern
    HDHolo,          // high-definition pattern (some modern products)
    Textured,        // raised surface texture with holo foil (URs, SIRs, HRs)
};

}  // namespace pokedex
