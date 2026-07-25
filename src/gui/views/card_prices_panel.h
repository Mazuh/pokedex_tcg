#pragma once

#include <QString>
#include <QWidget>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/app/card_price_dto.h"
#include "core/domain/card_reference.h"
#include "core/domain/types.h"

class QLabel;
class QPushButton;
class QToolButton;
class QTableWidget;
class QHBoxLayout;
class QTimer;

namespace pokedex {

class CardPriceLookupService;
class CardSearchService;
class CardCopyService;
struct CardCopy;
struct CardCandidate;

// GUI — the reusable "market prices" block for one owned copy. Shared by every
// owned-copy surface (the Edit page, the My Cards detail, and the binder-guide /
// Pokémon-browser copy detail). It is strictly on-demand: pointing it at a copy only
// renders what is already cached (no network); a fetch happens solely when the user
// clicks Fetch/Refresh, so merely viewing a card never hits the API.
//
// Linking is invisible. A copy that isn't yet tied to a catalog card but records
// enough identity (a set + species/name) still shows a "Fetch prices" button; the
// first fetch resolves the catalog card automatically (searching the catalog scoped
// by the copy's set + species/name, singling one out by collector number) and
// persists the link before fetching — the user never sees or manages "linking". Only
// a copy with too little data to resolve (no set) shows a hint to complete it in Edit.
//
// States it renders: nothing selected → hidden; unresolvable (unlinked, too little
// data) → a hint; ready-to-fetch (linked, or auto-resolvable) → a "Fetch prices"
// button; has-prices → a headline summary + "as of/fetched" line + a Refresh button +
// an expandable full table (every vendor × variant × metric); fetched-but-empty →
// "no market prices found".
class CardPricesPanel : public QWidget {
    Q_OBJECT

public:
    // `lookup`, `search`, and `copies` must outlive this panel. `search` + `copies`
    // back the invisible auto-link the first Fetch performs on an unlinked copy.
    CardPricesPanel(CardPriceLookupService& lookup, CardSearchService& search,
                    CardCopyService& copies, QWidget* parent = nullptr);

    // Point the panel at an owned copy and render its cached prices (never a network
    // fetch). Carries the copy's link context (id, printed reference, species) so the
    // Fetch button can auto-resolve the catalog card when the copy is still unlinked.
    void showCopy(const CardCopy& copy);
    // Empty state: no copy selected. Hides the block.
    void clear();

Q_SIGNALS:
    // A Fetch auto-resolved and persisted the catalog link for this copy. Hosts use it
    // to update their cached copy so a re-selection sees the copy as already linked.
    void cardLinked(const QString& copyId, const QString& externalCardId);

private:
    void render();           // rebuild the UI from the local cache for the current copy
    void repopulateTable();  // sort rows_ by the active header column and fill the table
    void onFetchClicked();   // the only path that spends a network request
    // Handle the catalog reply for a Fetch-triggered auto-link: pick the single matching
    // printing (disambiguating by collector number), persist the link, then fetch prices.
    void onLinkResults(const std::vector<CardCandidate>& cards);
    // Whether the current copy is unlinked but carries enough data (a set + species/name)
    // for the Fetch button to resolve its catalog card.
    bool canAutoLink() const;
    // Hide the whole block down to a single status message (empty text hides that too) —
    // the shared teardown for the not-selected / unresolvable / read-failure states.
    void resetToMessage(const QString& text);

    CardPriceLookupService& lookup_;
    CardSearchService& search_;
    CardCopyService& copies_;

    // The copy currently shown: its id, printed reference, species, and catalog link.
    // externalCardId_ empty == not yet linked.
    std::string copyId_;
    CardReference cardRef_;
    std::optional<PokemonDexNum> dexNumber_;
    QString externalCardId_;

    bool fetching_ = false;  // a Fetch (link and/or price fetch) is in flight
    bool linking_ = false;   // the auto-link search specifically is in flight
    std::uint64_t pendingLinkRequest_ = 0;  // the CardSearchService request we await
    // Recovers the Fetch button if the shared, debounced search service ever drops the
    // auto-link request without a reply (see the owned-cards watchdog rationale).
    QTimer* linkWatchdog_;

    // The current card's prices, cached so a header-sort click is a pure in-memory
    // reorder (no re-read). Header-driven sort state, re-applied on every render.
    std::vector<CardPrice> rows_;
    int sortColumn_ = -1;  // < 0 = natural (provenance, variant, metric) load order
    Qt::SortOrder sortOrder_ = Qt::AscendingOrder;

    QLabel* headline_;
    QToolButton* infoButton_;  // "ⓘ" — explains what the metrics mean, on click
    QLabel* status_;
    QLabel* links_;            // "View on TCGplayer/Cardmarket" listing links (rich text)
    QPushButton* fetchButton_;
    QToolButton* toggle_;
    QTableWidget* table_;
};

}  // namespace pokedex
