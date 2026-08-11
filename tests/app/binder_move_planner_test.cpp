#include "core/app/binder_move_planner.h"

#include <gtest/gtest.h>

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

    std::vector<CardBinderEntry> rows() { return guide.buildEntries(binder); }

    // Plan a move against the binder's current guide.
    BinderMovePlan plan(const std::string& copyId, int targetPocket) {
        return planCardMove(binder, rows(), copyId, targetPocket);
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

    // How many pockets the arrangement occupies. It SHRINKS as moves consume blanks, so a
    // target past the end clamps to a moving figure.
    static int pocketCount(const std::vector<CardBinderEntry>& entries) {
        int n = 0;
        for (const CardBinderEntry& e : entries) {
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
