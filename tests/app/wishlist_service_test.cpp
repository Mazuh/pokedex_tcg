#include "core/app/wishlist_service.h"

#include <gtest/gtest.h>

#include <optional>
#include <set>
#include <string>
#include <vector>

#include "core/domain/wishlist.h"
#include "core/storage/codecs.h"
#include "core/storage/database.h"
#include "core/storage/wishlist_repository.h"

namespace {

using pokedex::Database;
using pokedex::PokemonDexNum;
using pokedex::Timestamp;
using pokedex::WishlistEntry;
using pokedex::WishlistError;
using pokedex::WishlistRepository;
using pokedex::WishlistService;

Timestamp at(const char* iso) { return pokedex::timestampFromIso(iso); }

// A fixture with an in-memory DB and a service pinned to a fixed clock, so audit
// stamps are deterministic.
struct WishlistServiceTest : public ::testing::Test {
    Database db{":memory:"};
    WishlistRepository repo{db};
    Timestamp now = at("2026-07-15T10:00:00Z");
    WishlistService service{repo, [this] { return now; }};

    WishlistServiceTest() { db.migrate(); }
};

TEST_F(WishlistServiceTest, AddSourceCreatesThenAppends) {
    service.addSource(25, "ebay");
    service.addSource(25, "https://tcgplayer.com/x");

    auto found = service.forPokemon(25);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->sources,
              (std::set<std::string>{"ebay", "https://tcgplayer.com/x"}));
    EXPECT_EQ(found->insertedAt, now);
    EXPECT_EQ(found->updatedAt, now);
}

TEST_F(WishlistServiceTest, AddSourceTrimsAndRejectsBlank) {
    service.addSource(25, "  local shop  ");
    auto found = service.forPokemon(25);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->sources, (std::set<std::string>{"local shop"}));

    EXPECT_THROW(service.addSource(25, "   "), WishlistError);
    EXPECT_THROW(service.addSource(25, ""), WishlistError);
}

TEST_F(WishlistServiceTest, EditSourceSwapsValue) {
    service.addSource(25, "ebay");
    service.addSource(25, "shop");

    service.editSource(25, "ebay", "https://cardmarket.com/x");

    auto found = service.forPokemon(25);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->sources,
              (std::set<std::string>{"https://cardmarket.com/x", "shop"}));

    // Editing a Pokémon with no wishlist is a harmless no-op.
    EXPECT_NO_THROW(service.editSource(6, "x", "y"));
    EXPECT_FALSE(service.forPokemon(6).has_value());
}

TEST_F(WishlistServiceTest, RemoveSourceDeletesEmptyParent) {
    service.addSource(25, "ebay");
    service.addSource(25, "shop");

    service.removeSource(25, "ebay");
    auto stillThere = service.forPokemon(25);
    ASSERT_TRUE(stillThere.has_value());
    EXPECT_EQ(stillThere->sources, (std::set<std::string>{"shop"}));

    service.removeSource(25, "shop");  // last one → parent gone
    EXPECT_FALSE(service.forPokemon(25).has_value());
}

TEST_F(WishlistServiceTest, ListAllPairsCatalogSpeciesInDexOrder) {
    service.addSource(25, "ebay");        // Pikachu
    service.addSource(6, "shop-a");       // Charizard
    service.addSource(6, "shop-b");

    std::vector<WishlistEntry> all = service.listAll();
    ASSERT_EQ(all.size(), 2u);
    EXPECT_EQ(all[0].pokemon.dexNumber, 6);
    EXPECT_EQ(all[0].pokemon.name, "Charizard");
    EXPECT_EQ(all[0].sources, (std::vector<std::string>{"shop-a", "shop-b"}));
    EXPECT_EQ(all[1].pokemon.dexNumber, 25);
    EXPECT_EQ(all[1].pokemon.name, "Pikachu");
}

}  // namespace
