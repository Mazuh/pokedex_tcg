#pragma once

#include <functional>
#include <span>
#include <stdexcept>
#include <vector>

#include "core/domain/card_binder.h"
#include "core/domain/card_binder_entry.h"
#include "core/domain/collection_status.h"
#include "core/domain/types.h"

namespace pokedex {

// APP — raised when a move can't be planned at all: the card isn't filed in this
// binder, it holds no pocket, or the target pocket is negative.
class BinderMoveError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// APP — everything a move changes, computed but not yet written.
//
// It is deliberately a PLAN rather than a direct write, for two reasons. The GUI must
// tell the user how much the album will be rearranged BEFORE committing (shiftedCards),
// which means the whole outcome has to be knowable first. And keeping the arithmetic in
// one pure, Qt-free function is what makes it exhaustively testable — the alternative,
// working it out inside the dialog, would put the hardest logic in the least testable
// layer.
struct BinderMovePlan {
    // The card this plan is about. Named separately from `placement` because a reset has
    // no placement to carry it.
    CardCopyId cardCopyId;

    // Where the card ends up, as an anchor (never a page/pocket coordinate — see
    // CardBinderPlacement for why a coordinate would go stale). nullopt means the card
    // gives up its manual position and returns to derived order.
    std::optional<CardBinderPlacement> placement;

    // The blank-pocket runs to write, as ABSOLUTE counts (CardBinderBlank::blanks is the
    // final figure, not a delta — 0 means "no gap left here"). A move rebalances the
    // blanks around the card it places: those that must precede it are re-anchored onto
    // the card itself, those that follow stay on the anchor row, and the run the card
    // vacated is re-anchored to whatever now follows it. Deltas would double-count
    // whenever two of those anchors turn out to be the same one.
    std::vector<CardBinderBlank> blankSets;

    // The guide exactly as it will read once this plan is written — the same rows
    // BinderGuideService::buildEntries will then produce. Pinning the two against each
    // other in a test is what stops this simulation and the real emitter drifting apart.
    //
    // EMPTY for a reset: where a card lands once it stops being placed is derived order,
    // which only buildEntries knows, so a reset states its blank runs and leaves the row
    // sequence to the guide rather than guessing at it.
    std::vector<CardBinderEntry> projectedRows;

    // How many OTHER cards change pocket. Placeholder rows and blanks are excluded on
    // purpose: they are empty sleeves, so shuffling them costs the user nothing
    // physically, and counting them would cry wolf on every move in a fresh binder that
    // is mostly checklist. 0 means nothing else has to be touched, and the GUI can skip
    // the confirmation entirely.
    int shiftedCards = 0;
};

// Work out how to put `copyId` at `targetPocket` (0-based, counting only the rows that
// hold a pocket — see holdsPocket — exactly as the guide's Page/Pocket columns number
// them).
//
// The card is taken OUT first and then inserted at that pocket, so the target always
// names a position in the FINAL arrangement. That is the physical gesture: pull the card,
// the ones after it slide up, then slot it in where you wanted it. Naming a position in
// the current arrangement instead would land a forward move one pocket short.
//
// If the target pocket holds a blank, the blank is consumed — the card takes that exact
// sleeve and nothing after it moves. A target past the last row places the card at the
// very end. Throws BinderMoveError on the invalid inputs listed above.
//
// Moving a card OFF a reserved Pokédex slot it was the last one holding leaves a
// placeholder behind rather than closing the sleeve up (see BinderGuideService), so the
// projection has to grow one too. `placeholderStatusFor` supplies that row's status,
// which the planner cannot work out for itself — the verdict depends on the wishlist and
// on copies in other binders, both storage reads, and this stays a pure function. Omit it
// and the projected placeholder reads Incomplete, which is right whenever the species is
// neither wished nor owned elsewhere; the GUI passes
// BinderGuideService::placeholderStatusFor so the projection matches the guide exactly.
using PlaceholderStatusFn = std::function<CollectionStatus(PokemonDexNum)>;

BinderMovePlan planCardMove(const CardBinder& binder,
                            std::span<const CardBinderEntry> currentRows,
                            const CardCopyId& copyId, int targetPocket,
                            const PlaceholderStatusFn& placeholderStatusFor = {});

// Work out how to give `copyId` up to derived order again.
//
// The blank pockets recorded in front of it are RE-ANCHORED to whatever now follows,
// exactly as a move treats the gap it vacates — not dropped. That distinction is
// load-bearing: a move canonicalises blank runs onto the row they precede, so the page
// break a user opened before a species can end up recorded against the card they slid
// into it. Dropping "the card's own" run would then quietly delete the page break itself.
// A run with nothing after it is dropped, having no row left to sit in front of.
//
// Throws BinderMoveError when the card has no row in this binder.
BinderMovePlan planCardReset(const CardBinder& binder,
                             std::span<const CardBinderEntry> currentRows,
                             const CardCopyId& copyId);

}  // namespace pokedex
