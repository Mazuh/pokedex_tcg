#pragma once

#include <QHash>
#include <QIcon>
#include <QSize>
#include <QString>
#include <QStringList>

#include "gui/views/emoji_icon.h"
#include "gui/views/empty_option.h"

namespace pokedex {

// GUI — the card languages this project recognizes, shared by the card-copy form's
// Language picker and the Settings "Default language" picker so the two lists can
// never drift. The English-only catalog can't fill this, so language is always the
// user's choice. The leading blank entry is "unspecified" (rendered as "— None —"),
// and must stay first: the form resolves an unset language by selecting index 0.
//
// The order is by how often a collector here actually holds the print, NOT alphabetical
// and not by region: English, then the two this collection is local to (Spanish,
// Portuguese), then Japanese and the two Chinese markets, then the remaining European
// languages, and last the ones that turn up rarely or never. So the codes worth
// scrolling to sit at the top of a picker that is opened for nearly every card added.
inline const QStringList& languageCodes() {
    static const QStringList codes = {"",   "EN", "ES", "PT", "JP", "C",  "F",
                                      "FR", "DE", "IT", "KO", "I",  "T",  "LA"};
    return codes;
}

// GUI — the size a picker must give a language flag icon (see languageFlagIcon): a
// combo's default 16x16 box is SQUARE, and a flag emoji is wider than tall, so leaving
// it scales the glyph down to a third of the row's height. Every picker showing these
// icons sets it, hence the shared constant.
inline constexpr QSize kLanguageFlagIconSize{22, 16};

// GUI — a purely decorative flag for a code. Every code has one, so no row reads as a
// lone unexplained letter: each maps to the country whose print it names, except "LA"
// (Latin-American Spanish, which is many countries) — that one gets the Americas globe. The two Chinese codes are distinct markets,
// not one flag: "C" is Simplified Chinese (mainland China) and "F" Traditional Chinese
// (Taiwan / Hong Kong, labelled with Taiwan's flag).
inline QString languageFlag(const QString& code) {
    static const QHash<QString, QString> flags = {
        {"EN", QStringLiteral("🇺🇸")}, {"FR", QStringLiteral("🇫🇷")},
        {"DE", QStringLiteral("🇩🇪")}, {"IT", QStringLiteral("🇮🇹")},
        {"ES", QStringLiteral("🇪🇸")}, {"PT", QStringLiteral("🇧🇷")},
        {"JP", QStringLiteral("🇯🇵")}, {"KO", QStringLiteral("🇰🇷")},
        {"C", QStringLiteral("🇨🇳")},  {"F", QStringLiteral("🇹🇼")},
        {"T", QStringLiteral("🇹🇭")},  {"I", QStringLiteral("🇮🇩")},
        {"LA", QStringLiteral("🌎")},
    };
    return flags.value(code);
}

// GUI — that flag as a picker ICON rather than as label text. The distinction is not
// cosmetic: QComboBox type-ahead matches keystrokes against the DISPLAYED text, so a
// flag glyph prefixed onto the label makes every code unreachable by keyboard — typing
// "F" would stop matching "FR" and land on the bare "F" (Traditional Chinese) entry
// instead. As an icon the flag sits beside the code without joining the text it is
// searched by, and the icons line up in a column that a variable-width prefix wouldn't.
// The painting itself is the shared emojiIcon (see its header for the DPR, the
// don't-cache-a-QPixmap rule, and why the ink size is a separate argument from the box);
// this only picks the flag and the picker's icon size. A flag's ink is wide and short, so
// it fits this box at a pixel size equal to the box's height — which a square emoji would
// not, hence the constant is passed rather than assumed.
inline QIcon languageFlagIcon(const QString& code) {
    return emojiIcon(languageFlag(code), kLanguageFlagIconSize, kLanguageFlagIconSize.height());
}

// GUI — how a language code is DISPLAYED in a picker: the bare code, and the shared
// "— None —" for the blank entry. The flag carries the recognition (see
// languageFlagIcon) and this text carries the picker's type-ahead search key, so it
// stays exactly the code the user reads off the card. Each item also holds that bare
// code as its data, which is what round-trips to storage.
inline QString languageLabel(const QString& code) {
    return code.isEmpty() ? noneOptionLabel() : code;
}

// The config-file key under which the user's default card language is stored (see
// storage/workspace.h's key=value config). Empty/absent means "no default".
inline constexpr char kDefaultLanguageConfigKey[] = "default_language";

}  // namespace pokedex
