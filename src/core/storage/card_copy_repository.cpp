#include "core/storage/card_copy_repository.h"

#include "core/storage/codecs.h"
#include "core/storage/database.h"
#include "core/storage/statement.h"

namespace pokedex {

namespace {

// The column list shared by every SELECT, in the order readCopy() expects.
// ref_set_name (v2), ref_name (v3), rarity (v4), foil (v5) then external_card_id
// (v8) are appended last, so the earlier indices are unchanged.
constexpr const char* kCopyColumns =
    "id, pokemon_dex_num, ref_expansion, ref_language, ref_collector, ownership,"
    " condition, binder_id, comments, inserted_at, updated_at, ref_set_name,"
    " ref_name, rarity, foil, external_card_id";

// A species-free copy (no dex number) is stored as 0 in the NOT NULL
// pokemon_dex_num column — real national dex numbers start at 1, so 0 is an
// unambiguous "absent" sentinel, mirroring how condition uses "" for nullopt.
// This keeps the column simple (no nullable rebuild) while the domain field
// stays a genuine std::optional.
constexpr PokemonDexNum kNoDexNum = 0;

// Read a full CardCopy from a row whose columns are, in order:
// id, pokemon_dex_num, ref_expansion, ref_language, ref_collector, ownership,
// condition, binder_id, comments, inserted_at, updated_at, ref_set_name, ref_name,
// rarity, foil, external_card_id.
CardCopy readCopy(Statement& stmt) {
    CardCopy copy;
    copy.id = stmt.columnText(0);
    const int dexNum = stmt.columnInt(1);
    if (dexNum != kNoDexNum) {
        copy.pokemonDexNum = dexNum;
    }
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
    copy.cardRef.setName = stmt.columnText(11);
    copy.cardRef.name = stmt.columnText(12);
    copy.rarity = rarityFromText(stmt.columnText(13));
    copy.foil = foilFromText(stmt.columnText(14));
    copy.externalCardId = stmt.columnText(15);
    return copy;
}

}  // namespace

void CardCopyRepository::add(const CardCopy& copy) {
    Statement stmt(db_,
                   "INSERT INTO card_copy(id, pokemon_dex_num, ref_expansion,"
                   " ref_language, ref_collector, ownership, condition, binder_id,"
                   " comments, inserted_at, updated_at, ref_set_name, ref_name,"
                   " rarity, foil, external_card_id)"
                   " VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);");
    stmt.bindText(1, copy.id);
    stmt.bindInt(2, copy.pokemonDexNum.value_or(kNoDexNum));
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
    stmt.bindText(12, copy.cardRef.setName);
    stmt.bindText(13, copy.cardRef.name);
    stmt.bindText(14, rarityToText(copy.rarity));
    stmt.bindText(15, foilToText(copy.foil));
    stmt.bindText(16, copy.externalCardId);
    stmt.step();
}

std::optional<CardCopy> CardCopyRepository::find(const CardCopyId& id) {
    Statement stmt(db_, std::string("SELECT ") + kCopyColumns +
                            " FROM card_copy WHERE id = ?;");
    stmt.bindText(1, id);
    if (!stmt.step()) {
        return std::nullopt;
    }
    return readCopy(stmt);
}

std::vector<CardCopy> CardCopyRepository::listAll() {
    Statement stmt(db_, std::string("SELECT ") + kCopyColumns +
                            " FROM card_copy ORDER BY inserted_at, rowid;");
    std::vector<CardCopy> copies;
    while (stmt.step()) {
        copies.push_back(readCopy(stmt));
    }
    return copies;
}

void CardCopyRepository::update(const CardCopy& copy) {
    Statement stmt(db_,
                   "UPDATE card_copy SET pokemon_dex_num = ?, ref_expansion = ?,"
                   " ref_language = ?, ref_collector = ?, ownership = ?, condition = ?,"
                   " binder_id = ?, comments = ?, updated_at = ?, ref_set_name = ?,"
                   " ref_name = ?, rarity = ?, foil = ?, external_card_id = ?"
                   " WHERE id = ?;");
    stmt.bindInt(1, copy.pokemonDexNum.value_or(kNoDexNum));
    stmt.bindText(2, copy.cardRef.expansionCode);
    stmt.bindText(3, copy.cardRef.language);
    stmt.bindText(4, copy.cardRef.collectorNumber);
    stmt.bindText(5, ownershipToText(copy.ownership));
    stmt.bindText(6, conditionToText(copy.condition));
    if (copy.binderId) {
        stmt.bindText(7, *copy.binderId);
    } else {
        stmt.bindNull(7);
    }
    stmt.bindText(8, copy.comments);
    stmt.bindText(9, timestampToIso(copy.updatedAt));
    stmt.bindText(10, copy.cardRef.setName);
    stmt.bindText(11, copy.cardRef.name);
    stmt.bindText(12, rarityToText(copy.rarity));
    stmt.bindText(13, foilToText(copy.foil));
    stmt.bindText(14, copy.externalCardId);
    stmt.bindText(15, copy.id);
    stmt.step();
    if (db_.changes() == 0) {
        throw StorageError("no card copy with id " + copy.id);
    }
}

void CardCopyRepository::hardDelete(const CardCopyId& id) {
    Statement stmt(db_, "DELETE FROM card_copy WHERE id = ?;");
    stmt.bindText(1, id);
    stmt.step();
    if (db_.changes() == 0) {
        throw StorageError("no card copy with id " + id);
    }
}

std::vector<CardCopy> CardCopyRepository::listByBinder(const CardBinderId& binderId) {
    Statement stmt(db_, std::string("SELECT ") + kCopyColumns +
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
                   " WHERE ownership = ? AND pokemon_dex_num != 0"
                   " AND (binder_id IS NULL OR binder_id != ?);");
    stmt.bindText(1, ownershipToText(CardOwnership::Owned));
    stmt.bindText(2, binderId);
    std::vector<PokemonDexNum> dexNums;
    while (stmt.step()) {
        dexNums.push_back(stmt.columnInt(0));
    }
    return dexNums;
}

std::unordered_map<PokemonDexNum, int> CardCopyRepository::ownedCountsByDexNum() {
    // Bind the Owned token from the codec (single source of truth for the on-disk
    // spelling), same as ownedElsewhere. GROUP BY collapses the per-species count
    // in SQLite so the caller gets one row per owned species.
    Statement stmt(db_,
                   "SELECT pokemon_dex_num, COUNT(*) FROM card_copy"
                   " WHERE ownership = ? AND pokemon_dex_num != 0"
                   " GROUP BY pokemon_dex_num;");
    stmt.bindText(1, ownershipToText(CardOwnership::Owned));
    std::unordered_map<PokemonDexNum, int> counts;
    while (stmt.step()) {
        counts.emplace(stmt.columnInt(0), stmt.columnInt(1));
    }
    return counts;
}

}  // namespace pokedex
