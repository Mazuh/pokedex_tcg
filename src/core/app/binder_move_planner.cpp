#include "core/app/binder_move_planner.h"

#include <algorithm>
#include <cstddef>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>

namespace pokedex {

namespace {

int rowCount(std::span<const CardBinderEntry> rows) { return static_cast<int>(rows.size()); }

// The row standing for `copyId`, or -1. Every filed card has exactly one row wherever it
// sits, so this is unambiguous.
int indexOfCopy(std::span<const CardBinderEntry> rows, const CardCopyId& copyId) {
    for (int i = 0; i < rowCount(rows); ++i) {
        if (rows[i].cardCopyId == copyId) {
            return i;
        }
    }
    return -1;
}

// The row occupying 0-based `pocket`, counting only the rows that hold one — the same
// numbering the guide's Page/Pocket columns show. Returns rows.size() when the pocket is
// past the last row, which reads as "at the very end".
int rowAtPocket(std::span<const CardBinderEntry> rows, int pocket) {
    int seen = 0;
    for (int i = 0; i < rowCount(rows); ++i) {
        if (!holdsPocket(rows[i])) {
            continue;
        }
        if (seen == pocket) {
            return i;
        }
        ++seen;
    }
    return rowCount(rows);
}

// The first row at or after `from` that isn't a blank pocket, or rows.size().
int firstRealFrom(std::span<const CardBinderEntry> rows, int from) {
    int i = std::max(from, 0);
    while (i < rowCount(rows) && isBlankPocket(rows[i])) {
        ++i;
    }
    return i;
}

// How many blank pockets sit immediately before `index`. Because a blank always renders
// immediately before the row it is anchored to, this run IS that row's recorded count —
// which is what lets the plan read final counts straight off the projected arrangement.
int blanksBefore(std::span<const CardBinderEntry> rows, int index) {
    int n = 0;
    for (int i = index - 1; i >= 0 && isBlankPocket(rows[i]); --i) {
        ++n;
    }
    return n;
}

// The anchor naming `row` — the card it stands for, or its species when it stands for
// none (a placeholder). Callers must not pass a blank, which names neither.
CardBinderBlank anchorOf(const CardBinderEntry& row) {
    CardBinderBlank anchor;
    if (row.cardCopyId) {
        anchor.beforeCopyId = *row.cardCopyId;
    } else if (row.pokemon) {
        anchor.beforeDexNum = row.pokemon->dexNumber;
    }
    return anchor;
}

// Each card's pocket number, for the before/after comparison shiftedCards reports.
std::unordered_map<CardCopyId, int> pocketByCopy(std::span<const CardBinderEntry> rows) {
    std::unordered_map<CardCopyId, int> pockets;
    int pocket = 0;
    for (const CardBinderEntry& row : rows) {
        if (!holdsPocket(row)) {
            continue;
        }
        if (row.cardCopyId) {
            pockets.emplace(*row.cardCopyId, pocket);
        }
        ++pocket;
    }
    return pockets;
}

// The blank runs a binder currently records, dropping any malformed row exactly as the
// guide does. Keyed the way the anchor is written, since the two namings are distinct
// records even when they point at the same row.
struct RecordedBlanks {
    std::map<PokemonDexNum, int> byDex;
    std::map<CardCopyId, int> byCopy;
};

RecordedBlanks recordedBlanksOf(const CardBinder& binder) {
    RecordedBlanks recorded;
    for (const CardBinderBlank& blank : binder.pocketBlanks) {
        if (blank.blanks <= 0 ||
            blank.beforeDexNum.has_value() == blank.beforeCopyId.has_value()) {
            continue;
        }
        if (blank.beforeDexNum) {
            recorded.byDex[*blank.beforeDexNum] += blank.blanks;
        } else {
            recorded.byCopy[*blank.beforeCopyId] += blank.blanks;
        }
    }
    return recorded;
}

// Rewrite every blank run the moved arrangement implies, as absolute counts.
//
// Attributing a run to a single anchor by counting rows is only sound if each run has ONE
// owner, and by default it doesn't: a copy row can be named BOTH ways — by its own id and
// by its species' dex number — and blanks recorded either way render in the same stretch
// ahead of it. Reading one and leaving the other is how a consumed blank comes back.
//
// So a move CANONICALISES the whole arrangement instead of patching the anchors it
// happens to touch: every run is re-recorded against the exact row it precedes, named by
// copy id whenever that row has one. A species-level run on a species that has cards filed
// is normalised onto the row it actually sits in front of — which is where it was already
// rendering, so nothing on screen moves. Only a placeholder keeps a dex-numbered run,
// having no card to name it.
//
// Runs whose anchor names no row at all are left strictly alone: those are the orphans the
// guide deliberately keeps, so that restoring a region or re-filing a card brings the
// user's layout back intact.
std::vector<CardBinderBlank> canonicalBlankSets(const CardBinder& binder,
                                                std::span<const CardBinderEntry> projected) {
    const RecordedBlanks recorded = recordedBlanksOf(binder);

    std::map<PokemonDexNum, int> desiredByDex;
    std::map<CardCopyId, int> desiredByCopy;
    std::set<PokemonDexNum> speciesWithACardRow;
    for (int i = 0; i < rowCount(projected); ++i) {
        const CardBinderEntry& row = projected[i];
        if (isBlankPocket(row)) {
            continue;
        }
        if (row.cardCopyId) {
            desiredByCopy[*row.cardCopyId] = blanksBefore(projected, i);
            if (row.pokemon) {
                speciesWithACardRow.insert(row.pokemon->dexNumber);
            }
        } else if (row.pokemon) {
            desiredByDex[row.pokemon->dexNumber] = blanksBefore(projected, i);
        }
    }
    // A species-level run on a species represented only by card rows has just been
    // normalised onto one of them, so its own record must go to zero.
    for (const auto& [dexNum, count] : recorded.byDex) {
        if (count > 0 && !desiredByDex.contains(dexNum) && speciesWithACardRow.contains(dexNum)) {
            desiredByDex[dexNum] = 0;
        }
    }

    std::vector<CardBinderBlank> sets;
    for (const auto& [dexNum, blanks] : desiredByDex) {
        const auto was = recorded.byDex.find(dexNum);
        const int previous = was == recorded.byDex.end() ? 0 : was->second;
        if (blanks != previous) {
            sets.push_back({.beforeDexNum = dexNum, .blanks = blanks});
        }
    }
    for (const auto& [copyId, blanks] : desiredByCopy) {
        const auto was = recorded.byCopy.find(copyId);
        const int previous = was == recorded.byCopy.end() ? 0 : was->second;
        if (blanks != previous) {
            sets.push_back({.beforeCopyId = copyId, .blanks = blanks});
        }
    }
    return sets;
}

}  // namespace

BinderMovePlan planCardMove(const CardBinder& binder,
                            std::span<const CardBinderEntry> currentRows,
                            const CardCopyId& copyId, int targetPocket) {
    if (targetPocket < 0) {
        throw BinderMoveError("a target pocket cannot be negative");
    }
    const int source = indexOfCopy(currentRows, copyId);
    if (source < 0) {
        throw BinderMoveError("that card has no row in this binder: " + copyId);
    }
    if (!holdsPocket(currentRows[source])) {
        throw BinderMoveError("a removed card is not in a sleeve, so it cannot be moved");
    }

    // Take the card out FIRST, so the target pocket names a position in the final
    // arrangement rather than the current one — the physical gesture, and what keeps a
    // forward move from landing one pocket short.
    //
    // The blank pockets it was riding on stay where they are: they are deliberate empty
    // sleeves, and the card leaving shouldn't close a gap the user opened. They end up
    // immediately before whatever now follows, and are re-anchored to it below. The
    // exception is a card at the very END of the guide: there is no following row to
    // re-anchor to, and a run of empty pockets after the last card describes nothing, so
    // those are dropped.
    const bool gapSurvives = firstRealFrom(currentRows, source + 1) < rowCount(currentRows);
    const int strandedBlanks = gapSurvives ? 0 : blanksBefore(currentRows, source);

    std::vector<CardBinderEntry> reduced;
    reduced.reserve(currentRows.size());
    for (int i = 0; i < rowCount(currentRows); ++i) {
        if (i == source || (i >= source - strandedBlanks && i < source)) {
            continue;
        }
        reduced.push_back(currentRows[i]);
    }

    const int insertAt = rowAtPocket(reduced, targetPocket);
    // Landing on a blank consumes it — the card takes that exact sleeve, so nothing after
    // it moves at all. This is the case the whole feature is built around.
    const bool replacesBlank =
        insertAt < rowCount(reduced) && isBlankPocket(reduced[insertAt]);

    BinderMovePlan plan;
    plan.projectedRows.reserve(reduced.size() + 1);
    for (int i = 0; i < rowCount(reduced); ++i) {
        if (i == insertAt) {
            plan.projectedRows.push_back(currentRows[source]);
            if (replacesBlank) {
                continue;
            }
        }
        plan.projectedRows.push_back(reduced[i]);
    }
    if (insertAt >= rowCount(reduced)) {
        plan.projectedRows.push_back(currentRows[source]);
    }

    // The card sits immediately before the first NON-blank row after it: the blanks in
    // between belong to that row, not to the gap the card opens. Anchoring to a row that
    // is itself a moved card is not just allowed but necessary — it is how a card targets
    // a pocket another moved card already holds, and it keeps the card nearest its own
    // anchor, so its ordinal is always a plain append.
    const int placedAt = indexOfCopy(plan.projectedRows, copyId);
    const int anchorRow = firstRealFrom(plan.projectedRows, placedAt + 1);

    plan.cardCopyId = copyId;
    CardBinderPlacement placement;
    placement.cardCopyId = copyId;
    CardBinderBlank anchor;  // all-unset = the end, matching a placement's own encoding
    if (anchorRow < rowCount(plan.projectedRows)) {
        anchor = anchorOf(plan.projectedRows[anchorRow]);
        placement.beforeDexNum = anchor.beforeDexNum;
        placement.beforeCopyId = anchor.beforeCopyId;
    }
    int highestOrdinal = -1;
    for (const CardBinderPlacement& existing : binder.cardPlacements) {
        if (existing.cardCopyId == copyId) {
            continue;  // this card's own previous placement is being replaced
        }
        if (existing.beforeDexNum == placement.beforeDexNum &&
            existing.beforeCopyId == placement.beforeCopyId) {
            highestOrdinal = std::max(highestOrdinal, existing.ordinal);
        }
    }
    placement.ordinal = highestOrdinal + 1;
    plan.placement = placement;

    plan.blankSets = canonicalBlankSets(binder, plan.projectedRows);

    const auto before = pocketByCopy(currentRows);
    const auto after = pocketByCopy(plan.projectedRows);
    for (const auto& [id, pocket] : before) {
        if (id == copyId) {
            continue;  // the card the user is moving on purpose
        }
        const auto it = after.find(id);
        if (it == after.end() || it->second != pocket) {
            ++plan.shiftedCards;
        }
    }
    return plan;
}

BinderMovePlan planCardReset(const CardBinder& binder,
                             std::span<const CardBinderEntry> currentRows,
                             const CardCopyId& copyId) {
    const int source = indexOfCopy(currentRows, copyId);
    if (source < 0) {
        throw BinderMoveError("that card has no row in this binder: " + copyId);
    }

    BinderMovePlan plan;
    plan.cardCopyId = copyId;  // placement stays nullopt: the card gives its position up

    // Where the card lands is derived order, which only the guide can work out — but the
    // blank runs don't depend on that. They depend only on the card LEAVING, so canonical
    // ise the arrangement without its row: whatever it was sitting in front of inherits
    // the gap, and a run left with nothing after it falls away.
    std::vector<CardBinderEntry> withoutCard;
    withoutCard.reserve(currentRows.size());
    for (int i = 0; i < rowCount(currentRows); ++i) {
        if (i != source) {
            withoutCard.push_back(currentRows[i]);
        }
    }
    plan.blankSets = canonicalBlankSets(binder, withoutCard);

    // The card's own run is not in that arrangement, so state it explicitly: it keeps no
    // gap of its own once it is back among its species.
    bool stated = false;
    for (CardBinderBlank& blank : plan.blankSets) {
        if (blank.beforeCopyId == copyId) {
            blank.blanks = 0;
            stated = true;
        }
    }
    if (!stated) {
        plan.blankSets.push_back({.beforeCopyId = copyId, .blanks = 0});
    }
    return plan;
}

}  // namespace pokedex
