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

// A "YYYY<sep>MM<sep>DD…" date string's leading date as a midnight-UTC Timestamp, or
// nullopt when it is too short, mis-separated, or non-numeric. Only the leading 10 chars
// are read, so a trailing time component ("YYYY-MM-DDThh:mm:ssZ", "YYYY/MM/DD hh:mm") still
// yields the correct date rather than falling back. `sep` is the field separator the source
// uses ('/' for pokemontcg.io, '-' for tcgdex's ISO-8601). Clock-free (chrono calendar math
// only) so the parser stays pure and testable.
std::optional<Timestamp> dateAtMidnight(const std::string& d, char sep) {
    if (d.size() < 10 || d[4] != sep || d[7] != sep) {
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

// A pokemontcg.io vendor block's printed update date ("YYYY/MM/DD"), or nullopt.
std::optional<Timestamp> vendorUpdatedAt(const json& block) {
    return dateAtMidnight(strField(block, "updatedAt"), '/');
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

// Extract every price observation from one pokemontcg.io card object — its `tcgplayer`
// (USD, nested by variant) and `cardmarket` (EUR, flat) blocks — keyed by the card's own
// id. Used by parseCardSearchResponse: the search endpoint embeds these blocks, so the
// finder shows a display-only price hint for free (the owned-copy price feed is tcgdex, a
// separate provider). Non-positive/zero-rounding metrics are skipped as noise; a vendor
// block without a printed date uses `fallbackObservedAt`. Rows carry an empty `id`.
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

// A tcgdex vendor block's printed update date ("updated": ISO-8601 "YYYY-MM-DDThh:mm:ssZ").
std::optional<Timestamp> tcgdexUpdatedAt(const json& block) {
    return dateAtMidnight(strField(block, "updated"), '-');
}

// tcgdex names its price metrics differently from pokemontcg.io; these map the ones we care
// about to the SAME canonical vocabulary extractCardPrices emits, so downstream (the cache,
// vendorBest, the headline) never learns there is a second source. A key not in the map
// returns nullopt — which also WHITELISTS: the vendor blocks carry non-price fields
// (`idProduct`, `productId`, `unit`, `updated`) and holo-split variants (`avg-holo`…) that a
// blind numeric scan would otherwise turn into bogus money rows.
std::optional<std::string> canonicalTcgplayerMetric(const std::string& key) {
    // tcgdex nests each finish's metrics as lowPrice/midPrice/highPrice/marketPrice/
    // directLowPrice → pokemontcg.io's low/mid/high/market/directLow.
    if (key == "lowPrice") return "low";
    if (key == "midPrice") return "mid";
    if (key == "highPrice") return "high";
    if (key == "marketPrice") return "market";
    if (key == "directLowPrice") return "directLow";
    return std::nullopt;
}
std::optional<std::string> canonicalCardmarketMetric(const std::string& key) {
    // tcgdex's flat cardmarket metrics → pokemontcg.io's names. avg1/avg7/avg30 already match.
    if (key == "avg") return "averageSellPrice";
    if (key == "low") return "lowPrice";
    if (key == "trend") return "trendPrice";
    if (key == "avg1" || key == "avg7" || key == "avg30") return key;
    return std::nullopt;
}

// tcgdex's coarse pricing FINISH, normalized so the display can pick the price of the finish a
// copy actually is (instead of always the highest, which shows a holo price for a non-holo
// card). cardmarket keys prices by the printing's `type` (normal/holo/reverse/…); tcgplayer
// nests them under finish keys (normal/holofoil/reverse-holofoil). Both collapse to one
// vocabulary — "normal"/"holo"/"reverse" — the same tokens a CardFoil maps to (GUI-side). An
// unrecognized finish (lenticular, 1stEdition…) is carried through verbatim: it just won't
// match a copy's foil, so the pick falls back to the highest.
std::string canonicalFinish(const std::string& raw) {
    if (raw == "normal") return "normal";
    if (raw == "holo" || raw == "holofoil") return "holo";
    if (raw == "reverse" || raw == "reverse-holofoil") return "reverse";
    return raw;
}

// Read whitelisted metrics out of one vendor pricing block into `out`. `canon` maps (and
// whitelists) the source metric name; `variant` is the normalized finish (normal/holo/reverse)
// this price belongs to, so a copy's foil can select the matching row.
void collectTcgdexMetrics(const json& block, const char* provenance, const std::string& variant,
                          const std::string& currency, Timestamp observed,
                          std::optional<std::string> (*canon)(const std::string&),
                          const std::string& externalCardId, std::vector<CardPrice>& out) {
    for (const auto& [key, value] : block.items()) {
        const auto metric = canon(key);
        if (!metric) {
            continue;  // a non-price field (idProduct/unit/updated) or a metric we don't map
        }
        if (const auto cents = priceCents(value)) {
            out.push_back(CardPrice{.externalCardId = externalCardId,
                                    .provenance = provenance,
                                    .variant = variant,
                                    .metric = *metric,
                                    .amountCents = *cents,
                                    .currency = currency,
                                    .observedAt = observed});
        }
    }
}

// Extract every price observation from one tcgdex card object. Prices live per printing under
// `variants_detailed[].pricing`; standard-size printings are preferred (a jumbo/lenticular is
// a different product), falling back to all printings when a card has only oversized ones.
std::vector<CardPrice> extractTcgdexPrices(const json& card, Timestamp fallbackObservedAt) {
    std::vector<CardPrice> prices;
    const std::string externalCardId = strField(card, "id");

    const auto variants = card.find("variants_detailed");
    if (variants == card.end() || !variants->is_array()) {
        return prices;
    }

    // Prefer standard-size printings; only if none carry pricing do oversized (jumbo/lenticular
    // — a different product) ones stand in. Gate the fallback on whether a standard printing
    // CARRIED a pricing block, not on whether the standard pass produced positive rows: a
    // standard printing whose metrics are all noise (<=0 cents, dropped by priceCents) still
    // means this card's standard price is simply unlisted — better to show nothing than a jumbo's
    // price in its place.
    for (const bool standardOnly : {true, false}) {
        bool sawPricingThisPass = false;
        for (const json& variant : *variants) {
            if (!variant.is_object()) {
                continue;
            }
            const std::string size = strField(variant, "size");
            const bool isStandard = size.empty() || size == "standard";
            if (standardOnly != isStandard) {
                continue;
            }
            const auto pricing = variant.find("pricing");
            if (pricing == variant.end() || !pricing->is_object()) {
                continue;
            }
            sawPricingThisPass = true;
            // The printing's finish (normal/holo/reverse), tagged onto its cardmarket rows so a
            // copy's foil can select the matching price. tcgplayer carries its own finish keys.
            const std::string printingFinish = canonicalFinish(strField(variant, "type"));

            // cardmarket (EUR, flat): one set of metrics per printing, keyed by the printing's
            // finish so normal (€1.98) and holo (€9.04) prices stay distinguishable.
            if (const auto cm = pricing->find(kCardmarketProvenance);
                cm != pricing->end() && cm->is_object()) {
                const std::string currency = strField(*cm, "unit");
                collectTcgdexMetrics(*cm, kCardmarketProvenance, printingFinish,
                                     currency.empty() ? "EUR" : currency,
                                     tcgdexUpdatedAt(*cm).value_or(fallbackObservedAt),
                                     canonicalCardmarketMetric, externalCardId, prices);
            }

            // tcgplayer (USD): metrics are nested one level by finish ("holofoil"/"normal"/
            // "reverse-holofoil"), each an object of named metrics — the finish is normalized.
            if (const auto tcg = pricing->find(kTcgplayerProvenance);
                tcg != pricing->end() && tcg->is_object()) {
                const std::string currency = strField(*tcg, "unit");
                const Timestamp observed = tcgdexUpdatedAt(*tcg).value_or(fallbackObservedAt);
                for (const auto& [finish, metrics] : tcg->items()) {
                    if (!metrics.is_object()) {
                        continue;  // skips the sibling "unit"/"updated" scalars
                    }
                    collectTcgdexMetrics(metrics, kTcgplayerProvenance, canonicalFinish(finish),
                                         currency.empty() ? "USD" : currency, observed,
                                         canonicalTcgplayerMetric, externalCardId, prices);
                }
            }
        }
        if (sawPricingThisPass) {
            break;  // standard printings carry pricing — never substitute oversized rows,
                    // even if every standard metric was noise and dropped
        }
    }
    return prices;
}

std::string trimLowerAscii(const std::string& s) { return toLowerAscii(trim(s)); }

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

CardPricesParse parseTcgdexCardPricesResult(const std::string& jsonText,
                                            Timestamp fallbackObservedAt) {
    const json root = parseJson(jsonText);
    // tcgdex returns the card object at the ROOT (no "data" wrapper). A miss is a JSON error
    // object ({"status":404,…}) with no "id": treat that as no card present (degraded) so the
    // caller preserves cached prices instead of caching a false "no prices" (see CardPricesParse).
    if (!root.is_object() || strField(root, "id").empty()) {
        return {};  // cardPresent = false
    }
    return {.cardPresent = true, .prices = extractTcgdexPrices(root, fallbackObservedAt)};
}

std::vector<CardPrice> parseTcgdexCardPrices(const std::string& jsonText,
                                             Timestamp fallbackObservedAt) {
    return parseTcgdexCardPricesResult(jsonText, fallbackObservedAt).prices;
}

std::vector<CardSetInfo> parseTcgdexSets(const std::string& jsonText) {
    const json root = parseJson(jsonText);
    std::vector<CardSetInfo> sets;
    if (!root.is_array()) {
        return sets;  // tcgdex /v2/en/sets is a flat array, not "data"-wrapped
    }
    for (const json& s : root) {
        CardSetInfo info;
        info.id = strField(s, "id");
        if (info.id.empty()) {
            continue;  // a set with no id is unusable as a lookup key
        }
        info.name = strField(s, "name");
        // tcgdex publishes no printed code; carry the total from the nested cardCount.
        const auto count = s.find("cardCount");
        if (count != s.end() && count->is_object()) {
            info.printedTotal = intField(*count, "total");
        }
        sets.push_back(std::move(info));
    }
    return sets;
}

std::optional<std::string> resolveTcgdexCardId(const CardReference& ref,
                                               const std::vector<CardSetInfo>& tcgdexSets) {
    // The localId is the leading printing part of the collector number ("125/197" → "125").
    // Without it there is no card to address.
    std::string localId = trim(ref.collectorNumber);
    if (const auto slash = localId.find('/'); slash != std::string::npos) {
        localId = trim(localId.substr(0, slash));
    }
    if (localId.empty()) {
        return std::nullopt;
    }

    const std::string code = trimLowerAscii(ref.expansionCode);
    const std::string name = trimLowerAscii(ref.setName);

    // Only EXACT set matches are trusted — a wrong-but-confident link would show a different
    // card's market prices as this copy's value, so an unidentifiable set returns nullopt
    // (the caller reports "couldn't identify", never guesses) rather than a fuzzy substring
    // guess with no way to verify the card actually exists in that set.
    //
    // 1) The set name equal to a tcgdex set NAME (the reliable main-set path: the tcgdex id
    //    "sv03" is nothing like the printed "OBF", but the names match). Tried FIRST so a
    //    genuine name match always wins over the weaker code-as-id heuristic below.
    // 2) The printed code (or set name) equal to a tcgdex set NAME.
    for (const CardSetInfo& s : tcgdexSets) {
        const std::string setName = toLowerAscii(s.name);
        if ((!name.empty() && setName == name) || (!code.empty() && setName == code)) {
            return s.id + "-" + localId;
        }
    }
    // 3) The printed code AS a tcgdex set id (promos whose code IS the id, e.g. "MEP"→"mep")
    //    — the fallback when there is no name match, so a code that coincidentally equals an
    //    unrelated set's id can only mislead a copy that had no better (name) signal.
    for (const CardSetInfo& s : tcgdexSets) {
        if (!code.empty() && toLowerAscii(s.id) == code) {
            return s.id + "-" + localId;
        }
    }

    return std::nullopt;
}

SetNumberFilter parseSetAndNumberFilter(const std::string& typed) {
    const std::string s = trim(typed);
    if (s.empty()) {
        return {};
    }
    const auto lastSpace = s.find_last_of(" \t");
    const std::string lastTok = (lastSpace == std::string::npos) ? s : s.substr(lastSpace + 1);
    const std::string rest =
        (lastSpace == std::string::npos) ? std::string() : trim(s.substr(0, lastSpace));

    // A plausible collector-number token: the part before any '/' is alphanumeric with 1-3
    // digits (a 4+-digit run is a year/large id — a set NAME part, not a collector number).
    const std::string head = lastTok.substr(0, lastTok.find('/'));
    int digits = 0;
    bool alnumOnly = !head.empty();
    for (const unsigned char c : head) {
        if (std::isdigit(c) != 0) {
            ++digits;
        } else if (std::isalpha(c) == 0) {
            alnumOnly = false;
        }
    }
    const bool looksLikeNumber = alnumOnly && digits >= 1 && digits <= 3;
    const bool hasSlash = lastTok.find('/') != std::string::npos;

    // Split the number out only when it's a slash-formed number (unambiguous even alone) or a
    // number with a set part before it — never a lone bare token (keeps digit-named sets whole).
    if (looksLikeNumber && (hasSlash || !rest.empty())) {
        return {.setFilter = rest, .number = head};
    }
    return {.setFilter = s, .number = std::string()};
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
