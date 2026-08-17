#pragma once

#include <QHash>
#include <QPixmap>
#include <QString>
#include <QWidget>

#include <cstdint>
#include <vector>

#include "core/app/card_catalog_dto.h"

class QComboBox;
class QEvent;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QVBoxLayout;

namespace pokedex {

class CardSearchService;

// GUI — the reusable card finder: a set picker, an infinite-scroll printings list
// (with thumbnails), and a larger preview of the picked card. Extracted from
// AddCardCopyPage so both "Add a copy" and "Edit card" drive the exact same finder
// rather than duplicating its completer / infinite-scroll machinery.
//
// It owns only the *find + preview* concern: it depends on CardSearchService (the
// transport) and the target species' dex number, and reports the user's pick
// through signals — it never touches a form, a copy, or the image store. A host
// autofills its own fields / persists the image by reacting to cardSelected() and
// reading selectedCandidate()/selectedPreview() (e.g. at submit time). Nothing is
// fetched on open — a species can have hundreds of printings, so the user picks a
// set first.
//
// In species mode that picker is a searchable DROPDOWN over the catalog's set table,
// and CHOOSING a set is the only thing that searches — typing merely filters the
// list, locally. That is deliberate: free text used to fire a search on the third
// keystroke and then a second one when the user clicked the completion they were
// aiming at, spending two requests on a free public API to show one set. Free text
// bought nothing either, since a set outside the table resolves to nothing and never
// reaches the wire — the only sets that ever worked are exactly the ones the dropdown
// lists. When the table couldn't be loaded there is nothing to choose from, so the
// panel says the catalog may be having problems and offers a Retry (see updateStatus).
class CardFinderPanel : public QWidget {
    Q_OBJECT

public:
    // `search` must outlive this panel. `dexNumber` scopes every search to one
    // species; `speciesName` only personalizes the "pick a set" hint.
    CardFinderPanel(CardSearchService& search, int dexNumber, QString speciesName,
                    QWidget* parent = nullptr);

    // Tag selecting the by-name construction below (disambiguates it from the
    // species ctor above).
    struct NameSearchMode {};

    // Name-search mode: the search field is a free card-name query, for a card that
    // depicts no species (a Trainer or Energy card) and so cannot be scoped by dex
    // number. There is no species scope and no set dropdown — the field IS the query,
    // typed and debounced (3+ characters), exactly as before. `initialQuery` seeds the
    // field (pass the card's stored name so a host editing an existing card shows its
    // printings immediately; "" starts blank).
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
    // lead-in stays fixed. This is the GENUINELY-EMPTY status only: a search whose
    // request failed gets its own message and a Retry button, so a hint here should
    // not hedge about the catalog flaking.
    void setNoResultsHint(const QString& hint);

    // Place `widget` centered directly beneath the preview image (e.g. an "apply this
    // card" action, so it sits under the picture it acts on). Takes ownership. Used by
    // the Edit-card host; the Add-copy host leaves the preview footer empty.
    void setPreviewFooter(QWidget* widget);

    // Show `pixmap` in the preview pane whenever no card is picked, instead of the
    // "Select a card to preview it." hint. The Edit-card host passes the copy's current
    // image so the picture being edited is visible without running a search (the reason
    // it can leave the search field empty on open). A null pixmap restores the hint.
    // The placeholder is superseded by a picked card's image and returns when the
    // selection is cleared.
    void setPreviewPlaceholder(const QPixmap& pixmap);

public Q_SLOTS:
    // Drop the current selection + preview — e.g. a host's form was edited so it no
    // longer matches the picked card. Emits selectionCleared().
    void clearSelection();

    // Programmatically point the finder at `query`. Used by a host to prefill it (the
    // card scanner's reading, or reusing the last card's set across a booster) so the
    // search then does its natural job — list the printings and autofill the picked
    // card.
    //
    // In NAME mode the query is a card name and this behaves exactly as typing it. In
    // SPECIES mode it is a set code/name, and the panel RESOLVES it against the set
    // table: a query naming exactly one set selects it in the dropdown and searches it
    // (one request); a query naming none — or several — selects nothing, says so, and
    // spends no request, leaving the user to pick. A query that arrives before the set
    // table is held and resolved once it lands.
    void searchFor(const QString& query);

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
    // The USER picked a set from the dropdown. The finder only uses it to narrow the
    // search; a host form may additionally autofill its set fields from it. Not emitted
    // for a set the panel selected on a host's behalf (searchFor) — the host prefilling
    // the finder already knows the set and has filled its own fields.
    void setChosen(const CardSetInfo& set);

protected:
    // Keeps the printings list filling a viewport that grew taller than the loaded
    // rows, and rescales the preview when its pane is resized.
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void onSearchTextChanged(const QString& text);  // NAME MODE: gate (3+ chars) → search
    void onPrintingsReady(std::uint64_t requestId, int dexNumber,
                          const std::vector<CardCandidate>& cards);
    void onPrintingsFailed(std::uint64_t requestId, int dexNumber);
    void onThumbnailReady(const QString& cardId, const QPixmap& pixmap);
    void onSetsReady();          // set table arrived: refill the dropdown, resolve a prefill
    void rebuildSetCombo();      // fill the dropdown with "CODE — Name" entries
    // Narrow to `setId`: select it in the dropdown, optionally report it to the host,
    // and search it — unless its printings are already on screen, which costs nothing
    // to keep and a request to re-fetch.
    void chooseSet(const QString& setId, bool emitChosen);
    // Select the dropdown entry carrying `setId` without emitting activated(). Returns
    // false when no entry has it (an unknown id, or the table isn't loaded).
    bool selectSetEntry(const QString& setId);
    // Whether the set table is missing altogether, so the dropdown has nothing to offer
    // (species mode only). Pair with CardSearchService::setsLoading() to tell "still
    // loading" apart from "the catalog didn't give us one".
    bool setTableMissing() const;

    void clearResults();   // empty the list + invalidate any in-flight reply
    void loadMore();       // append the next chunk of printings (never rebuilds shown rows)
    void fillViewport();   // append until the list overflows its viewport
    void selectCandidate(int index);   // record the pick + emit cardSelected + preview it
    void showPreview(int index);       // request the large image for the selected card
    void clearPreview();               // drop the selection + its preview, emit selectionCleared
    void renderPreview();              // scale the preview pixmap to its label
    void init(const QString& initialQuery);  // shared body of both constructors
    void searchWith(const QString& query);   // species: exact set id; name mode: name query
    void updateStatus();

    CardSearchService& search_;
    int dexNumber_;          // 0 in name-search mode (no species)
    bool nameMode_ = false;  // search the field text as a card name, not a set filter
    QString speciesName_;
    // The host-specific tail of the "no printings found" status (see setNoResultsHint).
    // It no longer hedges about the API flaking: a failed request is its own state now
    // (failed_), so reaching this text really does mean the catalog answered with nothing.
    QString noResultsHint_ = tr("the catalog lists no such printing.");

    // Finder (set picker / name field + printings list). Exactly one input exists: the
    // dropdown in species mode, the free-text field in name mode; the other is null.
    QComboBox* setCombo_ = nullptr;    // species mode: the searchable set dropdown
    QLineEdit* searchField_ = nullptr; // name mode: the card-name query
    QLabel* status_ = nullptr;
    QPushButton* retryButton_ = nullptr;  // shown only in the failed / no-set-table states
    // Null until init() builds it — and eventFilter reads it on every event, so anything
    // this panel filters must be installed after this point.
    QListWidget* printings_ = nullptr;
    std::vector<CardCandidate> candidates_;  // the full result set; the list chunks it
    // The id of this panel's most recent search; replies for other ids (another live
    // finder, or our own superseded search) are ignored — the service is app-shared.
    // Set to 0 to invalidate any in-flight result (e.g. the query was cleared).
    std::uint64_t pendingRequestId_ = 0;
    int loadedCount_ = 0;
    bool filling_ = false;   // guards fillViewport() re-entry (as in PokemonListView)
    bool loading_ = false;   // a search is in flight
    // The last search FAILED (the transport exhausted its retry ladder) rather than
    // coming back empty. Held apart from an empty candidates_ because the two are
    // indistinguishable on screen yet mean opposite things — see updateStatus().
    bool failed_ = false;
    QString lastQuery_;  // the query searchWith() last ran; what Retry re-runs
    // The set id whose printings are on screen (species mode), so re-picking the set
    // already listed spends no request. Cleared with the results.
    QString shownSetId_;
    // A host's searchFor() query that arrived before the set table: held here and
    // resolved from onSetsReady(), so a cold-cache scan/reuse still lands on its set.
    QString pendingSetQuery_;
    // Why a host's searchFor() query selected no set (named none, or several). Shown
    // until the user picks one — INCLUDING while another set is still selected and
    // listed, which is exactly when the prefill's failure would otherwise be invisible
    // (a "reuse last card's set" whose set name is ambiguous leaves the previous set's
    // printings on screen, and nothing would say the reuse didn't take).
    QString prefillNote_;
    QHash<QString, QListWidgetItem*> itemById_;  // card id → row, for late-arriving thumbnails

    // Preview of the currently-selected card.
    QVBoxLayout* previewLayout_ = nullptr;  // holds preview_; a host may append a footer action
    QLabel* preview_ = nullptr;
    QLabel* priceHint_ = nullptr;  // subtle market-price line under the preview (from the search
                         // payload's embedded prices — no extra fetch); empty = hidden
    QPixmap previewPixmap_;      // full-res selected-card image; rescaled on resize
    QPixmap placeholderPixmap_;  // shown when nothing is picked (host's current image)
    QString previewCardId_;      // the thumbnail key we're awaiting for the preview
    int selectedIndex_ = -1;     // index into candidates_ of the picked card, or -1
};

}  // namespace pokedex
