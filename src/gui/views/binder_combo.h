#pragma once

#include <QComboBox>
#include <QObject>
#include <QString>
#include <QVariant>

#include <optional>
#include <vector>

#include "core/domain/card_binder.h"
#include "core/domain/types.h"
#include "gui/views/region_labels.h"

namespace pokedex {

// GUI — the shared "which binder is this copy filed in" combo logic, used by every
// flow that files a copy (the Add-copy form and the BinderPickerDialog). Kept in one
// place so the two never diverge on the load-bearing conventions: the leading
// "— None —" entry carries an empty-string data value that maps back to nullopt, and
// a binder is labelled "name — region" so two same-named binders stay distinct.

// Display text for a binder in a combo — name, plus its region when it has one.
inline QString binderComboLabel(const CardBinder& binder) {
    const QString name = QString::fromStdString(binder.name);
    if (binder.pokemonRegion) {
        return QStringLiteral("%1 — %2").arg(name, regionLabel(*binder.pokemonRegion));
    }
    return name;
}

// Populate `combo` with a leading "— None —" entry (empty-string data → nullopt)
// followed by one entry per binder (data = binder id), preselecting `current` when
// it still exists in the list.
inline void fillBinderCombo(QComboBox& combo, const std::vector<CardBinder>& binders,
                            const std::optional<CardBinderId>& current) {
    combo.clear();
    combo.addItem(QObject::tr("— None —"), QString());
    for (const CardBinder& binder : binders) {
        combo.addItem(binderComboLabel(binder), QString::fromStdString(binder.id));
    }
    if (current) {
        const int index = combo.findData(QString::fromStdString(*current));
        if (index >= 0) {
            combo.setCurrentIndex(index);
        }
    }
}

// The chosen binder id, or nullopt for the "— None —" entry.
inline std::optional<CardBinderId> binderComboSelection(const QComboBox& combo) {
    const QString id = combo.currentData().toString();
    if (id.isEmpty()) {
        return std::nullopt;
    }
    return id.toStdString();
}

}  // namespace pokedex
