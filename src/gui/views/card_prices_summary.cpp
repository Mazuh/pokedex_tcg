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

    // The single affordance for every price action — fetch, refresh, clear, hide/restore, and
    // whatever we add next — all of which live on the dedicated page this opens.
    manageButton_ = new QPushButton(tr("Manage prices"), this);
    auto* buttonRow = new QHBoxLayout;
    buttonRow->setContentsMargins(0, 0, 0, 0);
    buttonRow->addWidget(manageButton_);
    buttonRow->addStretch(1);

    layout->addLayout(headlineRow);
    layout->addWidget(status_);
    layout->addLayout(buttonRow);

    connect(manageButton_, &QPushButton::clicked, this, [this]() {
        if (!copyId_.empty()) {
            Q_EMIT managePricesRequested(QString::fromStdString(copyId_));
        }
    });

    // Re-render when a fetch / clear / suppression (ours, or another view's — the lookup service
    // is app-wide) changes our card's cache. A failed fetch changes no cache, so ignore
    // pricesFailed: our read-only display stays valid.
    connect(&lookup_, &CardPriceLookupService::pricesReady, this, [this](const QString& id) {
        if (id == externalCardId_) {
            render();
        }
    });
}

void CardPricesSummary::showCopy(const CardCopy& copy) {
    copyId_ = copy.id;
    cardRef_ = copy.cardRef;
    speciesName_ = copy.pokemonDexNum ? speciesName(*copy.pokemonDexNum) : QString();
    preferredFinish_ = finishForFoil(copy.foil);
    externalCardId_ = QString::fromStdString(copy.externalCardId);
    copyRemoved_ = copy.ownership == CardOwnership::Removed;
    render();
}

void CardPricesSummary::clear() {
    copyId_.clear();
    cardRef_ = CardReference{};
    speciesName_.clear();
    preferredFinish_.clear();
    externalCardId_.clear();
    copyRemoved_ = false;
    render();
}

void CardPricesSummary::render() {
    // A soft-Removed copy is frozen history and nothing is selected → no price block at all.
    if (copyId_.empty() || copyRemoved_) {
        headline_->hide();
        infoButton_->hide();
        status_->hide();
        manageButton_->hide();
        return;
    }

    // Every non-removed copy gets the "Manage prices" affordance — that is where an unlinked or
    // unfetched copy is fetched, and where anything is changed. The headline above it just shows
    // whatever is already cached.
    manageButton_->show();

    if (externalCardId_.isEmpty()) {
        // Not linked yet: nothing cached to show. Point the user at the button.
        headline_->hide();
        infoButton_->hide();
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
        status_->setText(tr("Couldn't read the stored prices."));
        status_->show();
        return;
    }

    // The headline names each vendor once (suppressed vendors filtered out — a hidden vendor
    // never surfaces here), with no hide/restore affordance (withHideLinks=false): those are the
    // page's job. Empty when the card carries no usable, non-hidden figure.
    const QString headline = headlineHtml(marketSearchTerm(cardRef_, speciesName_), cached,
                                          suppressed, preferredFinish_, /*withHideLinks=*/false);
    if (headline.isEmpty()) {
        // Fetched but nothing to show (no prices found, or every vendor hidden). Keep it to a
        // muted line; the details are on the page.
        headline_->hide();
        infoButton_->hide();
        status_->setText(fetchedAt ? tr("No market prices to show.")
                                   : tr("Not fetched yet."));
        status_->show();
        return;
    }

    headline_->setText(headline);
    headline_->show();

    // "as of" is the newest vendor date across the rows; also carry when WE fetched. Both live on
    // the ⓘ tooltip, mirroring the panel.
    Timestamp newest = cached.front().observedAt;
    for (const CardPrice& p : cached) {
        newest = std::max(newest, p.observedAt);
    }
    infoButton_->setToolTip(priceInfoWithFreshness(
        priceDateOf(newest), fetchedAt ? priceDateOf(*fetchedAt) : QString()));
    infoButton_->show();
    status_->hide();
}

}  // namespace pokedex
