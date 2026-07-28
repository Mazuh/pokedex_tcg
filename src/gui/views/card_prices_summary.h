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
class CardCopyService;
class CardPriceFetchController;
struct CardCopy;

// GUI — the compact "market prices" block shown in the card inspector (PokemonDetailPanel) and
// the Edit card page. It keeps the part of pricing that reads well in a tiny pane — the per-vendor
// headline figures, each vendor name linking to a marketplace search, and the ⓘ metrics/freshness
// popover — plus one inline "Fetch"/"Refresh" for the quick keep-updated action. Everything
// heavier (Clear, hide/restore a vendor, and future pricing verbs) lives behind its "Manage
// prices" button, which relays managePricesRequested() so an owning view pushes the dedicated
// PricesEditPage — the same move the wishlist made off the inspector.
//
// It never networks on mere selection: it renders whatever is already cached (suppressed vendors
// filtered out) and re-renders when a fetch/clear/suppression elsewhere touches its card. The
// inline Fetch/Refresh drives the SAME CardPriceFetchController the management panel uses, so it
// gets the invisible resolve-and-link (a first fetch links an unlinked copy; a legacy pre-tcgdex
// id is re-resolved) — the button never dead-ends, and a fetch that links the copy is relayed up
// via copyLinked() so the host updates its cached copy. That is the only way it mutates a copy, so
// it needs CardCopyService (for the controller) beyond the lookup service.
class CardPricesSummary : public QWidget {
    Q_OBJECT

public:
    // `lookup` and `copies` must outlive this widget; they back the cache reads and the shared
    // fetch controller.
    CardPricesSummary(CardPriceLookupService& lookup, CardCopyService& copies,
                      QWidget* parent = nullptr);

    // Point the summary at an owned copy and render its cached prices (never a network fetch).
    void showCopy(const CardCopy& copy);
    // Empty state: no copy selected. Hides the block.
    void clear();

Q_SIGNALS:
    // The user asked to manage this copy's prices (the "Manage prices" button). Carries the
    // copy id; the owning view pushes the PricesEditPage for it.
    void managePricesRequested(const QString& copyId);
    // An inline Fetch resolved and persisted this copy's tcgdex link. Forwarded up so the host
    // updates its cached copy (as the management page's cardLinked does).
    void copyLinked(const QString& copyId, const QString& externalCardId);

private:
    void render();          // rebuild the UI from the local cache for the current copy
    void onFetchClicked();  // inline Fetch/Refresh — delegates to the shared controller

    CardPriceLookupService& lookup_;
    CardPriceFetchController* fetcher_;  // the shared fetch/resolve/link state machine

    // The copy currently shown: enough to render the headline + its marketplace links.
    std::string copyId_;
    CardReference cardRef_;
    QString speciesName_;          // the copy's Pokémon name (blank for a species-free card)
    std::string preferredFinish_;  // the copy's foil → tcgdex finish (normal/holo/reverse)
    QString externalCardId_;       // empty == not yet linked (kept in sync via the controller)
    bool copyRemoved_ = false;     // a soft-Removed copy — frozen history, no price block
    QString fetchStatus_;          // transient progress from the controller ("Fetching…")
    QString fetchError_;           // transient note from the last failed fetch (cleared on a new
                                   // fetch / a successful one / a copy change)

    QLabel* headline_;         // per-vendor figures; each vendor name links to a marketplace search
    QToolButton* infoButton_;  // "ⓘ" — the metrics + freshness popover
    QLabel* status_;           // muted one-liner for the not-priced / fetching / error states
    QPushButton* fetchButton_;   // inline "Fetch"/"Refresh"; shown whenever a fetch is possible
    QPushButton* manageButton_;  // "Manage prices" — opens the dedicated page
};

}  // namespace pokedex
