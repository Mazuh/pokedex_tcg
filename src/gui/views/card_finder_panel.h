#pragma once

#include <QHash>
#include <QPixmap>
#include <QString>
#include <QWidget>

#include <cstdint>
#include <vector>

#include "core/app/card_catalog_dto.h"

class QEvent;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QVBoxLayout;

namespace pokedex {

class CardSearchService;

// GUI — the reusable card finder: a set-scoped search field, an infinite-scroll
// printings list (with thumbnails), and a larger preview of the picked card.
// Extracted from AddCardCopyPage so both "Add a copy" and "Edit card" drive the
// exact same finder rather than duplicating its debounce / completer / infinite-
// scroll machinery.
//
// It owns only the *find + preview* concern: it depends on CardSearchService (the
// transport) and the target species' dex number, and reports the user's pick
// through signals — it never touches a form, a copy, or the image store. A host
// autofills its own fields / persists the image by reacting to cardSelected() and
// reading selectedCandidate()/selectedPreview() (e.g. at submit time). Nothing is
// fetched on open — a species can have hundreds of printings, so the user searches
// by set first (3+ chars, debounced).
class CardFinderPanel : public QWidget {
    Q_OBJECT

public:
    // `search` must outlive this panel. `dexNumber` scopes every search to one
    // species; `speciesName` only personalizes the "type a code…" hint.
    CardFinderPanel(CardSearchService& search, int dexNumber, QString speciesName,
                    QWidget* parent = nullptr);

    // Tag selecting the by-name construction below (disambiguates it from the
    // species ctor above).
    struct NameSearchMode {};

    // Name-search mode: the search field is a free card-name query, for a card that
    // depicts no species (a Trainer or Energy card) and so cannot be scoped by dex
    // number. There is no species scope and no set-name completer — the field IS the
    // query. `initialQuery` seeds the field (pass the card's stored name so a host
    // editing an existing card shows its printings immediately; "" starts blank).
    CardFinderPanel(CardSearchService& search, NameSearchMode, QString initialQuery,
                    QWidget* parent = nullptr);

    // Whether a printing is currently picked (its preview may still be loading).
    bool hasSelection() const;
    // The picked candidate, or a default-constructed CardCandidate when nothing is
    // selected (guard with hasSelection()).
    CardCandidate selectedCandidate() const;
    // The picked card's large image, or a null QPixmap when nothing is selected or
    // its image hasn't finished loading yet (a caller can fall back to the URL on
    // selectedCandidate()).
    QPixmap selectedPreview() const;

    // Override the actionable tail of the "no printings found" status, so each host
    // can point the user at its own fallback (typing the form by hand when adding,
    // uploading a photo when editing). The shared "No printings found for that set —"
    // lead-in stays fixed.
    void setNoResultsHint(const QString& hint);

    // Place `widget` centered directly beneath the preview image (e.g. an "apply this
    // card" action, so it sits under the picture it acts on). Takes ownership. Used by
    // the Edit-card host; the Add-copy host leaves the preview footer empty.
    void setPreviewFooter(QWidget* widget);

public Q_SLOTS:
    // Drop the current selection + preview — e.g. a host's form was edited so it no
    // longer matches the picked card. Emits selectionCleared().
    void clearSelection();

Q_SIGNALS:
    // A printing was picked: its autofill payload (the preview image follows once it
    // loads — read it from selectedPreview()).
    void cardSelected(const CardCandidate& candidate);
    // The picked card's large image finished loading (selectedPreview() is now
    // non-null). Never fires for a printing that has no image. Lets a host that needs
    // the pixmap in hand (e.g. to save it synchronously) wait until it is ready.
    void previewReady();
    // The selection was dropped (fresh search, cleared query, or clearSelection()).
    void selectionCleared();
    // A set was picked from the search completer. The finder only uses it to narrow
    // the search; a host form may additionally autofill its set fields from it.
    void setChosen(const CardSetInfo& set);

protected:
    // Keeps the printings list filling a viewport that grew taller than the loaded
    // rows, and rescales the preview when its pane is resized.
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void onSearchTextChanged(const QString& text);  // gate (3+ chars) → search
    void onPrintingsReady(std::uint64_t requestId, int dexNumber,
                          const std::vector<CardCandidate>& cards);
    void onPrintingsFailed(std::uint64_t requestId, int dexNumber);
    void onThumbnailReady(const QString& cardId, const QPixmap& pixmap);
    void onSetsReady();            // set table arrived: rebuild completer + re-run search
    void rebuildSetCompleter();    // "CODE — Name" typeahead on the search field

    void clearResults();   // empty the list + invalidate any in-flight reply
    void loadMore();       // append the next chunk of printings (never rebuilds shown rows)
    void fillViewport();   // append until the list overflows its viewport
    void selectCandidate(int index);   // record the pick + emit cardSelected + preview it
    void showPreview(int index);       // request the large image for the selected card
    void clearPreview();               // drop the selection + its preview, emit selectionCleared
    void renderPreview();              // scale the preview pixmap to its label
    void init(const QString& initialQuery);  // shared body of both constructors
    void searchWith(const QString& query);   // species: set filter; name mode: name query
    void updateStatus();

    CardSearchService& search_;
    int dexNumber_;          // 0 in name-search mode (no species)
    bool nameMode_ = false;  // search the field text as a card name, not a set filter
    QString speciesName_;
    // The host-specific tail of the "no printings found" status (see setNoResultsHint).
    QString noResultsHint_ = tr("the catalog may not list it, or it may be flaking (retry).");

    // Finder (search field + printings list)
    QLineEdit* searchField_;
    QLabel* status_;
    QListWidget* printings_;
    std::vector<CardCandidate> candidates_;  // the full result set; the list chunks it
    // The id of this panel's most recent search; replies for other ids (another live
    // finder, or our own superseded search) are ignored — the service is app-shared.
    // Set to 0 to invalidate any in-flight result (e.g. the query was cleared).
    std::uint64_t pendingRequestId_ = 0;
    int loadedCount_ = 0;
    bool filling_ = false;   // guards fillViewport() re-entry (as in PokemonListView)
    bool loading_ = false;   // a search is in flight
    QHash<QString, QListWidgetItem*> itemById_;  // card id → row, for late-arriving thumbnails

    // Preview of the currently-selected card.
    QVBoxLayout* previewLayout_;  // holds preview_; a host may append a footer action
    QLabel* preview_;
    QLabel* priceHint_;  // subtle market-price line under the preview (from the search
                         // payload's embedded prices — no extra fetch); empty = hidden
    QPixmap previewPixmap_;      // full-res selected-card image; rescaled on resize
    QString previewCardId_;      // the thumbnail key we're awaiting for the preview
    int selectedIndex_ = -1;     // index into candidates_ of the picked card, or -1
};

}  // namespace pokedex
