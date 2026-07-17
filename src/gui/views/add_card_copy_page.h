#pragma once

#include <QHash>
#include <QPixmap>
#include <QString>
#include <QWidget>

#include <cstdint>
#include <optional>
#include <vector>

#include "core/app/card_catalog_dto.h"
#include "core/domain/types.h"

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
class BinderService;

// GUI — the "add a copy" screen for one Pokémon: a manual entry form on the left, a
// set-scoped card finder in the middle, and a preview of the picked card on the
// right. Nothing is fetched on open — a species can have hundreds of printings, so
// the user searches by set (code or name, 3+ chars, debounced) to pull just that
// set's cards. Selecting a card autofills the form's card reference and shows a
// larger image; the form stays usable by hand, and the page works even when the
// card API is unreachable.
//
// Submitting creates a copy via CardCopyService (no image is cached — search
// results stay display-only), then emits copyAdded() (so the host can refresh any
// owned-copy counts) and backRequested() to return to the previous screen. The
// form carries an optional binder picker: when opened unscoped (from the Pokémon
// browser) it defaults to "— None —" and the user may file the copy in any binder;
// when opened from within a binder it is pre-filled with that binder and locked, so
// the copy lands where the user already is. (The remove-with-note flow lives
// elsewhere, in OwnedCardsView.)
//
// It is an in-window page pushed onto a host's QStackedWidget (PokemonListView or
// BinderView); a Back button emits backRequested() and the host pops + disposes
// of it, so each open starts fresh.
class AddCardCopyPage : public QWidget {
    Q_OBJECT

public:
    // `search`, `copies` and `binders` must outlive this page. `speciesName` is
    // shown in the heading; `dexNumber` drives the printings search and the created
    // copy. `lockedBinder`, when set, pre-fills the binder picker with that binder
    // and locks it (the copy is created there and the user can't repick) — the
    // scoped case, opening from within a binder. When nullopt the picker is a free
    // choice defaulting to "— None —".
    AddCardCopyPage(CardSearchService& search, CardCopyService& copies,
                    BinderService& binders, int dexNumber, const QString& speciesName,
                    std::optional<CardBinderId> lockedBinder = std::nullopt,
                    QWidget* parent = nullptr);

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
    void onSetsReady();                // set table arrived: rebuild completer + re-run search
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
    BinderService& binders_;
    int dexNumber_;
    QString speciesName_;
    // Set when the page is scoped to a binder: the copy is filed here regardless of
    // the (disabled) combo's display state, so it never lands unfiled even if the
    // binder is absent from the combo (e.g. removed after the guide was opened).
    std::optional<CardBinderId> lockedBinder_;

    // Form
    QLineEdit* expansionCode_;
    QLineEdit* setName_;
    QComboBox* language_;
    QLineEdit* collectorNumber_;
    QComboBox* condition_;
    QComboBox* ownership_;
    QComboBox* binder_;
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
