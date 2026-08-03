#include "core/app/card_scan.h"

#include <string>
#include <utility>

#include <nlohmann/json.hpp>

namespace pokedex {

namespace {

using nlohmann::json;

// Read a string field, tolerating a missing key or a non-string value (→ "").
std::string strField(const json& obj, const char* key) {
    if (obj.is_object() && obj.contains(key) && obj[key].is_string()) {
        return obj[key].get<std::string>();
    }
    return "";
}

// Isolate the JSON object out of a reply that may carry ``` fences or stray prose
// around it — we set responseMimeType=application/json, but a model that ignores it
// (or a different provider) can still wrap the object. Returns "" when there's no
// brace pair to work with.
std::string isolateJsonObject(const std::string& text) {
    const std::string::size_type open = text.find('{');
    const std::string::size_type close = text.rfind('}');
    if (open == std::string::npos || close == std::string::npos || close < open) {
        return "";
    }
    return text.substr(open, close - open + 1);
}

// Trim ASCII whitespace from both ends.
std::string trimmed(const std::string& s) {
    const auto notSpace = [](unsigned char c) {
        return c != ' ' && c != '\t' && c != '\n' && c != '\r';
    };
    std::string::size_type begin = 0;
    while (begin < s.size() && !notSpace(static_cast<unsigned char>(s[begin]))) {
        ++begin;
    }
    std::string::size_type end = s.size();
    while (end > begin && !notSpace(static_cast<unsigned char>(s[end - 1]))) {
        --end;
    }
    return s.substr(begin, end - begin);
}

}  // namespace

std::string cardScanSystemInstruction() {
    // The model reads the printing and hands back a search string — it must NOT try to
    // identify a specific database entry or invent details it can't see. The query rule
    // is tuned for the app's flexible substring search (a distinctive slice of the
    // English set name, plus the collector number).
    return
        "You are a Pokemon Trading Card Game card reader. Look at the photo of a single "
        "physical card and read ONLY what is printed on it. Do not guess or invent values "
        "you cannot clearly see.\n"
        "Reply with a single JSON object and nothing else (no markdown, no code fences, no "
        "prose), using exactly these keys:\n"
        "  \"identified\": boolean — true only if you can read a Pokemon TCG card in the "
        "image.\n"
        "  \"cardName\": the printed card name in English (e.g. \"Bulbasaur\", \"Boss's "
        "Orders\"), or \"\".\n"
        "  \"setName\": the English set/expansion name if you can tell it (e.g. \"Base Set\", "
        "\"McDonald's Collection 2021\"), or \"\".\n"
        "  \"setCode\": the short set code/symbol printed on the card if clearly visible "
        "(e.g. \"MEW\", \"SVI\"), or \"\".\n"
        "  \"collectorNumber\": the collector number printed on the card, usually near a "
        "corner (e.g. \"4/102\", \"1/25\", \"025/165\"), or \"\".\n"
        "  \"query\": a short search string to find this card: a distinctive part of the "
        "English set name (or the set code if that is what is legible) followed by the "
        "collector number, e.g. \"Base Set 4/102\" or \"collection 2021 1/25\". Keep it "
        "lowercase and minimal.\n"
        "  \"note\": when identified is false, a very short reason (e.g. \"no card visible\", "
        "\"too blurry\"); otherwise \"\".\n"
        "If there is no readable Pokemon card, return identified=false with an empty query "
        "and a note.";
}

AiPrompt buildCardScanPrompt(std::string base64Jpeg, std::string mimeType) {
    AiPrompt prompt;
    prompt.systemInstruction = cardScanSystemInstruction();
    prompt.userText =
        "Read this card and return the JSON described. The collector number is the most "
        "important field to get right.";
    prompt.wantsJsonResponse = true;
    prompt.images.push_back(AiImagePart{std::move(mimeType), std::move(base64Jpeg)});
    return prompt;
}

ScannedCard parseScannedCard(const std::string& assistantText) {
    ScannedCard scanned;

    const std::string objectText = isolateJsonObject(assistantText);
    if (objectText.empty()) {
        scanned.note = "The assistant did not return a readable answer.";
        return scanned;
    }

    const json root = json::parse(objectText, nullptr, /*allow_exceptions=*/false);
    if (root.is_discarded() || !root.is_object()) {
        scanned.note = "The assistant's answer could not be read.";
        return scanned;
    }

    scanned.cardName = trimmed(strField(root, "cardName"));
    scanned.setName = trimmed(strField(root, "setName"));
    scanned.setCode = trimmed(strField(root, "setCode"));
    scanned.collectorNumber = trimmed(strField(root, "collectorNumber"));
    scanned.query = trimmed(strField(root, "query"));
    scanned.note = trimmed(strField(root, "note"));

    const bool claimsIdentified =
        root.contains("identified") && root["identified"].is_boolean() &&
        root["identified"].get<bool>();

    // When the model reports a card but forgot the query, synthesize one from the
    // components so the caller always has something searchable: the set name (or code)
    // plus the collector number.
    if (claimsIdentified && scanned.query.empty()) {
        const std::string setPart = !scanned.setName.empty() ? scanned.setName : scanned.setCode;
        if (!setPart.empty() && !scanned.collectorNumber.empty()) {
            scanned.query = setPart + " " + scanned.collectorNumber;
        } else if (!scanned.collectorNumber.empty()) {
            scanned.query = scanned.collectorNumber;
        } else {
            scanned.query = setPart;
        }
    }

    // Identified only if the model said so AND we actually have something to search on;
    // otherwise it's a miss (keep any note the model gave, else a generic one).
    scanned.identified = claimsIdentified && !scanned.query.empty();
    if (!scanned.identified && scanned.note.empty()) {
        scanned.note = "Couldn't read a card in the photo.";
    }
    return scanned;
}

}  // namespace pokedex
