#include "core/app/card_copy_service.h"

#include <gtest/gtest.h>

#include <optional>
#include <string>

#include "core/domain/card_condition.h"
#include "core/domain/card_foil.h"
#include "core/domain/card_ownership.h"
#include "core/domain/card_rarity.h"
#include "core/domain/card_reference.h"
#include "core/storage/card_copy_repository.h"
#include "core/storage/codecs.h"
#include "core/storage/database.h"

namespace {

using pokedex::CardCondition;
using pokedex::CardCopy;
using pokedex::CardCopyError;
using pokedex::CardCopyRepository;
using pokedex::CardCopyService;
using pokedex::CardFoil;
using pokedex::CardOwnership;
using pokedex::CardRarity;
using pokedex::CardReference;
using pokedex::Database;
using pokedex::Timestamp;

Timestamp at(const char* iso) { return pokedex::timestampFromIso(iso); }

// A deterministic fixture: in-memory DB, a fixed clock, and a sequential id
// generator (mirrors binder_service_test).
struct Fixture {
    Database db{":memory:"};
    CardCopyRepository repo{db};
    Timestamp now = at("2026-07-16T10:00:00Z");
    int idCounter = 0;
    CardCopyService service{repo, [this] { return now; },
                            [this] { return "copy-" + std::to_string(++idCounter); }};
    Fixture() { db.migrate(); }
};

CardReference ref() { return CardReference{"OBF", "EN", "125/197"}; }

TEST(CardCopyServiceTest, CreateMintsIdStampsAndPersists) {
    Fixture f;
    const CardCopy copy =
        f.service.create(6, ref(), CardOwnership::Owned, CardCondition::NearMint, std::nullopt,
                         std::nullopt, std::nullopt, "bought at a con");
    EXPECT_EQ(copy.id, "copy-1");
    EXPECT_EQ(copy.pokemonDexNum, 6);
    EXPECT_EQ(copy.cardRef, ref());
    EXPECT_EQ(copy.ownership, CardOwnership::Owned);
    EXPECT_EQ(copy.insertedAt, f.now);
    EXPECT_EQ(copy.updatedAt, f.now);

    const auto stored = f.repo.find("copy-1");
    ASSERT_TRUE(stored.has_value());
    EXPECT_EQ(stored->comments, "bought at a con");
}

TEST(CardCopyServiceTest, CreateAcceptsASpeciesFreeCard) {
    Fixture f;
    // A Trainer/Energy card: no dex number, but a card name carried on the reference.
    CardReference trainerRef{"SVI", "EN", "196/198"};
    trainerRef.name = "Boss's Orders";
    const CardCopy copy = f.service.create(std::nullopt, trainerRef, CardOwnership::Owned,
                                           std::nullopt, std::nullopt, std::nullopt, std::nullopt, "");
    EXPECT_EQ(copy.pokemonDexNum, std::nullopt);
    EXPECT_EQ(copy.cardRef.name, "Boss's Orders");
    // It round-trips through storage as species-free with its name intact.
    const auto stored = f.repo.find("copy-1");
    ASSERT_TRUE(stored.has_value());
    EXPECT_EQ(stored->pokemonDexNum, std::nullopt);
    EXPECT_EQ(stored->cardRef.name, "Boss's Orders");
}

TEST(CardCopyServiceTest, CreateAcceptsAnUngradedCondition) {
    Fixture f;
    const CardCopy copy = f.service.create(6, ref(), CardOwnership::Owned, std::nullopt,
                                           std::nullopt, std::nullopt, std::nullopt, "");
    EXPECT_EQ(copy.condition, std::nullopt);
    EXPECT_EQ(f.repo.find("copy-1")->condition, std::nullopt);
}

// Rarity and foil treatment are optional attributes carried by create and editable
// by editDetails — both default to nullopt and round-trip through storage.
TEST(CardCopyServiceTest, CreateAndEditDetailsCarryRarityAndFoil) {
    Fixture f;
    // Unspecified by default.
    const CardCopy blank = f.service.create(6, ref(), CardOwnership::Owned, std::nullopt,
                                            std::nullopt, std::nullopt, std::nullopt, "");
    EXPECT_EQ(blank.rarity, std::nullopt);
    EXPECT_EQ(blank.foil, std::nullopt);

    // Created with a rarity and foil, persisted.
    const CardCopy copy =
        f.service.create(6, ref(), CardOwnership::Owned, CardCondition::NearMint,
                         CardRarity::DoubleRare, CardFoil::Holo, std::nullopt, "");
    EXPECT_EQ(copy.rarity, CardRarity::DoubleRare);
    EXPECT_EQ(copy.foil, CardFoil::Holo);
    const auto stored = f.repo.find("copy-2");
    ASSERT_TRUE(stored.has_value());
    EXPECT_EQ(stored->rarity, CardRarity::DoubleRare);
    EXPECT_EQ(stored->foil, CardFoil::Holo);

    // editDetails changes both (and can clear them back to nullopt).
    f.service.editDetails("copy-2", ref(), CardOwnership::Owned, CardCondition::NearMint,
                          CardRarity::HyperRare, std::nullopt, "");
    const CardCopy edited = *f.repo.find("copy-2");
    EXPECT_EQ(edited.rarity, CardRarity::HyperRare);
    EXPECT_EQ(edited.foil, std::nullopt);
}

TEST(CardCopyServiceTest, CreateTrimsReferenceAndRequiresACollectorNumber) {
    Fixture f;
    const CardCopy copy = f.service.create(6, CardReference{"  OBF ", " EN", " 125/197 "},
                                           CardOwnership::Owned, CardCondition::NearMint,
                                           std::nullopt, std::nullopt, std::nullopt, "");
    EXPECT_EQ(copy.cardRef, (CardReference{"OBF", "EN", "125/197"}));

    EXPECT_THROW(f.service.create(6, CardReference{"OBF", "EN", "   "}, CardOwnership::Owned,
                                  CardCondition::NearMint, std::nullopt, std::nullopt, std::nullopt,
                                  ""),
                 CardCopyError);
}

TEST(CardCopyServiceTest, EditDetailsChangesReferenceOwnershipConditionCommentsAndBumpsUpdatedAt) {
    Fixture f;
    f.service.create(6, ref(), CardOwnership::Owned, CardCondition::NearMint, std::nullopt,
                     std::nullopt, std::nullopt, "");
    f.now = at("2026-07-17T08:00:00Z");
    // The copy's language is corrected (a reference field) alongside ownership,
    // condition, and comments; the rest of the printed identity is left as-is.
    f.service.editDetails("copy-1", CardReference{"OBF", "FR", "125/197"}, CardOwnership::Incoming,
                          CardCondition::LightlyPlayed, std::nullopt, std::nullopt,
                          "small edge whitening");

    const CardCopy stored = *f.repo.find("copy-1");
    EXPECT_EQ(stored.cardRef, (CardReference{"OBF", "FR", "125/197"}));
    EXPECT_EQ(stored.ownership, CardOwnership::Incoming);
    EXPECT_EQ(stored.condition, CardCondition::LightlyPlayed);
    EXPECT_EQ(stored.comments, "small edge whitening");
    EXPECT_EQ(stored.updatedAt, at("2026-07-17T08:00:00Z"));
    EXPECT_EQ(stored.insertedAt, at("2026-07-16T10:00:00Z"));
}

TEST(CardCopyServiceTest, EditDetailsTrimsReferenceAndRequiresACollectorNumber) {
    Fixture f;
    f.service.create(6, ref(), CardOwnership::Owned, CardCondition::NearMint, std::nullopt,
                     std::nullopt, std::nullopt, "");
    f.service.editDetails("copy-1", CardReference{" OBF ", " EN", " 125/197 "},
                          CardOwnership::Owned, CardCondition::NearMint, std::nullopt, std::nullopt,
                          "");
    EXPECT_EQ(f.repo.find("copy-1")->cardRef, (CardReference{"OBF", "EN", "125/197"}));

    EXPECT_THROW(f.service.editDetails("copy-1", CardReference{"OBF", "EN", "   "},
                                       CardOwnership::Owned, CardCondition::NearMint, std::nullopt,
                                       std::nullopt, ""),
                 CardCopyError);
}

TEST(CardCopyServiceTest, AssignToBinderFilesAndClearsWithoutTouchingOwnership) {
    Fixture f;
    // A binder to file into (the storage FK requires a real binder row).
    f.db.exec(
        "INSERT INTO card_binder(id,name,region,inserted_at,updated_at)"
        " VALUES('b1','Kanto',NULL,'2026-07-16T10:00:00Z','2026-07-16T10:00:00Z');");
    f.service.create(6, ref(), CardOwnership::Owned, CardCondition::NearMint, std::nullopt,
                     std::nullopt, std::nullopt, "");

    f.now = at("2026-07-17T08:00:00Z");
    f.service.assignToBinder("copy-1", "b1");
    CardCopy filed = *f.repo.find("copy-1");
    ASSERT_TRUE(filed.binderId.has_value());
    EXPECT_EQ(*filed.binderId, "b1");
    EXPECT_EQ(filed.ownership, CardOwnership::Owned);  // unchanged
    EXPECT_EQ(filed.updatedAt, at("2026-07-17T08:00:00Z"));

    f.service.assignToBinder("copy-1", std::nullopt);
    EXPECT_FALSE(f.repo.find("copy-1")->binderId.has_value());
}

TEST(CardCopyServiceTest, CreateFilesIntoABinderWhenGiven) {
    Fixture f;
    // The storage FK requires a real binder row to file into.
    f.db.exec(
        "INSERT INTO card_binder(id,name,region,inserted_at,updated_at)"
        " VALUES('b1','Kanto',NULL,'2026-07-16T10:00:00Z','2026-07-16T10:00:00Z');");

    const CardCopy copy = f.service.create(6, ref(), CardOwnership::Owned, CardCondition::NearMint,
                                           std::nullopt, std::nullopt, std::string("b1"), "");
    ASSERT_TRUE(copy.binderId.has_value());
    EXPECT_EQ(*copy.binderId, "b1");
    // And it survives the round-trip through storage (the read path the My Cards
    // binder column relies on).
    const auto stored = f.repo.find("copy-1");
    ASSERT_TRUE(stored.has_value());
    ASSERT_TRUE(stored->binderId.has_value());
    EXPECT_EQ(*stored->binderId, "b1");
}

TEST(CardCopyServiceTest, RemoveSoftDeletesToRemovedOwnership) {
    Fixture f;
    f.service.create(6, ref(), CardOwnership::Owned, CardCondition::NearMint, std::nullopt,
                     std::nullopt, std::nullopt, "");
    f.now = at("2026-07-18T08:00:00Z");
    f.service.remove("copy-1");

    const CardCopy stored = *f.repo.find("copy-1");
    EXPECT_EQ(stored.ownership, CardOwnership::Removed);
    EXPECT_EQ(stored.updatedAt, at("2026-07-18T08:00:00Z"));
}

TEST(CardCopyServiceTest, RemoveAppendsAnOptionalNoteToExistingComments) {
    Fixture f;
    f.service.create(6, ref(), CardOwnership::Owned, CardCondition::NearMint, std::nullopt,
                     std::nullopt, std::nullopt, "bought at a con");
    f.service.remove("copy-1", "  sold to Alex for $40  ");  // trimmed
    EXPECT_EQ(f.repo.find("copy-1")->comments, "bought at a con\nsold to Alex for $40");

    // A copy with no prior comments and a blank note keeps empty comments.
    f.service.create(4, ref(), CardOwnership::Owned, CardCondition::NearMint, std::nullopt,
                     std::nullopt, std::nullopt, "");
    f.service.remove("copy-2", "   ");
    EXPECT_TRUE(f.repo.find("copy-2")->comments.empty());
}

TEST(CardCopyServiceTest, RemovedCopyIsFrozenHistoryAndCannotBeEditedOrRefiled) {
    Fixture f;
    f.service.create(6, ref(), CardOwnership::Owned, CardCondition::NearMint, std::nullopt,
                     std::nullopt, std::nullopt, "");
    f.service.remove("copy-1");

    // Neither editing details nor refiling in a binder touches a removed copy.
    EXPECT_THROW(f.service.editDetails("copy-1", ref(), CardOwnership::Owned,
                                       CardCondition::NearMint, std::nullopt, std::nullopt,
                                       "second thoughts"),
                 CardCopyError);
    EXPECT_THROW(f.service.assignToBinder("copy-1", std::nullopt), CardCopyError);

    // The copy is untouched — still Removed, comments unchanged.
    const CardCopy stored = *f.repo.find("copy-1");
    EXPECT_EQ(stored.ownership, CardOwnership::Removed);
    EXPECT_TRUE(stored.comments.empty());
}

TEST(CardCopyServiceTest, HardDeleteDropsTheRow) {
    Fixture f;
    f.service.create(6, ref(), CardOwnership::Owned, CardCondition::NearMint, std::nullopt,
                     std::nullopt, std::nullopt, "");
    f.service.hardDelete("copy-1");
    EXPECT_FALSE(f.repo.find("copy-1").has_value());
}

TEST(CardCopyServiceTest, EditRemoveAndHardDeleteThrowForMissingId) {
    Fixture f;
    EXPECT_THROW(f.service.editDetails("ghost", ref(), CardOwnership::Owned,
                                       CardCondition::NearMint, std::nullopt, std::nullopt, ""),
                 CardCopyError);
    EXPECT_THROW(f.service.assignToBinder("ghost", std::nullopt), CardCopyError);
    EXPECT_THROW(f.service.remove("ghost"), CardCopyError);
    EXPECT_THROW(f.service.hardDelete("ghost"), CardCopyError);
}

TEST(CardCopyServiceTest, ListAllReturnsEveryCopy) {
    Fixture f;
    f.service.create(1, ref(), CardOwnership::Owned, CardCondition::NearMint, std::nullopt,
                     std::nullopt, std::nullopt, "");
    f.service.create(4, ref(), CardOwnership::Incoming, CardCondition::NearMint, std::nullopt,
                     std::nullopt, std::nullopt, "");
    EXPECT_EQ(f.service.listAll().size(), 2u);
}

TEST(CardCopyServiceTest, ListByBinderReturnsOnlyThatBindersCopies) {
    Fixture f;
    // The storage FK requires real binder rows to file into.
    f.db.exec(
        "INSERT INTO card_binder(id,name,region,inserted_at,updated_at)"
        " VALUES('b1','Kanto',NULL,'2026-07-16T10:00:00Z','2026-07-16T10:00:00Z'),"
        "('b2','Johto',NULL,'2026-07-16T10:00:00Z','2026-07-16T10:00:00Z');");
    f.service.create(1, ref(), CardOwnership::Owned, CardCondition::NearMint, std::nullopt,
                     std::nullopt, std::string("b1"), "");
    f.service.create(4, ref(), CardOwnership::Owned, CardCondition::NearMint, std::nullopt,
                     std::nullopt, std::string("b2"), "");
    f.service.create(7, ref(), CardOwnership::Owned, CardCondition::NearMint, std::nullopt,
                     std::nullopt, std::nullopt, "");
    const auto inB1 = f.service.listByBinder("b1");
    ASSERT_EQ(inB1.size(), 1u);
    EXPECT_EQ(inB1.front().pokemonDexNum, 1);
}

}  // namespace
