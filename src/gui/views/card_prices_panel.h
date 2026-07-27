#pragma once

#include <QString>
#include <QWidget>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/domain/card_reference.h"
#include "core/domain/types.h"

class QLabel;
class QPushButton;
class QToolButton;
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
// button; has-prices → a headline summary (one representative figure per vendor, each
// vendor name linking to its listing page; the "as of"/fetched dates live on the ⓘ
// tooltip) + a Refresh button; fetched-but-empty → "no market prices found". The full
// per-metric spread is deliberately left to the marketplace (the listing links) rather
// than shown as a raw cache table.
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
    // Whether the Fetch button may auto-resolve an unlinked copy's catalog card. Default
    // true. The Edit page sets it false: it has its own card finder for linking (and
    // shares this app-wide CardSearchService with it), so the panel there shows prices
    // for an already-linked copy but routes linking through the finder — no second,
    // racing search consumer.
    void setAutoLinkEnabled(bool enabled);

Q_SIGNALS:
    // A Fetch auto-resolved and persisted the catalog link for this copy. Hosts use it
    // to update their cached copy so a re-selection sees the copy as already linked.
    void cardLinked(const QString& copyId, const QString& externalCardId);

private:
    void render();          // rebuild the UI from the local cache for the current copy
    void onFetchClicked();  // the only path that spends a network request
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
    bool copyRemoved_ = false;  // a soft-Removed copy — frozen history, never auto-linked

    bool autoLinkEnabled_ = true;  // see setAutoLinkEnabled (false on the Edit page)
    bool fetching_ = false;  // a Fetch (link and/or price fetch) is in flight
    bool linking_ = false;   // the auto-link search specifically is in flight
    std::uint64_t pendingLinkRequest_ = 0;  // the CardSearchService request we await
    // Recovers the Fetch button if the shared, debounced search service ever drops the
    // auto-link request without a reply (see the owned-cards watchdog rationale).
    QTimer* linkWatchdog_;

    QLabel* headline_;         // per-vendor figures; each vendor name links to its listing
    QToolButton* infoButton_;  // "ⓘ" — explains the metrics + price freshness, on click
    QLabel* status_;
    QPushButton* fetchButton_;
};

}  // namespace pokedex
