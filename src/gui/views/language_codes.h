#pragma once

#include <QStringList>

namespace pokedex {

// GUI — the card languages this project recognizes, shared by the card-copy form's
// Language picker and the Settings "Default language" picker so the two lists can
// never drift. The English-only catalog can't fill this, so language is always the
// user's choice. The leading blank entry is "unspecified" (rendered as "— None —").
inline const QStringList& languageCodes() {
    static const QStringList codes = {"", "EN", "FR", "DE", "IT", "ES",
                                      "LA", "PT", "C",  "F",  "T",  "I"};
    return codes;
}

// The config-file key under which the user's default card language is stored (see
// storage/workspace.h's key=value config). Empty/absent means "no default".
inline constexpr char kDefaultLanguageConfigKey[] = "default_language";

}  // namespace pokedex
