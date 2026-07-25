#include "gui/views/card_prices_panel.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QToolTip>
#include <QVBoxLayout>

#include <algorithm>
#include <exception>
#include <optional>
#include <string>
#include <vector>

#include "core/app/card_catalog_dto.h"
#include "core/app/card_copy_service.h"
#include "core/app/card_price_dto.h"
#include "core/domain/card_copy.h"
#include "core/storage/codecs.h"
#include "gui/services/card_price_lookup_service.h"
#include "gui/services/card_search_service.h"
#include "gui/views/price_labels.h"
#include "gui/views/sortable_table.h"

namespace pokedex {

namespace {

// ISO-8601 UTC (from the timestamp codec) trimmed to its YYYY-MM-DD date — the day
// precision the vendor prices actually carry.
QString dateOf(Timestamp when) {
    return QString::fromStdString(timestampToIso(when)).left(10);
}

// The bare printing number from a collector number, lowercased for comparison:
// "125/197" and "125" both reduce to "125". Used to single one printing out when a
// set holds several of the same species during the auto-link resolve.
QString collectorKey(const std::string& raw) {
    QString value = QString::fromStdString(raw).trimmed();
    const int slash = value.indexOf(QLatin1Char('/'));
    if (slash >= 0) {
        value = value.left(slash);
    }
    return value.trimmed().toLower();
}

// The "ⓘ" popover: what each figure means, so a lone "Cardmarket €26" isn't a mystery.
const QString& priceInfoHtml() {
    static const QString kHtml = QStringLiteral(
        "<p><b>What these prices mean</b><br>Aggregated market estimates from two "
        "marketplaces, refreshed roughly daily — treat them as rough guidance, not a "
        "fixed value.</p>"
        "<p><b>The headline</b> shows one figure per source:<br>"
        "• <b>Cardmarket</b> (EUR) — its <i>trend price</i>: an estimate of the current "
        "going rate from recent sales and listings (not a min, median, or max).<br>"
        "• <b>TCGplayer</b> (USD) — its <i>market</i> price: the current market value; when "
        "a card has several finishes the highest such value is shown.</p>"
        "<p><b>Show all prices</b> expands the full spread — Cardmarket <i>low</i> "
        "(cheapest listing), <i>average sell</i> (mean of real sales), and 1/7/30-day "
        "averages; TCGplayer <i>low</i>/<i>mid</i>/<i>market</i>/<i>direct-low</i> per "
        "finish.</p>"
        "<p>The TCGplayer <i>high</i> (a single top listing) is deliberately hidden — it "
        "is routinely an unrealistic outlier.</p>");
    return kHtml;
}

// The listing links for the vendors present in `prices`, as rich text opening the real
// marketplace page. pokemontcg.io serves a stable per-vendor redirect keyed by the card
// id (verified to 302 → the TCGplayer product page / the exact Cardmarket single), so
// the URL is built from the id with no extra request or stored field.
QString listingLinksHtml(const std::string& externalCardId, const std::vector<CardPrice>& prices) {
    bool hasTcg = false;
    bool hasCm = false;
    for (const CardPrice& p : prices) {
        hasTcg = hasTcg || p.provenance == kTcgplayerProvenance;
        hasCm = hasCm || p.provenance == kCardmarketProvenance;
    }
    const QString id = QString::fromStdString(externalCardId);
    QStringList parts;
    if (hasTcg) {
        parts << QStringLiteral(
                     "<a href=\"https://prices.pokemontcg.io/tcgplayer/%1\">TCGplayer ↗</a>")
                     .arg(id);
    }
    if (hasCm) {
        parts << QStringLiteral(
                     "<a href=\"https://prices.pokemontcg.io/cardmarket/%1\">Cardmarket ↗</a>")
                     .arg(id);
    }
    if (parts.isEmpty()) {
        return {};
    }
    return QStringLiteral("Listings: ") + parts.join(QStringLiteral(" · "));
}

}  // namespace

CardPricesPanel::CardPricesPanel(CardPriceLookupService& lookup, CardSearchService& search,
                                 CardCopyService& copies, QWidget* parent)
    : QWidget(parent), lookup_(lookup), search_(search), copies_(copies) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    headline_ = new QLabel(this);
    headline_->setWordWrap(true);
    headline_->setStyleSheet(QStringLiteral("font-weight: 600;"));

    // "ⓘ" popover explaining the metrics — the same idiom the card-attribute pickers use.
    infoButton_ = new QToolButton(this);
    infoButton_->setText(QStringLiteral("ⓘ"));
    infoButton_->setAutoRaise(true);
    infoButton_->setFocusPolicy(Qt::NoFocus);
    infoButton_->setCursor(Qt::WhatsThisCursor);
    infoButton_->setToolTip(priceInfoHtml());
    infoButton_->setAccessibleName(tr("What these prices mean"));
    connect(infoButton_, &QToolButton::clicked, this, [this]() {
        QToolTip::showText(infoButton_->mapToGlobal(QPoint(0, infoButton_->height())),
                           priceInfoHtml(), infoButton_);
    });

    auto* headlineRow = new QHBoxLayout;
    headlineRow->setContentsMargins(0, 0, 0, 0);
    headlineRow->addWidget(headline_);
    headlineRow->addWidget(infoButton_);
    headlineRow->addStretch(1);

    status_ = new QLabel(this);
    status_->setWordWrap(true);
    status_->setStyleSheet(QStringLiteral("color: gray;"));

    // Listing links (rich text) that open the real marketplace pages in the browser.
    links_ = new QLabel(this);
    links_->setTextFormat(Qt::RichText);
    links_->setOpenExternalLinks(true);
    links_->setWordWrap(true);

    auto* buttonRow = new QHBoxLayout;
    fetchButton_ = new QPushButton(this);
    toggle_ = new QToolButton(this);
    toggle_->setCheckable(true);
    toggle_->setText(QStringLiteral("Show all prices ▸"));
    buttonRow->addWidget(fetchButton_);
    buttonRow->addWidget(toggle_);
    buttonRow->addStretch(1);

    table_ = new QTableWidget(this);
    table_->setColumnCount(5);
    table_->setHorizontalHeaderLabels(
        {QStringLiteral("Source"), QStringLiteral("Variant"), QStringLiteral("Metric"),
         QStringLiteral("Price"), QStringLiteral("As of")});
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->verticalHeader()->setVisible(false);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionMode(QAbstractItemView::NoSelection);
    table_->setVisible(false);
    // Header-click sorting (the repo convention for every table): a click reorders the
    // cached rows_ in memory and refills — never Qt's setSortingEnabled.
    installHeaderSort(table_, [this](int column, Qt::SortOrder order) {
        sortColumn_ = column;
        sortOrder_ = order;
        repopulateTable();
    });

    layout->addLayout(headlineRow);
    layout->addWidget(status_);
    layout->addWidget(links_);
    layout->addLayout(buttonRow);
    layout->addWidget(table_);

    connect(fetchButton_, &QPushButton::clicked, this, &CardPricesPanel::onFetchClicked);
    connect(toggle_, &QToolButton::toggled, this, [this](bool on) {
        table_->setVisible(on);
        toggle_->setText(on ? QStringLiteral("Hide prices ▾") : QStringLiteral("Show all prices ▸"));
    });
    // Re-render when a fetch we (or another view) triggered lands for our card.
    connect(&lookup_, &CardPriceLookupService::pricesReady, this, [this](const QString& id) {
        if (id == externalCardId_) {
            fetching_ = false;
            render();
        }
    });
    connect(&lookup_, &CardPriceLookupService::pricesFailed, this, [this](const QString& id) {
        if (id == externalCardId_) {
            fetching_ = false;
            fetchButton_->setEnabled(true);
            // Neutral wording: the failure may be a busy/flaky API OR a card the
            // catalog no longer lists (a 404, which the transport fails fast) — don't
            // assert "try again" when a retry may never help.
            status_->setText(QStringLiteral("Couldn't fetch prices for this card right now."));
        }
    });

    // The Fetch button's invisible auto-link runs on the shared, debounced search
    // service; route only the reply whose request id we await (the service is app-wide).
    connect(&search_, &CardSearchService::printingsReady, this,
            [this](std::uint64_t requestId, int, const std::vector<CardCandidate>& cards) {
                if (!linking_ || requestId != pendingLinkRequest_) {
                    return;
                }
                linkWatchdog_->stop();
                onLinkResults(cards);
            });
    connect(&search_, &CardSearchService::printingsFailed, this,
            [this](std::uint64_t requestId, int) {
                if (!linking_ || requestId != pendingLinkRequest_) {
                    return;
                }
                linkWatchdog_->stop();
                linking_ = false;
                fetching_ = false;
                fetchButton_->setEnabled(true);
                status_->setText(QStringLiteral("Couldn't reach the card catalog. Please try "
                                                "again."));
                status_->show();
            });

    // Recover the button if the auto-link search reply never lands (a superseded request
    // on the shared service): 20s comfortably outlasts the search retry/backoff, so this
    // only trips on a truly lost reply, not a slow-but-arriving failure.
    linkWatchdog_ = new QTimer(this);
    linkWatchdog_->setSingleShot(true);
    connect(linkWatchdog_, &QTimer::timeout, this, [this]() {
        if (!linking_) {
            return;
        }
        linking_ = false;
        fetching_ = false;
        fetchButton_->setEnabled(true);
        status_->setText(QStringLiteral("Looking up this card timed out. Please try again."));
        status_->show();
    });
}

void CardPricesPanel::showCopy(const CardCopy& copy) {
    copyId_ = copy.id;
    cardRef_ = copy.cardRef;
    dexNumber_ = copy.pokemonDexNum;
    externalCardId_ = QString::fromStdString(copy.externalCardId);
    copyRemoved_ = copy.ownership == CardOwnership::Removed;
    fetching_ = false;
    linking_ = false;
    linkWatchdog_->stop();
    toggle_->setChecked(false);
    render();
}

void CardPricesPanel::setAutoLinkEnabled(bool enabled) {
    if (autoLinkEnabled_ == enabled) {
        return;
    }
    autoLinkEnabled_ = enabled;
    render();
}

void CardPricesPanel::clear() {
    copyId_.clear();
    cardRef_ = CardReference{};
    dexNumber_.reset();
    externalCardId_.clear();
    copyRemoved_ = false;
    fetching_ = false;
    linking_ = false;
    linkWatchdog_->stop();
    toggle_->setChecked(false);
    render();
}

bool CardPricesPanel::canAutoLink() const {
    // A soft-Removed copy is frozen history: never spend a search + link + fetch on a
    // discarded card. When auto-link is disabled (the Edit page), its finder does the
    // linking instead. Either way, only an unlinked copy with enough data qualifies.
    if (!autoLinkEnabled_ || copyRemoved_ || !externalCardId_.isEmpty() || copyId_.empty()) {
        return false;
    }
    const bool hasSet = !cardRef_.setName.empty() || !cardRef_.expansionCode.empty();
    const bool hasIdentity = dexNumber_.has_value() || !cardRef_.name.empty();
    return hasSet && hasIdentity;
}

void CardPricesPanel::resetToMessage(const QString& text) {
    rows_.clear();
    headline_->hide();
    infoButton_->hide();
    links_->hide();
    toggle_->hide();
    table_->hide();
    fetchButton_->hide();
    if (text.isEmpty()) {
        status_->hide();
    } else {
        status_->setText(text);
        status_->show();
    }
}

void CardPricesPanel::render() {
    if (fetching_) {
        return;  // onFetchClicked owns the UI until the reply lands
    }

    if (copyId_.empty()) {
        resetToMessage(QString());  // nothing selected
        return;
    }

    if (externalCardId_.isEmpty()) {
        if (canAutoLink()) {
            // Unlinked but resolvable: present exactly like a linked-unfetched card — a
            // Fetch button. The first fetch resolves and persists the link invisibly
            // (onFetchClicked). resetToMessage hides the rest; then reveal the button.
            resetToMessage(QStringLiteral("Prices not fetched yet."));
            fetchButton_->setText(QStringLiteral("Fetch prices"));
            fetchButton_->show();
            fetchButton_->setEnabled(true);
            return;
        }
        // Not resolvable. A Removed copy is frozen history — no price affordance at all.
        // On the Edit page (auto-link disabled) the finder does the linking. Otherwise
        // the copy simply lacks a set/name to resolve; point to Edit to complete it.
        // (Linking itself is never named as a user action.)
        if (copyRemoved_) {
            resetToMessage(QString());
        } else if (!autoLinkEnabled_) {
            resetToMessage(QStringLiteral("Pick this card in the finder above to look up "
                                          "its market prices."));
        } else {
            resetToMessage(QStringLiteral("Add this card's set and name (via “Edit card…”) "
                                          "to look up its market prices."));
        }
        return;
    }

    // Read the cache defensively: render() runs from a selection-change slot, and a DB
    // read can throw (e.g. a second app instance holding the SQLite file lock, which
    // CLAUDE.md warns about). An exception escaping a Qt slot calls std::terminate — so
    // degrade to a message rather than crash the app on select.
    std::vector<CardPrice> cached;
    std::optional<Timestamp> fetchedAt;
    try {
        cached = lookup_.cached(externalCardId_);
        fetchedAt = lookup_.fetchedAt(externalCardId_);
    } catch (const std::exception&) {
        resetToMessage(QStringLiteral("Couldn't read the stored prices."));
        return;
    }

    fetchButton_->show();
    fetchButton_->setEnabled(true);

    // Emptiness is decided on the RAW cache, before hiding the "high" outlier — else a
    // card whose only cached metric is TCGplayer "high" would falsely read as priceless.
    if (cached.empty()) {
        rows_.clear();
        headline_->hide();
        infoButton_->hide();
        links_->hide();
        toggle_->hide();
        table_->hide();
        if (fetchedAt) {
            status_->setText(QStringLiteral("No market prices found for this card."));
            fetchButton_->setText(QStringLiteral("Refresh"));
        } else {
            status_->setText(QStringLiteral("Prices not fetched yet."));
            fetchButton_->setText(QStringLiteral("Fetch prices"));
        }
        status_->show();
        return;
    }

    // Drop the TCGplayer "high" (a single top listing, a routinely unrealistic outlier)
    // — but only when that still leaves a price to show; if "high" is the sole cached
    // metric, showing it beats claiming the card has no price. Erase in place (no aside
    // copy) once we've confirmed a non-"high" row survives. (std::erase_if — C++20.)
    rows_ = std::move(cached);
    const auto isHigh = [](const CardPrice& p) {
        return p.provenance == kTcgplayerProvenance && p.metric == "high";
    };
    if (std::any_of(rows_.begin(), rows_.end(), [&](const CardPrice& p) { return !isHigh(p); })) {
        std::erase_if(rows_, isHigh);
    }

    const QString headline = priceHeadline(rows_);
    headline_->setText(headline.isEmpty() ? QStringLiteral("Market prices") : headline);
    headline_->show();
    infoButton_->show();

    // Links to the vendors' own listing pages, for whichever vendors returned prices.
    const QString linksHtml = listingLinksHtml(externalCardId_.toStdString(), rows_);
    links_->setText(linksHtml);
    links_->setVisible(!linksHtml.isEmpty());

    // "as of" is the newest vendor date across the rows; also show when WE fetched.
    Timestamp newest = rows_.front().observedAt;
    for (const CardPrice& p : rows_) {
        newest = std::max(newest, p.observedAt);
    }
    QString status = QStringLiteral("as of %1").arg(dateOf(newest));
    if (fetchedAt) {
        status += QStringLiteral(" · fetched %1").arg(dateOf(*fetchedAt));
    }
    status_->setText(status);
    status_->show();
    fetchButton_->setText(QStringLiteral("Refresh"));

    repopulateTable();
    toggle_->show();
    table_->setVisible(toggle_->isChecked());
}

void CardPricesPanel::repopulateTable() {
    // A header click is a pure in-memory reorder of rows_ (never Qt's row sorting) —
    // sortColumn_ < 0 keeps the natural (provenance, variant, metric) load order.
    applyColumnSort(rows_, sortColumn_, sortOrder_,
                    [](const CardPrice& a, const CardPrice& b, int col) -> int {
                        switch (col) {
                            case 0:
                                return QString::fromStdString(a.provenance)
                                    .localeAwareCompare(QString::fromStdString(b.provenance));
                            case 1:
                                return QString::fromStdString(a.variant)
                                    .localeAwareCompare(QString::fromStdString(b.variant));
                            case 2:
                                return QString::fromStdString(a.metric)
                                    .localeAwareCompare(QString::fromStdString(b.metric));
                            case 3:
                                return compareValues(a.amountCents, b.amountCents);
                            case 4:
                                return compareValues(a.observedAt, b.observedAt);
                            default:
                                return 0;
                        }
                    });

    table_->setRowCount(static_cast<int>(rows_.size()));
    for (int row = 0; row < static_cast<int>(rows_.size()); ++row) {
        const CardPrice& p = rows_[static_cast<std::size_t>(row)];
        table_->setItem(row, 0, new QTableWidgetItem(QString::fromStdString(p.provenance)));
        table_->setItem(row, 1, new QTableWidgetItem(QString::fromStdString(p.variant)));
        table_->setItem(row, 2, new QTableWidgetItem(QString::fromStdString(p.metric)));
        table_->setItem(row, 3, new QTableWidgetItem(formatMoney(p.amountCents, p.currency)));
        table_->setItem(row, 4, new QTableWidgetItem(dateOf(p.observedAt)));
    }
    table_->resizeColumnsToContents();
}

void CardPricesPanel::onFetchClicked() {
    if (externalCardId_.isEmpty()) {
        // Unlinked: the first fetch resolves the catalog card first (invisible link),
        // then fetches its prices. Guard on canAutoLink — the button isn't shown otherwise.
        if (!canAutoLink()) {
            return;
        }
        // Prefer the human set name (the reliable disambiguator, and the only one for
        // code-less sets) to narrow the search, else the printed expansion code.
        const QString setFilter = QString::fromStdString(
            !cardRef_.setName.empty() ? cardRef_.setName : cardRef_.expansionCode);
        fetching_ = true;
        linking_ = true;
        fetchButton_->setEnabled(false);
        status_->setText(QStringLiteral("Looking up this card…"));
        status_->show();
        if (dexNumber_) {
            pendingLinkRequest_ = search_.searchPrintings(*dexNumber_, setFilter);
        } else {
            pendingLinkRequest_ =
                search_.searchByName(QString::fromStdString(cardRef_.name), setFilter);
        }
        linkWatchdog_->start(20000);
        return;
    }
    fetching_ = true;
    fetchButton_->setEnabled(false);
    status_->setText(QStringLiteral("Fetching prices…"));
    status_->show();
    // Force a network fetch — the button is an explicit user request for the latest.
    lookup_.fetch(externalCardId_, /*force=*/true);
}

void CardPricesPanel::onLinkResults(const std::vector<CardCandidate>& cards) {
    // Pick the one printing to link. A set + species/name usually yields exactly one;
    // when a species has several printings in the set, the copy's collector number
    // singles one out. Anything still ambiguous is reported, not guessed.
    const QString wantNumber = collectorKey(cardRef_.collectorNumber);
    const CardCandidate* match = nullptr;
    if (cards.size() == 1) {
        match = &cards.front();
    } else if (cards.size() > 1 && !wantNumber.isEmpty()) {
        for (const CardCandidate& candidate : cards) {
            if (collectorKey(candidate.cardRef.collectorNumber) != wantNumber) {
                continue;
            }
            if (match) {
                match = nullptr;  // two printings share the number — still ambiguous
                break;
            }
            match = &candidate;
        }
    }

    if (!match) {
        linking_ = false;
        fetching_ = false;
        fetchButton_->setEnabled(true);
        status_->setText(cards.empty()
                             ? QStringLiteral("Couldn't find this card in the catalog.")
                             : QStringLiteral("Found several possible cards — open “Edit "
                                              "card…” to pick the right one."));
        status_->show();
        return;
    }

    try {
        copies_.linkCatalogCard(copyId_, match->id);
    } catch (const std::exception& e) {
        // A persist failure here (e.g. a storage write error) is unexpected and leaves
        // the copy unlinked — surface it loudly with the detail, not just a transient
        // status line the next render would wipe.
        linking_ = false;
        fetching_ = false;
        fetchButton_->setEnabled(true);
        status_->setText(QStringLiteral("Couldn't look up this card right now."));
        status_->show();
        QMessageBox::warning(this, tr("Pokedex TCG"),
                             tr("Could not link this card:\n%1").arg(QString::fromUtf8(e.what())));
        return;
    }

    // Linked: keep the busy state (fetching_) and go straight on to fetch the prices, so
    // one Fetch click both resolves and fetches. Tell hosts so their cached copy learns
    // the id (a re-selection then sees it as already linked).
    externalCardId_ = QString::fromStdString(match->id);
    linking_ = false;
    Q_EMIT cardLinked(QString::fromStdString(copyId_), externalCardId_);
    lookup_.fetch(externalCardId_, /*force=*/true);  // pricesReady → fetching_=false, render()
}

}  // namespace pokedex
