#include "core/app/binder_move_planner.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "core/app/binder_guide_service.h"
#include "core/app/binder_service.h"
#include "core/domain/card_binder.h"
#include "core/domain/card_copy.h"
#include "core/domain/card_ownership.h"
#include "core/domain/card_reference.h"
#include "core/domain/region.h"
#include "core/domain/wishlist.h"
#include "core/storage/card_binder_repository.h"
#include "core/storage/card_copy_repository.h"
#include "core/storage/codecs.h"
#include "core/storage/database.h"
#include "core/storage/wishlist_repository.h"

namespace {

using pokedex::BinderMoveError;
using pokedex::BinderMovePlan;
using pokedex::CardBinder;
using pokedex::CardBinderEntry;
using pokedex::CardBinderRepository;
using pokedex::CardCopy;
using pokedex::CardCopyRepository;
using pokedex::CardOwnership;
using pokedex::CardReference;
using pokedex::Database;
using pokedex::planCardMove;
using pokedex::PokemonDexNum;
using pokedex::Region;
using pokedex::Timestamp;
using pokedex::WishlistRepository;

constexpr int kChespin = 650;  // Kalos — the first species after Kanto's 151

Timestamp at(const char* iso) { return pokedex::timestampFromIso(iso); }

// A binder with no regions lists only the species it holds cards for, which keeps these
// arrangements small enough to assert on row by row. The page-scale cases below scope it
// to Kanto + Kalos instead, reproducing the real screenshot.
struct MoveTest : ::testing::Test {
    Database db{":memory:"};
    CardBinderRepository binders{db};
    CardCopyRepository copies{db};
    WishlistRepository wishlist{db};
    pokedex::BinderGuideService guide{copies, wishlist};
    pokedex::BinderService service{binders};
    CardBinder binder;

    MoveTest() { db.migrate(); }

    void openBinder(std::vector<Region> regions = {}) {
        binder.id = "b1";
        binder.name = "Test";
        binder.pokemonRegions = std::move(regions);
        binder.insertedAt = at("2026-07-14T09:00:00Z");
        binder.updatedAt = at("2026-07-14T09:00:00Z");
        binders.add(binder);
    }

    // `hour` pins the repository's filed ordering (inserted_at, rowid) so a species'
    // several copies come back in a known sequence.
    void file(std::string id, std::optional<PokemonDexNum> dex, int hour = 9,
              CardOwnership ownership = CardOwnership::Owned) {
        CardCopy copy;
        copy.id = std::move(id);
        copy.pokemonDexNum = dex;
        copy.cardRef = CardReference{"MEW", "EN", "151/165"};
        copy.ownership = ownership;
        copy.binderId = "b1";
        const std::string stamp = "2026-07-14T" + std::string(hour < 10 ? "0" : "") +
                                  std::to_string(hour) + ":00:00Z";
        copy.insertedAt = at(stamp.c_str());
        copy.updatedAt = copy.insertedAt;
        copies.add(copy);
    }

    // File a card that keeps no home sleeve: the guide lists it in the loose run at the
    // very end, and nothing there is an arrangement target.
    void fileLoose(std::string id, std::optional<PokemonDexNum> dex, int hour = 9) {
        file(id, dex, hour);
        CardCopy loose = *copies.find(id);
        loose.noFixedPosition = true;
        copies.update(loose);
    }

    void wish(PokemonDexNum dex) {
        pokedex::Wishlist w;
        w.pokemonDexNum = dex;
        w.sources = {"ebay"};
        w.insertedAt = at("2026-07-14T09:00:00Z");
        w.updatedAt = w.insertedAt;
        wishlist.save(w);
    }

    std::vector<CardBinderEntry> rows() { return guide.buildEntries(binder); }

    // Plan a move against the binder's current guide, handing the planner the same
    // placeholder verdict the guide itself would reach — otherwise a projected placeholder
    // for a wished species would carry the wrong status and the anti-drift check below
    // would fail on a difference that isn't a real disagreement about ORDER.
    BinderMovePlan plan(const std::string& copyId, int targetPocket) {
        return planCardMove(binder, rows(), copyId, targetPocket, [this](PokemonDexNum dex) {
            return guide.placeholderStatusFor(binder.id, dex);
        });
    }

    // Commit a plan and pick the updated binder back up, exactly as the GUI does.
    void apply(const BinderMovePlan& p) { binder = service.applyMove("b1", p); }

    // The row layout as a readable sequence: a card's id, "(blank)", or "#<dex>" for a
    // placeholder — so an assertion reads like the screen does.
    static std::vector<std::string> layout(const std::vector<CardBinderEntry>& entries) {
        std::vector<std::string> out;
        for (const CardBinderEntry& e : entries) {
            if (e.cardCopyId) {
                out.push_back(*e.cardCopyId);
            } else if (e.pokemon) {
                out.push_back("#" + std::to_string(e.pokemon->dexNumber));
            } else {
                out.emplace_back("(blank)");
            }
        }
        return out;
    }

    std::vector<std::string> layoutNow() { return layout(rows()); }

    // The rows' verdicts, so a projection can't match on order alone while getting a
    // substituted placeholder's status wrong.
    static std::vector<int> statuses(const std::vector<CardBinderEntry>& entries) {
        std::vector<int> out;
        for (const CardBinderEntry& e : entries) {
            out.push_back(e.status ? static_cast<int>(*e.status) : -1);
        }
        return out;
    }

    // How many pockets the arrangement occupies. It SHRINKS as moves consume blanks, so a
    // target past the end clamps to a moving figure.
    static int pocketCount(const std::vector<CardBinderEntry>& entries) {
        int n = 0;
        for (const CardBinderEntry& e : entries) {
            n += pokedex::holdsPocket(e) ? 1 : 0;
        }
        return n;
    }

    // How many pockets the ARRANGED album occupies — the loose run at the end excluded,
    // since no pocket there is a move target.
    static int arrangedPocketCount(const std::vector<CardBinderEntry>& entries) {
        int n = 0;
        for (const CardBinderEntry& e : entries) {
            if (e.noFixedPosition) {
                break;
            }
            n += pokedex::holdsPocket(e) ? 1 : 0;
        }
        return n;
    }

    // The 0-based pocket a card occupies, counting only rows that hold one.
    static int pocketOf(const std::vector<CardBinderEntry>& entries, const std::string& copyId) {
        int pocket = 0;
        for (const CardBinderEntry& e : entries) {
            if (!pokedex::holdsPocket(e)) {
                continue;
            }
            if (e.cardCopyId == copyId) {
                return pocket;
            }
            ++pocket;
        }
        return -1;
    }
};

// THE ANTI-DRIFT TEST. The planner simulates the arrangement and buildEntries emits it —
// two encodings of one ordering rule. Applying a plan for real and re-reading the guide is
// the only thing that stops them diverging, so it runs over a spread of shapes rather than
// one, and every other test here is only as trustworthy as this one.
TEST_F(MoveTest, ProjectedRowsMatchWhatTheGuideActuallyEmits) {
    openBinder();
    file("a", 1);
    file("b", 25, 10);
    file("c", 25, 11);
    file("trainer", std::nullopt, 12);
    file("energy", std::nullopt, 13);
    binder = service.insertBlanks("b1", {.beforeDexNum = 25, .blanks = 2});

    // Every card into every pocket, one move at a time, each verified against reality
    // before the next is planned on top of it.
    for (const std::string& copyId : {"trainer", "a", "energy", "c", "b"}) {
        for (int pocket = 0; pocket < 8; ++pocket) {
            const BinderMovePlan p = plan(copyId, pocket);
            apply(p);
            const auto actual = rows();
            EXPECT_EQ(layout(p.projectedRows), layout(actual))
                << "moving " << copyId << " to pocket " << pocket;
            // And the card really is where it was sent — clamped to the last pocket,
            // since a consumed blank leaves the arrangement one sleeve shorter.
            ASSERT_EQ(pocketOf(actual, copyId), std::min(pocket, pocketCount(actual) - 1))
                << "moving " << copyId << " to pocket " << pocket;
        }
    }
}

// The same check over a REGION-SCOPED binder, where the reserved Pokédex slots are in
// play. This is the shape the sweep above can't reach: a region-less binder lists only
// what it holds, so taking a card out of its species never leaves a placeholder behind,
// and the projection never has to grow a row where one was removed.
//
// Bulbasaur is wished, so the substituted placeholder's status is exercised too — a
// projection that always guessed Incomplete would pass every other case here.
TEST_F(MoveTest, ProjectedRowsMatchTheGuideWhenAMoveVacatesAReservedSlot) {
    openBinder({Region::Kanto});
    wish(1);              // Bulbasaur — a placeholder verdict that isn't Incomplete
    file("bulba", 1);     // its only copy: moving it must leave the slot reserved
    file("pika", 25);
    file("mew", 151);
    file("trainer", std::nullopt, 12);
    binder = service.insertBlanks("b1", {.beforeDexNum = 25, .blanks = 2});

    for (const std::string& copyId : {"bulba", "trainer", "pika", "mew"}) {
        for (int pocket = 0; pocket < 10; ++pocket) {
            const BinderMovePlan p = plan(copyId, pocket);
            apply(p);
            const auto actual = rows();
            EXPECT_EQ(layout(p.projectedRows), layout(actual))
                << "moving " << copyId << " to pocket " << pocket;
            EXPECT_EQ(statuses(p.projectedRows), statuses(actual))
                << "moving " << copyId << " to pocket " << pocket;
            ASSERT_EQ(pocketOf(actual, copyId), std::min(pocket, pocketCount(actual) - 1))
                << "moving " << copyId << " to pocket " << pocket;
        }
    }
    // The checklist never lost a slot along the way, however much was shuffled.
    EXPECT_EQ(pokedex::listedSpecies(binder, rows()).size(), 151u);
}

// The sweep once more, this time with cards that keep NO fixed position filed alongside.
// Their rows trail every other one and are not move targets, so the planner reasons about
// the arranged prefix alone while still having to project the loose run verbatim — exactly
// the kind of split that drifts from buildEntries when only one side knows about it.
TEST_F(MoveTest, ProjectedRowsMatchTheGuideWithLooseCardsAtTheEnd) {
    openBinder();
    file("a", 1);
    file("b", 25, 10);
    fileLoose("loose", 25, 11);
    file("trainer", std::nullopt, 12);
    fileLoose("looseTrainer", std::nullopt, 13);
    binder = service.insertBlanks("b1", {.beforeDexNum = 25, .blanks = 2});

    for (const std::string& copyId : {"trainer", "a", "b"}) {
        for (int pocket = 0; pocket < 8; ++pocket) {
            const BinderMovePlan p = plan(copyId, pocket);
            apply(p);
            const auto actual = rows();
            EXPECT_EQ(layout(p.projectedRows), layout(actual))
                << "moving " << copyId << " to pocket " << pocket;
            // A pocket past the arranged album lands the card in the LAST ARRANGED sleeve,
            // never among the loose ones — that run is not somewhere a card can be sent.
            ASSERT_EQ(pocketOf(actual, copyId),
                      std::min(pocket, arrangedPocketCount(actual) - 1))
                << "moving " << copyId << " to pocket " << pocket;
            // And the loose run itself is exactly where it was, in filed order.
            const auto layoutRows = layout(actual);
            ASSERT_GE(layoutRows.size(), 2u);
            EXPECT_EQ(layoutRows[layoutRows.size() - 2], "loose");
            EXPECT_EQ(layoutRows.back(), "looseTrainer");
        }
    }
}

// A card that gave its position up has no pocket to be moved from or to, so the planner
// refuses rather than quietly pinning it — which would be the exact opposite of what the
// flag asks for. (The GUI disables the button; this is the contract behind it.)
TEST_F(MoveTest, ALooseCardCannotBeMoved) {
    openBinder();
    file("a", 1);
    fileLoose("loose", 25);

    EXPECT_THROW(plan("loose", 0), BinderMoveError);
}

// The planner truncates the loose run off the end, which is only sound because the guide
// emits it strictly last. Rows that break that (hand-built, or re-sorted by a view) are
// refused rather than indexing the truncated span past its end.
TEST_F(MoveTest, RowsWithALooseCardBeforeAnArrangedOneAreRefused) {
    openBinder();
    file("a", 1);
    fileLoose("loose", 25);

    std::vector<CardBinderEntry> reversed = rows();
    std::reverse(reversed.begin(), reversed.end());  // the loose card now comes first
    EXPECT_THROW(planCardMove(binder, reversed, "a", 0, nullptr), BinderMoveError);
}

// A move must never DELETE a page break recorded against a species that has become an
// extra. canonicalBlankSets only spares a run whose anchor names no row at all, so this
// held only once the guide started emitting those anchors in the extras.
TEST_F(MoveTest, AMoveDoesNotDestroyAPageBreakRecordedOnAnExtrasSpecies) {
    openBinder({Region::Kanto});
    file("kanto", 25);
    file("johto", 200);  // out of region: an extra
    binder = service.insertBlanks("b1", {.beforeDexNum = 200, .blanks = 2});
    const auto blanksNow = [this] {
        int n = 0;
        for (const CardBinderEntry& e : rows()) {
            n += pokedex::isBlankPocket(e) ? 1 : 0;
        }
        return n;
    };
    ASSERT_EQ(blanksNow(), 2);

    apply(plan("kanto", 0));

    EXPECT_EQ(blanksNow(), 2) << "the user's gap must survive an unrelated move";
}

// --- riders --------------------------------------------------------------------------

// A card pinned before another RIDES on it: the guide emits it wherever its anchor goes.
// So moving the anchor has to carry the whole convoy, both in the projection and in the
// shiftedCards figure the user is warned with.
TEST_F(MoveTest, MovingACardCarriesTheCardsPinnedToIt) {
    openBinder();
    file("a", 1);
    file("b", 25);
    file("c", 100);

    apply(plan("c", 0));  // c pins itself before a
    apply(plan("b", 0));  // b pins itself before c — b is now c's rider
    ASSERT_EQ(layoutNow(), (std::vector<std::string>{"b", "c", "a"}));

    const BinderMovePlan p = plan("c", 2);
    apply(p);

    // b came along, so the projection must say so — and it must match reality.
    EXPECT_EQ(layout(p.projectedRows), layoutNow());
    EXPECT_EQ(layoutNow(), (std::vector<std::string>{"a", "b", "c"}));
    // Both a and b changed sleeve, so the user is warned about two cards, not one.
    EXPECT_EQ(p.shiftedCards, 2);
}

// --- reserved slots ------------------------------------------------------------------

// How many rows stand for `dex` with no card in them.
int placeholdersFor(const std::vector<CardBinderEntry>& entries, PokemonDexNum dex) {
    int n = 0;
    for (const CardBinderEntry& e : entries) {
        if (e.pokemon && e.pokemon->dexNumber == dex && !e.cardCopyId) {
            ++n;
        }
    }
    return n;
}

// A loose sibling is not holding its species' reserved slot: it sits at the back of the
// album, so moving the copy that WAS in the Pokédex sleeve must still leave a placeholder
// there. (vacatesReservedSlot's sibling scan is what decides this.)
TEST_F(MoveTest, ALooseSiblingDoesNotHoldTheReservedSlot) {
    openBinder({Region::Kanto});
    file("pika", 25);
    fileLoose("loosePika", 25, 10);
    ASSERT_EQ(placeholdersFor(rows(), 25), 0);  // the natural copy is in the sleeve

    apply(plan("pika", 0));  // move it to the front, off its slot

    const auto after = rows();
    EXPECT_EQ(placeholdersFor(after, 25), 1) << "the loose copy does not stand in for it";
    EXPECT_EQ(pocketOf(after, "pika"), 0);
    // The loose one never moved, and is still last.
    EXPECT_EQ(layout(after).back(), "loosePika");
}

// Pulling a card off its Pokédex slot and dropping it into a blank is a pure swap: the
// blank is consumed, the slot it left becomes a placeholder, and NOTHING else changes
// pocket. So the user is not prompted — there is nothing to warn about.
TEST_F(MoveTest, MovingACardOffItsReservedSlotIntoABlankShiftsNothing) {
    openBinder({Region::Kanto});
    file("pika", 25);
    binder = service.insertBlanks("b1", {.beforeDexNum = 1, .blanks = 1});

    const std::size_t before = rows().size();
    const BinderMovePlan p = plan("pika", 0);  // the blank, right at the front
    EXPECT_EQ(p.shiftedCards, 0);
    apply(p);

    const auto after = rows();
    EXPECT_EQ(pocketOf(after, "pika"), 0);
    EXPECT_EQ(placeholdersFor(after, 25), 1);  // #25's sleeve is held open
    // One blank spent, one placeholder gained — the album is exactly as full as it was.
    EXPECT_EQ(after.size(), before);
}

// A card that is ALREADY pinned left its placeholder behind on the first move. Moving it
// again must not mint a second one — that would invent a pocket out of nothing, and every
// later move would compound it.
TEST_F(MoveTest, MovingAnAlreadyPlacedCardDoesNotMintASecondPlaceholder) {
    openBinder({Region::Kanto});
    file("pika", 25);

    apply(plan("pika", 0));
    ASSERT_EQ(placeholdersFor(rows(), 25), 1);

    apply(plan("pika", 40));
    const auto after = rows();
    EXPECT_EQ(placeholdersFor(after, 25), 1);
    EXPECT_EQ(pocketOf(after, "pika"), 40);
    EXPECT_EQ(after.size(), 152u);  // 151 slots + the card, however often it is moved
}

// A gap in front of the LAST row is normally dropped when that row leaves: empty pockets
// trailing off the end of a binder describe nothing. But a vacated slot leaves a
// placeholder standing in that spot, so there is still a row for the gap to sit in front
// of, and the user's page break survives.
TEST_F(MoveTest, AGapBeforeTheLastCardSurvivesWhenItsSlotStaysReserved) {
    openBinder({Region::Kanto});
    file("mew", 151);  // the final row of a Kanto binder
    binder = service.insertBlanks("b1", {.beforeCopyId = "mew", .blanks = 1});
    ASSERT_EQ(rows().size(), 152u);  // 151 species + the blank

    apply(plan("mew", 0));

    const auto after = rows();
    int blanks = 0;
    for (const CardBinderEntry& e : after) {
        blanks += pokedex::isBlankPocket(e) ? 1 : 0;
    }
    EXPECT_EQ(blanks, 1) << "the page break in front of #151 must not be swept away";
    EXPECT_EQ(placeholdersFor(after, 151), 1);
    EXPECT_EQ(pocketOf(after, "mew"), 0);
}

// --- landing on a blank --------------------------------------------------------------

// The case the feature exists for: the blank is consumed, the card takes that exact
// sleeve, and because a slot was freed and a slot filled, nothing else moves at all.
TEST_F(MoveTest, MovingIntoABlankConsumesItAndShiftsNothing) {
    openBinder();
    file("pika", 25);
    file("trainer", std::nullopt, 10);
    binder = service.insertBlanks("b1", {.beforeDexNum = 25, .blanks = 1});
    ASSERT_EQ(layoutNow(), (std::vector<std::string>{"(blank)", "pika", "trainer"}));

    const BinderMovePlan p = plan("trainer", 0);
    EXPECT_EQ(p.shiftedCards, 0);  // pika stays at its pocket, so no prompt is warranted
    apply(p);

    EXPECT_EQ(layoutNow(), (std::vector<std::string>{"trainer", "pika"}));
}

// A two-blank gap, targeted at its SECOND pocket. This is the case the count-based blank
// table looked unable to express: the surviving blank is re-anchored onto the moved card
// so it renders ahead of it, putting the card in the sleeve the user actually pointed at.
TEST_F(MoveTest, MovingIntoTheSecondBlankOfAGapTakesThatExactSleeve) {
    openBinder();
    file("pika", 25);
    file("trainer", std::nullopt, 10);
    binder = service.insertBlanks("b1", {.beforeDexNum = 25, .blanks = 2});
    ASSERT_EQ(layoutNow(),
              (std::vector<std::string>{"(blank)", "(blank)", "pika", "trainer"}));

    apply(plan("trainer", 1));  // the second empty sleeve

    EXPECT_EQ(layoutNow(), (std::vector<std::string>{"(blank)", "trainer", "pika"}));
}

// And its mirror: the FIRST pocket of the same gap, where the blank must end up after the
// card instead. One target apart, opposite rebalancing, both exact.
TEST_F(MoveTest, MovingIntoTheFirstBlankOfAGapLeavesTheOtherBehindIt) {
    openBinder();
    file("pika", 25);
    file("trainer", std::nullopt, 10);
    binder = service.insertBlanks("b1", {.beforeDexNum = 25, .blanks = 2});

    apply(plan("trainer", 0));

    EXPECT_EQ(layoutNow(), (std::vector<std::string>{"trainer", "(blank)", "pika"}));
}

// --- landing on a card ---------------------------------------------------------------

// The target names a position in the FINAL arrangement, which is what makes a forward
// move land where it was asked to. Taking the card out first is what achieves that:
// against the CURRENT arrangement the same target would come up one pocket short.
TEST_F(MoveTest, ForwardMoveLandsOnTheRequestedPocket) {
    openBinder();
    file("a", 1);
    file("b", 2, 10);
    file("c", 3, 11);
    file("d", 4, 12);
    ASSERT_EQ(pocketOf(rows(), "a"), 0);

    const BinderMovePlan p = plan("a", 2);
    EXPECT_EQ(p.shiftedCards, 2);  // b and c each slide up one
    apply(p);

    EXPECT_EQ(layoutNow(), (std::vector<std::string>{"b", "c", "a", "d"}));
    EXPECT_EQ(pocketOf(rows(), "a"), 2);
}

TEST_F(MoveTest, BackwardMoveLandsOnTheRequestedPocket) {
    openBinder();
    file("a", 1);
    file("b", 2, 10);
    file("c", 3, 11);
    file("d", 4, 12);

    const BinderMovePlan p = plan("d", 1);
    EXPECT_EQ(p.shiftedCards, 2);  // b and c each slide down one
    apply(p);

    EXPECT_EQ(layoutNow(), (std::vector<std::string>{"a", "d", "b", "c"}));
    EXPECT_EQ(pocketOf(rows(), "d"), 1);
}

// Moving a card onto the pocket it already occupies changes nothing and must not claim
// otherwise — the figure the confirmation prompt is built on.
TEST_F(MoveTest, MovingACardOntoItsOwnPocketShiftsNothing) {
    openBinder();
    file("a", 1);
    file("b", 2, 10);
    file("c", 3, 11);

    const BinderMovePlan p = plan("b", 1);
    EXPECT_EQ(p.shiftedCards, 0);
    apply(p);

    EXPECT_EQ(layoutNow(), (std::vector<std::string>{"a", "b", "c"}));
}

// A pocket past the last row is the "at the very end" anchor — the only way to reach the
// final sleeve, since every other target is phrased as "before some row".
TEST_F(MoveTest, ATargetPastTheLastRowPlacesTheCardAtTheEnd) {
    openBinder();
    file("a", 1);
    file("b", 2, 10);
    file("c", 3, 11);

    apply(plan("a", 99));

    EXPECT_EQ(layoutNow(), (std::vector<std::string>{"b", "c", "a"}));
    EXPECT_FALSE(binder.cardPlacements.empty());
    EXPECT_FALSE(binder.cardPlacements[0].beforeDexNum.has_value());
    EXPECT_FALSE(binder.cardPlacements[0].beforeCopyId.has_value());
}

// --- what happens to the gap a card leaves behind -------------------------------------

// A deliberate empty sleeve is a fact about the album, so the card leaving must not close
// it. The run is re-anchored onto whatever now follows.
TEST_F(MoveTest, TheGapACardLeavesBehindSurvivesTheMove) {
    openBinder();
    file("a", 1);
    file("b", 2, 10);
    file("c", 3, 11);
    binder = service.insertBlanks("b1", {.beforeDexNum = 2, .blanks = 1});
    ASSERT_EQ(layoutNow(), (std::vector<std::string>{"a", "(blank)", "b", "c"}));

    apply(plan("b", 3));  // send b to the end

    EXPECT_EQ(layoutNow(), (std::vector<std::string>{"a", "(blank)", "c", "b"}));
}

// The exception: a run at the very END of the guide has no following row to re-anchor to,
// and empty sleeves after the last card describe nothing, so they are dropped.
TEST_F(MoveTest, AGapStrandedAtTheEndIsDroppedRatherThanCarried) {
    openBinder();
    file("a", 1);
    file("b", 2, 10);
    binder = service.insertBlanks("b1", {.beforeCopyId = "b", .blanks = 1});
    ASSERT_EQ(layoutNow(), (std::vector<std::string>{"a", "(blank)", "b"}));

    apply(plan("b", 0));

    EXPECT_EQ(layoutNow(), (std::vector<std::string>{"b", "a"}));
}

// --- several moved cards at one anchor -------------------------------------------------

// Filling a gap left to right: each card targets the next free sleeve and lands there,
// which works because a card aimed at the anchor row's pocket appends nearest to it.
TEST_F(MoveTest, SeveralCardsFillOneGapInTheOrderTheyAreMoved) {
    openBinder();
    file("pika", 25);
    file("one", std::nullopt, 10);
    file("two", std::nullopt, 11);
    binder = service.insertBlanks("b1", {.beforeDexNum = 25, .blanks = 2});
    ASSERT_EQ(layoutNow(),
              (std::vector<std::string>{"(blank)", "(blank)", "pika", "one", "two"}));

    apply(plan("one", 0));
    apply(plan("two", 1));

    EXPECT_EQ(layoutNow(), (std::vector<std::string>{"one", "two", "pika"}));
}

// Aiming at a sleeve a moved card already holds pushes that card along, rather than
// landing after it — the case that makes placements chain.
TEST_F(MoveTest, MovingOntoAnAlreadyMovedCardsPocketGoesAheadOfIt) {
    openBinder();
    file("pika", 25);
    file("one", std::nullopt, 10);
    file("two", std::nullopt, 11);

    apply(plan("one", 0));  // one now sits ahead of pika
    ASSERT_EQ(layoutNow(), (std::vector<std::string>{"one", "pika", "two"}));

    apply(plan("two", 0));  // aim at one's sleeve

    EXPECT_EQ(layoutNow(), (std::vector<std::string>{"two", "one", "pika"}));
}

// --- returning to natural order --------------------------------------------------------

// Reset must PRESERVE the page break, not delete it. Because a move canonicalises blank
// runs onto the row they precede, the gap the user opened before Pikachu ends up recorded
// against the card slid into it — so treating that run as "the card's own" and dropping
// it would quietly destroy the page break they were arranging around.
TEST_F(MoveTest, ResetReturnsTheCardAndLeavesThePageBreakStanding) {
    openBinder();
    file("pika", 25);
    file("trainer", std::nullopt, 10);
    binder = service.insertBlanks("b1", {.beforeDexNum = 25, .blanks = 2});
    apply(plan("trainer", 1));
    ASSERT_EQ(layoutNow(), (std::vector<std::string>{"(blank)", "trainer", "pika"}));

    binder = service.applyMove("b1", pokedex::planCardReset(binder, rows(), "trainer"));

    // The card is back after the species rows, and the empty sleeve it was sitting behind
    // stayed where it was — re-anchored onto Pikachu, which now follows it.
    EXPECT_EQ(layoutNow(), (std::vector<std::string>{"(blank)", "pika", "trainer"}));
    EXPECT_TRUE(binder.cardPlacements.empty());
}

// The exception, mirroring a move: a run with nothing after it has no row left to sit in
// front of, so it falls away rather than becoming a gap at the end of the binder.
TEST_F(MoveTest, ResetDropsAGapThatWouldBeStrandedAtTheEnd) {
    openBinder();
    file("pika", 25);
    file("trainer", std::nullopt, 10);
    // Pin the card at the very end, then open a gap in front of it — the one arrangement
    // whose run has no row after it to inherit the pockets.
    apply(plan("trainer", 99));
    binder = service.insertBlanks("b1", {.beforeCopyId = "trainer", .blanks = 1});
    ASSERT_EQ(layoutNow(), (std::vector<std::string>{"pika", "(blank)", "trainer"}));

    binder = service.applyMove("b1", pokedex::planCardReset(binder, rows(), "trainer"));

    EXPECT_EQ(layoutNow(), (std::vector<std::string>{"pika", "trainer"}));
    EXPECT_TRUE(binder.cardPlacements.empty());
    EXPECT_TRUE(binder.pocketBlanks.empty());
}

TEST_F(MoveTest, ResetRejectsACardWithNoRowHere) {
    openBinder();
    file("pika", 25);
    EXPECT_THROW(pokedex::planCardReset(binder, rows(), "not-filed-here"), BinderMoveError);
}

// --- rejections -------------------------------------------------------------------------

TEST_F(MoveTest, PlanningRejectsInputItCannotPlace) {
    openBinder();
    file("pika", 25);
    file("gone", 26, 10, CardOwnership::Removed);

    EXPECT_THROW(plan("pika", -1), BinderMoveError);
    EXPECT_THROW(plan("not-filed-here", 0), BinderMoveError);
    // A removed card is frozen history and holds no sleeve, so there is nothing to move.
    EXPECT_THROW(plan("gone", 0), BinderMoveError);
}

// A removed card keeps its row but holds no pocket, so it must not consume a target and
// must survive the move untouched.
TEST_F(MoveTest, RemovedRowsAreSkippedWhenCountingPockets) {
    openBinder();
    file("a", 1);
    file("gone", 2, 10, CardOwnership::Removed);
    file("c", 3, 11);
    file("trainer", std::nullopt, 12);
    ASSERT_EQ(layoutNow(), (std::vector<std::string>{"a", "gone", "c", "trainer"}));

    apply(plan("trainer", 1));  // pocket 1 is c, since `gone` holds none

    EXPECT_EQ(layoutNow(), (std::vector<std::string>{"a", "gone", "trainer", "c"}));
}

// --- the screenshot, end to end ----------------------------------------------------------

// Kanto ends mid-page 17; two blanks push Chespin to page 18 pocket 1x1. A Trainer card
// moved into 17.3x3 must take that exact sleeve, leave 17.3x2 empty, and leave Chespin
// exactly where it was.
TEST_F(MoveTest, TheKantoToKalosGapAcceptsACardAtTheSleeveAimedAt) {
    openBinder({Region::Kanto, Region::Kalos});
    file("trainer", std::nullopt);
    binder = service.insertBlanks("b1", {.beforeDexNum = kChespin, .blanks = 2});

    const auto gapped = rows();
    int chespinPocket = 0;
    for (const CardBinderEntry& e : gapped) {
        if (e.pokemon && e.pokemon->dexNumber == kChespin) {
            break;
        }
        chespinPocket += pokedex::holdsPocket(e) ? 1 : 0;
    }
    ASSERT_EQ(chespinPocket, 153);      // 0-based: page 18, pocket 1x1 of a 3x3 album
    ASSERT_EQ(chespinPocket % 9, 0);

    // 17.3x3 is the pocket immediately before Chespin's.
    const BinderMovePlan p = plan("trainer", chespinPocket - 1);
    EXPECT_EQ(p.shiftedCards, 0);  // nothing else has to be re-sleeved
    apply(p);

    const auto after = rows();
    EXPECT_EQ(pocketOf(after, "trainer"), chespinPocket - 1);  // page 17, pocket 3x3
    int chespinAfter = 0;
    for (const CardBinderEntry& e : after) {
        if (e.pokemon && e.pokemon->dexNumber == kChespin) {
            break;
        }
        chespinAfter += pokedex::holdsPocket(e) ? 1 : 0;
    }
    EXPECT_EQ(chespinAfter, chespinPocket);  // still opens page 18
}

}  // namespace
