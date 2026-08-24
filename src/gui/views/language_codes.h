#pragma once

#include <QHash>
#include <QString>
#include <QStringList>

#include "gui/views/empty_option.h"

namespace pokedex {

// GUI — the card languages this project recognizes, shared by the card-copy form's
// Language picker and the Settings "Default language" picker so the two lists can
// never drift. The English-only catalog can't fill this, so language is always the
// user's choice. The leading blank entry is "unspecified" (rendered as "— None —").
// The stored value is always this bare code; see languageLabel() for what a picker
// shows.
inline const QStringList& languageCodes() {
    static const QStringList codes = {"",   "EN", "FR", "DE", "IT", "ES", "LA",
                                      "PT", "JP", "KO", "C",  "F",  "T",  "I"};
    return codes;
}

// GUI — a purely decorative flag for a code, so a picker row is recognizable at a
// glance instead of being read letter by letter. Empty when a code has no ONE obvious
// country: "LA" is Latin-American Spanish (many), and the four single-letter codes are
// of unrecorded origin. An arbitrary flag would be worse than none — it would assert a
// print's country wrongly — so those simply render bare.
inline QString languageFlag(const QString& code) {
    static const QHash<QString, QString> flags = {
        {"EN", QStringLiteral("🇺🇸")}, {"FR", QStringLiteral("🇫🇷")},
        {"DE", QStringLiteral("🇩🇪")}, {"IT", QStringLiteral("🇮🇹")},
        {"ES", QStringLiteral("🇪🇸")}, {"PT", QStringLiteral("🇧🇷")},
        {"JP", QStringLiteral("🇯🇵")}, {"KO", QStringLiteral("🇰🇷")},
    };
    return flags.value(code);
}

// GUI — how a language code is DISPLAYED in a picker: the flag (when there is one)
// ahead of the code, and the shared "— None —" for the blank entry. Every picker builds
// its items through this, so the decoration can never appear on one screen and not the
// other; each item still carries the bare code as its data, which is what round-trips
// to storage.
inline QString languageLabel(const QString& code) {
    if (code.isEmpty()) return noneOptionLabel();
    const QString flag = languageFlag(code);
    return flag.isEmpty() ? code : flag + QStringLiteral(" ") + code;
}

// The config-file key under which the user's default card language is stored (see
// storage/workspace.h's key=value config). Empty/absent means "no default".
inline constexpr char kDefaultLanguageConfigKey[] = "default_language";

}  // namespace pokedex
