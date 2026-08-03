#include "core/app/gemini_assistant.h"

#include <string>
#include <utility>

#include <nlohmann/json.hpp>

namespace pokedex {

namespace {

using nlohmann::json;

// The Gemini generateContent endpoint for a given model. The API key is NOT put
// here (it rides in the x-goog-api-key header) so it never reaches the URL log.
std::string endpointFor(const std::string& model) {
    return "https://generativelanguage.googleapis.com/v1beta/models/" + model +
           ":generateContent";
}

// Pull a human-readable message out of a Gemini error body — `{"error":{"code":…,
// "message":"…","status":"…"}}` — falling back to a generic line when the shape
// isn't what we expect. Never throws.
std::string errorMessageFrom(const json& root, const std::string& fallback) {
    if (root.is_object() && root.contains("error")) {
        const json& err = root["error"];
        if (err.is_object() && err.contains("message") && err["message"].is_string()) {
            return err["message"].get<std::string>();
        }
    }
    return fallback;
}

}  // namespace

GeminiAssistant::GeminiAssistant(std::string model) : model_(std::move(model)) {}

AiRequest GeminiAssistant::buildRequest(const AiPrompt& prompt,
                                        const std::string& apiKey) const {
    // Body shape:
    //   { "contents": [ { "role": "user", "parts": [ { "text": "…" },
    //                     { "inline_data": { "mime_type": "…", "data": "<b64>" } } ] } ],
    //     "systemInstruction": { "parts": [ { "text": "…" } ] },        // optional
    //     "generationConfig": { "responseMimeType": "application/json" } }  // optional
    //
    // The text part comes first, then any image parts — Gemini accepts either order,
    // and text-first keeps the plain-prompt case byte-identical to before.
    json parts = json::array({json{{"text", prompt.userText}}});
    for (const AiImagePart& image : prompt.images) {
        parts.push_back(json{{"inline_data",
                              json{{"mime_type", image.mimeType}, {"data", image.base64Data}}}});
    }

    json body;
    body["contents"] = json::array({
        json{{"role", "user"}, {"parts", std::move(parts)}},
    });
    if (!prompt.systemInstruction.empty()) {
        body["systemInstruction"] =
            json{{"parts", json::array({json{{"text", prompt.systemInstruction}}})}};
    }
    if (prompt.wantsJsonResponse) {
        // Make the model return raw JSON (no ``` fences / prose), so the card-scan
        // parser gets a clean body. Ignored by models that don't support it.
        body["generationConfig"] = json{{"responseMimeType", "application/json"}};
    }

    AiRequest request;
    request.url = endpointFor(model_);
    request.body = body.dump();
    request.headers = {
        {"Content-Type", "application/json"},
        {"x-goog-api-key", apiKey},
    };
    return request;
}

AiResult GeminiAssistant::parseResponse(const std::string& responseBody, bool httpOk) const {
    AiResult result;

    json root = json::parse(responseBody, nullptr, /*allow_exceptions=*/false);
    if (root.is_discarded()) {
        // A body we can't parse at all — most likely a proxy/HTML error page or an
        // empty reply. Report the transport's own verdict.
        result.errorMessage = httpOk
                                  ? "The assistant returned a response that could not be read."
                                  : "The assistant request failed (unreadable error response).";
        return result;
    }

    // A non-2xx (or even a 200 carrying an error node) means the call failed; the
    // provider explains why in the error object.
    if (!httpOk || (root.is_object() && root.contains("error"))) {
        result.errorMessage = errorMessageFrom(root, "The assistant request failed.");
        return result;
    }

    // Success shape: candidates[0].content.parts[*].text (parts concatenated).
    if (root.is_object() && root.contains("candidates") && root["candidates"].is_array() &&
        !root["candidates"].empty()) {
        const json& candidate = root["candidates"][0];
        if (candidate.is_object() && candidate.contains("content") &&
            candidate["content"].is_object() && candidate["content"].contains("parts") &&
            candidate["content"]["parts"].is_array()) {
            std::string text;
            for (const json& part : candidate["content"]["parts"]) {
                if (part.is_object() && part.contains("text") && part["text"].is_string()) {
                    text += part["text"].get<std::string>();
                }
            }
            if (!text.empty()) {
                result.ok = true;
                result.text = std::move(text);
                return result;
            }
        }
    }

    // No usable candidate: the prompt may have been blocked by a safety filter, in
    // which case Gemini reports promptFeedback.blockReason instead of candidates.
    if (root.is_object() && root.contains("promptFeedback") &&
        root["promptFeedback"].is_object() &&
        root["promptFeedback"].contains("blockReason") &&
        root["promptFeedback"]["blockReason"].is_string()) {
        result.errorMessage = "The assistant declined to answer (blocked: " +
                              root["promptFeedback"]["blockReason"].get<std::string>() + ").";
        return result;
    }

    result.errorMessage = "The assistant returned no answer.";
    return result;
}

}  // namespace pokedex
