#pragma once

#include <QString>
#include <QStringLiteral>

#include "core/domain/card_copy.h"
#include "core/domain/card_reference.h"
#include "core/domain/pokemon_catalog.h"
#include "core/domain/types.h"
#include "gui/views/language_codes.h"
#include "gui/views/table_cell.h"
#include "gui/views/tooltip_text.h"

namespace pokedex {

// GUI — human-facing labels for a CardCopy's printed identity, shared by every
// view that names a copy (the My Cards table + image panel, the Edit-card
// heading, and the binder guide's detail panel) so the wording never diverges.
// Kept header-only in gui/views/ like the other *_labels.h helpers, and out of
// the Qt-free core.

// The catalog entry for a dex number (contiguous 1..N, so index == dex - 1), or
// nullptr for an out-of-range number (defensive). The single bounds/index guard
// that species-name and species-region lookups share. The returned pointer is
// stable — pokemonCatalog() is a span over compile-time storage, not a temporary.
inline const Pokemon* catalogEntry(PokemonDexNum dexNumber) {
    const auto catalog = pokemonCatalog();
    if (dexNumber < 1 || dexNumber > static_cast<int>(catalog.size())) {
        return nullptr;
    }
    return &catalog[dexNumber - 1];
}

// The species name for a dex number. Empty for an out-of-range number (defensive).
inline QString speciesName(PokemonDexNum dexNumber) {
    const Pokemon* entry = catalogEntry(dexNumber);
    return entry ? QString::fromStdString(entry->name) : QString();
}

// The printed identity as one cell: "BS 44/102", or just the number when the
// expansion code is unknown.
inline QString cardText(const CardReference& ref) {
    const QString expansion = QString::fromStdString(ref.expansionCode);
    const QString number = QString::fromStdString(ref.collectorNumber);
    return expansion.isEmpty() ? number : expansion + QStringLiteral(" ") + number;
}

// The set as one cell for the "Set" column: the full set name with its abbreviation in
// parentheses ("Base Set (BS)"), degrading to whichever part is present ("Base Set" or
// "BS") and empty when the card records neither.
inline QString setLabel(const CardReference& ref) {
    const QString name = QString::fromStdString(ref.setName);
    const QString code = QString::fromStdString(ref.expansionCode);
    if (name.isEmpty()) {
        return code;  // code, or "" when neither is recorded
    }
    if (code.isEmpty()) {
        return name;
    }
    return name + QStringLiteral(" (") + code + QStringLiteral(")");
}

// The inspector's compact printed-identity line: the set (its abbreviation, or the full
// set name when there's no abbreviation) followed by the collector number. Any absent
// part drops out; empty only when the card records neither a set nor a number. Unlike
// cardText, this falls back to the set name rather than showing a bare number when the
// abbreviation is missing.
inline QString collectorLine(const CardReference& ref) {
    const QString code = QString::fromStdString(ref.expansionCode);
    const QString set = code.isEmpty() ? QString::fromStdString(ref.setName) : code;
    const QString number = QString::fromStdString(ref.collectorNumber);
    if (set.isEmpty()) {
        return number;
    }
    if (number.isEmpty()) {
        return set;
    }
    return set + QStringLiteral(" ") + number;
}

// A copy's identifying label: its species name, or (for a species-free Trainer/Energy
// card, or an out-of-range dex) its printed card name. Empty when neither resolves.
// The single source for both the table's name column and titleFor(), so they agree.
inline QString speciesOrCardName(const CardCopy& copy) {
    const QString species =
        copy.pokemonDexNum ? speciesName(*copy.pokemonDexNum) : QString();
    return species.isEmpty() ? QString::fromStdString(copy.cardRef.name) : species;
}

// A copy's display title: its label plus its printed identity ("Pikachu · BS 44/102"),
// or just the printed identity when the label is empty. Shared by the image panel and
// the Edit-card heading so the two never diverge.
inline QString titleFor(const CardCopy& copy) {
    const QString label = speciesOrCardName(copy);
    const QString card = cardText(copy.cardRef);
    return label.isEmpty() ? card : label + QStringLiteral(" · ") + card;
}

// The "Lang" column's cell for a copy's printed language: the bare code as the text, the
// flag as the cell's ICON, and the language SPELLED OUT on its tooltip. Shared by the two
// card tables (My Cards, the binder guide) so a flag can never appear in one of them
// meaning something it doesn't mean in the other.
//
// The flag is an icon for exactly the reason the pickers' is (see languageFlagIcon): an
// item view's keyboardSearch does type-ahead against the CURRENT COLUMN's display text
// with MatchStartsWith, so a flag baked into the text makes every row of this column
// unreachable by keyboard — typing "e" stops finding "EN", because every cell now starts
// with a regional-indicator pair instead. The rule is therefore uniform: a flag is in the
// text only where nothing searches that text (the inspector's QLabel), and an icon
// everywhere else. A table showing these MUST setIconSize(kLanguageFlagIconSize), or Qt
// falls back to the style's small-icon box and squashes a wide-and-short flag.
//
// The tooltip REPLACES cell()'s own rather than appending to it (addToolTip), which is the
// one place that is right: languageTooltip already repeats the code, so nothing an elided
// column needed is lost, and what it adds is the only thing a flag by itself cannot say.
// An unset language keeps cell()'s em-dash, gets no icon, and gets no tooltip.
inline QTableWidgetItem* languageCell(const std::string& code) {
    const QString language = QString::fromStdString(code);
    QTableWidgetItem* item = cell(language);
    if (!language.isEmpty()) {
        item->setIcon(languageFlagIcon(language));
        item->setToolTip(tooltipText(languageTooltip(language)));
    }
    return item;
}

}  // namespace pokedex
