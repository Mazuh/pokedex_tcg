#pragma once

#include <optional>
#include <string>
#include <vector>

#include "core/domain/region.h"
#include "core/domain/types.h"

namespace pokedex {

// COLLECTION (value object) — the pocket layout of ONE page of a binder, as rows
// of pockets across columns (a 3x3 album page holds nine cards). It describes the
// physical album, not the collection: it is what lets the guide say which page and
// which pocket a card sits in.
//
// Both fields are meaningful only together, so a binder holds the WHOLE struct as
// an optional rather than two independent optional ints — that makes the "rows
// known but columns unknown" state unrepresentable instead of merely invalid, and
// spares every reader a defensive branch. Both are >= 1 whenever the struct exists;
// the app layer enforces that on the way in.
struct CardBinderPocketGrid {
    int rows = 0;
    int columns = 0;
};

// How many cards one page of that grid holds.
inline int pocketsPerPage(const CardBinderPocketGrid& grid) {
    return grid.rows * grid.columns;
}

// COLLECTION (source of truth) — a run of deliberate EMPTY pockets the user left
// immediately before one row of a binder's guide.
//
// This is the region-agnostic way to control where a page breaks. Species run
// contiguously by dex number, so a region can end mid-page and split the next
// region's first evolution line across two pages; leaving a few pockets empty
// pushes what follows onto a fresh page. Collectors break pages by different
// rules (by region, by evolution line, by set), so rather than guess at one, the
// app just stores where they chose to leave gaps.
//
// Exactly ONE anchor is set, and the blank sits immediately before that row:
//
//   beforeDexNum — a species. The durable choice, and the one the GUI mints for
//       any row that has a species: it survives the card being deleted and
//       re-added, and works on a species not owned yet (a placeholder row). A
//       species with several copies filed takes the blanks before the first of
//       them, since the anchor names the species, not one physical card.
//   beforeCopyId — one exact filed card. For a species-free row (a Trainer,
//       Energy or promo card), which has no dex number to name it.
//
// An anchor whose row isn't in the guide (the region was un-scoped, the copy was
// deleted or moved to another binder) simply produces nothing — the record is kept
// rather than pruned, so restoring the region restores the layout intact.
struct CardBinderBlank {
    std::optional<PokemonDexNum> beforeDexNum;
    std::optional<CardCopyId> beforeCopyId;
    int blanks = 0;  // >= 1
};

// COLLECTION (source of truth) — one card pulled OUT of the guide's natural filed
// order and pinned immediately before another row.
//
// The guide's order is otherwise entirely derived (species by dex number, a species'
// copies in filed order, species-free cards last), which is the right default but
// leaves no way to say "this card goes at page 18, pocket 2x2" — the gesture a
// collector arranging a real album actually makes. A placement is that override, and
// it is deliberately stored the same way a blank pocket is: RELATIVE to another row,
// never as a page/pocket coordinate. Coordinates are a display of the resolved order,
// so storing one would go stale the moment anything before it changed; an anchor
// survives every such edit.
//
// The anchor is the row this card sits immediately before:
//
//   beforeCopyId — one exact filed card. The usual case, and deliberately more
//       precise than the choice CardBinderBlank made: a move is about ONE sleeve, so
//       it must be able to land between the second and third copy of a species.
//   beforeDexNum — a species, used only when the target row is a PLACEHOLDER (a
//       listed species holding nothing), which has no card to name.
//   NEITHER set — at the very end of the guide, after the species-free tail. Unlike
//       a blank (which always names a row), a placement needs this so the last pocket
//       is reachable at all.
//
// An anchor whose row isn't in the guide (the card was deleted or refiled elsewhere)
// makes the placement UNRESOLVABLE, and the copy simply renders in its natural dex
// position instead — visible and recoverable, never lost. The same holds for a cycle.
//
// A placement may anchor to a copy that is itself placed (that is how you target a
// pocket a moved card already holds), so placements chain. `ordinal` orders the
// placements that share ONE anchor, ascending, emitted nearest-last: a card aimed at
// the anchor row's own pocket belongs closest to it, so appending (max + 1) is the
// correct assignment and no renumbering is ever needed.
struct CardBinderPlacement {
    CardCopyId cardCopyId;  // the copy being placed
    std::optional<PokemonDexNum> beforeDexNum;
    std::optional<CardCopyId> beforeCopyId;
    int ordinal = 0;
};

// COLLECTION (source of truth) — a physical album that stores and displays
// cards in plastic pockets and pages (usually ungraded, unless a slab binder).
// It owns nothing directly; it is a lens. Copies point back at it via binderId,
// and its per-Pokémon "guide" is computed lazily, never stored.
//
// pokemonRegions is the set of regions the binder is scoped to — a binder may
// span more than one (e.g. a "Kanto + Johto" album), so the guide lists every
// species to capture across all of them (including ones the user owns no copy of
// yet). An empty vector means the binder is region-less: its guide shows only the
// species it already has cards filed for. Held as a vector kept in canonical
// (enum) order with no duplicates — the app/storage layers enforce that shape.
//
// capacity and pocketGrid describe the PHYSICAL album and are both optional — a
// binder recorded before they existed, or one whose owner never measured it,
// simply doesn't say. Neither is derived from the other: capacity is a fact about
// the album ("a 360-card binder"), not pages x pocketsPerPage, since the page
// count isn't recorded. Capacity is never enforced — a stuffed binder is a real
// thing, so exceeding it is displayed, never blocked.
//
// pocketBlanks and cardPlacements are the binder's MANUAL ARRANGEMENT — where the
// user chose to leave a pocket empty and where they chose to override the natural
// filed order. Both are written only by their own dedicated verbs, never by the
// name/region/layout edit path — see CardBinderRepository::update.
struct CardBinder {
    CardBinderId id;
    std::string name;
    std::vector<Region> pokemonRegions;
    std::optional<int> capacity;                     // max cards the album holds
    std::optional<CardBinderPocketGrid> pocketGrid;  // one page's pocket layout
    std::vector<CardBinderBlank> pocketBlanks;
    std::vector<CardBinderPlacement> cardPlacements;
    Timestamp insertedAt;  // UTC, set by app
    Timestamp updatedAt;   // UTC, set by app
};

}  // namespace pokedex
