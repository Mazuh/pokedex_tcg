#include "core/app/pokemon_external_api.h"

#include <gtest/gtest.h>

#include <string>

#include "core/app/media_cache_layout.h"
#include "core/app/poke_api.h"

namespace {

using pokedex::MediaKind;
using pokedex::MediaRequest;
using pokedex::PokeApi;

// Convenience: the resolved resourceName for a (dex, display name).
std::string resourceName(int dex, const char* name) {
    PokeApi api;
    return api.resolveMedia({dex, name}, MediaKind::OfficialArtwork).resourceName;
}

// The official-artwork URL is the static GitHub-raw PNG addressed by dex number.
TEST(PokeApiTest, ResolvesOfficialArtworkUrlByDexNumber) {
    PokeApi api;
    const MediaRequest request = api.resolveMedia({25, "Pikachu"}, MediaKind::OfficialArtwork);
    EXPECT_EQ(request.url,
              "https://raw.githubusercontent.com/PokeAPI/sprites/master/"
              "sprites/pokemon/other/official-artwork/25.png");
    EXPECT_EQ(request.resourceName, "pikachu");
}

// The Nidoran gender forms are the only names the transform cannot recover (the
// ♀/♂ sign must become a letter, else both collapse to "nidoran"); the override
// table pins them.
TEST(PokeApiTest, ResourceNameOverridesNidoranGenderForms) {
    EXPECT_EQ(resourceName(29, "Nidoran♀"), "nidoran-f");
    EXPECT_EQ(resourceName(32, "Nidoran♂"), "nidoran-m");
}

// Everything else falls out of the default transform: dropped punctuation,
// folded diacritics, preserved hyphens/digits.
TEST(PokeApiTest, ResourceNameTransformHandlesPunctuationAndDiacritics) {
    EXPECT_EQ(resourceName(83, "Farfetch'd"), "farfetchd");
    EXPECT_EQ(resourceName(865, "Sirfetch'd"), "sirfetchd");
    EXPECT_EQ(resourceName(122, "Mr. Mime"), "mr-mime");
    EXPECT_EQ(resourceName(866, "Mr. Rime"), "mr-rime");
    EXPECT_EQ(resourceName(439, "Mime Jr."), "mime-jr");
    EXPECT_EQ(resourceName(772, "Type: Null"), "type-null");
    EXPECT_EQ(resourceName(669, "Flabébé"), "flabebe");
    EXPECT_EQ(resourceName(250, "Ho-Oh"), "ho-oh");
    EXPECT_EQ(resourceName(474, "Porygon-Z"), "porygon-z");
    EXPECT_EQ(resourceName(233, "Porygon2"), "porygon2");
    EXPECT_EQ(resourceName(785, "Tapu Koko"), "tapu-koko");
    EXPECT_EQ(resourceName(1, "Bulbasaur"), "bulbasaur");
}

// The cache path is human-readable and organized by subject + kind.
TEST(MediaCacheLayoutTest, ComposesHumanReadablePath) {
    EXPECT_EQ(pokedex::mediaCacheRelPath("mime-jr", MediaKind::OfficialArtwork),
              "pokemon/mime-jr/official-artwork.png");
    EXPECT_EQ(pokedex::mediaCacheRelPath("pikachu", MediaKind::OfficialArtwork),
              "pokemon/pikachu/official-artwork.png");
}

}  // namespace
