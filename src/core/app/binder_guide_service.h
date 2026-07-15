#pragma once

#include <vector>

#include "core/domain/card_binder.h"
#include "core/domain/card_binder_entry.h"

namespace pokedex {

class CardCopyRepository;
class WishlistRepository;

// APP — builds a binder's guide: the CardBinderEntry projection the "open binder"
// screen shows. This is the buildBinderEntries service the inferred zone
// (card_binder_entry.h, collection_status.h) refers to. It reads the
// source-of-truth entities (copies + wishlist) and the compile-time Pokédex
// catalog, then recomputes each row — it stores nothing.
class BinderGuideService {
public:
    BinderGuideService(CardCopyRepository& copies, WishlistRepository& wishlist)
        : copies_(copies), wishlist_(wishlist) {}

    // One entry per Pokémon in the binder's guide, ordered by dex number. The row
    // set is the binder's region species (when it has a region) unioned with any
    // species that has a copy filed in it (so a filed card is never hidden). A
    // regionless binder therefore shows only its filed species. Each Pokémon's
    // status follows the first-match-wins CollectionStatus precedence.
    std::vector<CardBinderEntry> buildEntries(const CardBinder& binder);

private:
    CardCopyRepository& copies_;
    WishlistRepository& wishlist_;
};

}  // namespace pokedex
