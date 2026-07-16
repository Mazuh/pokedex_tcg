#pragma once

#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QObject>
#include <QString>
#include <QWidget>

#include <exception>

#include "core/app/wishlist_service.h"

namespace pokedex {

// GUI — prompt to edit a wishlist source in place (pre-filled with `oldSource`)
// and persist it via WishlistService. Shared by the unscoped wishlist table and
// the per-Pokémon sources editor so the dialog copy, no-op rules, and error
// handling stay identical between the two editors of the same data. A blank or
// unchanged entry is a no-op — the store is not touched (which would otherwise
// reject a blank value or needlessly bump updatedAt). Returns true when the
// caller should reload its view (an edit was attempted), false on cancel/no-op.
inline bool promptEditWishlistSource(QWidget* parent, WishlistService& wishlist,
                                     int dexNumber, const QString& oldSource) {
    bool ok = false;
    const QString entered = QInputDialog::getText(
        parent, QObject::tr("Edit Source"), QObject::tr("Seller or link:"),
        QLineEdit::Normal, oldSource, &ok);
    if (!ok) {
        return false;
    }
    const QString trimmed = entered.trimmed();
    if (trimmed.isEmpty() || trimmed == oldSource) {
        return false;  // blank or unchanged: nothing to persist
    }
    try {
        wishlist.editSource(dexNumber, oldSource.toStdString(), entered.toStdString());
    } catch (const std::exception& e) {
        QMessageBox::warning(parent, QObject::tr("Pokedex TCG"),
                             QString::fromUtf8(e.what()));
    }
    return true;
}

}  // namespace pokedex
