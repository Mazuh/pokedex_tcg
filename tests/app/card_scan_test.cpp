#include "core/app/card_scan.h"

#include <gtest/gtest.h>

#include <string>

#include "core/app/ai_assistant.h"

namespace {

using pokedex::AiPrompt;
using pokedex::buildCardScanPrompt;
using pokedex::parseScannedCard;
using pokedex::ScannedCard;

// The scan prompt carries the image as an inline part and asks for JSON back.
TEST(CardScanTest, BuildsVisionPromptWithImageAndJsonHint) {
    const AiPrompt prompt = buildCardScanPrompt("QUJD", "image/png");

    ASSERT_EQ(prompt.images.size(), 1u);
    EXPECT_EQ(prompt.images[0].mimeType, "image/png");
    EXPECT_EQ(prompt.images[0].base64Data, "QUJD");
    EXPECT_TRUE(prompt.wantsJsonResponse);
    EXPECT_FALSE(prompt.systemInstruction.empty());
}

// A well-formed reply is projected field-for-field.
TEST(CardScanTest, ParsesIdentifiedCard) {
    const ScannedCard s = parseScannedCard(
        R"({"identified":true,"cardName":"Bulbasaur","setName":"McDonald's Collection 2021",)"
        R"("setCode":"","collectorNumber":"1/25","query":"collection 2021 1/25","note":""})");

    EXPECT_TRUE(s.identified);
    EXPECT_EQ(s.cardName, "Bulbasaur");
    EXPECT_EQ(s.setName, "McDonald's Collection 2021");
    EXPECT_EQ(s.collectorNumber, "1/25");
    EXPECT_EQ(s.query, "collection 2021 1/25");
}

// A reply wrapped in ``` fences / prose is still parsed (the JSON object is isolated).
TEST(CardScanTest, ParsesFencedJson) {
    const ScannedCard s = parseScannedCard(
        "Here you go:\n```json\n"
        R"({"identified":true,"cardName":"Charizard","collectorNumber":"4/102",)"
        R"("query":"base set 4/102"})"
        "\n```");

    EXPECT_TRUE(s.identified);
    EXPECT_EQ(s.cardName, "Charizard");
    EXPECT_EQ(s.query, "base set 4/102");
}

// When identified but the model omits the query, one is synthesized from set + number.
TEST(CardScanTest, SynthesizesQueryFromComponents) {
    const ScannedCard s = parseScannedCard(
        R"({"identified":true,"setName":"Base Set","collectorNumber":"4/102"})");

    EXPECT_TRUE(s.identified);
    EXPECT_EQ(s.query, "Base Set 4/102");
}

// identified=false yields a miss carrying the model's note.
TEST(CardScanTest, ReportsNotIdentified) {
    const ScannedCard s = parseScannedCard(
        R"({"identified":false,"query":"","note":"too blurry"})");

    EXPECT_FALSE(s.identified);
    EXPECT_TRUE(s.query.empty());
    EXPECT_EQ(s.note, "too blurry");
}

// identified=true but nothing searchable degrades to a miss (never search on nothing).
TEST(CardScanTest, IdentifiedWithoutSearchableDataIsMiss) {
    const ScannedCard s = parseScannedCard(R"({"identified":true,"cardName":"Pikachu"})");

    EXPECT_FALSE(s.identified);
    EXPECT_FALSE(s.note.empty());
}

// Garbage / non-JSON never throws; it degrades to a miss with a note.
TEST(CardScanTest, NonJsonIsMissNotThrow) {
    const ScannedCard s = parseScannedCard("<html>502</html>");

    EXPECT_FALSE(s.identified);
    EXPECT_FALSE(s.note.empty());
}

}  // namespace
