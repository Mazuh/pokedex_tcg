#include "core/storage/card_binder_repository.h"

#include "core/storage/codecs.h"
#include "core/storage/database.h"
#include "core/storage/statement.h"

namespace pokedex {

void CardBinderRepository::add(const CardBinder& binder) {
    Statement stmt(db_,
                   "INSERT INTO card_binder(id, name, region, inserted_at, updated_at)"
                   " VALUES(?, ?, ?, ?, ?);");
    stmt.bindText(1, binder.id);
    stmt.bindText(2, binder.name);
    if (binder.pokemonRegion) {
        stmt.bindText(3, regionToText(*binder.pokemonRegion));
    } else {
        stmt.bindNull(3);
    }
    stmt.bindText(4, timestampToIso(binder.insertedAt));
    stmt.bindText(5, timestampToIso(binder.updatedAt));
    stmt.step();
}

void CardBinderRepository::updateName(const CardBinderId& id, const std::string& name,
                                      Timestamp updatedAt) {
    Statement stmt(db_,
                   "UPDATE card_binder SET name = ?, updated_at = ? WHERE id = ?;");
    stmt.bindText(1, name);
    stmt.bindText(2, timestampToIso(updatedAt));
    stmt.bindText(3, id);
    stmt.step();
    if (db_.changes() == 0) {
        // No row matched — renaming a binder that isn't there is a caller error,
        // not a silent success.
        throw StorageError("no binder with id: " + id);
    }
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
                   "SELECT id, name, region, inserted_at, updated_at"
                   " FROM card_binder ORDER BY inserted_at, rowid;");
    std::vector<CardBinder> binders;
    while (stmt.step()) {
        CardBinder binder;
        binder.id = stmt.columnText(0);
        binder.name = stmt.columnText(1);
        if (!stmt.columnIsNull(2)) {
            // A region token we don't recognize (a hand-edited or newer-schema
            // row) shouldn't wipe the whole list — keep the binder, just leave
            // its region unset rather than aborting the query.
            try {
                binder.pokemonRegion = regionFromText(stmt.columnText(2));
            } catch (const StorageError&) {
                binder.pokemonRegion = std::nullopt;
            }
        }
        binder.insertedAt = timestampFromIso(stmt.columnText(3));
        binder.updatedAt = timestampFromIso(stmt.columnText(4));
        binders.push_back(std::move(binder));
    }
    return binders;
}

}  // namespace pokedex
