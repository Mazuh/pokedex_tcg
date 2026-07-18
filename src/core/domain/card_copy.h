#pragma once

#include <optional>
#include <string>

#include "core/domain/card_condition.h"
#include "core/domain/card_ownership.h"
#include "core/domain/card_reference.h"
#include "core/domain/types.h"

namespace pokedex {

// COLLECTION (source of truth) — one individual physical print the user owns,
// is receiving, or once owned. Copies are genuinely indistinguishable, so
// identity is a synthetic id minted by the app.
//
// It records which species it depicts (pokemonDexNum) and which printing it is
// (cardRef) directly: there is no separate Card entity to resolve through. Its
// remaining fields describe the physical copy — state, condition, free-text
// history, and which binder (if any) it is filed in. It deliberately does NOT
// know its "collected" standing; that is inferred (see CardBinderEntry).
//
// pokemonDexNum is optional: most cards depict a species, but a TCG collection
// also holds cards that depict none — Trainer and Energy cards, promos, etc.
// A species-free copy (nullopt) is a fully supported second-class citizen: it is
// created, edited, image-searched, and filed in binders like any other, but it
// carries no dex number and so never appears in a species-oriented projection
// (the Pokémon browser, a binder guide) — only in the flat "My Cards" inventory.
struct CardCopy {
    CardCopyId id;
    std::optional<PokemonDexNum> pokemonDexNum;  // which species, if any (nullopt
                                                 // for Trainer/Energy/etc. cards)
    CardReference cardRef;                 // which printing this is a copy of
    CardOwnership ownership;
    std::optional<CardCondition> condition;  // grading is optional — a copy may be
                                             // recorded ungraded (nullopt)
    std::optional<CardBinderId> binderId;  // filed in this binder, if any;
                                           // clearing it never affects the copy
    std::string comments;                  // multiline free text: capture story,
                                           // price, seller, imperfections, dates…
    Timestamp insertedAt;                  // UTC, set by app
    Timestamp updatedAt;                   // UTC, set by app
};

}  // namespace pokedex
