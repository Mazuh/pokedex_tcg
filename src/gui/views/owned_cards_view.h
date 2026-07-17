#pragma once

#include <QString>
#include <QWidget>

#include <string>
#include <vector>

#include "core/domain/card_copy.h"

class QLabel;
class QLineEdit;
class QPushButton;
class QShowEvent;
class QTableWidget;

namespace pokedex {

class CardCopyService;
class BinderService;
class CardImageStore;
class CardImagePanel;

// GUI — the "My Cards" section: a flat, read-only inventory of every card copy the
// user has recorded (Owned, Incoming, or soft-Removed), so they can keep track of
// their collection. A thin shell over CardCopyService::listAll(); species names
// come from the compile-time Pokédex catalog. A right-hand panel shows the selected
// copy's stored card image (loaded from the workspace via CardImageStore); copies
// added before that image was saved, or without a preview picked, show a placeholder.
//
// It reloads every time it becomes visible (showEvent), so a copy added elsewhere
// (from a Pokémon's "Add copy" page) appears the moment the user switches here,
// without any cross-section signalling. A live search box filters the rows on any
// visible field, and a selected card can be filed into / out of a binder via a
// picker. It is a section embedded in MainWindow's stack, not a separate window.
class OwnedCardsView : public QWidget {
    Q_OBJECT

public:
    OwnedCardsView(CardCopyService& copies, BinderService& binders, CardImageStore& images,
                   QWidget* parent = nullptr);

protected:
    void showEvent(QShowEvent* event) override;

private:
    // Re-query all copies and rebuild the table (sorted by dex number, then age),
    // then reapply the current search filter.
    void reload();
    // Hide rows that don't match the search text (case-insensitive substring over
    // every visible column), and refresh the "Showing N of M" count.
    void applyFilter();
    // Enable the row-action buttons only when a row is selected.
    void updateButtonState();
    // Show the selected copy's card image (and title) in the right panel, or clear
    // the panel when there is no selection.
    void showSelectedImage();
    // Open the binder picker for the selected copy and file it accordingly.
    void assignSelected();
    // Soft-remove the selected copy, prompting for an optional note to append.
    void removeSelected();

    CardCopyService& copies_;
    BinderService& binders_;
    CardImageStore& images_;
    CardImagePanel* panel_;   // right-hand card-image detail panel
    QLineEdit* search_;
    QTableWidget* table_;
    QLabel* emptyLabel_;   // shown in place of the table when no cards are recorded yet
    QPushButton* assignButton_;
    QPushButton* removeButton_;
    QLabel* countLabel_;
    // The copies backing the current rows, in display order (row i ⇄ loaded_[i]);
    // filtering only hides rows, so this stays aligned with the table.
    std::vector<CardCopy> loaded_;
    // Per-row lowercased search text, precomputed in reload() so filtering is a
    // plain substring compare with no per-keystroke allocation (row i ⇄ haystacks_[i]).
    std::vector<QString> haystacks_;
    // The copy id currently shown in the panel (empty when the panel is cleared), so
    // showSelectedImage() can skip the disk read when the selection hasn't changed —
    // it fires on every keystroke via applyFilter().
    std::string shownCopyId_;
};

}  // namespace pokedex
