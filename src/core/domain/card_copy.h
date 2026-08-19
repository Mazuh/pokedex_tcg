#pragma once

#include <optional>
#include <string>

#include "core/domain/card_condition.h"
#include "core/domain/card_foil.h"
#include "core/domain/card_ownership.h"
#include "core/domain/card_rarity.h"
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
// A species-free copy (nullopt) is fully supported: it is created, edited,
// image-searched, and filed in binders like any other. It shows up in the flat
// "My Cards" inventory AND in the guide of the binder it is filed in — a binder is
// a physical object, so its guide must account for every card in it — where, having
// no dex number to sort among the species, it lands after them. It is absent only
// from the Pokémon browser, which is indexed by species by construction.
struct CardCopy {
    CardCopyId id;
    std::optional<PokemonDexNum> pokemonDexNum;  // which species, if any (nullopt
                                                 // for Trainer/Energy/etc. cards)
    CardReference cardRef;                 // which printing this is a copy of
    CardOwnership ownership;
    std::optional<CardCondition> condition;  // grading is optional — a copy may be
                                             // recorded ungraded (nullopt)
    std::optional<CardRarity> rarity;  // how hard the card is to obtain; optional
    std::optional<CardFoil> foil;      // foil treatment / finish; optional, and
                                       // independent of rarity
    std::optional<CardBinderId> binderId;  // filed in this binder, if any;
                                           // clearing it never affects the copy
    // Filed in the binder, but with no home sleeve: a card the user keeps in a loose
    // run at the BACK of the album and rearranges on demand (duplicates, trade fodder,
    // the odd Trainer card). The binder guide then leaves it out of the Pokédex
    // checklist and out of the arrangement machinery entirely, listing it after
    // everything else. It qualifies the FILING, not the card, so it is inert while
    // binderId is unset — and it is the opposite of a CardBinderPlacement, which pins a
    // card to one exact pocket.
    bool noFixedPosition = false;
    std::string comments;                  // multiline free text: capture story,
                                           // price, seller, imperfections, dates…
    std::string externalCardId;            // link to the external catalog card (a
                                           // pokemontcg.io id like "sv3-125") so this
                                           // copy's market prices can be looked up;
                                           // empty when the copy is not linked
    Timestamp insertedAt;                  // UTC, set by app
    Timestamp updatedAt;                   // UTC, set by app
};

}  // namespace pokedex
