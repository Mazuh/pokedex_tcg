#pragma once

#include <QString>
#include <QStringLiteral>

#include "core/domain/card_copy.h"
#include "core/domain/card_reference.h"
#include "core/domain/pokemon_catalog.h"
#include "core/domain/types.h"

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

}  // namespace pokedex
