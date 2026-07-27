#include "core/app/card_catalog_parse.h"

#include <cctype>
#include <chrono>
#include <cmath>
#include <optional>
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

// The single-card node: `data` is an object on the /v2/cards/{id} endpoint, but be
// lenient and also accept the first element of a `data` array (a search response),
// so the same parser serves either shape. Returns nullptr when neither is present.
const json* cardNode(const json& root) {
    if (!root.is_object()) {
        return nullptr;
    }
    const auto it = root.find("data");
    if (it == root.end()) {
        return nullptr;
    }
    if (it->is_object()) {
        return &*it;
    }
    if (it->is_array() && !it->empty() && it->front().is_object()) {
        return &it->front();
    }
    return nullptr;
}

// A vendor block's printed update date ("YYYY/MM/DD") as a midnight-UTC Timestamp,
// or nullopt when the field is absent or malformed. Only the leading 10 chars are
// read, so a value that ever carries a trailing time component ("YYYY/MM/DD hh:mm")
// still yields the correct date rather than silently falling back. Kept clock-free
// (chrono calendar math only) so the parser stays pure and testable.
std::optional<Timestamp> vendorUpdatedAt(const json& block) {
    const std::string d = strField(block, "updatedAt");
    if (d.size() < 10 || d[4] != '/' || d[7] != '/') {
        return std::nullopt;
    }
    for (const int i : {0, 1, 2, 3, 5, 6, 8, 9}) {
        if (std::isdigit(static_cast<unsigned char>(d[i])) == 0) {
            return std::nullopt;
        }
    }
    const std::chrono::year_month_day ymd{
        std::chrono::year{std::stoi(d.substr(0, 4))},
        std::chrono::month{static_cast<unsigned>(std::stoi(d.substr(5, 2)))},
        std::chrono::day{static_cast<unsigned>(std::stoi(d.substr(8, 2)))}};
    if (!ymd.ok()) {
        return std::nullopt;
    }
    return std::chrono::sys_days{ymd};
}

// Money (a JSON number of currency units) as integer minor units. A non-number, a
// non-positive value, or a positive amount that rounds down to zero cents (a
// sub-half-cent metric) yields nullopt — a zero/absent price is noise, not a price.
std::optional<long long> priceCents(const json& value) {
    if (!value.is_number()) {
        return std::nullopt;
    }
    const long long cents = std::llround(value.get<double>() * 100.0);
    if (cents <= 0) {
        return std::nullopt;
    }
    return cents;
}

// Extract every price observation from one card object — its `tcgplayer` (USD,
// nested by variant) and `cardmarket` (EUR, flat) blocks — keyed by the card's own
// id. Shared by parseCardPrices (the per-card endpoint) and parseCardSearchResponse
// (the search endpoint embeds the same blocks, so a browse gets prices for free).
// Non-positive/zero-rounding metrics are skipped as noise; a vendor block without a
// printed date uses `fallbackObservedAt`. Rows carry an empty `id` (minted on persist).
std::vector<CardPrice> extractCardPrices(const json& card, Timestamp fallbackObservedAt) {
    std::vector<CardPrice> prices;
    const std::string externalCardId = strField(card, "id");

    // tcgplayer (USD): prices are nested one level by variant ("holofoil",
    // "normal"…), each variant an object of named metrics (low/mid/high/market…).
    if (const auto tcg = card.find(kTcgplayerProvenance); tcg != card.end() && tcg->is_object()) {
        const Timestamp observed = vendorUpdatedAt(*tcg).value_or(fallbackObservedAt);
        if (const auto pricesObj = tcg->find("prices");
            pricesObj != tcg->end() && pricesObj->is_object()) {
            for (const auto& [variant, metrics] : pricesObj->items()) {
                if (!metrics.is_object()) {
                    continue;
                }
                for (const auto& [metric, value] : metrics.items()) {
                    if (const auto cents = priceCents(value)) {
                        prices.push_back(CardPrice{.externalCardId = externalCardId,
                                                   .provenance = kTcgplayerProvenance,
                                                   .variant = variant,
                                                   .metric = metric,
                                                   .amountCents = *cents,
                                                   .currency = "USD",
                                                   .observedAt = observed});
                    }
                }
            }
        }
    }

    // cardmarket (EUR): a single flat prices object, no per-variant split.
    if (const auto cm = card.find(kCardmarketProvenance); cm != card.end() && cm->is_object()) {
        const Timestamp observed = vendorUpdatedAt(*cm).value_or(fallbackObservedAt);
        if (const auto pricesObj = cm->find("prices");
            pricesObj != cm->end() && pricesObj->is_object()) {
            for (const auto& [metric, value] : pricesObj->items()) {
                if (const auto cents = priceCents(value)) {
                    prices.push_back(CardPrice{.externalCardId = externalCardId,
                                               .provenance = kCardmarketProvenance,
                                               .variant = "",
                                               .metric = metric,
                                               .amountCents = *cents,
                                               .currency = "EUR",
                                               .observedAt = observed});
                }
            }
        }
    }

    return prices;
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
        // Carry the printed card name into the stored reference: for a species-free
        // card (Trainer/Energy) it is the only human-readable title the copy keeps.
        c.cardRef.name = c.name;

        // The search payload embeds the same price blocks as the per-card endpoint —
        // extract them (display-only, never persisted) so the finder can show a price
        // hint with no extra request. No "now" here (the parser is clock-free), so a
        // vendor block missing its date falls back to the epoch; in practice the date
        // is present and the finder shows amounts, not the fallback.
        c.prices = extractCardPrices(card, Timestamp{});

        candidates.push_back(std::move(c));
    }
    return candidates;
}

CardPricesParse parseCardPricesResult(const std::string& jsonText, Timestamp fallbackObservedAt) {
    const json root = parseJson(jsonText);
    const json* card = cardNode(root);
    if (card == nullptr) {
        return {};  // cardPresent = false, no prices — a degraded/error body
    }
    return {.cardPresent = true, .prices = extractCardPrices(*card, fallbackObservedAt)};
}

std::vector<CardPrice> parseCardPrices(const std::string& jsonText,
                                       Timestamp fallbackObservedAt) {
    return parseCardPricesResult(jsonText, fallbackObservedAt).prices;
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
