#pragma once

#include <QWidget>

class QLabel;
class QLineEdit;
class QShowEvent;
class QTableWidget;

namespace pokedex {

class CardCopyService;

// GUI — the "My Cards" section: a flat, read-only inventory of every card copy the
// user has recorded (Owned, Incoming, or soft-Removed), so they can keep track of
// their collection. A thin shell over CardCopyService::listAll(); species names
// come from the compile-time Pokédex catalog. No card image is shown — the app
// does not store or cache card artwork.
//
// It reloads every time it becomes visible (showEvent), so a copy added elsewhere
// (from a Pokémon's "Add copy" page) appears the moment the user switches here,
// without any cross-section signalling. A live search box filters the rows on any
// visible field. It is a section embedded in MainWindow's stack, not a separate
// window.
class OwnedCardsView : public QWidget {
    Q_OBJECT

public:
    explicit OwnedCardsView(CardCopyService& copies, QWidget* parent = nullptr);

protected:
    void showEvent(QShowEvent* event) override;

private:
    // Re-query all copies and rebuild the table (sorted by dex number, then age),
    // then reapply the current search filter.
    void reload();
    // Hide rows that don't match the search text (case-insensitive substring over
    // every visible column), and refresh the "Showing N of M" count.
    void applyFilter();

    CardCopyService& copies_;
    QLineEdit* search_;
    QTableWidget* table_;
    QLabel* countLabel_;
};

}  // namespace pokedex
