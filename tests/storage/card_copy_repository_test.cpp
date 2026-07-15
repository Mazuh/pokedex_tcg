#include "core/storage/card_copy_repository.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <optional>
#include <string>

#include "core/domain/card_condition.h"
#include "core/domain/card_copy.h"
#include "core/domain/card_ownership.h"
#include "core/domain/card_reference.h"
#include "core/storage/codecs.h"
#include "core/storage/database.h"

namespace {

using pokedex::CardCondition;
using pokedex::CardCopy;
using pokedex::CardCopyRepository;
using pokedex::CardOwnership;
using pokedex::CardReference;
using pokedex::Database;
using pokedex::PokemonDexNum;
using pokedex::Timestamp;

Timestamp at(const char* iso) { return pokedex::timestampFromIso(iso); }

CardCopy makeCopy(std::string id, PokemonDexNum dex, CardOwnership ownership,
                  std::optional<std::string> binderId,
                  const char* stamp = "2026-07-14T09:00:00Z") {
    CardCopy copy;
    copy.id = std::move(id);
    copy.pokemonDexNum = dex;
    copy.cardRef = CardReference{"MEW", "EN", "151/165"};
    copy.ownership = ownership;
    copy.condition = CardCondition::NearMint;
    copy.binderId = std::move(binderId);
    copy.comments = "bought at a con";
    copy.insertedAt = at(stamp);
    copy.updatedAt = at(stamp);
    return copy;
}

TEST(CardCopyRepositoryTest, AddThenListByBinderRoundTripsAllFields) {
    Database db(":memory:");
    db.migrate();
    db.exec(
        "INSERT INTO card_binder(id,name,region,inserted_at,updated_at)"
        " VALUES('b1','Kanto',NULL,'2026-07-14T09:00:00Z','2026-07-14T09:00:00Z');");
    CardCopyRepository repo(db);

    repo.add(makeCopy("c1", 25, CardOwnership::Owned, "b1"));

    const auto copies = repo.listByBinder("b1");
    ASSERT_EQ(copies.size(), 1u);
    const CardCopy& c = copies[0];
    EXPECT_EQ(c.id, "c1");
    EXPECT_EQ(c.pokemonDexNum, 25);
    EXPECT_EQ(c.cardRef, (CardReference{"MEW", "EN", "151/165"}));
    EXPECT_EQ(c.ownership, CardOwnership::Owned);
    EXPECT_EQ(c.condition, CardCondition::NearMint);
    ASSERT_TRUE(c.binderId.has_value());
    EXPECT_EQ(*c.binderId, "b1");
    EXPECT_EQ(c.comments, "bought at a con");
    EXPECT_EQ(c.insertedAt, at("2026-07-14T09:00:00Z"));
}

TEST(CardCopyRepositoryTest, ListByBinderReturnsOnlyThatBindersCopies) {
    Database db(":memory:");
    db.migrate();
    db.exec(
        "INSERT INTO card_binder(id,name,region,inserted_at,updated_at)"
        " VALUES('b1','A',NULL,'2026-07-14T09:00:00Z','2026-07-14T09:00:00Z'),"
        "       ('b2','B',NULL,'2026-07-14T09:00:00Z','2026-07-14T09:00:00Z');");
    CardCopyRepository repo(db);
    repo.add(makeCopy("c1", 1, CardOwnership::Owned, "b1"));
    repo.add(makeCopy("c2", 4, CardOwnership::Owned, "b2"));
    repo.add(makeCopy("c3", 7, CardOwnership::Owned, std::nullopt));  // filed nowhere

    const auto copies = repo.listByBinder("b1");
    ASSERT_EQ(copies.size(), 1u);
    EXPECT_EQ(copies[0].id, "c1");
}

// ownedElsewhere: species with an Owned copy NOT in the given binder — either in
// another binder or filed nowhere. Incoming/Removed copies never count, and an
// Owned copy in *this* binder is excluded.
TEST(CardCopyRepositoryTest, OwnedElsewhereExcludesThisBinderAndNonOwned) {
    Database db(":memory:");
    db.migrate();
    db.exec(
        "INSERT INTO card_binder(id,name,region,inserted_at,updated_at)"
        " VALUES('b1','A',NULL,'2026-07-14T09:00:00Z','2026-07-14T09:00:00Z'),"
        "       ('b2','B',NULL,'2026-07-14T09:00:00Z','2026-07-14T09:00:00Z');");
    CardCopyRepository repo(db);
    repo.add(makeCopy("c1", 1, CardOwnership::Owned, "b1"));          // owned HERE → excluded
    repo.add(makeCopy("c2", 4, CardOwnership::Owned, "b2"));          // owned elsewhere → in
    repo.add(makeCopy("c3", 7, CardOwnership::Owned, std::nullopt));  // owned nowhere → in
    repo.add(makeCopy("c4", 10, CardOwnership::Incoming, "b2"));      // not owned → out
    repo.add(makeCopy("c5", 12, CardOwnership::Removed, std::nullopt));  // not owned → out

    auto dexNums = repo.ownedElsewhere("b1");
    std::sort(dexNums.begin(), dexNums.end());
    EXPECT_EQ(dexNums, (std::vector<PokemonDexNum>{4, 7}));
}

}  // namespace
