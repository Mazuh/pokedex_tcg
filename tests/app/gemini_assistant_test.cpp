#include "core/app/gemini_assistant.h"

#include <gtest/gtest.h>

#include <string>

#include "core/app/ai_assistant.h"

namespace {

using pokedex::AiHttpHeader;
using pokedex::AiRequest;
using pokedex::AiResult;
using pokedex::GeminiAssistant;

// Find a header's value by (case-sensitive) name, or "" when absent.
std::string headerValue(const AiRequest& req, const std::string& name) {
    for (const AiHttpHeader& h : req.headers) {
        if (h.name == name) {
            return h.value;
        }
    }
    return "";
}

bool bodyContains(const AiRequest& req, const std::string& needle) {
    return req.body.find(needle) != std::string::npos;
}

// The request targets the model's generateContent endpoint, and the API key rides
// in the x-goog-api-key header — never in the URL, so it can't leak to the log.
TEST(GeminiAssistantTest, BuildsRequestForModelWithKeyInHeaderNotUrl) {
    GeminiAssistant assistant("gemini-2.5-flash");
    const AiRequest req = assistant.buildRequest({.userText = "Hi"}, "SECRET-KEY");

    EXPECT_EQ(req.url,
              "https://generativelanguage.googleapis.com/v1beta/models/"
              "gemini-2.5-flash:generateContent");
    EXPECT_EQ(headerValue(req, "x-goog-api-key"), "SECRET-KEY");
    EXPECT_EQ(headerValue(req, "Content-Type"), "application/json");
    // The secret must not appear in the URL at all.
    EXPECT_EQ(req.url.find("SECRET-KEY"), std::string::npos);
}

// The user prompt becomes a user-role text part; with no system instruction the
// body carries no systemInstruction node.
TEST(GeminiAssistantTest, EncodesUserPromptWithoutSystemInstruction) {
    GeminiAssistant assistant;
    const AiRequest req = assistant.buildRequest({.userText = "What set is this?"}, "k");

    EXPECT_TRUE(bodyContains(req, "\"contents\""));
    EXPECT_TRUE(bodyContains(req, "\"role\":\"user\""));
    EXPECT_TRUE(bodyContains(req, "What set is this?"));
    EXPECT_FALSE(bodyContains(req, "systemInstruction"));
}

// A non-empty system instruction is emitted as its own node.
TEST(GeminiAssistantTest, EncodesSystemInstructionWhenPresent) {
    GeminiAssistant assistant;
    const AiRequest req =
        assistant.buildRequest({.userText = "Hi", .systemInstruction = "Be brief."}, "k");

    EXPECT_TRUE(bodyContains(req, "systemInstruction"));
    EXPECT_TRUE(bodyContains(req, "Be brief."));
}

// A text-only prompt carries no inline_data and no generationConfig — the plain
// prompt path stays byte-for-byte the shape it always was.
TEST(GeminiAssistantTest, TextOnlyPromptHasNoImageOrJsonConfig) {
    GeminiAssistant assistant;
    const AiRequest req = assistant.buildRequest({.userText = "Hi"}, "k");

    EXPECT_FALSE(bodyContains(req, "inline_data"));
    EXPECT_FALSE(bodyContains(req, "generationConfig"));
}

// An image part becomes an inline_data part with its mime type and base64 payload,
// alongside the text part.
TEST(GeminiAssistantTest, EncodesInlineImagePart) {
    GeminiAssistant assistant;
    pokedex::AiPrompt prompt{.userText = "Read this card."};
    prompt.images.push_back({.mimeType = "image/jpeg", .base64Data = "QUJD"});
    const AiRequest req = assistant.buildRequest(prompt, "k");

    EXPECT_TRUE(bodyContains(req, "inline_data"));
    EXPECT_TRUE(bodyContains(req, "image/jpeg"));
    EXPECT_TRUE(bodyContains(req, "QUJD"));
    EXPECT_TRUE(bodyContains(req, "Read this card."));  // text part still present
}

// The JSON-response hint emits generationConfig.responseMimeType so the model
// returns raw JSON.
TEST(GeminiAssistantTest, EmitsJsonResponseHint) {
    GeminiAssistant assistant;
    const AiRequest req =
        assistant.buildRequest({.userText = "Hi", .wantsJsonResponse = true}, "k");

    EXPECT_TRUE(bodyContains(req, "generationConfig"));
    EXPECT_TRUE(bodyContains(req, "application/json"));
}

// A well-formed success payload yields the concatenated candidate text.
TEST(GeminiAssistantTest, ParsesCandidateText) {
    GeminiAssistant assistant;
    const AiResult result = assistant.parseResponse(
        R"({"candidates":[{"content":{"role":"model","parts":[)"
        R"({"text":"Base "},{"text":"Set."}]}}]})",
        /*httpOk=*/true);

    EXPECT_TRUE(result.ok);
    EXPECT_EQ(result.text, "Base Set.");
    EXPECT_TRUE(result.errorMessage.empty());
}

// A provider error body (non-2xx) surfaces the provider's message.
TEST(GeminiAssistantTest, ParsesErrorBody) {
    GeminiAssistant assistant;
    const AiResult result = assistant.parseResponse(
        R"({"error":{"code":400,"message":"API key not valid","status":"INVALID_ARGUMENT"}})",
        /*httpOk=*/false);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.text, "");
    EXPECT_EQ(result.errorMessage, "API key not valid");
}

// A blocked prompt (no candidates, a promptFeedback.blockReason) is a failure that
// names the reason.
TEST(GeminiAssistantTest, ReportsBlockedPrompt) {
    GeminiAssistant assistant;
    const AiResult result = assistant.parseResponse(
        R"({"promptFeedback":{"blockReason":"SAFETY"}})", /*httpOk=*/true);

    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.errorMessage.find("SAFETY"), std::string::npos);
}

// A body that isn't JSON at all degrades to a failure, never a throw.
TEST(GeminiAssistantTest, HandlesNonJsonBody) {
    GeminiAssistant assistant;
    const AiResult result = assistant.parseResponse("<html>502 Bad Gateway</html>",
                                                    /*httpOk=*/false);

    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.errorMessage.empty());
}

}  // namespace
