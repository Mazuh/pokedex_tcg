#include "core/app/card_catalog_parse.h"

#include <cctype>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

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

        candidates.push_back(std::move(c));
    }
    return candidates;
}

std::vector<std::string> resolveSetCodeToIds(const std::string& typedCode,
                                             const std::vector<CardSetInfo>& sets) {
    std::vector<std::string> ids;
    // Trim surrounding whitespace, then compare case-insensitively.
    std::size_t begin = 0;
    std::size_t end = typedCode.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(typedCode[begin]))) {
        ++begin;
    }
    while (end > begin && std::isspace(static_cast<unsigned char>(typedCode[end - 1]))) {
        --end;
    }
    const std::string want = toLowerAscii(typedCode.substr(begin, end - begin));
    if (want.empty()) {
        return ids;
    }
    for (const CardSetInfo& s : sets) {
        if (!s.ptcgoCode.empty() && toLowerAscii(s.ptcgoCode) == want) {
            ids.push_back(s.id);
        }
    }
    return ids;
}

}  // namespace pokedex
