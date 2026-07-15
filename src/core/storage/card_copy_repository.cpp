#include "core/storage/card_copy_repository.h"

#include "core/storage/codecs.h"
#include "core/storage/statement.h"

namespace pokedex {

namespace {

// Read a full CardCopy from a row whose columns are, in order:
// id, pokemon_dex_num, ref_expansion, ref_language, ref_collector, ownership,
// condition, binder_id, comments, inserted_at, updated_at.
CardCopy readCopy(Statement& stmt) {
    CardCopy copy;
    copy.id = stmt.columnText(0);
    copy.pokemonDexNum = stmt.columnInt(1);
    copy.cardRef.expansionCode = stmt.columnText(2);
    copy.cardRef.language = stmt.columnText(3);
    copy.cardRef.collectorNumber = stmt.columnText(4);
    copy.ownership = ownershipFromText(stmt.columnText(5));
    copy.condition = conditionFromText(stmt.columnText(6));
    if (!stmt.columnIsNull(7)) {
        copy.binderId = stmt.columnText(7);
    }
    copy.comments = stmt.columnText(8);
    copy.insertedAt = timestampFromIso(stmt.columnText(9));
    copy.updatedAt = timestampFromIso(stmt.columnText(10));
    return copy;
}

}  // namespace

void CardCopyRepository::add(const CardCopy& copy) {
    Statement stmt(db_,
                   "INSERT INTO card_copy(id, pokemon_dex_num, ref_expansion,"
                   " ref_language, ref_collector, ownership, condition, binder_id,"
                   " comments, inserted_at, updated_at)"
                   " VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);");
    stmt.bindText(1, copy.id);
    stmt.bindInt(2, copy.pokemonDexNum);
    stmt.bindText(3, copy.cardRef.expansionCode);
    stmt.bindText(4, copy.cardRef.language);
    stmt.bindText(5, copy.cardRef.collectorNumber);
    stmt.bindText(6, ownershipToText(copy.ownership));
    stmt.bindText(7, conditionToText(copy.condition));
    if (copy.binderId) {
        stmt.bindText(8, *copy.binderId);
    } else {
        stmt.bindNull(8);
    }
    stmt.bindText(9, copy.comments);
    stmt.bindText(10, timestampToIso(copy.insertedAt));
    stmt.bindText(11, timestampToIso(copy.updatedAt));
    stmt.step();
}

std::vector<CardCopy> CardCopyRepository::listByBinder(const CardBinderId& binderId) {
    Statement stmt(db_,
                   "SELECT id, pokemon_dex_num, ref_expansion, ref_language,"
                   " ref_collector, ownership, condition, binder_id, comments,"
                   " inserted_at, updated_at"
                   " FROM card_copy WHERE binder_id = ? ORDER BY inserted_at, rowid;");
    stmt.bindText(1, binderId);
    std::vector<CardCopy> copies;
    while (stmt.step()) {
        copies.push_back(readCopy(stmt));
    }
    return copies;
}

std::vector<PokemonDexNum> CardCopyRepository::ownedElsewhere(const CardBinderId& binderId) {
    // Bind the Owned token from the codec rather than spelling it inline, so the
    // on-disk token has a single source of truth (a rename can't silently make
    // this query match nothing).
    Statement stmt(db_,
                   "SELECT DISTINCT pokemon_dex_num FROM card_copy"
                   " WHERE ownership = ? AND (binder_id IS NULL OR binder_id != ?);");
    stmt.bindText(1, ownershipToText(CardOwnership::Owned));
    stmt.bindText(2, binderId);
    std::vector<PokemonDexNum> dexNums;
    while (stmt.step()) {
        dexNums.push_back(stmt.columnInt(0));
    }
    return dexNums;
}

}  // namespace pokedex
