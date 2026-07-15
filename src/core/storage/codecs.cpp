#include "core/storage/codecs.h"

#include <cstdio>
#include <ctime>

#include "core/storage/database.h"  // StorageError

namespace pokedex {

std::string regionToText(Region region) {
    // The single definition of every region's on-disk token. These strings are
    // part of the storage format, so they must stay stable even if the enum is
    // reordered — an explicit mapping, never the enum's numeric value. A new
    // enumerator makes this switch fail -Wswitch under -Werror, forcing a token
    // to be chosen rather than silently defaulting.
    switch (region) {
        case Region::Kanto:  return "Kanto";
        case Region::Johto:  return "Johto";
        case Region::Hoenn:  return "Hoenn";
        case Region::Sinnoh: return "Sinnoh";
        case Region::Unova:  return "Unova";
        case Region::Kalos:  return "Kalos";
        case Region::Alola:  return "Alola";
        case Region::Galar:  return "Galar";
        case Region::Paldea: return "Paldea";
    }
    // Unreachable for a valid enum value; guards against an unmapped addition.
    throw StorageError("unknown Region enum value");
}

Region regionFromText(const std::string& text) {
    // Derived from the one token definition above, over the canonical enum list,
    // so there is no second table to keep in sync.
    for (const Region region : kRegions) {
        if (text == regionToText(region)) {
            return region;
        }
    }
    throw StorageError("unknown region token: " + text);
}

std::string timestampToIso(Timestamp when) {
    const std::time_t secs = std::chrono::system_clock::to_time_t(
        std::chrono::time_point_cast<std::chrono::seconds>(when));
    std::tm tm{};
    gmtime_r(&secs, &tm);
    // gmtime_r yields fields in their normal calendar ranges, but the compiler
    // can't prove that: it sees each %0Nd fed a full-range int and computes a
    // worst case (six ints of up to 11 digits, plus separators and the NUL)
    // that overflows a 21-byte buffer (-Wformat-truncation). Size for that
    // worst case so truncation is provably impossible.
    char buf[73];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec);
    return buf;
}

Timestamp timestampFromIso(const std::string& text) {
    std::tm tm{};
    char trailing = '\0';
    // The trailing %c catches any junk after the 'Z'; a well-formed stamp leaves
    // exactly 6 fields matched and nothing for it to consume.
    const int matched = std::sscanf(text.c_str(), "%4d-%2d-%2dT%2d:%2d:%2dZ%c",
                                    &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
                                    &tm.tm_hour, &tm.tm_min, &tm.tm_sec, &trailing);
    if (matched != 6) {
        throw StorageError("malformed ISO-8601 UTC timestamp: " + text);
    }
    tm.tm_year -= 1900;
    tm.tm_mon -= 1;
    // timegm interprets the fields as UTC (unlike mktime's local time); available
    // on macOS and glibc, the two platforms this project targets.
    const std::time_t secs = timegm(&tm);
    return std::chrono::system_clock::from_time_t(secs);
}

}  // namespace pokedex
