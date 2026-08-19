#include "core/app/binder_move_planner.h"

#include <algorithm>
#include <cstddef>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>

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

// Whether taking `source` out empties a RESERVED Pokédex slot — in which case the guide
// leaves a placeholder holding that sleeve rather than closing it up, and the projection
// has to do the same or it will mispage everything downstream (see BinderGuideService).
//
// Every clause earns its place. Only a region-scoped binder reserves slots at all. Only a
// row sitting in its natural position is holding one — a card already pinned elsewhere
// left its placeholder behind on the earlier move, and substituting a second would
// invent a pocket. And a species outside the binder's regions is an extra, never a slot,
// even though its row carries a species like any other.
bool vacatesReservedSlot(const CardBinder& binder, std::span<const CardBinderEntry> rows,
                         int source) {
    const CardBinderEntry& src = rows[source];
    if (binder.pokemonRegions.empty() || !src.pokemon || src.placedByHand) {
        return false;
    }
    const auto& regions = binder.pokemonRegions;
    if (std::find(regions.begin(), regions.end(), src.pokemon->region) == regions.end()) {
        return false;
    }
    for (int i = 0; i < rowCount(rows); ++i) {
        // A loose sibling holds nothing either — same reason as a pinned one. (Callers hand
        // this the arranged rows only, so it is belt and braces, but the two predicates
        // must not disagree.)
        if (i == source || !rows[i].cardCopyId || rows[i].placedByHand ||
            rows[i].noFixedPosition) {
            continue;
        }
        if (rows[i].pokemon && rows[i].pokemon->dexNumber == src.pokemon->dexNumber) {
            return false;  // a sibling copy stays behind to hold the slot
        }
    }
    return true;
}

// Where the ARRANGED album ends: the index of the first loose row (a card that declared no
// fixed position), or the row count when there is none. The guide emits that run strictly
// last and pins nothing inside it, so everything this planner reasons about — pockets,
// anchors, the shift count — stops here. Those rows are carried across untouched.
int arrangedRowCount(std::span<const CardBinderEntry> rows) {
    for (int i = 0; i < rowCount(rows); ++i) {
        if (rows[i].noFixedPosition) {
            return i;
        }
    }
    return rowCount(rows);
}

// Every card that RIDES on `copyId` — pinned before it, or before something pinned before
// it, transitively. The guide emits a placement's riders ahead of it wherever it goes
// (emitBefore recurses), so they travel with the card and the projection must carry them
// too; leaving them behind projects an arrangement that never happens and under-reports
// how many cards actually change sleeve.
std::unordered_set<CardCopyId> ridersOf(const CardBinder& binder, const CardCopyId& copyId) {
    std::unordered_set<CardCopyId> riders;
    for (bool grew = true; grew;) {
        grew = false;
        for (const CardBinderPlacement& p : binder.cardPlacements) {
            if (!p.beforeCopyId || riders.contains(p.cardCopyId)) {
                continue;
            }
            if (*p.beforeCopyId == copyId || riders.contains(*p.beforeCopyId)) {
                riders.insert(p.cardCopyId);
                grew = true;
            }
        }
    }
    return riders;
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
        if (isBlankPocket(row) || row.noFixedPosition) {
            continue;  // a loose card anchors nothing: it is past the arranged album, and
                       // recording a run against it would strand the gap when it shuffles
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
                            const CardCopyId& copyId, int targetPocket,
                            const PlaceholderStatusFn& placeholderStatusFor) {
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
    if (currentRows[source].noFixedPosition) {
        throw BinderMoveError("that card keeps no fixed position, so it has no pocket to "
                              "move: " +
                              copyId);
    }

    // Everything below plans over the ARRANGED album only. The loose run trails it and is
    // reattached, untouched, at the end: no pocket in it is a target (a too-far pocket
    // therefore lands the card immediately BEFORE the run, which is the last arranged
    // sleeve), nothing anchors to it, and its cards are left out of shiftedCards — they are
    // fluid by declaration, so counting them would cry wolf on every move.
    const auto arranged = static_cast<std::size_t>(arrangedRowCount(currentRows));
    if (static_cast<std::size_t>(source) >= arranged) {
        // The guide emits the loose run strictly last, so a non-loose row after a loose one
        // means these rows didn't come from it. Say so rather than indexing a truncated span
        // past its end, which is how a caller passing hand-built or re-sorted rows would
        // otherwise be met with undefined behaviour instead of an error.
        throw BinderMoveError("these rows are not in guide order: a card sits after one with "
                              "no fixed position");
    }
    const std::span<const CardBinderEntry> looseRows = currentRows.subspan(arranged);
    currentRows = currentRows.first(arranged);

    // Take the card out FIRST, so the target pocket names a position in the final
    // arrangement rather than the current one — the physical gesture, and what keeps a
    // forward move from landing one pocket short.
    //
    // Taking it out of a reserved slot leaves a PLACEHOLDER in the sleeve instead of
    // closing it up, so the checklist keeps its length whatever the user rearranges.
    const bool vacatesSlot = vacatesReservedSlot(binder, currentRows, source);
    CardBinderEntry vacated;
    if (vacatesSlot) {
        vacated.pokemon = currentRows[source].pokemon;
        vacated.status = placeholderStatusFor
                             ? placeholderStatusFor(currentRows[source].pokemon->dexNumber)
                             : CollectionStatus::Incomplete;
    }

    // The blank pockets it was riding on stay where they are: they are deliberate empty
    // sleeves, and the card leaving shouldn't close a gap the user opened. They end up
    // immediately before whatever now follows, and are re-anchored to it below. The
    // exception is a card at the very END of the guide: there is no following row to
    // re-anchor to, and a run of empty pockets after the last card describes nothing, so
    // those are dropped. A vacated slot is itself that following row, so its gap always
    // survives.
    const bool gapSurvives =
        vacatesSlot || firstRealFrom(currentRows, source + 1) < rowCount(currentRows);
    const int strandedBlanks = gapSurvives ? 0 : blanksBefore(currentRows, source);

    // The cards riding on this one sit immediately ahead of it — after any rider's own gap,
    // but BEFORE the moved card's own blanks, which stay behind for whatever now follows.
    // Walk back over that run so the whole convoy moves as a unit.
    const std::unordered_set<CardCopyId> riders = ridersOf(binder, copyId);
    const int riderEnd = source - blanksBefore(currentRows, source);
    int riderStart = riderEnd;
    while (riderStart > 0) {
        const CardBinderEntry& prev = currentRows[riderStart - 1];
        if (!prev.cardCopyId || !riders.contains(*prev.cardCopyId)) {
            break;
        }
        // A rider's own gap belongs to the rider, so it comes along.
        riderStart = (riderStart - 1) - blanksBefore(currentRows, riderStart - 1);
    }
    const auto inConvoy = [&](int i) {
        return i == source || (i >= riderStart && i < riderEnd);
    };

    std::vector<CardBinderEntry> reduced;
    reduced.reserve(currentRows.size());
    for (int i = 0; i < rowCount(currentRows); ++i) {
        if (i == source) {
            if (vacatesSlot) {
                reduced.push_back(vacated);
            }
            continue;
        }
        if (inConvoy(i)) {
            continue;
        }
        if (i >= source - strandedBlanks && i < source) {
            continue;
        }
        reduced.push_back(currentRows[i]);
    }

    const int insertAt = rowAtPocket(reduced, targetPocket);
    // Landing on a blank consumes it — the card takes that exact sleeve, so nothing after
    // it moves at all. This is the case the whole feature is built around.
    const bool replacesBlank =
        insertAt < rowCount(reduced) && isBlankPocket(reduced[insertAt]);

    // The convoy lands as one: the riders in their existing order, then the card itself.
    const auto pushConvoy = [&](std::vector<CardBinderEntry>& out) {
        for (int i = riderStart; i < riderEnd; ++i) {
            out.push_back(currentRows[i]);
        }
        out.push_back(currentRows[source]);
    };

    BinderMovePlan plan;
    plan.projectedRows.reserve(reduced.size() + (riderEnd - riderStart) + 1);
    for (int i = 0; i < rowCount(reduced); ++i) {
        if (i == insertAt) {
            pushConvoy(plan.projectedRows);
            if (replacesBlank) {
                continue;
            }
        }
        plan.projectedRows.push_back(reduced[i]);
    }
    if (insertAt >= rowCount(reduced)) {
        pushConvoy(plan.projectedRows);
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

    // Reattached last, once the anchor, the blank runs and the shift count have all been
    // derived from the arranged album alone — projectedRows still has to read exactly like
    // the guide's own output, loose run included, or the anti-drift test is lying.
    plan.projectedRows.insert(plan.projectedRows.end(), looseRows.begin(), looseRows.end());
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
