#pragma once

#include <QString>
#include <QWidget>

#include <string>

#include "core/domain/card_reference.h"

class QLabel;
class QPushButton;
class QToolButton;

namespace pokedex {

class CardPriceLookupService;
struct CardCopy;

// GUI — the compact, READ-ONLY "market prices" block shown in the card inspector
// (PokemonDetailPanel) and the Edit card page. It keeps the part of pricing that reads well in
// a tiny pane — the per-vendor headline figures, each vendor name linking to a marketplace
// search, and the ⓘ metrics/freshness popover — and nothing that mutates. Every management
// action (Fetch/Refresh, Clear, hide/restore a vendor, and future pricing verbs) lives behind
// its one "Manage prices" button, which relays managePricesRequested() so an owning view pushes
// the dedicated PricesEditPage — the same move the wishlist made off the inspector.
//
// It never mutates a copy and never networks on mere selection: it renders whatever is already
// cached (suppressed vendors filtered out, so a hidden vendor never surfaces here) and re-renders
// when a fetch/clear/suppression elsewhere emits pricesReady for its card. Its one active
// affordance is an inline "Refresh" shown ONLY when the card has figures on screen — a plain
// re-fetch of the stored id (`lookup.fetch(externalCardId)`), no resolve and no copy mutation, so
// it still needs only the lookup service, not CardCopyService. It is deliberately absent when
// nothing is shown (never fetched, fetched-empty, or a stale pre-tcgdex id yielding no figures):
// the inline path can't re-resolve a legacy id, so those cases go through "Manage prices", whose
// fetch re-resolves and links. So the inline button never dead-ends a fetch, and first-time
// fetching (which resolves + links a copy) stays on the management page.
class CardPricesSummary : public QWidget {
    Q_OBJECT

public:
    // `lookup` must outlive this widget; it supplies the cached prices, suppressions, and
    // fetch stamp this read-only view renders.
    explicit CardPricesSummary(CardPriceLookupService& lookup, QWidget* parent = nullptr);

    // Point the summary at an owned copy and render its cached prices (never a network fetch).
    void showCopy(const CardCopy& copy);
    // Empty state: no copy selected. Hides the block.
    void clear();

Q_SIGNALS:
    // The user asked to manage this copy's prices (the "Manage prices" button). Carries the
    // copy id; the owning view pushes the PricesEditPage for it.
    void managePricesRequested(const QString& copyId);

private:
    void render();          // rebuild the UI from the local cache for the current copy
    void onFetchClicked();  // re-fetch this (already-linked) card's prices — no resolve/link

    CardPriceLookupService& lookup_;

    // The copy currently shown: enough to render the headline + its marketplace links.
    std::string copyId_;
    CardReference cardRef_;
    QString speciesName_;          // the copy's Pokémon name (blank for a species-free card)
    std::string preferredFinish_;  // the copy's foil → tcgdex finish (normal/holo/reverse)
    QString externalCardId_;       // empty == not yet linked (nothing cached to show)
    bool copyRemoved_ = false;     // a soft-Removed copy — frozen history, no price block
    bool fetching_ = false;        // an inline Fetch/Refresh is in flight
    QString fetchError_;           // transient note from the last failed inline fetch (cleared on
                                   // a new fetch / a successful one / a copy change)

    QLabel* headline_;         // per-vendor figures; each vendor name links to a marketplace search
    QToolButton* infoButton_;  // "ⓘ" — the metrics + freshness popover
    QLabel* status_;           // muted one-liner for the not-priced / fetching / error states
    QPushButton* fetchButton_;   // inline "Fetch"/"Refresh" (linked copies only); re-fetch prices
    QPushButton* manageButton_;  // "Manage prices" — opens the dedicated page
};

}  // namespace pokedex
