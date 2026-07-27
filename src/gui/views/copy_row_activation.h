#pragma once

#include <QMessageBox>
#include <QObject>
#include <QString>
#include <QWidget>

#include <functional>

namespace pokedex {

// GUI — the shared "activate a species row" gesture (double-click / Enter) for the
// two copy-mode hosts: the Pokémon browser (PokemonListView) and a binder guide
// (BinderView). Both share the same confirm-then-act shape — offer to add a copy
// when the species owns none on this surface, else confirm an owned-row action —
// but the owned-row action itself diverges: the binder guide edits the copy the
// detail panel is showing, while the browser jumps to "My Cards" pre-filtered to
// the species. The variable pieces (both dialogs' wording, the callbacks, and
// whether the owned action targets the shown copy) are passed in; the decision +
// the two QMessageBox prompts live here so a change to the flow is made once.
struct CopyRowActivation {
    QWidget* parent;
    bool hasOwnedCopy;         // the species owns a copy on this surface
    QString addPrompt;         // shown when hasOwnedCopy is false (host-specific wording)
    QString ownedTitle;        // the owned-row confirmation's title (host-specific)
    QString ownedPrompt;       // the owned-row confirmation's text (host-specific)
    bool ownedNeedsShownCopy;  // the owned action targets the shown copy (edit) — guard on it
    QString shownCopyId;       // the detail panel's shown copy ("" = none); checked iff above
    std::function<void()> onAdd;    // push the species' add-copy page
    std::function<void()> onOwned;  // the owned-row action (edit the shown copy / jump to My Cards)
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
    // Owned: confirm the host's owned-row action. An action that targets the shown copy
    // (edit) needs one on screen; selection precedes activation, so it normally is.
    if (a.ownedNeedsShownCopy && a.shownCopyId.isEmpty()) {
        return;  // defensive: nothing shown to act on
    }
    const auto choice = QMessageBox::question(a.parent, a.ownedTitle, a.ownedPrompt,
                                              QMessageBox::Yes | QMessageBox::No,
                                              QMessageBox::Yes);
    if (choice == QMessageBox::Yes) {
        a.onOwned();
    }
}

}  // namespace pokedex
