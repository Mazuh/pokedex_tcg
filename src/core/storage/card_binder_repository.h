#pragma once

#include <optional>
#include <string>
#include <vector>

#include "core/domain/card_binder.h"
#include "core/domain/region.h"
#include "core/domain/types.h"

namespace pokedex {

class Database;

// STORAGE — persistence for the CardBinder root, backed by the card_binder
// table. Pure storage: it neither mints ids nor reads the clock (the app layer
// supplies both), so it takes and returns fully-formed CardBinder values. All
// queries bind their parameters, so binder names are never interpreted as SQL.
class CardBinderRepository {
public:
    explicit CardBinderRepository(Database& db) : db_(db) {}

    // Insert a whole binder — the parent row plus one card_binder_region row per
    // region, one card_binder_blank row per blank run, and one card_binder_placement
    // row per moved card — in a transaction (the parent and its child sets are one
    // logical unit). Unlike update(), this takes the whole entity, so the struct
    // round-trips; a freshly created binder simply has no children. Throws
    // StorageError (e.g. on a duplicate id, or a placement naming a copy that isn't
    // stored — card_copy_id carries a foreign key).
    void add(const CardBinder& binder);

    // Overwrite the fields the binder's edit form owns — name, region set, physical
    // layout, and the updatedAt stamp. Throws StorageError when no binder has that id (a
    // caller error, not a no-op).
    //
    // The REGION SET may only change while the binder is still EMPTY (see hasContents).
    // Its regions decide which species the guide reserves a slot for, so re-scoping an
    // album that already holds cards re-derives every page break underneath the blanks and
    // moves arranged against the old layout — and there is no sound answer to "where does
    // the newly added region begin" with cards already filed. An empty binder has no such
    // layout to invalidate, which is what leaves a mistyped scope correctable instead of
    // forcing a delete-and-refile. Requesting a change on a non-empty binder throws
    // StorageError; passing the regions it already has is always fine. The check and the
    // write share one transaction, so the two can't race.
    //
    // It deliberately takes the individual fields rather than a CardBinder, and it
    // NEVER touches card_binder_blank or card_binder_placement: the binder's manual
    // arrangement is written only by the dedicated verbs below. Were this to take the
    // whole entity and rewrite those sets, a save from a stale in-memory binder would
    // silently wipe blanks or moves recorded since it was read. Pinned by
    // CardBinderRepositoryTest.UpdateLeavesTheManualArrangementUntouched.
    void update(const CardBinderId& id, const std::string& name,
                const std::vector<Region>& regions, std::optional<int> capacity,
                const std::optional<CardBinderPocketGrid>& pocketGrid, Timestamp updatedAt);

    // Whether anything is filed in or arranged about this binder — a card, a blank pocket,
    // or a moved card. The gate on re-scoping above, and what the edit page asks so it can
    // enable or disable the region checkboxes.
    bool hasContents(const CardBinderId& id);

    // Delete a binder row. Because card_copy.binder_id is ON DELETE SET NULL, any
    // copies filed under it survive with their binder association cleared — the
    // "removing a binder doesn't affect its cards" contract; its region and blank
    // rows drop via ON DELETE CASCADE. No-op for a missing id.
    void remove(const CardBinderId& id);

    // Add `blank.blanks` empty pockets at `blank`'s anchor, accumulating with any
    // already recorded there (so inserting twice before one species leaves a gap of
    // two). Throws StorageError on an anchor that doesn't name exactly one row, or a
    // non-positive count — a row failing that could never be read back.
    void addBlanks(const CardBinderId& id, const CardBinderBlank& blank);

    // Take `blank.blanks` pockets back off that anchor, dropping the row once none
    // remain. A no-op for an anchor that holds none. Same validation as addBlanks.
    void removeBlanks(const CardBinderId& id, const CardBinderBlank& blank);

    // Set the pocket count at one anchor to `blanks` outright, dropping the row at 0.
    // The ABSOLUTE counterpart to the accumulate/decrement pair above, and the one a
    // move uses: a move rebalances the blanks around the card it places, so it must
    // state final counts. Expressing that as a delta would double-count whenever the
    // card's old and new anchors turn out to be the same one. Throws StorageError on an
    // anchor that doesn't name exactly one row, or a negative count (0 is legal here —
    // it means "no gap left", which is exactly how a run is cleared).
    void setBlanks(const CardBinderId& id, const CardBinderBlank& blank);

    // Pin `placement.cardCopyId` immediately before the row its anchor names, replacing
    // any placement that copy already had (one placement per copy per binder). Throws
    // StorageError on a blank copy id, a placement naming BOTH anchors, or a negative
    // ordinal. Naming NEITHER anchor is legal and means "at the very end".
    void setPlacement(const CardBinderId& id, const CardBinderPlacement& placement);

    // Return a copy to the guide's natural filed order. A no-op when it isn't placed.
    void clearPlacement(const CardBinderId& id, const CardCopyId& copyId);

    // Write one card's whole arrangement in a single transaction: its placement (or, with
    // nullopt, its removal — `copyId` names the card either way) plus every absolute blank
    // run the move rebalances. The three verbs above are each ONE statement precisely so
    // they compose here; Database::transaction is not reentrant.
    //
    // Atomic because a half-applied move describes the album wrongly — a gap opened with
    // no card in it, or a card moved away with the gap it came from still recorded.
    void arrangeCard(const CardBinderId& id, const CardCopyId& copyId,
                     const std::optional<CardBinderPlacement>& placement,
                     const std::vector<CardBinderBlank>& blankSets);

    // One binder with its region set, blanks and placements attached, or nullopt when
    // no binder has that id. The read-back behind every app-layer verb that returns the
    // persisted binder.
    std::optional<CardBinder> find(const CardBinderId& id);

    // All binders, oldest first (ORDER BY inserted_at, rowid), each with its region
    // set attached in canonical (enum) order and its manual arrangement attached.
    std::vector<CardBinder> listAll();

private:
    Database& db_;
};

}  // namespace pokedex
