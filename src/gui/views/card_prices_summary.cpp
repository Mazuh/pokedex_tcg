#include "gui/views/card_prices_summary.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>
#include <exception>
#include <optional>
#include <string>
#include <vector>

#include "core/app/card_price_dto.h"
#include "core/domain/card_copy.h"
#include "core/domain/card_ownership.h"
#include "gui/services/card_price_lookup_service.h"
#include "gui/views/card_copy_labels.h"  // speciesName(dexNumber)
#include "gui/views/price_headline.h"
#include "gui/views/price_labels.h"

namespace pokedex {

CardPricesSummary::CardPricesSummary(CardPriceLookupService& lookup, QWidget* parent)
    : QWidget(parent), lookup_(lookup) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    // The headline carries the per-vendor figures with the vendor name linking to a
    // marketplace search (rich text). Read-only: no "action:" links here (hide/restore live on
    // the management page), so the label may open its http links itself.
    headline_ = new QLabel(this);
    headline_->setWordWrap(true);
    headline_->setTextFormat(Qt::RichText);
    headline_->setOpenExternalLinks(true);
    headline_->setTextInteractionFlags(Qt::TextBrowserInteraction);
    headline_->setStyleSheet(QStringLiteral("font-weight: 600;"));

    // "ⓘ" popover explaining the metrics + freshness — the shared idiom (makeInfoButton).
    infoButton_ = makeInfoButton(this);

    auto* headlineRow = new QHBoxLayout;
    headlineRow->setContentsMargins(0, 0, 0, 0);
    headlineRow->addWidget(headline_, /*stretch=*/1);
    headlineRow->addWidget(infoButton_, 0, Qt::AlignTop | Qt::AlignRight);

    status_ = new QLabel(this);
    status_->setWordWrap(true);
    status_->setStyleSheet(QStringLiteral("color: gray;"));

    // Inline "Fetch"/"Refresh" — the one quick action kept in the summary (a plain re-fetch of an
    // already-linked card's prices). Everything heavier (Clear, hide/restore, first-time linking,
    // and future actions) lives behind "Manage prices", which opens the dedicated page.
    fetchButton_ = new QPushButton(this);
    manageButton_ = new QPushButton(tr("Manage prices"), this);
    auto* buttonRow = new QHBoxLayout;
    buttonRow->setContentsMargins(0, 0, 0, 0);
    buttonRow->addWidget(fetchButton_);
    buttonRow->addWidget(manageButton_);
    buttonRow->addStretch(1);

    layout->addLayout(headlineRow);
    layout->addWidget(status_);
    layout->addLayout(buttonRow);

    connect(fetchButton_, &QPushButton::clicked, this, &CardPricesSummary::onFetchClicked);
    connect(manageButton_, &QPushButton::clicked, this, [this]() {
        if (!copyId_.empty()) {
            Q_EMIT managePricesRequested(QString::fromStdString(copyId_));
        }
    });

    // Re-render when a fetch / clear / suppression (ours, or another view's — the lookup service
    // is app-wide) changes our card's cache; clear any prior in-flight/error state for our card.
    connect(&lookup_, &CardPriceLookupService::pricesReady, this, [this](const QString& id) {
        if (id == externalCardId_) {
            fetching_ = false;
            fetchError_.clear();
            render();
        }
    });
    // Only OUR inline fetch surfaces a failure (a background fetch by another view, which we
    // didn't start, leaves our cached display untouched — a failed fetch changes no cache).
    connect(&lookup_, &CardPriceLookupService::pricesFailed, this, [this](const QString& id) {
        if (id == externalCardId_ && fetching_) {
            fetching_ = false;
            fetchError_ = tr("Couldn't fetch prices right now.");
            render();
        }
    });
}

void CardPricesSummary::onFetchClicked() {
    // Only ever a re-fetch of an already-linked card (the button is shown only then). No resolve,
    // no linkCatalogCard — so the summary needs no CardCopyService and never mutates the copy.
    if (fetching_ || externalCardId_.isEmpty()) {
        return;
    }
    fetching_ = true;
    fetchError_.clear();
    render();  // reflect the in-flight state (disabled button + "Fetching…")
    lookup_.fetch(externalCardId_);
}

void CardPricesSummary::showCopy(const CardCopy& copy) {
    copyId_ = copy.id;
    cardRef_ = copy.cardRef;
    speciesName_ = copy.pokemonDexNum ? speciesName(*copy.pokemonDexNum) : QString();
    preferredFinish_ = finishForFoil(copy.foil);
    externalCardId_ = QString::fromStdString(copy.externalCardId);
    copyRemoved_ = copy.ownership == CardOwnership::Removed;
    fetching_ = false;
    fetchError_.clear();
    render();
}

void CardPricesSummary::clear() {
    copyId_.clear();
    cardRef_ = CardReference{};
    speciesName_.clear();
    preferredFinish_.clear();
    externalCardId_.clear();
    copyRemoved_ = false;
    fetching_ = false;
    fetchError_.clear();
    render();
}

void CardPricesSummary::render() {
    // A soft-Removed copy is frozen history and nothing is selected → no price block at all.
    if (copyId_.empty() || copyRemoved_) {
        headline_->hide();
        infoButton_->hide();
        status_->hide();
        manageButton_->hide();
        fetchButton_->hide();
        return;
    }

    // Every non-removed copy gets the "Manage prices" affordance — that is where an unlinked copy
    // is fetched (its first fetch resolves + links it) and where anything is changed.
    manageButton_->show();

    if (externalCardId_.isEmpty()) {
        // Not linked yet: no inline re-fetch (there's nothing linked to fetch), only Manage.
        headline_->hide();
        infoButton_->hide();
        fetchButton_->hide();
        status_->setText(tr("Not fetched yet."));
        status_->show();
        return;
    }

    // Read the cache defensively: render() runs from a selection-change slot, and a DB read can
    // throw (e.g. a second app instance holding the SQLite file lock). An exception escaping a
    // Qt slot calls std::terminate — degrade to a message rather than crash.
    std::vector<CardPrice> cached;
    std::optional<Timestamp> fetchedAt;
    std::vector<std::string> suppressed;
    try {
        CardPriceLookupService::CachedPrices snapshot = lookup_.cachedPrices(externalCardId_);
        cached = std::move(snapshot.prices);
        fetchedAt = snapshot.fetchedAt;
        suppressed = lookup_.suppressedVendors(externalCardId_);
    } catch (const std::exception&) {
        headline_->hide();
        infoButton_->hide();
        fetchButton_->hide();
        status_->setText(tr("Couldn't read the stored prices."));
        status_->show();
        return;
    }

    // The headline names each vendor once (suppressed vendors filtered out — a hidden vendor
    // never surfaces here), with no hide/restore affordance (withHideLinks=false): those are the
    // page's job. Empty when the card carries no usable, non-hidden figure.
    const QString headline = headlineHtml(marketSearchTerm(cardRef_, speciesName_), cached,
                                          suppressed, preferredFinish_, /*withHideLinks=*/false);
    const bool hasHeadline = !headline.isEmpty();
    if (hasHeadline) {
        headline_->setText(headline);
        headline_->show();
        // "as of" is the newest vendor date across the rows; also carry when WE fetched. Both
        // live on the ⓘ tooltip, mirroring the panel.
        Timestamp newest = cached.front().observedAt;
        for (const CardPrice& p : cached) {
            newest = std::max(newest, p.observedAt);
        }
        infoButton_->setToolTip(priceInfoWithFreshness(
            priceDateOf(newest), fetchedAt ? priceDateOf(*fetchedAt) : QString()));
        infoButton_->show();
        // Inline "Refresh" ONLY when there are figures to refresh — a plain re-fetch of the
        // stored id (disabled mid-fetch). Deliberately NOT offered when nothing is shown (never
        // fetched, fetched-empty, or a stale pre-tcgdex id that yields no figures): the inline
        // path can't re-resolve a legacy id, so those go through "Manage prices", whose fetch
        // (CardPricesPanel::resolveAndFetch) re-resolves it. Keeps the naive re-fetch off the
        // states where it would dead-end.
        fetchButton_->setText(tr("Refresh"));
        fetchButton_->setEnabled(!fetching_);
        fetchButton_->show();
    } else {
        headline_->hide();
        infoButton_->hide();
        fetchButton_->hide();
    }

    // Status precedence: an in-flight fetch, then a failed-fetch note, then the empty-cache hint
    // (a card with a visible headline and no fetch activity needs no status line).
    QString statusText;
    if (fetching_) {
        statusText = tr("Fetching prices…");
    } else if (!fetchError_.isEmpty()) {
        statusText = fetchError_;
    } else if (!hasHeadline) {
        statusText = fetchedAt ? tr("No market prices to show.") : tr("Not fetched yet.");
    }
    if (statusText.isEmpty()) {
        status_->hide();
    } else {
        status_->setText(statusText);
        status_->show();
    }
}

}  // namespace pokedex
