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
    // region and one card_binder_blank row per blank run — in a transaction (the
    // parent and its child sets are one logical unit). Throws StorageError (e.g. on
    // a duplicate id).
    void add(const CardBinder& binder);

    // Overwrite the fields the binder's edit form owns — name, region set, physical
    // layout, and the updatedAt stamp — in a transaction: bump the row, then replace
    // its whole card_binder_region set. Throws StorageError when no binder has that
    // id (a caller error, not a no-op).
    //
    // It deliberately takes the individual fields rather than a CardBinder, and it
    // NEVER touches card_binder_blank: blanks are written only by addBlanks /
    // removeBlanks. Were this to take the whole entity and rewrite the blank set, a
    // save from a stale in-memory binder would silently wipe blanks inserted since it
    // was read. Pinned by CardBinderRepositoryTest.UpdateLeavesTheBlankSetUntouched.
    void update(const CardBinderId& id, const std::string& name,
                const std::vector<Region>& regions, std::optional<int> capacity,
                const std::optional<CardBinderPocketGrid>& pocketGrid, Timestamp updatedAt);

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

    // One binder with its region set and blanks attached, or nullopt when no binder
    // has that id. The read-back behind every app-layer verb that returns the
    // persisted binder.
    std::optional<CardBinder> find(const CardBinderId& id);

    // All binders, oldest first (ORDER BY inserted_at, rowid), each with its region
    // set attached in canonical (enum) order and its blanks attached.
    std::vector<CardBinder> listAll();

private:
    Database& db_;
};

}  // namespace pokedex
