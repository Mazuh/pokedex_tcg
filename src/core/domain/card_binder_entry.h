#pragma once

#include <optional>

#include "core/domain/collection_status.h"
#include "core/domain/pokemon.h"
#include "core/domain/types.h"

namespace pokedex {

// INFERRED (projection) — one row of a binder's guide. Built on demand by an
// app-layer service (BinderGuideService::buildEntries) from the source-of-truth
// entities; it has no id, no repository, and is never mutated — only recomputed.
//
// A row is a SLOT, not a species. A binder is a physical object, so its guide has
// to account for every card actually in it: duplicates of the same Pokémon are
// distinct physical cards, and a Trainer/Energy card depicts no species at all yet
// still occupies a page. Hence one row per filed copy, plus a placeholder row for
// each listed species holding nothing — rather than the old one-row-per-species
// checklist, which could only fold several copies into a single verdict and had
// nowhere to put a species-free card.
//
// All four combinations of the two optionals are produced:
//
//   pokemon set, cardCopyId unset — PLACEHOLDER: a species the binder lists (it
//       falls in one of the binder's regions) with no copy filed here. Its status
//       is the species-level verdict: Wished, Elsewhere, or Incomplete.
//   pokemon set, cardCopyId set   — a copy of that species filed in this binder.
//       Its status is that copy's own ownership (Owned→Completed, Incoming→
//       Incoming, Removed→Removed) — not a per-species precedence.
//   pokemon unset, cardCopyId set — a species-free card filed in this binder (a
//       Trainer/Energy/promo, or a copy whose dex number isn't in the catalog).
//       Same per-copy status rule; it has no Pokédex number to sort among the
//       species rows, so it lands after them.
//   both unset                    — a BLANK POCKET the user deliberately left
//       empty to control where a page breaks (see CardBinderBlank). It stands for
//       no species and no card, so it carries NO status — hence the optional.
//
// It carries just the verdict plus the identity of the copy it stands for. For
// the copy's own fields (printing, condition, prices) or the wishlist sources,
// the app looks those entities up itself.
struct CardBinderEntry {
    std::optional<Pokemon> pokemon;        // nullopt = a species-free card, or a blank
    std::optional<CardCopyId> cardCopyId;  // nullopt = a species placeholder, or a blank
    std::optional<CollectionStatus> status;  // nullopt = a blank pocket: nothing to report
};

// Whether this row occupies a physical pocket in the album — the predicate that
// decides which rows the page/pocket count advances over, and so where every page
// break falls.
//
// READ IT CAREFULLY: everything holds a pocket EXCEPT a Removed copy. That row stays
// listed and grayed as frozen history, but the card is not in the sleeve, so counting
// it would push every card after it one pocket along and misreport the page for the
// whole rest of the binder. The two row kinds that hold NO card do still hold a
// pocket, deliberately: a PLACEHOLDER (a listed species whose sleeve is reserved for
// it) and a BLANK (a pocket whose entire purpose is to occupy space).
//
// Writing the tempting inverse — "has a card and isn't Removed" — makes blanks free
// and breaks paging outright while every test still passes, which is exactly why this
// lives in one shared place rather than being spelled out at each use: the guide view
// renders from it and the move planner counts pockets with it, and the two must agree
// on every row or a move would land a card in the wrong sleeve.
inline bool holdsPocket(const CardBinderEntry& entry) {
    return !(entry.status && *entry.status == CollectionStatus::Removed);
}

// Whether this row is a pocket the user deliberately left empty — the fourth shape
// above, naming neither a species nor a card. Shared rather than re-spelled per caller
// because a blank is the one row kind with no identity of its own: the guide view, the
// move planner and the blank button all recognise it purely by this shape.
inline bool isBlankPocket(const CardBinderEntry& entry) {
    return !entry.pokemon && !entry.cardCopyId;
}

}  // namespace pokedex
