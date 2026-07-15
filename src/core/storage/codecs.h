#pragma once

#include <string>

#include "core/domain/region.h"
#include "core/domain/types.h"

namespace pokedex {

// STORAGE — the encoding between domain values and their on-disk TEXT form.
// Enums persist as stable tokens (never their numeric value, which would break
// if the enum is reordered) and timestamps as ISO-8601 UTC. These live in the
// storage layer, not the domain, so the domain stays ignorant of persistence;
// repositories share them (the timestamp codec is reused by every root).

// Region <-> its stable storage token ("Kanto" … "Paldea"). regionFromText
// throws StorageError on an unrecognized token.
std::string regionToText(Region region);
Region regionFromText(const std::string& text);

// Timestamp <-> ISO-8601 UTC, second precision ("YYYY-MM-DDThh:mm:ssZ").
// Sub-second parts are truncated on encode. timestampFromIso throws StorageError
// when the text is not in that exact form.
std::string timestampToIso(Timestamp when);
Timestamp timestampFromIso(const std::string& text);

}  // namespace pokedex
