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

// GUI — the reusable "market prices" block for one owned copy. Shared by every
// owned-copy surface (the Edit page, the My Cards detail, and the binder-guide /
// Pokémon-browser copy detail). It is strictly on-demand: pointing it at a copy only
// renders what is already cached (no network); a fetch happens solely when the user
// clicks Fetch/Refresh, so merely viewing a card never hits the API.
//
// Linking is invisible. A copy that isn't yet tied to a tcgdex card but records enough
// identity (a set + collector number) still shows a "Fetch prices" button; the first fetch
// resolves the tcgdex card id DIRECTLY from that printed set+number (via the lookup service's
// set table — no catalog search) and persists it before fetching — the user never sees or
// manages "linking". A copy still linked to a pre-tcgdex id is transparently re-resolved on
// its next Fetch. Only a copy with too little data to resolve (no set/number) shows a hint to
// complete it in Edit.
//
// States it renders: nothing selected → hidden; unresolvable (no set/number, unlinked) → a
// hint; ready-to-fetch (resolvable, or already linked) → a "Fetch prices" button; has-prices
// → a headline summary (one representative figure per vendor, each vendor name linking to a
// marketplace search; the "as of"/fetched dates live in the ⓘ dialog) + a Refresh button;
// fetched-but-empty → "no market prices found". The full per-metric spread is deliberately
// left to the marketplace (the listing links) rather than shown as a raw cache table.
class CardPricesPanel : public QWidget {
    Q_OBJECT

public:
    // `lookup` and `copies` must outlive this panel. `lookup` supplies the cached prices, the
    // tcgdex fetch, and the set table that resolves a copy's set+number to a tcgdex card id;
    // `copies` persists that resolved link.
    CardPricesPanel(CardPriceLookupService& lookup, CardCopyService& copies,
                    QWidget* parent = nullptr);

    // Point the panel at an owned copy and render its cached prices (never a network
    // fetch). Carries the copy's link context (id, printed reference) so the Fetch button
    // can resolve the tcgdex card when the copy is still unlinked.
    void showCopy(const CardCopy& copy);
    // Empty state: no copy selected. Hides the block.
    void clear();

Q_SIGNALS:
    // A Fetch resolved and persisted the tcgdex link for this copy. Hosts use it to update
    // their cached copy so a re-selection sees the copy as already linked.
    void cardLinked(const QString& copyId, const QString& externalCardId);

private:
    void render();          // rebuild the UI from the local cache for the current copy
    void onFetchClicked();  // the only path that spends a network request
    void onClearClicked();  // forget this card's cached prices (back to not-fetched)
    // Route a headline link: an http(s) marketplace link opens in the browser; an in-app
    // "action:hide:<vendor>" / "action:show:<vendor>" scheme suppresses / restores that vendor
    // for the current card (the headline's per-vendor ✕ / restore affordance).
    void onHeadlineLinkActivated(const QString& href);
    // Hide the whole block down to a single status message (empty text hides that too) —
    // the shared teardown for the not-selected / unresolvable / read-failure states.
    void resetToMessage(const QString& text);
    // A status message paired with a live Fetch/Refresh button — the shared shape for the
    // "resolvable but unfetched" and "fetched but empty" states (resetToMessage hides the
    // button, so this re-shows it with `buttonText`).
    void showFetchAffordance(const QString& message, const QString& buttonText);
    // End a failed Fetch: re-enable the Fetch button, re-show Clear if prices are still on screen
    // (a failed fetch changes no cache, so they remain valid and clearable), and show `message`.
    void reportFetchFailure(const QString& message);

    CardPriceLookupService& lookup_;
    CardCopyService& copies_;
    CardPriceFetchController* fetcher_;  // the shared fetch/resolve/link state machine

    // The copy currently shown: its id, printed reference, and tcgdex link.
    // externalCardId_ empty == not yet linked (kept in sync with fetcher_ on cardLinked).
    std::string copyId_;
    CardReference cardRef_;
    QString speciesName_;  // the copy's Pokémon name (blank for a species-free card) — the
                           // marketplace search's name fallback when there is no printed card name
    std::string preferredFinish_;  // the copy's foil → tcgdex finish (normal/holo/reverse), so
                                   // the headline shows the price of the finish it actually is
    QString externalCardId_;
    bool copyRemoved_ = false;  // a soft-Removed copy — frozen history, never auto-resolved

    QLabel* headline_;         // per-vendor figures; each vendor name links to a marketplace search
    QToolButton* infoButton_;  // "ⓘ" — opens the modal explaining the metrics + price freshness
    QString infoHtml_;         // that modal's body: rebuilt per render (it names THIS card's
                               // dates), read by the button's provider at click time
    QLabel* status_;
    QPushButton* fetchButton_;
    QPushButton* clearButton_;  // "Clear" — shown once a card has been fetched (has prices or a
                                // fetched-empty stamp); wipes the cache back to not-fetched
};

}  // namespace pokedex
