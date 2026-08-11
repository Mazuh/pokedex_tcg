#include "core/storage/card_binder_repository.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <unordered_map>

#include "core/storage/codecs.h"
#include "core/storage/database.h"
#include "core/storage/statement.h"

namespace pokedex {

namespace {

// The layout columns use 0 as "unset" (see the v12 migration), the same sentinel
// convention as pokemon_dex_num's 0 and condition's "" — it keeps the columns NOT
// NULL without a nullable-column table rebuild.
constexpr int kUnsetLayout = 0;

std::optional<int> capacityFrom(int stored) {
    return stored > kUnsetLayout ? std::optional<int>(stored) : std::nullopt;
}

// A grid is meaningful only with BOTH dimensions, so a half-set pair (a hand-edited
// row, or a future writer that only filled one) decodes as unset rather than as a
// grid with a zero side, which would divide by zero downstream.
std::optional<CardBinderPocketGrid> gridFrom(int rows, int columns) {
    if (rows <= kUnsetLayout || columns <= kUnsetLayout) {
        return std::nullopt;
    }
    return CardBinderPocketGrid{.rows = rows, .columns = columns};
}

// Whether a blank names exactly one anchor and at least one pocket. Both halves of
// the anchor set (or neither) is unplaceable, and a non-positive run is not a gap —
// such a row is skipped on read and rejected on write rather than guessed at.
bool blankIsWellFormed(bool hasDexAnchor, bool hasCopyAnchor, int count) {
    return count > 0 && hasDexAnchor != hasCopyAnchor;
}

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

// Bind a blank's anchor to a statement's (before_dex_num, before_copy_id) pair at
// `first` and `first + 1`, encoding the unused half as its unset sentinel. One
// helper so the insert, the update and the delete can't spell the pair differently.
void bindBlankAnchor(Statement& stmt, int first, const CardBinderBlank& blank) {
    stmt.bindInt(first, blank.beforeDexNum.value_or(kUnsetLayout));
    stmt.bindText(first + 1, blank.beforeCopyId.value_or(std::string{}));
}

void requireWellFormedBlank(const CardBinderBlank& blank) {
    if (!blankIsWellFormed(blank.beforeDexNum.has_value(), blank.beforeCopyId.has_value(),
                           blank.blanks)) {
        throw StorageError(
            "a blank must name exactly one anchor (a species or a card) and at least one "
            "pocket");
    }
}

// setBlanks accepts a count of 0 (the "no gap left" write a move makes when it takes the
// last pocket of a run), which the add/remove pair reject — hence its own check rather
// than requireWellFormedBlank.
void requireBlankAnchor(const CardBinderBlank& blank) {
    if (blank.beforeDexNum.has_value() == blank.beforeCopyId.has_value() || blank.blanks < 0) {
        throw StorageError(
            "a blank run must name exactly one anchor (a species or a card) and a "
            "non-negative pocket count");
    }
}

// A placement's anchor, unlike a blank's, may be EMPTY — that is the "at the very end"
// case. What it must never be is both halves at once, which names no single row.
void requireWellFormedPlacement(const CardBinderPlacement& placement) {
    if (placement.cardCopyId.empty()) {
        throw StorageError("a placement must name the card it places");
    }
    if (placement.beforeDexNum.has_value() && placement.beforeCopyId.has_value()) {
        throw StorageError(
            "a placement sits before at most one row — a species or a card, not both");
    }
    if (placement.ordinal < 0) {
        throw StorageError("a placement's ordinal must not be negative");
    }
}

// Bind a placement's anchor pair, mirroring bindBlankAnchor. Both halves unset encodes
// as (0, '') — legal here, and read back as "at the very end".
void bindPlacementAnchor(Statement& stmt, int first, const CardBinderPlacement& placement) {
    stmt.bindInt(first, placement.beforeDexNum.value_or(kUnsetLayout));
    stmt.bindText(first + 1, placement.beforeCopyId.value_or(std::string{}));
}

// Read the parent columns of one card_binder row, in the order the two SELECTs below
// spell them. The child sets are attached separately (see attachChildren).
CardBinder readBinderRow(Statement& stmt) {
    CardBinder binder;
    binder.id = stmt.columnText(0);
    binder.name = stmt.columnText(1);
    binder.capacity = capacityFrom(stmt.columnInt(2));
    binder.pocketGrid = gridFrom(stmt.columnInt(3), stmt.columnInt(4));
    binder.insertedAt = timestampFromIso(stmt.columnText(5));
    binder.updatedAt = timestampFromIso(stmt.columnText(6));
    return binder;
}

// The parent columns both reads select, in readBinderRow's order. Note the vestigial
// v1 `region` column is deliberately absent — v11 moved regions to the join table.
constexpr char kBinderColumns[] =
    "id, name, capacity, pocket_rows, pocket_columns, inserted_at, updated_at";

// Attach every binder's region set and blanks in two flat queries rather than N+1,
// mapping each child row back through `indexById`. `scopeId` narrows both queries to
// one binder (the find() path) or is null for the whole table (listAll).
void attachChildren(Database& db, std::vector<CardBinder>& binders,
                    const std::unordered_map<std::string, std::size_t>& indexById,
                    const CardBinderId* scopeId) {
    const std::string where = scopeId != nullptr ? " WHERE binder_id = ?" : "";

    // A region token we don't recognize (a hand-edited or newer-schema row) is skipped
    // rather than aborting the whole listing.
    Statement regions(db, "SELECT binder_id, region FROM card_binder_region" + where + ";");
    if (scopeId != nullptr) {
        regions.bindText(1, *scopeId);
    }
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

    // Ordered so a binder's blanks come back in a stable sequence regardless of write
    // order; the guide aggregates them by anchor, so this is for reproducibility.
    Statement blanks(db,
                     "SELECT binder_id, before_dex_num, before_copy_id, blanks"
                     " FROM card_binder_blank" +
                         where + " ORDER BY before_dex_num, before_copy_id;");
    if (scopeId != nullptr) {
        blanks.bindText(1, *scopeId);
    }
    while (blanks.step()) {
        const auto it = indexById.find(blanks.columnText(0));
        if (it == indexById.end()) {
            continue;  // orphan blank row (shouldn't happen; CASCADE prevents it)
        }
        const int dexNum = blanks.columnInt(1);
        const std::string copyId = blanks.columnText(2);
        const int count = blanks.columnInt(3);
        if (!blankIsWellFormed(dexNum > kUnsetLayout, !copyId.empty(), count)) {
            continue;  // unplaceable row — keep the binder, drop the blank
        }
        CardBinderBlank blank;
        if (dexNum > kUnsetLayout) {
            blank.beforeDexNum = dexNum;
        } else {
            blank.beforeCopyId = copyId;
        }
        blank.blanks = count;
        binders[it->second].pocketBlanks.push_back(blank);
    }

    // Ordered by (anchor, ordinal) so the placements sharing an anchor come back in the
    // sequence the guide emits them — the reader relies on ordinal order and sorting
    // here saves it from re-sorting per anchor.
    Statement placements(db,
                         "SELECT binder_id, card_copy_id, before_dex_num, before_copy_id,"
                         " ordinal FROM card_binder_placement" +
                             where + " ORDER BY before_dex_num, before_copy_id, ordinal;");
    if (scopeId != nullptr) {
        placements.bindText(1, *scopeId);
    }
    while (placements.step()) {
        const auto it = indexById.find(placements.columnText(0));
        if (it == indexById.end()) {
            continue;  // orphan placement row (shouldn't happen; CASCADE prevents it)
        }
        CardBinderPlacement placement;
        placement.cardCopyId = placements.columnText(1);
        const int dexNum = placements.columnInt(2);
        const std::string beforeCopyId = placements.columnText(3);
        if (placement.cardCopyId.empty() || (dexNum > kUnsetLayout && !beforeCopyId.empty())) {
            continue;  // unplaceable row — keep the binder, drop the placement
        }
        // Neither anchor set is the legal "at the very end" case, so this is an
        // if/else-if rather than the blank decode's exhaustive if/else.
        if (dexNum > kUnsetLayout) {
            placement.beforeDexNum = dexNum;
        } else if (!beforeCopyId.empty()) {
            placement.beforeCopyId = beforeCopyId;
        }
        placement.ordinal = placements.columnInt(4);
        binders[it->second].cardPlacements.push_back(placement);
    }

    // Keep each binder's regions in canonical (enum) order so display/sorting is
    // stable regardless of insertion or row order.
    for (CardBinder& binder : binders) {
        std::sort(binder.pokemonRegions.begin(), binder.pokemonRegions.end());
    }
}

}  // namespace

void CardBinderRepository::add(const CardBinder& binder) {
    // The parent row and its child rows are one logical unit spread across several
    // statements, so they run in a transaction — mirroring WishlistRepository::save.
    db_.transaction([&] {
        // The old single-region column is vestigial (v11 moved regions to the join
        // table); leave it NULL and let card_binder_region carry the truth.
        Statement stmt(db_,
                       "INSERT INTO card_binder(id, name, region, capacity, pocket_rows,"
                       " pocket_columns, inserted_at, updated_at)"
                       " VALUES(?, ?, NULL, ?, ?, ?, ?, ?);");
        stmt.bindText(1, binder.id);
        stmt.bindText(2, binder.name);
        stmt.bindInt(3, binder.capacity.value_or(kUnsetLayout));
        stmt.bindInt(4, binder.pocketGrid ? binder.pocketGrid->rows : kUnsetLayout);
        stmt.bindInt(5, binder.pocketGrid ? binder.pocketGrid->columns : kUnsetLayout);
        stmt.bindText(6, timestampToIso(binder.insertedAt));
        stmt.bindText(7, timestampToIso(binder.updatedAt));
        stmt.step();

        insertRegions(db_, binder.id, binder.pokemonRegions);
        // Unlike update(), add() takes the whole entity, so it writes the manual
        // arrangement too and the struct round-trips. A freshly created binder has none.
        for (const CardBinderBlank& blank : binder.pocketBlanks) {
            requireWellFormedBlank(blank);
            Statement insert(db_,
                             "INSERT INTO card_binder_blank(binder_id, before_dex_num,"
                             " before_copy_id, blanks) VALUES(?, ?, ?, ?);");
            insert.bindText(1, binder.id);
            bindBlankAnchor(insert, 2, blank);
            insert.bindInt(4, blank.blanks);
            insert.step();
        }
        for (const CardBinderPlacement& placement : binder.cardPlacements) {
            requireWellFormedPlacement(placement);
            Statement insert(db_,
                             "INSERT INTO card_binder_placement(binder_id, card_copy_id,"
                             " before_dex_num, before_copy_id, ordinal)"
                             " VALUES(?, ?, ?, ?, ?);");
            insert.bindText(1, binder.id);
            insert.bindText(2, placement.cardCopyId);
            bindPlacementAnchor(insert, 3, placement);
            insert.bindInt(5, placement.ordinal);
            insert.step();
        }
    });
}

void CardBinderRepository::update(const CardBinderId& id, const std::string& name,
                                  const std::vector<Region>& regions,
                                  std::optional<int> capacity,
                                  const std::optional<CardBinderPocketGrid>& pocketGrid,
                                  Timestamp updatedAt) {
    db_.transaction([&] {
        // All the parent's editable fields in ONE statement — Database::transaction is
        // not reentrant, and there is no reason to split them.
        Statement stmt(db_,
                       "UPDATE card_binder SET name = ?, capacity = ?, pocket_rows = ?,"
                       " pocket_columns = ?, updated_at = ? WHERE id = ?;");
        stmt.bindText(1, name);
        stmt.bindInt(2, capacity.value_or(kUnsetLayout));
        stmt.bindInt(3, pocketGrid ? pocketGrid->rows : kUnsetLayout);
        stmt.bindInt(4, pocketGrid ? pocketGrid->columns : kUnsetLayout);
        stmt.bindText(5, timestampToIso(updatedAt));
        stmt.bindText(6, id);
        stmt.step();
        if (db_.changes() == 0) {
            // No row matched — editing a binder that isn't there is a caller error,
            // not a silent success. Throwing rolls the transaction back.
            throw StorageError("no binder with id: " + id);
        }

        // Replace the whole region set: clear the old rows, then re-insert. The blank
        // set is deliberately untouched here — see the declaration.
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

void CardBinderRepository::addBlanks(const CardBinderId& id, const CardBinderBlank& blank) {
    requireWellFormedBlank(blank);
    // An upsert rather than a read-then-write: one statement, so it needs no
    // transaction and can't race itself. Inserting again at an anchor that already has
    // blanks widens the existing gap instead of failing on the primary key.
    Statement stmt(db_,
                   "INSERT INTO card_binder_blank(binder_id, before_dex_num, before_copy_id,"
                   " blanks) VALUES(?, ?, ?, ?)"
                   " ON CONFLICT(binder_id, before_dex_num, before_copy_id)"
                   " DO UPDATE SET blanks = blanks + excluded.blanks;");
    stmt.bindText(1, id);
    bindBlankAnchor(stmt, 2, blank);
    stmt.bindInt(4, blank.blanks);
    stmt.step();
}

void CardBinderRepository::removeBlanks(const CardBinderId& id, const CardBinderBlank& blank) {
    requireWellFormedBlank(blank);
    db_.transaction([&] {
        Statement stmt(db_,
                       "UPDATE card_binder_blank SET blanks = blanks - ?"
                       " WHERE binder_id = ? AND before_dex_num = ? AND before_copy_id = ?;");
        stmt.bindInt(1, blank.blanks);
        stmt.bindText(2, id);
        bindBlankAnchor(stmt, 3, blank);
        stmt.step();

        // Taking the last pocket back removes the anchor entirely rather than leaving a
        // zero (or negative, if the caller over-removed) row the reader would skip.
        Statement prune(db_,
                        "DELETE FROM card_binder_blank"
                        " WHERE binder_id = ? AND before_dex_num = ? AND before_copy_id = ?"
                        " AND blanks <= 0;");
        prune.bindText(1, id);
        bindBlankAnchor(prune, 2, blank);
        prune.step();
    });
}

void CardBinderRepository::setBlanks(const CardBinderId& id, const CardBinderBlank& blank) {
    requireBlankAnchor(blank);
    // Deliberately ONE statement either way, so this composes inside arrangeCard's
    // transaction (Database::transaction is not reentrant). A count of 0 is a delete
    // rather than a stored zero the reader would have to skip.
    if (blank.blanks == 0) {
        Statement stmt(db_,
                       "DELETE FROM card_binder_blank"
                       " WHERE binder_id = ? AND before_dex_num = ? AND before_copy_id = ?;");
        stmt.bindText(1, id);
        bindBlankAnchor(stmt, 2, blank);
        stmt.step();
        return;
    }
    // Upsert to the stated count rather than accumulating, so a caller can open a gap and
    // resize one with the same verb, and a run that has no row yet is created by the same
    // statement that would have updated it.
    Statement stmt(db_,
                   "INSERT INTO card_binder_blank(binder_id, before_dex_num,"
                   " before_copy_id, blanks) VALUES(?, ?, ?, ?)"
                   " ON CONFLICT(binder_id, before_dex_num, before_copy_id)"
                   " DO UPDATE SET blanks = excluded.blanks;");
    stmt.bindText(1, id);
    bindBlankAnchor(stmt, 2, blank);
    stmt.bindInt(4, blank.blanks);
    stmt.step();
}

void CardBinderRepository::arrangeCard(const CardBinderId& id, const CardCopyId& copyId,
                                       const std::optional<CardBinderPlacement>& placement,
                                       const std::vector<CardBinderBlank>& blankSets) {
    // One transaction: a move is a placement PLUS the blank runs it rebalances, and a
    // half-applied move would leave the album described wrongly — a gap opened with no
    // card in it, or a card moved with the gap it came from still recorded.
    db_.transaction([&] {
        if (placement) {
            setPlacement(id, *placement);
        } else {
            clearPlacement(id, copyId);
        }
        for (const CardBinderBlank& blank : blankSets) {
            setBlanks(id, blank);
        }
    });
}

void CardBinderRepository::setPlacement(const CardBinderId& id,
                                        const CardBinderPlacement& placement) {
    requireWellFormedPlacement(placement);
    // One placement per copy per binder, so re-placing an already-moved card overwrites
    // its anchor rather than colliding on the primary key. A single statement, so no
    // transaction is needed.
    Statement stmt(db_,
                   "INSERT INTO card_binder_placement(binder_id, card_copy_id,"
                   " before_dex_num, before_copy_id, ordinal) VALUES(?, ?, ?, ?, ?)"
                   " ON CONFLICT(binder_id, card_copy_id)"
                   " DO UPDATE SET before_dex_num = excluded.before_dex_num,"
                   " before_copy_id = excluded.before_copy_id, ordinal = excluded.ordinal;");
    stmt.bindText(1, id);
    stmt.bindText(2, placement.cardCopyId);
    bindPlacementAnchor(stmt, 3, placement);
    stmt.bindInt(5, placement.ordinal);
    stmt.step();
}

void CardBinderRepository::clearPlacement(const CardBinderId& id, const CardCopyId& copyId) {
    Statement stmt(db_,
                   "DELETE FROM card_binder_placement"
                   " WHERE binder_id = ? AND card_copy_id = ?;");
    stmt.bindText(1, id);
    stmt.bindText(2, copyId);
    stmt.step();
}

std::optional<CardBinder> CardBinderRepository::find(const CardBinderId& id) {
    Statement stmt(db_,
                   std::string("SELECT ") + kBinderColumns + " FROM card_binder WHERE id = ?;");
    stmt.bindText(1, id);
    if (!stmt.step()) {
        return std::nullopt;
    }
    std::vector<CardBinder> one{readBinderRow(stmt)};
    const std::unordered_map<std::string, std::size_t> indexById{{one.front().id, 0}};
    attachChildren(db_, one, indexById, &id);
    return std::move(one.front());
}

std::vector<CardBinder> CardBinderRepository::listAll() {
    // Tiebreak on rowid, not id: ids are random UUIDs, so ordering by id after a
    // second-granularity inserted_at could reverse two binders created in the
    // same second. rowid increases with insertion, preserving creation order.
    Statement stmt(db_, std::string("SELECT ") + kBinderColumns +
                            " FROM card_binder ORDER BY inserted_at, rowid;");
    std::vector<CardBinder> binders;
    std::unordered_map<std::string, std::size_t> indexById;
    while (stmt.step()) {
        CardBinder binder = readBinderRow(stmt);
        indexById.emplace(binder.id, binders.size());
        binders.push_back(std::move(binder));
    }
    attachChildren(db_, binders, indexById, nullptr);
    return binders;
}

}  // namespace pokedex
