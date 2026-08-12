#pragma once

#include <set>
#include <span>
#include <vector>

#include "core/domain/card_binder.h"
#include "core/domain/card_binder_entry.h"
#include "core/domain/types.h"

namespace pokedex {

class CardCopyRepository;
class WishlistRepository;

// The CHECKLIST a binder holds slots for: every species across all of its regions, or —
// for a binder with no regions, which has no checklist to derive — whatever species its
// rows happen to show.
//
// It is deliberately NOT "the distinct species among the rows": a binder scoped to Kanto
// can hold a Johto card as an extra, and that card's species is not a slot the album
// reserves. Counting it would make a 343-species checklist read 344 the moment an extra
// was filed. Shared so the guide and the screen that totals it can't disagree.
std::set<PokemonDexNum> listedSpecies(const CardBinder& binder,
                                      std::span<const CardBinderEntry> rows);

// APP — builds a binder's guide: the CardBinderEntry projection the "open binder"
// screen shows, one row per card filed in the binder plus a placeholder row per
// listed species holding none. This is the buildBinderEntries service the inferred
// zone (card_binder_entry.h, collection_status.h) refers to. It reads the
// source-of-truth entities (copies + wishlist) and the compile-time Pokédex
// catalog, then recomputes each row — it stores nothing.
class BinderGuideService {
public:
    BinderGuideService(CardCopyRepository& copies, WishlistRepository& wishlist)
        : copies_(copies), wishlist_(wishlist) {}

    // The binder's guide as a list of rows, where a row is a SLOT rather than a
    // species (see CardBinderEntry). Ordered as:
    //
    //   1. The CHECKLIST rows, by dex number: every species across all of the
    //      binder's regions (see listedSpecies). A species with nothing filed here
    //      yields ONE placeholder row whose status follows the first-match-wins
    //      CollectionStatus precedence (Wished / Elsewhere / Incomplete); a species
    //      with N copies filed here yields N rows in filed order and NO
    //      placeholder, each carrying its own copy's ownership as its status. The
    //      slot is RESERVED: a species whose every copy was pinned to some other
    //      pocket falls back to a placeholder, so this run's length — and hence
    //      every page break after it — never changes as cards are filed or moved.
    //   2. The EXTRAS, in filed order, after every checklist row: everything the
    //      checklist doesn't claim. Species-free Trainer/Energy/promo copies, copies
    //      whose dex number doesn't resolve, and copies of a species outside the
    //      binder's regions all land here, with no derived order to speak of —
    //      the user arranges them by hand (see CardBinderPlacement).
    //
    // Interleaved through both: the binder's BLANK POCKETS (CardBinderBlank), one row
    // each, immediately before the row their anchor names — a species (ahead of all its
    // copy rows, or of its placeholder) or one exact filed card. A blank whose anchor
    // has no row here (the region was un-scoped, the copy was deleted or moved away)
    // produces nothing; this stays a read-only projection and never prunes the record,
    // so restoring the region restores the layout.
    //
    // A regionless binder therefore shows exactly what is filed in it — and only there
    // does a species stop being listed once its last copy is moved away, since it was
    // never a reserved slot to begin with. "Filed order" is the repository's
    // inserted_at, rowid ordering.
    std::vector<CardBinderEntry> buildEntries(const CardBinder& binder);

    // The verdict a placeholder row for `dexNum` would carry — the same first-match-wins
    // precedence buildEntries applies (Wished / Elsewhere / Incomplete).
    //
    // Published for the move planner, which has to project the placeholder a move leaves
    // behind but cannot reach the wishlist or the other binders to judge it.
    //
    // `binderId` is REQUIRED, and is the binder being asked about: it scopes the "owned
    // elsewhere" read to the OTHER binders, exactly as buildEntries does. Passing an empty
    // id excludes nothing, so a species owned right here reads as Elsewhere — which is
    // silently wrong rather than obviously so, hence no default.
    CollectionStatus placeholderStatusFor(const CardBinderId& binderId, PokemonDexNum dexNum);

private:
    CardCopyRepository& copies_;
    WishlistRepository& wishlist_;
};

}  // namespace pokedex
