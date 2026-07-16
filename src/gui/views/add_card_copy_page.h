#pragma once

#include <QHash>
#include <QString>
#include <QWidget>

#include <vector>

#include "core/app/card_catalog_dto.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPixmap;
class QPlainTextEdit;
class QPushButton;

namespace pokedex {

class CardSearchService;

// GUI — the "add a copy" screen for one Pokémon: a manual entry form beside a
// scrollable, picture-backed list of that species' real printings fetched from
// the external card catalog. Selecting a printing autofills the form's card
// reference; the list is a pure convenience — the form is fully usable by hand,
// and the page works even when the card API is unreachable.
//
// In THIS slice the submit button is intentionally disabled: persisting a copy
// (minting it, writing the card_copy row, caching its image) is a separate,
// future task. The page therefore only reads from CardSearchService; it never
// writes to storage.
//
// It is an in-window page pushed onto a host's QStackedWidget (PokemonListView or
// BinderView); a Back button emits backRequested() and the host pops + disposes
// of it, so each open starts fresh.
class AddCardCopyPage : public QWidget {
    Q_OBJECT

public:
    // `search` must outlive this page. `speciesName` is shown in the heading and
    // labels; `dexNumber` drives the printings search.
    AddCardCopyPage(CardSearchService& search, int dexNumber, const QString& speciesName,
                    QWidget* parent = nullptr);

Q_SIGNALS:
    void backRequested();

protected:
    // Keeps the printings list filling a viewport that grew taller than the loaded
    // rows (no scrollbar yet, so scrolling can't drive the next chunk).
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void onPrintingsReady(int dexNumber, const std::vector<CardCandidate>& cards);
    void onPrintingsFailed(int dexNumber);
    void onThumbnailReady(const QString& cardId, const QPixmap& pixmap);
    void rebuildExpansionCompleter();

    void loadMore();       // append the next chunk of printings (never rebuilds shown rows)
    void fillViewport();   // append until the list overflows its viewport
    void selectCandidate(int index);   // autofill the form from a chosen printing
    void narrowByExpansionCode();      // re-search this species scoped to the typed set code
    void updateStatus();

    CardSearchService& search_;
    int dexNumber_;
    QString speciesName_;

    // Form
    QLineEdit* expansionCode_;
    QComboBox* language_;
    QLineEdit* collectorNumber_;
    QComboBox* condition_;
    QComboBox* ownership_;
    QPlainTextEdit* comments_;
    QPushButton* submit_;

    // Printings list
    QLabel* status_;
    QListWidget* printings_;
    std::vector<CardCandidate> candidates_;  // the full result set; the list chunks it
    int loadedCount_ = 0;
    bool filling_ = false;   // guards fillViewport() re-entry (as in PokemonListView)
    bool loading_ = true;    // a search is in flight
    QHash<QString, QListWidgetItem*> itemById_;  // card id → row, for late-arriving thumbnails
};

}  // namespace pokedex
