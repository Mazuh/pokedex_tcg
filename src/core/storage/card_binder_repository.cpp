#include "core/storage/card_binder_repository.h"

#include <algorithm>
#include <unordered_map>

#include "core/storage/codecs.h"
#include "core/storage/database.h"
#include "core/storage/statement.h"

namespace pokedex {

namespace {

// Write a binder's region set into card_binder_region. Deduplicated defensively
// (the PK would reject a repeat anyway); each token comes from the stable codec.
void insertRegions(Database& db, const CardBinderId& id,
                   const std::vector<Region>& regions) {
    for (const Region region : regions) {
        Statement stmt(db,
                       "INSERT OR IGNORE INTO card_binder_region(binder_id, region)"
                       " VALUES(?, ?);");
        stmt.bindText(1, id);
        stmt.bindText(2, regionToText(region));
        stmt.step();
    }
}

}  // namespace

void CardBinderRepository::add(const CardBinder& binder) {
    // The parent row and its region rows are one logical unit spread across several
    // statements, so they run in a transaction — mirroring WishlistRepository::save.
    db_.transaction([&] {
        // The old single-region column is vestigial (v11 moved regions to the join
        // table); leave it NULL and let card_binder_region carry the truth.
        Statement stmt(db_,
                       "INSERT INTO card_binder(id, name, region, inserted_at, updated_at)"
                       " VALUES(?, ?, NULL, ?, ?);");
        stmt.bindText(1, binder.id);
        stmt.bindText(2, binder.name);
        stmt.bindText(3, timestampToIso(binder.insertedAt));
        stmt.bindText(4, timestampToIso(binder.updatedAt));
        stmt.step();

        insertRegions(db_, binder.id, binder.pokemonRegions);
    });
}

void CardBinderRepository::update(const CardBinderId& id, const std::string& name,
                                  const std::vector<Region>& regions, Timestamp updatedAt) {
    db_.transaction([&] {
        Statement stmt(db_,
                       "UPDATE card_binder SET name = ?, updated_at = ? WHERE id = ?;");
        stmt.bindText(1, name);
        stmt.bindText(2, timestampToIso(updatedAt));
        stmt.bindText(3, id);
        stmt.step();
        if (db_.changes() == 0) {
            // No row matched — editing a binder that isn't there is a caller error,
            // not a silent success. Throwing rolls the transaction back.
            throw StorageError("no binder with id: " + id);
        }

        // Replace the whole region set: clear the old rows, then re-insert.
        Statement clear(db_, "DELETE FROM card_binder_region WHERE binder_id = ?;");
        clear.bindText(1, id);
        clear.step();

        insertRegions(db_, id, regions);
    });
}

void CardBinderRepository::remove(const CardBinderId& id) {
    Statement stmt(db_, "DELETE FROM card_binder WHERE id = ?;");
    stmt.bindText(1, id);
    stmt.step();
}

std::vector<CardBinder> CardBinderRepository::listAll() {
    // Tiebreak on rowid, not id: ids are random UUIDs, so ordering by id after a
    // second-granularity inserted_at could reverse two binders created in the
    // same second. rowid increases with insertion, preserving creation order.
    Statement stmt(db_,
                   "SELECT id, name, inserted_at, updated_at"
                   " FROM card_binder ORDER BY inserted_at, rowid;");
    std::vector<CardBinder> binders;
    std::unordered_map<std::string, std::size_t> indexById;
    while (stmt.step()) {
        CardBinder binder;
        binder.id = stmt.columnText(0);
        binder.name = stmt.columnText(1);
        binder.insertedAt = timestampFromIso(stmt.columnText(2));
        binder.updatedAt = timestampFromIso(stmt.columnText(3));
        indexById.emplace(binder.id, binders.size());
        binders.push_back(std::move(binder));
    }

    // A second pass over the join table attaches each binder's regions — one query
    // rather than N+1. A region token we don't recognize (a hand-edited or
    // newer-schema row) is skipped rather than aborting the whole listing.
    Statement regions(db_, "SELECT binder_id, region FROM card_binder_region;");
    while (regions.step()) {
        const auto it = indexById.find(regions.columnText(0));
        if (it == indexById.end()) {
            continue;  // orphan region row (shouldn't happen; CASCADE prevents it)
        }
        try {
            binders[it->second].pokemonRegions.push_back(regionFromText(regions.columnText(1)));
        } catch (const StorageError&) {
            // Unknown token — keep the binder, just drop the unrecognized region.
        }
    }

    // Keep each binder's regions in canonical (enum) order so display/sorting is
    // stable regardless of insertion or row order.
    for (CardBinder& binder : binders) {
        std::sort(binder.pokemonRegions.begin(), binder.pokemonRegions.end());
    }
    return binders;
}

}  // namespace pokedex
