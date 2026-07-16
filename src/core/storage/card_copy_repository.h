#pragma once

#include <optional>
#include <unordered_map>
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
// Now carries the full CRUD surface behind copy management: the add primitive the
// binder guide relies on, the reads that resolve a Pokémon's CollectionStatus, and
// the find / listAll / update / hardDelete needed to browse and edit owned copies.
class CardCopyRepository {
public:
    explicit CardCopyRepository(Database& db) : db_(db) {}

    // Insert a new copy row. Its binderId maps to a NULL binder_id column when
    // unset; the CardReference is flattened into ref_* columns and the two enums
    // persist as stable tokens. Throws StorageError (e.g. on a duplicate id).
    void add(const CardCopy& copy);

    // The copy with this id, or nullopt if none exists.
    std::optional<CardCopy> find(const CardCopyId& id);

    // Every copy in the collection, oldest first — the read behind the owned-cards
    // browser (which filters by ownership itself).
    std::vector<CardCopy> listAll();

    // Overwrite a copy's mutable columns (everything but id and inserted_at) from
    // the given value, matched by id. The caller supplies the bumped updated_at.
    // Throws StorageError if no row has that id.
    void update(const CardCopy& copy);

    // Permanently delete a copy row. Throws StorageError if no row has that id.
    void hardDelete(const CardCopyId& id);

    // Every copy filed in the given binder (WHERE binder_id = ?), in insertion
    // order. Drives the Incoming / Completed / Removed cases of the guide.
    std::vector<CardCopy> listByBinder(const CardBinderId& binderId);

    // The dex numbers of species with at least one Owned copy that is NOT filed
    // in the given binder (filed elsewhere or nowhere). Drives the Elsewhere case
    // — a species owned in another binder reads as available, not missing.
    std::vector<PokemonDexNum> ownedElsewhere(const CardBinderId& binderId);

    // How many Owned copies exist per species across the whole collection (every
    // binder plus unfiled), keyed by dex number. Only Owned counts — Incoming and
    // Removed copies are excluded. A species with no Owned copy is simply absent
    // from the map (callers treat a missing key as zero). Drives the unscoped
    // Pokédex browser's owned-cards column.
    std::unordered_map<PokemonDexNum, int> ownedCountsByDexNum();

private:
    Database& db_;
};

}  // namespace pokedex
