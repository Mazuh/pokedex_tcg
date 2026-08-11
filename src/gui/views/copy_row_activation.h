#pragma once

#include <QMessageBox>
#include <QObject>
#include <QString>
#include <QWidget>

#include <functional>

namespace pokedex {

// GUI — the shared "activate a row" gesture (double-click / Enter) for the Pokémon
// browser (PokemonListView) and the binder guide (BinderView). Both share the same
// confirm-then-act shape — offer to add a copy when the row has none to act on, else
// confirm an action on the copy it does have — but two things vary. What "has a copy"
// MEANS differs with the row model: for the browser a row is a species, so it asks
// whether that species owns a copy on this surface; for the guide a row is one card, so
// it asks whether this row is a copy row rather than a species placeholder. And the
// owned-row action diverges: the guide edits that row's card, while the browser jumps to
// "My Cards" pre-filtered to the species. The variable pieces (both dialogs' wording, the
// callbacks, and whether the owned action targets a named copy) are passed in; the
// decision + the two QMessageBox prompts live here so a change to the flow is made once.
struct CopyRowActivation {
    QWidget* parent;
    bool hasOwnedCopy;         // this row has a copy to act on (see the note above: the
                               // browser reads it per species, the guide per row)
    QString addPrompt;         // shown when hasOwnedCopy is false (host-specific wording)
    QString ownedTitle;        // the owned-row confirmation's title (host-specific)
    QString ownedPrompt;       // the owned-row confirmation's text (host-specific)
    bool ownedNeedsShownCopy;  // the owned action targets a specific copy (edit) — guard on it
    QString shownCopyId;       // that copy's id ("" = none); checked iff above. The guide passes
                               // its ROW's copy id; the browser, the detail panel's shown copy
    std::function<void()> onAdd;    // push the species' add-copy page
    std::function<void()> onOwned;  // the owned-row action (edit that copy / jump to My Cards)
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
