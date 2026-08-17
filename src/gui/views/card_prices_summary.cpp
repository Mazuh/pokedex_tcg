#include "gui/views/card_prices_summary.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

#include <exception>
#include <optional>
#include <string>
#include <vector>

#include "core/app/card_price_dto.h"
#include "core/domain/card_copy.h"
#include "core/domain/card_ownership.h"
#include "gui/services/card_price_lookup_service.h"
#include "gui/views/card_copy_labels.h"  // speciesName(dexNumber)
#include "gui/views/card_price_fetch_controller.h"
#include "gui/views/info_button.h"  // makeInfoButton — the ⓘ that opens the explainer
#include "gui/views/price_headline.h"
#include "gui/views/price_labels.h"

namespace pokedex {

CardPricesSummary::CardPricesSummary(CardPriceLookupService& lookup, CardCopyService& copies,
                                     QWidget* parent)
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

    // "ⓘ" explaining the metrics + freshness — the shared idiom (info_button.h), opening the
    // modal InfoDialog. Its body is rebuilt per render (it names this card's dates), so the
    // button asks for it at click time rather than holding a snapshot from construction.
    infoHtml_ = priceInfoHtml();
    infoButton_ = makeInfoButton(this, tr("What these prices mean"),
                                 [this] { return infoHtml_; });

    auto* headlineRow = new QHBoxLayout;
    headlineRow->setContentsMargins(0, 0, 0, 0);
    headlineRow->addWidget(headline_, /*stretch=*/1);
    headlineRow->addWidget(infoButton_, 0, Qt::AlignTop | Qt::AlignRight);

    status_ = new QLabel(this);
    status_->setWordWrap(true);
    status_->setStyleSheet(QStringLiteral("color: gray;"));

    // Inline "Fetch"/"Refresh" — the one quick action kept in the summary. It drives the shared
    // controller, so it resolves + links an unlinked copy and re-resolves a legacy id just like
    // the management page (never a dead-end). Everything heavier (Clear, hide/restore, future
    // actions) lives behind "Manage prices".
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

    // The shared fetch/resolve/link state machine; we render its progress. It is the single
    // subscriber to the lookup service's per-card signals, so this widget talks only to it.
    fetcher_ = new CardPriceFetchController(lookup_, copies, this);
    connect(fetchButton_, &QPushButton::clicked, this, &CardPricesSummary::onFetchClicked);
    connect(manageButton_, &QPushButton::clicked, this, [this]() {
        if (!copyId_.empty()) {
            Q_EMIT managePricesRequested(QString::fromStdString(copyId_));
        }
    });
    connect(fetcher_, &CardPriceFetchController::statusMessage, this,
            [this](const QString& text) {
                fetchStatus_ = text;
                render();
            });
    // pricesChanged fires on our fetch landing OR another view touching this card — re-read.
    connect(fetcher_, &CardPriceFetchController::pricesChanged, this, [this]() {
        fetchStatus_.clear();
        fetchError_.clear();
        render();
    });
    connect(fetcher_, &CardPriceFetchController::fetchFailed, this,
            [this](const QString& message) {
                fetchStatus_.clear();
                fetchError_ = message;
                render();
            });
    connect(fetcher_, &CardPriceFetchController::cardLinked, this,
            [this](const QString& copyId, const QString& externalCardId) {
                externalCardId_ = externalCardId;  // keep render()'s cache read in sync
                Q_EMIT copyLinked(copyId, externalCardId);
                render();
            });
}

void CardPricesSummary::showCopy(const CardCopy& copy) {
    copyId_ = copy.id;
    cardRef_ = copy.cardRef;
    speciesName_ = copy.pokemonDexNum ? speciesName(*copy.pokemonDexNum) : QString();
    preferredFinish_ = finishForFoil(copy.foil);
    externalCardId_ = QString::fromStdString(copy.externalCardId);
    copyRemoved_ = copy.ownership == CardOwnership::Removed;
    fetchStatus_.clear();
    fetchError_.clear();
    fetcher_->setCopy(copy);
    render();
}

void CardPricesSummary::clear() {
    copyId_.clear();
    cardRef_ = CardReference{};
    speciesName_.clear();
    preferredFinish_.clear();
    externalCardId_.clear();
    copyRemoved_ = false;
    fetchStatus_.clear();
    fetchError_.clear();
    fetcher_->clearCopy();
    render();
}

void CardPricesSummary::onFetchClicked() {
    fetchError_.clear();
    fetcher_->fetch();  // emits statusMessage → render(); resolves/links as needed
}

void CardPricesSummary::render() {
    // Drop any freshness carried over from the last card up front — it names dates, and the
    // has-headline branch below is the only one entitled to state them. Doing it here covers
    // every hide path at once, so a ⓘ can never quote the previously shown copy.
    infoHtml_ = priceInfoHtml();

    // A soft-Removed copy is frozen history and nothing is selected → no price block at all.
    if (copyId_.empty() || copyRemoved_) {
        headline_->hide();
        infoButton_->hide();
        status_->hide();
        manageButton_->hide();
        fetchButton_->hide();
        return;
    }

    // Every non-removed copy gets "Manage prices" — its fuller controls (and, for an unlinked
    // copy, its first fetch's resolve + link).
    manageButton_->show();

    const bool fetching = fetcher_->isFetching();
    const bool canFetch = fetcher_->canFetch();

    // Read the cache defensively (only meaningful once linked): render() runs from a
    // selection-change slot, and a DB read can throw (e.g. a second app instance holding the
    // SQLite file lock). An exception escaping a Qt slot calls std::terminate — degrade instead.
    std::vector<CardPrice> cached;
    std::optional<Timestamp> fetchedAt;
    std::vector<std::string> suppressed;
    if (!externalCardId_.isEmpty()) {
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
    }

    // The headline names each vendor once (suppressed vendors filtered out — a hidden vendor
    // never surfaces here), with no hide/restore affordance (withHideLinks=false): those are the
    // page's job. Empty when the card carries no usable, non-hidden figure (or isn't linked).
    const QString headline =
        externalCardId_.isEmpty()
            ? QString()
            : headlineHtml(marketSearchTerm(cardRef_, speciesName_), cached, suppressed,
                           preferredFinish_, /*withHideLinks=*/false);
    const bool hasHeadline = !headline.isEmpty();
    if (hasHeadline) {
        headline_->setText(headline);
        headline_->show();
        // "as of" is the newest vendor date across the rows; also carry when WE fetched. Both
        // live in the ⓘ dialog, mirroring the panel (shared newestObservedAt).
        infoHtml_ = priceInfoWithFreshness(priceDateOf(newestObservedAt(cached)),
                                           fetchedAt ? priceDateOf(*fetchedAt) : QString());
        infoButton_->show();
    } else {
        headline_->hide();
        infoButton_->hide();
    }

    // The inline button whenever a fetch is possible (resolvable, or already linked): "Refresh"
    // once fetched, "Fetch" before; disabled mid-fetch. The controller re-resolves a legacy id,
    // so this never dead-ends. Absent only when there's nothing to fetch (no set/number).
    if (canFetch) {
        fetchButton_->setText(fetchedAt ? tr("Refresh") : tr("Fetch"));
        fetchButton_->setEnabled(!fetching);
        fetchButton_->show();
    } else {
        fetchButton_->hide();
    }

    // Status precedence: an in-flight fetch, then a failed-fetch note, then a hint (unresolvable,
    // or nothing fetched yet) — a card with a visible headline and no fetch activity needs none.
    QString statusText;
    if (fetching) {
        statusText = fetchStatus_.isEmpty() ? tr("Fetching prices…") : fetchStatus_;
    } else if (!fetchError_.isEmpty()) {
        statusText = fetchError_;
    } else if (!canFetch) {
        statusText = tr("Add this card's set and collector number (via “Edit card…”) to look up "
                        "its market prices.");
    } else if (!hasHeadline) {
        statusText = fetchedAt ? tr("No market prices to show.") : tr("Prices not fetched yet.");
    }
    if (statusText.isEmpty()) {
        status_->hide();
    } else {
        status_->setText(statusText);
        status_->show();
    }
}

}  // namespace pokedex
