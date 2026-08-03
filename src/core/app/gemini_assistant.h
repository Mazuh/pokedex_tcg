#pragma once

#include <string>

#include "core/app/ai_assistant.h"

namespace pokedex {

// The default model queried when none is given. This is Google's ROLLING alias that
// always resolves to the current GA Flash tier, rather than a pinned version — so the
// app tracks the newest Flash automatically and never breaks when an older, specific
// version is retired or gated off new accounts (which is what bit gemini-2.x). Fast
// and inexpensive, plenty for short text prompts. Trivially swappable: the composition
// root can override it from the `assistant_model` config key (e.g. to pin an exact
// version) without a rebuild.
//
// To check which models exist / are supported (and pick a value for the override),
// see Google's model list: https://ai.google.dev/gemini-api/docs/models — or query
// the account's own access via GET https://generativelanguage.googleapis.com/v1beta/models
// with the x-goog-api-key header.
inline constexpr char kGeminiDefaultModel[] = "gemini-flash-latest";

// APP — the default AiAssistant implementation, backed by Google's Gemini REST
// API (https://generativelanguage.googleapis.com/v1beta). This is the ONE place in
// the whole codebase that knows that provider's URL scheme, JSON request shape, and
// response layout — everything else talks to the vendor-neutral AiAssistant seam.
// Swapping to another provider is a matter of writing a sibling class and changing
// the single construction line in main.cpp; no caller learns the difference.
//
// Auth uses the `x-goog-api-key` request header (not the `?key=` query parameter),
// so the secret never appears in the URL — and therefore never in the network log.
// Pure and Qt-free: it only builds the request string and parses the reply.
class GeminiAssistant : public AiAssistant {
public:
    explicit GeminiAssistant(std::string model = kGeminiDefaultModel);

    AiRequest buildRequest(const AiPrompt& prompt,
                           const std::string& apiKey) const override;
    AiResult parseResponse(const std::string& responseBody, bool httpOk) const override;

private:
    std::string model_;
};

}  // namespace pokedex
