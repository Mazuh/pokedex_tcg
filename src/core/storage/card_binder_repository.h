#pragma once

#include <string>
#include <vector>

#include "core/domain/card_binder.h"
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

    // Insert a new binder row. Its pokemonRegion maps to a NULL region column
    // when unset. Throws StorageError (e.g. on a duplicate id).
    void add(const CardBinder& binder);

    // Update just the name and updatedAt stamp of an existing binder. Throws
    // StorageError when no binder has that id (a caller error, not a no-op).
    void updateName(const CardBinderId& id, const std::string& name, Timestamp updatedAt);

    // Delete a binder row. Because card_copy.binder_id is ON DELETE SET NULL, any
    // copies filed under it survive with their binder association cleared — the
    // "removing a binder doesn't affect its cards" contract. No-op for a missing id.
    void remove(const CardBinderId& id);

    // All binders, oldest first (ORDER BY inserted_at, id).
    std::vector<CardBinder> listAll();

private:
    Database& db_;
};

}  // namespace pokedex
