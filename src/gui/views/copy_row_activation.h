#pragma once

#include <QMessageBox>
#include <QObject>
#include <QString>
#include <QWidget>

#include <functional>

namespace pokedex {

// GUI — the shared "activate a species row" gesture (double-click / Enter) for the
// two copy-mode hosts: the Pokémon browser (PokemonListView) and a binder guide
// (BinderView). Both do the same confirm-then-act flow — edit the copy the detail
// panel is showing, or offer to add one when the species owns none on this surface
// — and differ only in the "no copy" wording and in which inner stack the add/edit
// pages push onto. Those variable pieces are passed in; the decision + the two
// confirmation dialogs live here so a change to the flow is made once, not twice.
struct CopyRowActivation {
    QWidget* parent;
    bool hasOwnedCopy;      // the species owns a copy on this surface
    QString species;        // for the edit-confirmation wording
    QString shownCopyId;    // the detail panel's currently shown copy ("" = none)
    QString addPrompt;      // shown when hasOwnedCopy is false (host-specific wording)
    std::function<void()> onAdd;   // push the species' add-copy page
    std::function<void()> onEdit;  // push the shown copy's edit page
};

inline void activateCopyRow(const CopyRowActivation& a) {
    if (!a.hasOwnedCopy) {
        // No copy on this surface: confirm opening the add-copy page for the species.
        const auto choice = QMessageBox::question(
            a.parent, QObject::tr("Add a card"), a.addPrompt,
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (choice == QMessageBox::Yes) {
            a.onAdd();
        }
        return;
    }
    // Owned: confirm editing the copy the detail panel is showing. Selection precedes
    // activation, so a copy is already on screen.
    if (a.shownCopyId.isEmpty()) {
        return;  // defensive: nothing shown to edit
    }
    const auto choice = QMessageBox::question(
        a.parent, QObject::tr("Edit card"),
        QObject::tr("Edit the shown card of %1?").arg(a.species),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
    if (choice == QMessageBox::Yes) {
        a.onEdit();
    }
}

}  // namespace pokedex
