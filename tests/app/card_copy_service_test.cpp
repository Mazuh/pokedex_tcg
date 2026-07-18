#include "core/app/card_copy_service.h"

#include <gtest/gtest.h>

#include <optional>
#include <string>

#include "core/domain/card_condition.h"
#include "core/domain/card_ownership.h"
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
using pokedex::CardOwnership;
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
    const CardCopy copy = f.service.create(6, ref(), CardOwnership::Owned,
                                           CardCondition::NearMint, std::nullopt, "bought at a con");
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
                                           std::nullopt, std::nullopt, "");
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
                                           std::nullopt, "");
    EXPECT_EQ(copy.condition, std::nullopt);
    EXPECT_EQ(f.repo.find("copy-1")->condition, std::nullopt);
}

TEST(CardCopyServiceTest, CreateTrimsReferenceAndRequiresACollectorNumber) {
    Fixture f;
    const CardCopy copy = f.service.create(6, CardReference{"  OBF ", " EN", " 125/197 "},
                                           CardOwnership::Owned, CardCondition::NearMint,
                                           std::nullopt, "");
    EXPECT_EQ(copy.cardRef, (CardReference{"OBF", "EN", "125/197"}));

    EXPECT_THROW(f.service.create(6, CardReference{"OBF", "EN", "   "}, CardOwnership::Owned,
                                  CardCondition::NearMint, std::nullopt, ""),
                 CardCopyError);
}

TEST(CardCopyServiceTest, EditDetailsChangesConditionCommentsAndBumpsUpdatedAt) {
    Fixture f;
    f.service.create(6, ref(), CardOwnership::Owned, CardCondition::NearMint, std::nullopt, "");
    f.now = at("2026-07-17T08:00:00Z");
    f.service.editDetails("copy-1", CardCondition::LightlyPlayed, "small edge whitening");

    const CardCopy stored = *f.repo.find("copy-1");
    EXPECT_EQ(stored.condition, CardCondition::LightlyPlayed);
    EXPECT_EQ(stored.comments, "small edge whitening");
    EXPECT_EQ(stored.updatedAt, at("2026-07-17T08:00:00Z"));
    EXPECT_EQ(stored.insertedAt, at("2026-07-16T10:00:00Z"));
}

TEST(CardCopyServiceTest, AssignToBinderFilesAndClearsWithoutTouchingOwnership) {
    Fixture f;
    // A binder to file into (the storage FK requires a real binder row).
    f.db.exec(
        "INSERT INTO card_binder(id,name,region,inserted_at,updated_at)"
        " VALUES('b1','Kanto',NULL,'2026-07-16T10:00:00Z','2026-07-16T10:00:00Z');");
    f.service.create(6, ref(), CardOwnership::Owned, CardCondition::NearMint, std::nullopt, "");

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

    const CardCopy copy = f.service.create(6, ref(), CardOwnership::Owned,
                                           CardCondition::NearMint, std::string("b1"), "");
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
    f.service.create(6, ref(), CardOwnership::Owned, CardCondition::NearMint, std::nullopt, "");
    f.now = at("2026-07-18T08:00:00Z");
    f.service.remove("copy-1");

    const CardCopy stored = *f.repo.find("copy-1");
    EXPECT_EQ(stored.ownership, CardOwnership::Removed);
    EXPECT_EQ(stored.updatedAt, at("2026-07-18T08:00:00Z"));
}

TEST(CardCopyServiceTest, RemoveAppendsAnOptionalNoteToExistingComments) {
    Fixture f;
    f.service.create(6, ref(), CardOwnership::Owned, CardCondition::NearMint, std::nullopt,
                     "bought at a con");
    f.service.remove("copy-1", "  sold to Alex for $40  ");  // trimmed
    EXPECT_EQ(f.repo.find("copy-1")->comments, "bought at a con\nsold to Alex for $40");

    // A copy with no prior comments and a blank note keeps empty comments.
    f.service.create(4, ref(), CardOwnership::Owned, CardCondition::NearMint, std::nullopt, "");
    f.service.remove("copy-2", "   ");
    EXPECT_TRUE(f.repo.find("copy-2")->comments.empty());
}

TEST(CardCopyServiceTest, HardDeleteDropsTheRow) {
    Fixture f;
    f.service.create(6, ref(), CardOwnership::Owned, CardCondition::NearMint, std::nullopt, "");
    f.service.hardDelete("copy-1");
    EXPECT_FALSE(f.repo.find("copy-1").has_value());
}

TEST(CardCopyServiceTest, EditRemoveAndHardDeleteThrowForMissingId) {
    Fixture f;
    EXPECT_THROW(f.service.editDetails("ghost", CardCondition::NearMint, ""), CardCopyError);
    EXPECT_THROW(f.service.assignToBinder("ghost", std::nullopt), CardCopyError);
    EXPECT_THROW(f.service.remove("ghost"), CardCopyError);
    EXPECT_THROW(f.service.hardDelete("ghost"), CardCopyError);
}

TEST(CardCopyServiceTest, ListAllReturnsEveryCopy) {
    Fixture f;
    f.service.create(1, ref(), CardOwnership::Owned, CardCondition::NearMint, std::nullopt, "");
    f.service.create(4, ref(), CardOwnership::Incoming, CardCondition::NearMint, std::nullopt, "");
    EXPECT_EQ(f.service.listAll().size(), 2u);
}

}  // namespace
