#include "core/app/card_catalog_parse.h"

#include <cctype>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "core/strings.h"

namespace pokedex {

namespace {

using nlohmann::json;

// Parse a payload into a JSON value, translating a syntax error into our own
// exception type (so callers never see the nlohmann type — it stays a private
// dependency of this translation unit).
json parseJson(const std::string& text) {
    try {
        return json::parse(text);
    } catch (const json::parse_error& e) {
        throw CardCatalogParseError(e.what());
    }
}

// Defensive field extractors: return a blank/zero when the key is absent, null,
// or the wrong JSON type, so a partial or evolving payload never throws.
std::string strField(const json& obj, const char* key) {
    if (!obj.is_object()) {
        return {};
    }
    const auto it = obj.find(key);
    if (it == obj.end() || !it->is_string()) {
        return {};
    }
    return it->get<std::string>();
}

int intField(const json& obj, const char* key) {
    if (!obj.is_object()) {
        return 0;
    }
    const auto it = obj.find(key);
    if (it == obj.end() || !it->is_number_integer()) {
        return 0;
    }
    return it->get<int>();
}

// The array of records lives under "data" in every pokemontcg.io response; a
// payload without it (e.g. an error body) yields no rows rather than throwing.
const json* dataArray(const json& root) {
    if (!root.is_object()) {
        return nullptr;
    }
    const auto it = root.find("data");
    if (it == root.end() || !it->is_array()) {
        return nullptr;
    }
    return &*it;
}

std::string toLowerAscii(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (const unsigned char c : s) {
        out += static_cast<char>(std::tolower(c));
    }
    return out;
}

}  // namespace

std::vector<CardSetInfo> parseSetsResponse(const std::string& jsonText) {
    const json root = parseJson(jsonText);
    std::vector<CardSetInfo> sets;
    const json* data = dataArray(root);
    if (data == nullptr) {
        return sets;
    }
    for (const json& s : *data) {
        CardSetInfo info;
        info.id = strField(s, "id");
        if (info.id.empty()) {
            continue;  // a set with no id is unusable as a lookup key
        }
        info.ptcgoCode = strField(s, "ptcgoCode");
        info.name = strField(s, "name");
        info.printedTotal = intField(s, "printedTotal");
        sets.push_back(std::move(info));
    }
    return sets;
}

std::vector<CardCandidate> parseCardSearchResponse(const std::string& jsonText,
                                                   const std::vector<CardSetInfo>& sets) {
    // Index the set table by id so each card resolves its printed code and total
    // from the authoritative /v2/sets data rather than the unreliable embedded set.
    std::unordered_map<std::string, const CardSetInfo*> byId;
    byId.reserve(sets.size());
    for (const CardSetInfo& s : sets) {
        byId.emplace(s.id, &s);
    }

    const json root = parseJson(jsonText);
    std::vector<CardCandidate> candidates;
    const json* data = dataArray(root);
    if (data == nullptr) {
        return candidates;
    }

    for (const json& card : *data) {
        if (!card.is_object()) {
            continue;
        }

        CardCandidate c;
        c.id = strField(card, "id");
        c.name = strField(card, "name");
        c.rarity = strField(card, "rarity");
        c.artist = strField(card, "artist");

        const auto imagesIt = card.find("images");
        if (imagesIt != card.end() && imagesIt->is_object()) {
            c.imageUrlSmall = strField(*imagesIt, "small");
            c.imageUrlLarge = strField(*imagesIt, "large");
        }

        // The embedded set gives us the id; the printed code / total / name come
        // from the parsed set table (falling back to the embedded set only when
        // the id is not in the table, e.g. a brand-new set).
        std::string ptcgoCode;
        int printedTotal = 0;
        const auto setIt = card.find("set");
        if (setIt != card.end() && setIt->is_object()) {
            c.setId = strField(*setIt, "id");
        }
        if (const auto found = byId.find(c.setId); found != byId.end()) {
            ptcgoCode = found->second->ptcgoCode;
            printedTotal = found->second->printedTotal;
            c.setName = found->second->name;
        } else if (setIt != card.end() && setIt->is_object()) {
            ptcgoCode = strField(*setIt, "ptcgoCode");
            printedTotal = intField(*setIt, "printedTotal");
            c.setName = strField(*setIt, "name");
        }

        const std::string number = strField(card, "number");
        c.cardRef.expansionCode = ptcgoCode;
        c.cardRef.language = "";  // English-only source; the user picks the language
        c.cardRef.collectorNumber =
            printedTotal > 0 ? number + "/" + std::to_string(printedTotal) : number;
        // Carry the set name into the stored reference too: for code-less sets it is
        // the only thing that distinguishes otherwise-identical collector numbers.
        c.cardRef.setName = c.setName;

        candidates.push_back(std::move(c));
    }
    return candidates;
}

std::vector<std::string> resolveSetFilterToIds(const std::string& typed,
                                               const std::vector<CardSetInfo>& sets) {
    std::vector<std::string> ids;
    const std::string want = toLowerAscii(trim(typed));
    if (want.empty()) {
        return ids;
    }
    // Substring name matching only kicks in at 3+ chars: a 1-2 char fragment ("e",
    // "ma") would match a large fraction of the ~150 sets, which is both useless as
    // a narrow and risks an over-long OR query. Exact-code matching has no minimum
    // (codes are short, e.g. "BS").
    const bool allowNameMatch = want.size() >= 3;
    for (const CardSetInfo& s : sets) {
        // Match an exact printed code (e.g. "OBF"), OR a substring of the set name
        // (e.g. "mcdonald" → every McDonald's Collection year) — the latter is the
        // only way to narrow to a code-less set.
        const bool codeMatch = !s.ptcgoCode.empty() && toLowerAscii(s.ptcgoCode) == want;
        const bool nameMatch =
            allowNameMatch && toLowerAscii(s.name).find(want) != std::string::npos;
        if (codeMatch || nameMatch) {
            ids.push_back(s.id);
        }
    }
    return ids;
}

}  // namespace pokedex
