#include "core/app/pokemon_browse_service.h"

#include <gtest/gtest.h>

#include <optional>
#include <string>

#include "core/domain/card_condition.h"
#include "core/domain/card_copy.h"
#include "core/domain/card_ownership.h"
#include "core/domain/card_reference.h"
#include "core/domain/pokemon_catalog.h"
#include "core/storage/card_copy_repository.h"
#include "core/storage/codecs.h"
#include "core/storage/database.h"

namespace {

using pokedex::CardCondition;
using pokedex::CardCopy;
using pokedex::CardCopyRepository;
using pokedex::CardOwnership;
using pokedex::CardReference;
using pokedex::Database;
using pokedex::PokemonBrowseEntry;
using pokedex::PokemonBrowseService;
using pokedex::PokemonDexNum;
using pokedex::Timestamp;

constexpr int kBulbasaur = 1;
constexpr int kPikachu = 25;

Timestamp at(const char* iso) { return pokedex::timestampFromIso(iso); }

CardCopy makeCopy(std::string id, PokemonDexNum dex, CardOwnership ownership,
                  std::optional<std::string> binderId) {
    CardCopy copy;
    copy.id = std::move(id);
    copy.pokemonDexNum = dex;
    copy.cardRef = CardReference{"MEW", "EN", "151/165"};
    copy.ownership = ownership;
    copy.condition = CardCondition::NearMint;
    copy.binderId = std::move(binderId);
    copy.insertedAt = at("2026-07-14T09:00:00Z");
    copy.updatedAt = at("2026-07-14T09:00:00Z");
    return copy;
}

// The owned count of a given dex number in the entries, or nullopt if absent.
std::optional<int> ownedOf(const std::vector<PokemonBrowseEntry>& entries,
                           PokemonDexNum dex) {
    for (const PokemonBrowseEntry& e : entries) {
        if (e.pokemon.dexNumber == dex) {
            return e.ownedCount;
        }
    }
    return std::nullopt;
}

struct BrowseTest : ::testing::Test {
    Database db{":memory:"};
    CardCopyRepository copies{db};
    PokemonBrowseService browse{copies};

    BrowseTest() { db.migrate(); }
};

// With no copies, the list still spans the whole catalog in dex order, every
// count zero.
TEST_F(BrowseTest, EmptyCollectionListsWholeCatalogAllZero) {
    const auto entries = browse.listAll();

    ASSERT_EQ(entries.size(), pokedex::pokemonCatalog().size());
    EXPECT_EQ(entries.front().pokemon.dexNumber, kBulbasaur);
    for (std::size_t i = 0; i < entries.size(); ++i) {
        EXPECT_EQ(entries[i].pokemon.dexNumber, static_cast<int>(i) + 1);
        EXPECT_EQ(entries[i].ownedCount, 0);
    }
}

// Owned copies are counted per species across binders and unfiled; Incoming and
// Removed copies never count.
TEST_F(BrowseTest, CountsOwnedCopiesAcrossBindersAndExcludesNonOwned) {
    db.exec(
        "INSERT INTO card_binder(id,name,region,inserted_at,updated_at)"
        " VALUES('b1','A',NULL,'2026-07-14T09:00:00Z','2026-07-14T09:00:00Z'),"
        "       ('b2','B',NULL,'2026-07-14T09:00:00Z','2026-07-14T09:00:00Z');");
    copies.add(makeCopy("c1", kPikachu, CardOwnership::Owned, "b1"));
    copies.add(makeCopy("c2", kPikachu, CardOwnership::Owned, "b2"));
    copies.add(makeCopy("c3", kPikachu, CardOwnership::Owned, std::nullopt));
    copies.add(makeCopy("c4", kPikachu, CardOwnership::Incoming, "b1"));  // not owned
    copies.add(makeCopy("c5", kBulbasaur, CardOwnership::Removed, "b1"));  // not owned

    const auto entries = browse.listAll();

    EXPECT_EQ(ownedOf(entries, kPikachu), 3);      // three Owned copies
    EXPECT_EQ(ownedOf(entries, kBulbasaur), 0);    // only a Removed copy
}

}  // namespace
