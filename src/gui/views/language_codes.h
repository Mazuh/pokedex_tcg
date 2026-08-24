#pragma once

#include <QFont>
#include <QGuiApplication>
#include <QHash>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QRect>
#include <QSize>
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

// GUI — the size a picker must give a language flag icon (see languageFlagIcon): a
// combo's default 16x16 box is SQUARE, and a flag emoji is wider than tall, so leaving
// it scales the glyph down to a third of the row's height. Every picker showing these
// icons sets it, hence the shared constant.
inline constexpr QSize kLanguageFlagIconSize{22, 16};

// GUI — a purely decorative flag for a code. Every code has one, so no row reads as a
// lone unexplained letter: each maps to the country whose print it names, except "LA"
// (Latin-American Spanish, which is many countries) — that one gets the Americas globe
// and leans on its label to say the rest. The two Chinese codes are distinct markets,
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
// Rendered rather than shipped as assets: an emoji is the one "image" the system font
// already has at every size. Deliberately NOT cached in a static — a QPixmap outliving
// QGuiApplication is a documented crash-at-exit, and repainting a dozen glyphs when a
// page opens is far too cheap to be worth that hazard.
inline QIcon languageFlagIcon(const QString& code) {
    const QString flag = languageFlag(code);
    if (flag.isEmpty()) return {};

    const int width = kLanguageFlagIconSize.width();
    const int height = kLanguageFlagIconSize.height();
    QPixmap pixmap(kLanguageFlagIconSize * qApp->devicePixelRatio());
    pixmap.setDevicePixelRatio(qApp->devicePixelRatio());
    pixmap.fill(Qt::transparent);
    {
        QPainter painter(&pixmap);
        QFont font = QGuiApplication::font();
        font.setPixelSize(height);
        painter.setFont(font);
        painter.drawText(QRect(0, 0, width, height), Qt::AlignCenter, flag);
    }
    return QIcon(pixmap);
}

// GUI — how a language code is DISPLAYED in a picker: the bare code, the shared
// "— None —" for the blank entry, and a parenthesised name for "LA", the one code whose
// icon (a globe, for want of a single country) can't say which market it means. It is
// the picker's search key (see languageFlagIcon), and each item still carries the bare
// code as its data, which is what round-trips to storage.
inline QString languageLabel(const QString& code) {
    if (code.isEmpty()) return noneOptionLabel();
    if (code == QLatin1String("LA")) return QObject::tr("LA (Latin America)");
    return code;
}

// The config-file key under which the user's default card language is stored (see
// storage/workspace.h's key=value config). Empty/absent means "no default".
inline constexpr char kDefaultLanguageConfigKey[] = "default_language";

}  // namespace pokedex
