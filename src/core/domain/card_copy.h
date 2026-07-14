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
struct CardCopy {
    CardCopyId id;
    PokemonDexNum pokemonDexNum;           // which species this copy depicts
    CardReference cardRef;                 // which printing this is a copy of
    CardOwnership ownership;
    CardCondition condition;
    std::optional<CardBinderId> binderId;  // filed in this binder, if any;
                                           // clearing it never affects the copy
    std::string comments;                  // multiline free text: capture story,
                                           // price, seller, imperfections, dates…
    Timestamp insertedAt;                  // UTC, set by app
    Timestamp updatedAt;                   // UTC, set by app
};

}  // namespace pokedex
