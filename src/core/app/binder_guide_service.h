#pragma once

#include <vector>

#include "core/domain/card_binder.h"
#include "core/domain/card_binder_entry.h"

namespace pokedex {

class CardCopyRepository;
class WishlistRepository;

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
    //   1. The species rows, by dex number: every species across all of the
    //      binder's regions, unioned with any species that has a copy filed here
    //      (so a filed card is never hidden). A species with nothing filed here
    //      yields ONE placeholder row whose status follows the first-match-wins
    //      CollectionStatus precedence (Wished / Elsewhere / Incomplete); a species
    //      with N copies filed here yields N rows in filed order and NO
    //      placeholder, each carrying its own copy's ownership as its status.
    //   2. The species-free cards filed here — Trainer/Energy/promo copies, plus
    //      any copy whose dex number doesn't resolve in the catalog — in filed
    //      order, after every species row, since they have no dex number to sort
    //      among them.
    //
    // Interleaved through both: the binder's BLANK POCKETS (CardBinderBlank), one row
    // each, immediately before the row their anchor names — a species (ahead of all its
    // copy rows, or of its placeholder) or one exact filed card. A blank whose anchor
    // has no row here (the region was un-scoped, the copy was deleted or moved away)
    // produces nothing; this stays a read-only projection and never prunes the record,
    // so restoring the region restores the layout.
    //
    // A regionless binder therefore shows exactly what is filed in it. "Filed
    // order" is the repository's inserted_at, rowid ordering.
    std::vector<CardBinderEntry> buildEntries(const CardBinder& binder);

private:
    CardCopyRepository& copies_;
    WishlistRepository& wishlist_;
};

}  // namespace pokedex
