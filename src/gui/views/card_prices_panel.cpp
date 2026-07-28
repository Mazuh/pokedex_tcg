#include "gui/views/card_prices_panel.h"

#include <QDesktopServices>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <exception>
#include <optional>
#include <string>
#include <vector>

#include "core/app/card_copy_service.h"
#include "core/app/card_price_dto.h"
#include "core/domain/card_copy.h"
#include "gui/services/card_price_lookup_service.h"
#include "gui/views/card_copy_labels.h"  // speciesName(dexNumber)
#include "gui/views/price_headline.h"    // headlineHtml, marketSearchTerm, priceInfo…
#include "gui/views/price_labels.h"

namespace pokedex {

CardPricesPanel::CardPricesPanel(CardPriceLookupService& lookup, CardCopyService& copies,
                                 QWidget* parent)
    : QWidget(parent), lookup_(lookup), copies_(copies) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    // The headline carries the per-vendor figures with the vendor name linking to a
    // marketplace search (rich text), so a vendor is named once rather than repeated in a
    // separate "Listings" line.
    headline_ = new QLabel(this);
    headline_->setWordWrap(true);
    headline_->setTextFormat(Qt::RichText);
    // Route clicks ourselves (openExternalLinks OFF): the headline mixes http marketplace
    // links with in-app "action:" links (hide/restore a vendor), so a single linkActivated
    // handler dispatches by scheme. The label still needs link-interaction flags to be
    // clickable at all (the same pairing the About dialog / source_label use).
    headline_->setOpenExternalLinks(false);
    headline_->setTextInteractionFlags(Qt::TextBrowserInteraction);
    connect(headline_, &QLabel::linkActivated, this,
            &CardPricesPanel::onHeadlineLinkActivated);
    headline_->setStyleSheet(QStringLiteral("font-weight: 600;"));

    // "ⓘ" popover explaining the metrics — the shared idiom (makeInfoButton). Its tooltip also
    // carries the price freshness (vendor "as of" + our fetch date), set per-render; the click
    // shows whatever the current tooltip holds.
    infoButton_ = makeInfoButton(this);

    // The ⓘ sits at the far right, top-aligned so it pairs with the first headline line
    // (the headline can span two lines, one per vendor).
    auto* headlineRow = new QHBoxLayout;
    headlineRow->setContentsMargins(0, 0, 0, 0);
    headlineRow->addWidget(headline_, /*stretch=*/1);
    headlineRow->addWidget(infoButton_, 0, Qt::AlignTop | Qt::AlignRight);

    status_ = new QLabel(this);
    status_->setWordWrap(true);
    status_->setStyleSheet(QStringLiteral("color: gray;"));

    auto* buttonRow = new QHBoxLayout;
    fetchButton_ = new QPushButton(this);
    // "Clear" — a secondary action beside Fetch/Refresh, shown only once a card has been
    // fetched (see render). A normal (not flat/greyed) button so it doesn't read as disabled.
    // Low-stakes: clearing just re-offers Fetch, so no confirm dialog.
    clearButton_ = new QPushButton(tr("Clear"), this);
    clearButton_->hide();
    buttonRow->addWidget(fetchButton_);
    buttonRow->addWidget(clearButton_);
    buttonRow->addStretch(1);

    layout->addLayout(headlineRow);
    layout->addWidget(status_);
    layout->addLayout(buttonRow);

    connect(fetchButton_, &QPushButton::clicked, this, &CardPricesPanel::onFetchClicked);
    connect(clearButton_, &QPushButton::clicked, this, &CardPricesPanel::onClearClicked);
    // Re-render when a fetch we (or another view) triggered lands for our card.
    connect(&lookup_, &CardPriceLookupService::pricesReady, this, [this](const QString& id) {
        if (id == externalCardId_) {
            fetching_ = false;
            render();
        }
    });
    connect(&lookup_, &CardPriceLookupService::pricesFailed, this, [this](const QString& id) {
        if (id != externalCardId_) {
            return;
        }
        if (!fetching_) {
            // A background fetch for this same card, started by ANOTHER panel (the lookup
            // service is app-wide and its signal carries only the id), failed. We never
            // asked, and a failed fetch changes no cache — so leave our current display
            // (valid cached prices and their "as of" line) untouched rather than painting a
            // contradictory error over it.
            return;
        }
        // Neutral wording: the failure may be a busy/flaky API OR a card the provider does
        // not list (a 404, which the transport fails fast) — don't assert "try again" when a
        // retry may never help.
        reportFetchFailure(QStringLiteral("Couldn't fetch prices for this card right now."));
    });

    // When the (lazily fetched) tcgdex set table becomes available, a Fetch that was waiting
    // on it can resolve the card and go on to fetch prices. The signal is app-wide, so act
    // only if THIS panel is the one waiting.
    connect(&lookup_, &CardPriceLookupService::tcgdexSetsResolved, this, [this](bool ok) {
        if (!awaitingSets_) {
            return;
        }
        awaitingSets_ = false;
        if (ok) {
            resolveAndFetch();
            return;
        }
        // The set table is unavailable. If the copy already carries an id, fetch it as a
        // best effort; otherwise there is nothing to look up — offer a retry.
        if (!externalCardId_.isEmpty()) {
            lookup_.fetch(externalCardId_);
            return;
        }
        reportFetchFailure(QStringLiteral("Couldn't reach the pricing catalog. Please try again."));
    });
}

void CardPricesPanel::showCopy(const CardCopy& copy) {
    copyId_ = copy.id;
    cardRef_ = copy.cardRef;
    // The Pokémon name (from the copy's dex number) — the marketplace search's name fallback for
    // a card with no printed name. Blank for a species-free card (no dex number).
    speciesName_ = copy.pokemonDexNum ? speciesName(*copy.pokemonDexNum) : QString();
    preferredFinish_ = finishForFoil(copy.foil);  // pick the price of the finish this copy is
    externalCardId_ = QString::fromStdString(copy.externalCardId);
    copyRemoved_ = copy.ownership == CardOwnership::Removed;
    fetching_ = false;
    awaitingSets_ = false;
    triedSetRefresh_ = false;
    render();
}

void CardPricesPanel::clear() {
    copyId_.clear();
    cardRef_ = CardReference{};
    speciesName_.clear();
    preferredFinish_.clear();
    externalCardId_.clear();
    copyRemoved_ = false;
    fetching_ = false;
    awaitingSets_ = false;
    triedSetRefresh_ = false;
    render();
}

bool CardPricesPanel::canResolve() const {
    // A soft-Removed copy is frozen history: never spend a resolve + fetch on a discarded
    // card. Otherwise a set (name or printed code) plus a collector number is all tcgdex needs
    // to address the card by set+number.
    if (copyRemoved_ || copyId_.empty()) {
        return false;
    }
    const bool hasSet = !cardRef_.setName.empty() || !cardRef_.expansionCode.empty();
    const bool hasNumber = !cardRef_.collectorNumber.empty();
    return hasSet && hasNumber;
}

void CardPricesPanel::resetToMessage(const QString& text) {
    headline_->hide();
    infoButton_->hide();
    fetchButton_->hide();
    clearButton_->hide();  // re-shown by render() only once the card has been fetched
    if (text.isEmpty()) {
        status_->hide();
    } else {
        status_->setText(text);
        status_->show();
    }
}

void CardPricesPanel::showFetchAffordance(const QString& message, const QString& buttonText) {
    resetToMessage(message);  // hides the button…
    fetchButton_->setText(buttonText);
    fetchButton_->show();  // …so re-show + enable it here
    fetchButton_->setEnabled(true);
}

void CardPricesPanel::reportFetchFailure(const QString& message) {
    fetching_ = false;
    fetchButton_->setEnabled(true);
    // A failed fetch changes no cache, so any prices already shown are still valid and still
    // clearable — re-show Clear (onFetchClicked hid it for the fetch's duration).
    if (headline_->isVisible()) {
        clearButton_->show();
    }
    status_->setText(message);
    status_->show();
}

void CardPricesPanel::render() {
    if (fetching_) {
        return;  // onFetchClicked owns the UI until the reply lands
    }

    if (copyId_.empty()) {
        resetToMessage(QString());  // nothing selected
        return;
    }
    if (copyRemoved_) {
        resetToMessage(QString());  // frozen history — no price affordance at all
        return;
    }

    const bool resolvable = canResolve();
    if (externalCardId_.isEmpty() && !resolvable) {
        // Nothing to look up: too little data to resolve a tcgdex card. Point to Edit to
        // complete it. (Linking itself is never named as a user action.)
        resetToMessage(QStringLiteral("Add this card's set and collector number (via “Edit "
                                      "card…”) to look up its market prices."));
        return;
    }
    if (externalCardId_.isEmpty()) {
        // Resolvable but never linked/fetched: present exactly like a linked-unfetched card.
        // The first fetch resolves and persists the link invisibly (onFetchClicked).
        showFetchAffordance(QStringLiteral("Prices not fetched yet."),
                            QStringLiteral("Fetch prices"));
        return;
    }

    // Read the cache defensively: render() runs from a selection-change slot, and a DB
    // read can throw (e.g. a second app instance holding the SQLite file lock, which
    // CLAUDE.md warns about). An exception escaping a Qt slot calls std::terminate — so
    // degrade to a message rather than crash the app on select. One combined read for both
    // the prices and the fetch stamp (see cachedPrices), so a selection consults the cache
    // through one call and one error path.
    std::vector<CardPrice> cached;
    std::optional<Timestamp> fetchedAt;
    std::vector<std::string> suppressed;
    try {
        CardPriceLookupService::CachedPrices snapshot = lookup_.cachedPrices(externalCardId_);
        cached = std::move(snapshot.prices);
        fetchedAt = snapshot.fetchedAt;
        suppressed = lookup_.suppressedVendors(externalCardId_);
    } catch (const std::exception&) {
        resetToMessage(QStringLiteral("Couldn't read the stored prices."));
        return;
    }

    if (cached.empty()) {
        // A message with a live Fetch/Refresh button so the user can (re)fetch.
        showFetchAffordance(fetchedAt ? QStringLiteral("No market prices found for this card.")
                                      : QStringLiteral("Prices not fetched yet."),
                            fetchedAt ? QStringLiteral("Refresh")
                                      : QStringLiteral("Fetch prices"));
        // Fetched-but-empty (has a stamp): offer Clear to drop the stamp and re-offer Fetch.
        // Never-fetched has nothing to clear.
        if (fetchedAt) {
            clearButton_->show();
        }
        return;
    }

    // The headline names each vendor once, the name itself linking to a marketplace search —
    // one representative figure per source (vendorBest), so the full per-metric spread is
    // left to the marketplace rather than shown as a raw cache table. (vendorBest never
    // picks the TCGplayer "high" outlier, so it can't surface here.)
    const QString headline = headlineHtml(marketSearchTerm(cardRef_, speciesName_), cached,
                                          suppressed, preferredFinish_, /*withHideLinks=*/true);
    headline_->setText(headline.isEmpty() ? QStringLiteral("Market prices") : headline);
    headline_->show();

    // "as of" is the newest vendor date across the rows; also carry when WE fetched. Both
    // move onto the ⓘ tooltip (no separate visible line), so a glance stays uncluttered.
    Timestamp newest = cached.front().observedAt;
    for (const CardPrice& p : cached) {
        newest = std::max(newest, p.observedAt);
    }
    infoButton_->setToolTip(priceInfoWithFreshness(
        priceDateOf(newest), fetchedAt ? priceDateOf(*fetchedAt) : QString()));
    infoButton_->show();
    status_->hide();  // freshness lives on the ⓘ tooltip now, not a visible line
    fetchButton_->show();
    fetchButton_->setEnabled(true);
    fetchButton_->setText(QStringLiteral("Refresh"));
    clearButton_->show();  // has prices → offer to clear them
}

void CardPricesPanel::onFetchClicked() {
    if (fetching_) {
        return;  // a fetch is already in flight
    }
    if (canResolve()) {
        // Resolve the tcgdex card id from the copy's set+number, then fetch. This both links
        // an unlinked copy and re-resolves one still on a pre-tcgdex id — invisibly.
        fetching_ = true;
        triedSetRefresh_ = false;  // this explicit Fetch gets one set-refresh retry
        fetchButton_->setEnabled(false);
        clearButton_->hide();  // no Clear mid-fetch — it's guarded, so it would be a dead click
        status_->setText(QStringLiteral("Looking up this card…"));
        status_->show();
        if (lookup_.tcgdexSetsReady()) {
            resolveAndFetch();
        } else {
            awaitingSets_ = true;
            lookup_.ensureTcgdexSets();  // tcgdexSetsResolved → resolveAndFetch()
        }
        return;
    }
    if (!externalCardId_.isEmpty()) {
        // Not resolvable from what the copy records, but it already carries an id — fetch it
        // directly. The button is an explicit user request for the latest, so hit the wire.
        fetching_ = true;
        fetchButton_->setEnabled(false);
        clearButton_->hide();
        status_->setText(QStringLiteral("Fetching prices…"));
        status_->show();
        lookup_.fetch(externalCardId_);
    }
}

void CardPricesPanel::onClearClicked() {
    if (fetching_ || externalCardId_.isEmpty()) {
        return;  // nothing cached to clear (the button is hidden in those states anyway)
    }
    // Wipe the cache for this card; clearPrices emits pricesReady, whose handler re-renders us
    // (and any other view showing the card) into the not-fetched state.
    lookup_.clearPrices(externalCardId_);
}

void CardPricesPanel::onHeadlineLinkActivated(const QString& href) {
    // A marketplace search link — open it in the user's browser (we turned openExternalLinks
    // off to intercept the "action:" links, so http(s) must be opened by hand here).
    if (href.startsWith(QLatin1String("http"))) {
        QDesktopServices::openUrl(QUrl(href));
        return;
    }
    // In-app vendor suppression: "action:hide:<vendor>" / "action:show:<vendor>". Both persist
    // via the lookup service, which emits pricesReady → this panel (and any other showing the
    // card) re-renders with the vendor gone / back. A suppression survives Refresh; only Clear
    // drops it.
    if (externalCardId_.isEmpty()) {
        return;
    }
    bool hide = false;
    QString vendor;
    if (href.startsWith(QLatin1String("action:hide:"))) {
        hide = true;
        vendor = href.mid(QStringLiteral("action:hide:").size());
    } else if (href.startsWith(QLatin1String("action:show:"))) {
        vendor = href.mid(QStringLiteral("action:show:").size());
    } else {
        return;
    }
    // Defer the toggle: suppressVendor/unsuppressVendor SYNCHRONOUSLY emit pricesReady, whose
    // handler re-renders headline_ via setText — but we are inside headline_'s OWN linkActivated
    // emission, so rebuilding that label mid-signal is re-entrant. Queue it (bound to a snapshot
    // of the current card) to run once the signal has unwound. `this` as the invoke context
    // means a panel destroyed before then simply drops the call.
    const QString cardId = externalCardId_;
    QMetaObject::invokeMethod(
        this,
        [this, cardId, vendor, hide]() {
            if (hide) {
                lookup_.suppressVendor(cardId, vendor);
            } else {
                lookup_.unsuppressVendor(cardId, vendor);
            }
        },
        Qt::QueuedConnection);
}

void CardPricesPanel::resolveAndFetch() {
    const std::optional<QString> id = lookup_.resolveTcgdexId(cardRef_);
    if (!id) {
        // Couldn't identify the set from a (possibly cached) table. The set may simply be newer
        // than our cached copy, so force ONE fresh /v2/en/sets fetch and retry before giving up.
        if (!triedSetRefresh_) {
            triedSetRefresh_ = true;
            awaitingSets_ = true;
            lookup_.ensureTcgdexSets(/*forceRefresh=*/true);  // tcgdexSetsResolved → retry
            return;
        }
        // Still unidentifiable after a fresh table. Do NOT fall back to fetching
        // externalCardId_: a legitimately linked copy's tcgdex id would have re-resolved here,
        // so a non-empty id at this point is a stale/foreign key (e.g. a pre-tcgdex pokemontcg
        // id) whose GET would 404 with a misleading error. Report the accurate guidance.
        reportFetchFailure(QStringLiteral("Couldn't identify this card for pricing — check its "
                                          "set and collector number in “Edit card…”."));
        return;
    }

    if (*id != externalCardId_) {
        // Persist the resolved link (new, or migrated off a pre-tcgdex id) before fetching, so
        // a re-selection sees the copy as linked and the cache keys to the tcgdex id.
        try {
            copies_.linkCatalogCard(copyId_, id->toStdString());
        } catch (const std::exception& e) {
            reportFetchFailure(QStringLiteral("Couldn't look up this card right now."));
            QMessageBox::warning(this, tr("Pokedex TCG"),
                                 tr("Could not link this card:\n%1").arg(QString::fromUtf8(e.what())));
            return;
        }
        externalCardId_ = *id;
        Q_EMIT cardLinked(QString::fromStdString(copyId_), externalCardId_);
    }
    lookup_.fetch(externalCardId_);  // pricesReady → fetching_=false, render()
}

}  // namespace pokedex
