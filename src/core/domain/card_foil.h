#pragma once

namespace pokedex {

// COLLECTION — a physical CardCopy's foil treatment (a.k.a. finish): how the card is
// printed and where the holographic effect appears. Independent of CardRarity — e.g. a
// Double Rare can be Holo while an Ultra Rare is Textured. Optional on a CardCopy: a
// copy may be recorded without one (nullopt).
//
// A categorical list, not a ranking. Declaration order is kAllFoils' order, the picker's
// order and the Foil column's sort rank, and runs roughly from the everyday finishes to
// the pattern-specific ones; NonHolo is first as the plain default and the retired
// values are last. Pinned by a test.
//
// STORAGE CONTRACT — same as CardRarity: the on-disk token is the enumerator's spelling
// and an unknown token throws, so this enum is APPEND-ONLY. A value may be reordered or
// relabelled, never renamed or removed; withdrawing one from the picker is what
// foilIsRetired() expresses.
enum class CardFoil {
    NonHolo,           // no holographic effect anywhere
    Holo,              // only the artwork window is holographic
    ReverseHolo,       // foil everywhere EXCEPT the artwork window
    FullCardHolo,      // foil across most or all of the card
    Textured,          // raised/embossed texture over the foil (URs, SIRs, HRs)
    CosmosHolo,        // circular "galaxy" orb pattern
    WaterWebHolo,      // flowing wavy pattern, heavily used in Sun & Moon
    VerticalLineHolo,  // vertical-line pattern, Sword & Shield era
    MirageHolo,        // modern horizontal refraction pattern
    CrackedIceHolo,    // shattered-glass appearance, older promos
    ConfettiHolo,      // tiny reflective confetti-like particles
    CrosshatchHolo,    // crosshatched / grid-like pattern
    MirrorHolo,        // uniform mirror-like reflective foil
    OtherFoil,         // an unusual or unidentified treatment — see the note below

    // Retired — never offered in the picker, kept so copies already recorded with it
    // still load and display. Never delete one; see the storage contract above.
    HDHolo,  // "high definition" was never a distinct printed treatment
};

// Every CardFoil, in declaration order — the ONE list the codecs, the picker and the
// tests read, so none of them can drift from the enum (mirrors kAllRarities).
//
// INVARIANT, pinned by a test: kAllFoils[i] == static_cast<CardFoil>(i).
inline constexpr CardFoil kAllFoils[] = {
    CardFoil::NonHolo,       CardFoil::Holo,           CardFoil::ReverseHolo,
    CardFoil::FullCardHolo,  CardFoil::Textured,       CardFoil::CosmosHolo,
    CardFoil::WaterWebHolo,  CardFoil::VerticalLineHolo, CardFoil::MirageHolo,
    CardFoil::CrackedIceHolo, CardFoil::ConfettiHolo,  CardFoil::CrosshatchHolo,
    CardFoil::MirrorHolo,    CardFoil::OtherFoil,      CardFoil::HDHolo,
};

// Whether a foil is withdrawn from the picker (still storable and still displayed).
// An exhaustive switch, so a NEW enumerator fails -Wswitch under -Werror until it is
// classified — the foil counterpart of rarityGroup().
//
// Note OtherFoil is NOT retired and is NOT the same as leaving the field unset: unset
// means "not recorded yet" (and keeps the form's ⚠ marker lit), OtherFoil means
// "recorded, and it is something unusual".
constexpr bool foilIsRetired(CardFoil foil) {
    switch (foil) {
        case CardFoil::NonHolo:
        case CardFoil::Holo:
        case CardFoil::ReverseHolo:
        case CardFoil::FullCardHolo:
        case CardFoil::Textured:
        case CardFoil::CosmosHolo:
        case CardFoil::WaterWebHolo:
        case CardFoil::VerticalLineHolo:
        case CardFoil::MirageHolo:
        case CardFoil::CrackedIceHolo:
        case CardFoil::ConfettiHolo:
        case CardFoil::CrosshatchHolo:
        case CardFoil::MirrorHolo:
        case CardFoil::OtherFoil:
            return false;

        case CardFoil::HDHolo:
            return true;
    }
    return true;  // unreachable for a valid enum value
}

}  // namespace pokedex
