#pragma once

#include <QHash>
#include <QPixmap>
#include <QString>
#include <QWidget>

#include <cstdint>
#include <vector>

#include "core/app/card_catalog_dto.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPlainTextEdit;
class QPushButton;

namespace pokedex {

class CardSearchService;
class CardCopyService;

// GUI — the "add a copy" screen for one Pokémon: a manual entry form on the left, a
// set-scoped card finder in the middle, and a preview of the picked card on the
// right. Nothing is fetched on open — a species can have hundreds of printings, so
// the user searches by set (code or name, 3+ chars, debounced) to pull just that
// set's cards. Selecting a card autofills the form's card reference and shows a
// larger image; the form stays usable by hand, and the page works even when the
// card API is unreachable.
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
    // rows, and rescales the card preview when its pane is resized.
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void onSearchTextChanged(const QString& text);  // gate (3+ chars, matches a set) → search
    void onPrintingsReady(std::uint64_t requestId, int dexNumber,
                          const std::vector<CardCandidate>& cards);
    void onPrintingsFailed(std::uint64_t requestId, int dexNumber);
    void onThumbnailReady(const QString& cardId, const QPixmap& pixmap);
    void rebuildSetCompleter();        // "CODE — Name" typeahead on the search field
    void chooseSet(const CardSetInfo& set);  // fill the form's code + set-name from one set

    void clearResults();   // empty the list + invalidate any in-flight reply
    void loadMore();       // append the next chunk of printings (never rebuilds shown rows)
    void fillViewport();   // append until the list overflows its viewport
    void selectCandidate(int index);   // autofill the form + preview from a chosen printing
    void showPreview(int index);       // request the large image for the selected card
    void clearPreview();               // drop the selection + its preview
    void renderPreview();              // scale the preview pixmap to its label
    void checkUnmatch();               // clear the preview once the form no longer matches
    void searchWith(const QString& filter);  // search this species scoped to a set filter
    void updateStatus();
    void updateSubmitEnabled();        // enable submit once the form is valid
    void submitCopy();                 // create the copy from the form fields

    CardSearchService& search_;
    CardCopyService& copies_;
    int dexNumber_;
    QString speciesName_;

    // Form
    QLineEdit* expansionCode_;
    QLineEdit* setName_;
    QComboBox* language_;
    QLineEdit* collectorNumber_;
    QComboBox* condition_;
    QComboBox* ownership_;
    QPlainTextEdit* comments_;
    QPushButton* submit_;

    // Finder (search field + printings list)
    QLineEdit* searchField_;
    QLabel* status_;
    QListWidget* printings_;
    std::vector<CardCandidate> candidates_;  // the full result set; the list chunks it
    // The id of this page's most recent search; replies for other ids (another live
    // page, or our own superseded search) are ignored — the service is app-shared.
    // Set to 0 to invalidate any in-flight result (e.g. the query was cleared).
    std::uint64_t pendingRequestId_ = 0;
    int loadedCount_ = 0;
    bool filling_ = false;   // guards fillViewport() re-entry (as in PokemonListView)
    bool loading_ = false;   // a search is in flight
    QHash<QString, QListWidgetItem*> itemById_;  // card id → row, for late-arriving thumbnails

    // Preview of the currently-selected card.
    QLabel* preview_;
    QPixmap previewPixmap_;      // full-res selected-card image; rescaled on resize
    QString previewCardId_;      // the thumbnail key we're awaiting for the preview
    int selectedIndex_ = -1;     // index into candidates_ of the picked card, or -1
};

}  // namespace pokedex
