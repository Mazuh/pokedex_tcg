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
class CardCopyService;

// GUI — the "add a copy" screen for one Pokémon: a manual entry form beside a
// scrollable, picture-backed list of that species' real printings fetched from
// the external card catalog. Selecting a printing autofills the form's card
// reference; the list is a pure convenience — the form is fully usable by hand,
// and the page works even when the card API is unreachable.
//
// Submitting creates a copy via CardCopyService (no image is cached — search
// results stay display-only). On success the page stays open with the form
// cleared, so several copies of the same species can be added in a row, and
// copyAdded() lets the host refresh any owned-copy counts. A created copy is filed
// nowhere for now — binder assignment and the remove-with-note flow are separate,
// later concerns.
//
// It is an in-window page pushed onto a host's QStackedWidget (PokemonListView or
// BinderView); a Back button emits backRequested() and the host pops + disposes
// of it, so each open starts fresh.
class AddCardCopyPage : public QWidget {
    Q_OBJECT

public:
    // `search` and `copies` must outlive this page. `speciesName` is shown in the
    // heading; `dexNumber` drives the printings search and the created copy.
    AddCardCopyPage(CardSearchService& search, CardCopyService& copies, int dexNumber,
                    const QString& speciesName, QWidget* parent = nullptr);

Q_SIGNALS:
    void backRequested();
    // A copy was persisted; the host should refresh any owned-copy counts it shows.
    void copyAdded();

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
    void updateSubmitEnabled();        // enable submit once the form is valid
    void submitCopy();                 // create the copy from the form fields

    CardSearchService& search_;
    CardCopyService& copies_;
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
