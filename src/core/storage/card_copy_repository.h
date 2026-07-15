#pragma once

#include <vector>

#include "core/domain/card_copy.h"
#include "core/domain/types.h"

namespace pokedex {

class Database;

// STORAGE — persistence for the CardCopy root, backed by the card_copy table.
// Pure storage: it neither mints ids nor reads the clock (the app layer supplies
// both), so it takes and returns fully-formed CardCopy values. All queries bind
// their parameters, so free-text fields are never interpreted as SQL.
//
// This slice exposes only what the binder guide needs: a write primitive (so the
// status pipeline is unit-testable) and the two reads that resolve a Pokémon's
// CollectionStatus within a binder. The full read/update/remove surface lands
// with the copy-management feature.
class CardCopyRepository {
public:
    explicit CardCopyRepository(Database& db) : db_(db) {}

    // Insert a new copy row. Its binderId maps to a NULL binder_id column when
    // unset; the CardReference is flattened into ref_* columns and the two enums
    // persist as stable tokens. Throws StorageError (e.g. on a duplicate id).
    void add(const CardCopy& copy);

    // Every copy filed in the given binder (WHERE binder_id = ?), in insertion
    // order. Drives the Incoming / Completed / Removed cases of the guide.
    std::vector<CardCopy> listByBinder(const CardBinderId& binderId);

    // The dex numbers of species with at least one Owned copy that is NOT filed
    // in the given binder (filed elsewhere or nowhere). Drives the Elsewhere case
    // — a species owned in another binder reads as available, not missing.
    std::vector<PokemonDexNum> ownedElsewhere(const CardBinderId& binderId);

private:
    Database& db_;
};

}  // namespace pokedex
