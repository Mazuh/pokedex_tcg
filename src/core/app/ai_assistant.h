#pragma once

#include <string>
#include <vector>

namespace pokedex {

// APP — the neutral input to the assistant: a user prompt, plus an optional
// system instruction that steers the assistant's behavior (persona, output
// format). Vendor-agnostic on purpose — no field names a provider's wire format.
//
// MULTIMODAL / VISION (assessment only — deliberately NOT implemented): a text
// LLM's HTTP API can also accept image input alongside the text (Gemini's
// generateContent takes base64 `inline_data` parts; the OpenAI/Anthropic message
// APIs take image blocks the same way), which is exactly what a future "scan a
// card photo from the webcam → autofill its name + set" feature would send. This
// struct is intentionally left open to grow an image-parts field for that, but no
// capture/encoding/vision code exists anywhere in the app today. See
// docs/ai-assistant.md for the feasibility notes.
struct AiPrompt {
    std::string userText;
    std::string systemInstruction;  // optional; empty = none
};

// One HTTP header the transport must set on the outbound request — e.g. the
// content type, or the provider's API-key header. Auth is carried here (a header)
// rather than in the URL precisely so the secret key never lands in the network
// log, which records only the URL.
struct AiHttpHeader {
    std::string name;
    std::string value;
};

// A resolved HTTP POST the transport issues verbatim: where to POST, the request
// body, and the headers to set. Parallels the card catalog's HttpRequest, but the
// assistant POSTs a JSON body rather than GETting, so it also carries the body and
// the headers. (The method is always POST for today's providers, so it stays
// implicit rather than a field.)
struct AiRequest {
    std::string url;
    std::string body;                   // the JSON request payload
    std::vector<AiHttpHeader> headers;  // content-type + auth
};

// The neutral result of a completion. On success `ok` is true and `text` holds
// the assistant's answer; on failure `ok` is false and `errorMessage` explains
// why (a provider error body, a blocked/refused prompt, or an unparseable
// response). The transport folds its own HTTP/network failures into this same
// shape, so every caller sees exactly one outcome contract regardless of provider.
struct AiResult {
    bool ok = false;
    std::string text;
    std::string errorMessage;
};

// APP — the swappable seam for a text LLM (an "AI assistant"), deliberately
// vendor-neutral: no client code ever names the concrete provider (Gemini today).
// Like the CardCatalogApi seam it is Qt-free and pure — it only *builds* the HTTP
// request and *parses* the response body; the actual POST happens GUI-side (in
// AssistantService), so core stays Qt- and network-free and headlessly testable
// against saved fixtures with no HTTP.
//
// BOTH halves live behind this one interface — unlike the card catalog, where the
// parser is a free function — precisely because both the request shape and the
// response shape are provider-specific. To swap providers you replace this one
// implementation at the composition root (main.cpp) and no caller changes: the
// config key, the transport, the Settings field, and the demo UI all stay put.
class AiAssistant {
public:
    virtual ~AiAssistant() = default;

    // Build the HTTP POST that asks the model to answer `prompt`, authenticated
    // with `apiKey` (the key the user stored in config). Pure — no I/O, no clock.
    virtual AiRequest buildRequest(const AiPrompt& prompt,
                                   const std::string& apiKey) const = 0;

    // Parse the provider's HTTP response body into a neutral AiResult. `httpOk` is
    // whether the transport saw a 2xx status: a non-2xx body is usually a JSON
    // error object the provider returns, which this maps to AiResult::errorMessage.
    // Never throws — an unparseable body degrades to ok=false with a message.
    virtual AiResult parseResponse(const std::string& responseBody,
                                   bool httpOk) const = 0;
};

}  // namespace pokedex
