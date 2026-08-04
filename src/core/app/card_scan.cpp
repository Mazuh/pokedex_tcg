#include "core/app/card_scan.h"

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/domain/pokemon.h"
#include "core/domain/pokemon_catalog.h"

namespace pokedex {

namespace {

using nlohmann::json;

// Split `s` into lowercased word tokens. Words break on WHITESPACE only; within a word,
// non-ASCII-alphanumeric bytes (punctuation like ' . : -, and non-ASCII UTF-8 bytes such as
// the ♀/♂ symbols) are dropped, NOT treated as breaks — so a reader that omits the symbol
// still matches. So "Ash's Pikachu" → ["ashs","pikachu"], "Mr. Mime" → ["mr","mime"],
// "Farfetch'd" and "Farfetchd" both → ["farfetchd"], "Nidoran♀" → ["nidoran"]. Splitting on
// whitespace (not on every non-alnum) is what keeps whole-word matching: "Parasol Lady"
// stays ["parasol","lady"], so the bare species "Paras" doesn't match. Locale-free.
std::vector<std::string> tokenize(const std::string& s) {
    std::vector<std::string> tokens;
    std::string cur;
    for (const char ch : s) {
        const unsigned char c = static_cast<unsigned char>(ch);
        const bool isSpace = c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' ||
                             c == '\v';
        if (isSpace) {
            if (!cur.empty()) {
                tokens.push_back(cur);
                cur.clear();
            }
            continue;
        }
        const bool isDigit = c >= '0' && c <= '9';
        const bool isUpper = c >= 'A' && c <= 'Z';
        const bool isLower = c >= 'a' && c <= 'z';
        if (isDigit || isLower) {
            cur.push_back(static_cast<char>(c));
        } else if (isUpper) {
            cur.push_back(static_cast<char>(c | 0x20));
        }
        // else: intra-word punctuation / symbol → dropped, without breaking the word.
    }
    if (!cur.empty()) {
        tokens.push_back(cur);
    }
    return tokens;
}

// True if `needle` appears as a contiguous run of tokens within `hay` (whole-word match).
bool containsTokenRun(const std::vector<std::string>& hay,
                      const std::vector<std::string>& needle) {
    if (needle.empty() || needle.size() > hay.size()) {
        return false;
    }
    for (std::size_t i = 0; i + needle.size() <= hay.size(); ++i) {
        bool all = true;
        for (std::size_t j = 0; j < needle.size(); ++j) {
            if (hay[i + j] != needle[j]) {
                all = false;
                break;
            }
        }
        if (all) {
            return true;
        }
    }
    return false;
}

// One catalog species reduced to its match key: its dex number, tokenized name, and total
// token length (the tie-break). Built once (the catalog is compile-time constant) so a
// per-keystroke detect only tokenizes the card name, not all ~1025 species.
struct SpeciesTokens {
    PokemonDexNum dex;
    std::vector<std::string> tokens;
    std::size_t chars;
};

const std::vector<SpeciesTokens>& speciesIndex() {
    static const std::vector<SpeciesTokens> index = [] {
        std::vector<SpeciesTokens> out;
        for (const Pokemon& species : pokemonCatalog()) {
            std::vector<std::string> tokens = tokenize(species.name);
            std::size_t chars = 0;
            for (const std::string& t : tokens) {
                chars += t.size();
            }
            out.push_back(SpeciesTokens{species.dexNumber, std::move(tokens), chars});
        }
        return out;
    }();
    return index;
}

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
        "  \"cardName\": the card's OFFICIAL ENGLISH name, ALWAYS translated to English even "
        "when the card is printed in another language — give the name the English-language "
        "print of this exact card uses, not a literal word-for-word translation (e.g. a card "
        "printed \"Pikachu do Ash\" or \"Pikachu di Ash\" is \"Ash's Pikachu\"; \"Dracaufeu\" "
        "or \"Glurak\" is \"Charizard\"; \"Dresséur d'élite\" / \"Boss\" is \"Boss's "
        "Orders\"). Use \"\" only when you truly cannot read the name.\n"
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

std::optional<PokemonDexNum> detectScannedSpecies(const std::string& cardName) {
    const std::vector<std::string> hay = tokenize(cardName);
    if (hay.empty()) {
        return std::nullopt;
    }
    std::optional<PokemonDexNum> best;
    std::size_t bestTokens = 0;
    std::size_t bestChars = 0;
    for (const SpeciesTokens& species : speciesIndex()) {
        // Prefer the most specific match: more tokens, then more characters. Only bother
        // running the (more expensive) token-run check when it could beat the current best.
        const bool couldBeBetter = species.tokens.size() > bestTokens ||
                                   (species.tokens.size() == bestTokens && species.chars > bestChars);
        if (couldBeBetter && containsTokenRun(hay, species.tokens)) {
            best = species.dex;
            bestTokens = species.tokens.size();
            bestChars = species.chars;
        }
    }
    return best;
}

}  // namespace pokedex
