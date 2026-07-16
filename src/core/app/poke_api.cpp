#include "core/app/poke_api.h"

#include <cstddef>
#include <string>

namespace pokedex {

namespace {

// Fold a Latin-1 accented letter (Unicode U+00C0..U+00FF) to its base ASCII
// letter, or 0 if it has no fold. Covers the only accented name in the National
// Pokédex — Flabébé — plus the rest of the block for good measure.
char foldLatin1(unsigned int cp) {
    switch (cp) {
        case 0xC0: case 0xC1: case 0xC2: case 0xC3: case 0xC4: case 0xC5:
        case 0xE0: case 0xE1: case 0xE2: case 0xE3: case 0xE4: case 0xE5:
            return 'a';
        case 0xC7: case 0xE7:
            return 'c';
        case 0xC8: case 0xC9: case 0xCA: case 0xCB:
        case 0xE8: case 0xE9: case 0xEA: case 0xEB:
            return 'e';
        case 0xCC: case 0xCD: case 0xCE: case 0xCF:
        case 0xEC: case 0xED: case 0xEE: case 0xEF:
            return 'i';
        case 0xD1: case 0xF1:
            return 'n';
        case 0xD2: case 0xD3: case 0xD4: case 0xD5: case 0xD6:
        case 0xF2: case 0xF3: case 0xF4: case 0xF5: case 0xF6:
            return 'o';
        case 0xD9: case 0xDA: case 0xDB: case 0xDC:
        case 0xF9: case 0xFA: case 0xFB: case 0xFC:
            return 'u';
        case 0xDD: case 0xFD: case 0xFF:
            return 'y';
        default:
            return 0;
    }
}

// Default display-name → resource-name transform: lowercase, fold Latin-1
// diacritics to their base letter, drop anything that isn't a-z/0-9 (so
// apostrophes, dots and colons vanish), turn spaces/underscores into '-', then
// collapse repeated '-' and trim the ends. Existing hyphens and digits survive,
// so "Ho-Oh"→"ho-oh", "Porygon-Z"→"porygon-z", "Porygon2"→"porygon2",
// "Farfetch'd"→"farfetchd", "Mr. Mime"→"mr-mime", "Type: Null"→"type-null",
// "Flabébé"→"flabebe". The only names this can't recover are the Nidoran gender
// forms, handled by the override table in resourceNameFor().
std::string defaultResourceName(const std::string& name) {
    std::string out;
    out.reserve(name.size());
    for (std::size_t i = 0; i < name.size();) {
        const unsigned char c = static_cast<unsigned char>(name[i]);
        if (c < 0x80) {
            if (c >= 'A' && c <= 'Z') {
                out += static_cast<char>(c - 'A' + 'a');
            } else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
                out += static_cast<char>(c);
            } else if (c == ' ' || c == '-' || c == '_') {
                out += '-';
            }
            // Any other ASCII (', ., :, …) is dropped.
            ++i;
            continue;
        }
        // Multi-byte UTF-8: fold a Latin-1 accented letter to its base, else drop
        // the whole codepoint. Advance by the sequence length either way.
        std::size_t len = 1;
        if ((c & 0xE0) == 0xC0) {
            len = 2;
        } else if ((c & 0xF0) == 0xE0) {
            len = 3;
        } else if ((c & 0xF8) == 0xF0) {
            len = 4;
        }
        if (c == 0xC3 && i + 1 < name.size()) {
            const unsigned int cp =
                0xC0u | (static_cast<unsigned char>(name[i + 1]) & 0x3Fu);
            if (const char folded = foldLatin1(cp)) {
                out += folded;
            }
        }
        i += len;
    }

    // Collapse runs of '-' and trim leading/trailing '-'.
    std::string collapsed;
    collapsed.reserve(out.size());
    bool prevHyphen = false;
    for (const char ch : out) {
        if (ch == '-') {
            if (!prevHyphen && !collapsed.empty()) {
                collapsed += '-';
            }
            prevHyphen = true;
        } else {
            collapsed += ch;
            prevHyphen = false;
        }
    }
    while (!collapsed.empty() && collapsed.back() == '-') {
        collapsed.pop_back();
    }
    return collapsed;
}

}  // namespace

std::string PokeApi::resourceNameFor(const MediaSubject& subject) const {
    // Override table — cases the default transform cannot recover because the
    // distinguishing character is symbolic and must become a letter, else the two
    // forms collide. Keyed by dex number; also the home for any future pin.
    switch (subject.dexNumber) {
        case 29:  // Nidoran♀
            return "nidoran-f";
        case 32:  // Nidoran♂
            return "nidoran-m";
        default:
            return defaultResourceName(subject.name);
    }
}

MediaRequest PokeApi::resolveMedia(const MediaSubject& subject, MediaKind kind) const {
    MediaRequest request;
    request.resourceName = resourceNameFor(subject);
    switch (kind) {
        case MediaKind::OfficialArtwork:
            // PokeAPI's official artwork lives as a static PNG on GitHub raw,
            // addressed directly by dex number — no REST/GraphQL call needed.
            request.url =
                "https://raw.githubusercontent.com/PokeAPI/sprites/master/"
                "sprites/pokemon/other/official-artwork/" +
                std::to_string(subject.dexNumber) + ".png";
            break;
    }
    return request;
}

}  // namespace pokedex
