#include "gui/views/binder_view.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QString>
#include <QStyle>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <algorithm>
#include <exception>
#include <map>
#include <optional>
#include <unordered_set>
#include <string>
#include <unordered_map>

#include "core/app/binder_guide_service.h"
#include "core/app/binder_service.h"
#include "core/app/card_copy_service.h"
#include "core/domain/card_ownership.h"
#include "gui/services/card_image_store.h"
#include "gui/services/card_price_lookup_service.h"
#include "gui/views/add_card_copy_page.h"
#include "gui/views/back_button.h"
#include "gui/views/binder_combo.h"
#include "gui/views/card_copy_labels.h"
#include "gui/views/condition_labels.h"
#include "gui/views/copy_row_activation.h"
#include "gui/views/edit_copy_page_host.h"
#include "gui/views/foil_labels.h"
#include "gui/views/owned_copy_buckets.h"
#include "gui/views/pokemon_detail_panel.h"
#include "gui/views/price_labels.h"
#include "gui/views/rarity_labels.h"
#include "gui/views/select_all_line_edit.h"
#include "gui/views/sortable_table.h"
#include "gui/views/splitter_style.h"
#include "gui/views/status_labels.h"
#include "gui/views/table_cell.h"

namespace pokedex {

BinderView::BinderView(BinderGuideService& guide, const CardBinder& binder,
                       WishlistService& wishlist, MediaService& media,
                       CardSearchService& cardSearch, CardPriceLookupService& priceLookup,
                       CardCopyService& cardCopies, CardImageStore& cardImages,
                       BinderService& binders, QWidget* parent)
    : QWidget(parent),
      guide_(guide),
      binder_(binder),
      cardSearch_(cardSearch),
      priceLookup_(priceLookup),
      cardCopies_(cardCopies),
      cardImages_(cardImages),
      binders_(binders) {
    auto* backButton = makeBackButton(this);
    auto* heading = new QLabel(binderComboLabel(binder), this);

    connect(backButton, &QPushButton::clicked, this, &BinderView::backRequested);

    // A top bar: Back on the left, the binder's name beside it.
    auto* topBar = new QHBoxLayout;
    topBar->addWidget(backButton);
    topBar->addWidget(heading);
    topBar->addStretch();

    // A muted subtitle line under the top bar carrying the binder's stats:
    // how many species are listed, how many captured (+%), and the market $ value
    // of the captured cards. Filled by updateStats() on every refresh().
    stats_ = new QLabel(this);
    stats_->setStyleSheet(QStringLiteral("color: gray;"));

    search_ = new SelectAllLineEdit(this);
    search_->setPlaceholderText(tr("Search Pokémon…"));
    search_->setClearButtonEnabled(true);

    // A read-only table: dex number, name, then the printed-identity columns mirroring
    // My Cards (Card code, Set, condition, rarity, foil) for a representative owned copy
    // filed here, and finally the capture Status. Whole-row selection, no editing. The
    // Set column takes the slack (as in My Cards); the Pokémon column sizes to content so
    // the species name is never truncated. The copy columns are blank for a species with
    // no copy filed here (most rows in a fresh binder).
    table_ = new QTableWidget(this);
    table_->setColumnCount(8);
    table_->setHorizontalHeaderLabels({tr("#"), tr("Pokémon"), tr("Card"), tr("Set"),
                                       tr("Cond."), tr("Rarity"), tr("Foil"), tr("Status")});
    table_->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    // The "#" header sits over right-aligned dex numbers, so right-align it to match.
    table_->horizontalHeaderItem(0)->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->verticalHeader()->setVisible(false);
    auto* header = table_->horizontalHeader();
    // Pokémon (col 1) and the short metadata columns size to content; Set (col 3) is the
    // flexible slack absorber that grows when there's room and elides (full value on a
    // tooltip) when space is tight — mirroring OwnedCardsView.
    for (const int col : {0, 1, 2, 4, 5, 6, 7}) {  // #, Pokémon, Card, Cond., Rarity, Foil, Status
        header->setSectionResizeMode(col, QHeaderView::ResizeToContents);
    }
    header->setSectionResizeMode(3, QHeaderView::Stretch);  // Set — flexible slack absorber
    // Cell padding so content clears the edges and the overlay scrollbar.
    table_->setStyleSheet("QTableView::item { padding-left: 8px; padding-right: 16px; }");

    detail_ = new PokemonDetailPanel(media, wishlist, &cardImages_, &priceLookup_, &cardSearch_,
                                     &cardCopies_, this);

    connect(search_, &QLineEdit::textChanged, this, &BinderView::applyFilter);
    // Show the current row's Pokémon in the detail panel. currentCellChanged
    // covers both a mouse click (which moves the current cell) and keyboard arrow
    // navigation, so one connection suffices — connecting cellClicked too would
    // just fire showRow twice per click. (cellActivated would be double-click/Enter
    // — the wrong gesture.)
    connect(table_, &QTableWidget::currentCellChanged, this, &BinderView::showRow);
    // Double-click / Enter is the confirm-then-act shortcut (edit the shown copy, or add
    // one if none is filed here). cellActivated is exactly that gesture and never fires on
    // plain selection, so it won't race showRow — by the time it fires the row is selected
    // and a copy is on screen. Mirrors the Pokémon browser and My Cards.
    connect(table_, &QTableWidget::cellActivated, this,
            [this](int row, int) { activateRow(row); });
    // Clicking a header sorts the guide by that column; store the choice and
    // repopulate from the cached entries_ (re-sorted so they stay 1:1 with the rows).
    // A pure reorder — it never recomputes the guide or re-reads the binder's copies.
    installHeaderSort(table_, [this](int column, Qt::SortOrder order) {
        sortColumn_ = column;
        sortOrder_ = order;
        repopulate();
    });
    // The detail panel's "Add copy" relays up to an in-place page push.
    connect(detail_, &PokemonDetailPanel::addCopyRequested, this, &BinderView::openAddCopy);
    // In copy mode, "Edit card" relays up to an in-place edit-page push.
    connect(detail_, &PokemonDetailPanel::editCopyRequested, this, &BinderView::openEditCopy);
    // When the detail panel's Fetch auto-links a copy, write the id back into both cached
    // copy stores so a re-selection shows it linked (not re-resolved) and the header value
    // can count it once its prices land. No updateStats here: the fetch that immediately
    // follows the link emits pricesReady, which recomputes below — linking on its own can't
    // change the total (no prices are cached for the new id yet).
    connect(detail_, &PokemonDetailPanel::copyLinked, this,
            [this](const QString& copyId, const QString& externalCardId) {
                applyLinkedCardToBuckets(ownedHere_, copyId, externalCardId);
                applyLinkedCardToVector(filedCopies_, copyId, externalCardId);
            });
    // A price fetch (from the detail panel, for a card filed here) can raise the binder's
    // market-value total; the header is computed from cached prices at stat time, so
    // recompute it when such a card's prices land rather than only on a full reopen. The
    // lookup service is app-wide, so a pricesReady for a card NOT filed here (most of them)
    // is ignored instead of re-running the whole batched value query on every fetch anywhere.
    connect(&priceLookup_, &CardPriceLookupService::pricesReady, this,
            [this](const QString& externalCardId) {
                const std::string id = externalCardId.toStdString();
                const bool filedHere =
                    std::any_of(filedCopies_.begin(), filedCopies_.end(),
                                [&](const CardCopy& c) { return c.externalCardId == id; });
                if (filedHere) {
                    updateStats(filedCopies_);
                }
            });

    // The list (top bar + search + table) on the left, the detail panel on the
    // right, in a draggable horizontal split. The list takes the slack.
    auto* listPane = new QWidget(this);
    auto* listLayout = new QVBoxLayout(listPane);
    listLayout->setContentsMargins(16, 12, 16, 12);  // match the other sections' padding
    listLayout->addLayout(topBar);
    listLayout->addWidget(stats_);
    listLayout->addWidget(search_);
    listLayout->addWidget(table_);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(listPane);
    splitter->addWidget(detail_);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 0);
    splitter->setSizes({560, 240});
    thinDivider(splitter);

    // Page 0 of an inner stack is the guide splitter; "Add copy" pushes an
    // AddCardCopyPage as page 1 and Back returns here.
    stack_ = new QStackedWidget(this);
    stack_->addWidget(splitter);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(stack_);

    refresh();  // compute the guide and populate the table for the first time
}

void BinderView::refresh() {
    // (Re)compute the guide's entries. A failure here (e.g. the workspace went
    // away) is reported and leaves an empty table rather than crashing.
    try {
        entries_ = guide_.buildEntries(binder_);
    } catch (const std::exception& e) {
        entries_.clear();
        QMessageBox::critical(this, tr("Pokedex TCG"),
                              tr("Could not open this binder:\n%1")
                                  .arg(QString::fromUtf8(e.what())));
    }

    // Bucket the binder's owned copies by species so showRow() can hand the detail
    // panel the copies to display (copy mode). bucketOwnedCopiesByDex applies the shared
    // "owned + species-tied" predicate (mirroring the "Completed" status). Scoped read
    // (listByBinder), not a whole-inventory scan — unlike the Pokémon browser's listAll.
    // The full filed list is kept to also drive the header value stat (which includes
    // species-free owned copies, so it reads the list rather than the bucketed map).
    ownedHere_.clear();
    std::vector<CardCopy> filed;
    try {
        filed = cardCopies_.listByBinder(binder_.id);
        ownedHere_ = bucketOwnedCopiesByDex(filed);
    } catch (const std::exception&) {
        filed.clear();
        ownedHere_.clear();  // best-effort: fall back to artwork-only if the read fails
    }

    filedCopies_ = std::move(filed);
    updateStats(filedCopies_);
    repopulate();
}

void BinderView::updateStats(const std::vector<CardCopy>& filedCopies) {
    // Listed = every species the guide lists. Captured = species with at least one Owned
    // copy filed here; ownedHere_ is exactly that set (bucketOwnedCopiesByDex keeps only
    // Owned, species-tied copies), so its size is the count directly. Counting
    // CollectionStatus::Completed would undercount: a species that ALSO has an Incoming
    // copy filed here resolves to Incoming (higher precedence) yet is genuinely owned here.
    const int listed = static_cast<int>(entries_.size());
    const int captured = static_cast<int>(ownedHere_.size());

    // Market value of the Owned cards filed in this binder (species-free ones too — they
    // are cards in the binder), totalled per currency; no FX conversion, so USD (TCGplayer)
    // and EUR (Cardmarket) stay separate. Reads are network-free local-cache lookups, so a
    // copy contributes only once its prices were fetched (and it is linked); the figure is
    // therefore a lower bound over what has been priced. Cache each id's rows so repeated
    // printings aren't re-read (every copy still counts — three of a card is worth 3×). The
    // read is best-effort like the rest of refresh(): a storage failure omits the $ rather
    // than crashing the binder open.
    std::map<std::string, long long> totals;
    try {
        // Gather the distinct linked ids, then read every one's prices in a single
        // batched query (cachedMany) rather than one SELECT per card — opening or
        // re-statting a large binder must not be N synchronous round-trips. Each copy
        // still contributes (three of a card counts 3×), by looking its id up in the map.
        std::vector<std::string> ids;
        std::unordered_set<std::string> seen;
        for (const CardCopy& copy : filedCopies) {
            if (copy.ownership == CardOwnership::Owned && !copy.externalCardId.empty() &&
                seen.insert(copy.externalCardId).second) {
                ids.push_back(copy.externalCardId);
            }
        }
        const std::unordered_map<std::string, std::vector<CardPrice>> byId =
            priceLookup_.cachedMany(ids);
        for (const CardCopy& copy : filedCopies) {
            if (copy.ownership != CardOwnership::Owned || copy.externalCardId.empty()) {
                continue;
            }
            const auto it = byId.find(copy.externalCardId);
            if (it != byId.end()) {
                accumulateBestPrices(totals, it->second);
            }
        }
    } catch (const std::exception&) {
        totals.clear();  // best-effort: show the counts without a value rather than crash
    }

    QString text = tr("Listed %1").arg(listed);
    if (listed > 0) {
        const int percent = qRound(100.0 * captured / listed);
        // Guard both rounding extremes so the figure never contradicts the count: a tiny
        // nonzero ratio shows "<1%" rather than "0%", and a nearly-complete-but-not one
        // (e.g. 997/1000 → qRound(99.7) == 100) shows ">99%" rather than a false "100%".
        QString pct;
        if (percent == 0 && captured > 0) {
            pct = tr("<1%");
        } else if (percent == 100 && captured < listed) {
            pct = tr(">99%");
        } else {
            pct = tr("%1%").arg(percent);
        }
        text += tr(" · Captured %1 (%2)").arg(captured).arg(pct);
    }
    const QString value = formatMoneyTotals(totals);
    if (!value.isEmpty()) {
        text += QStringLiteral(" · ") + value;
    }
    stats_->setText(text);
}

void BinderView::repopulate() {
    // Remember which species + copy the detail panel is showing before the rebuild.
    // A header-sort reorders the rows, so the selection must be restored by IDENTITY
    // (the species' dex number), not by the old row index — restoring by row index
    // would leave a different species highlighted and the panel + "Edit card…"
    // targeting the wrong one. shownDex_ is the shown species; "" / -1 when nothing
    // is shown (no selection).
    const QString shownCopyBefore = detail_->shownCopyId();
    const int selectedDex = table_->selectedItems().isEmpty() ? -1 : shownDex_;

    sortEntries();

    // Rebuild every row from the freshly computed entries; entries_ and table rows
    // stay 1:1 and aligned. Statuses are fixed until the next refresh, so filtering
    // then just toggles row visibility — no per-keystroke allocation.
    table_->setRowCount(static_cast<int>(entries_.size()));
    for (int i = 0; i < static_cast<int>(entries_.size()); ++i) {
        const CardBinderEntry& entry = entries_[i];
        auto* number = cell(QString::number(entry.pokemon.dexNumber));
        number->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        table_->setItem(i, 0, number);
        table_->setItem(i, 1, cell(QString::fromStdString(entry.pokemon.name)));
        // A representative owned copy filed here fills the printed-identity columns; a
        // species with none leaves them blank (rendered as an em-dash by cell()).
        const CardCopy* rep = representativeCopy(entry.pokemon.dexNumber);
        table_->setItem(i, 2, cell(rep ? cardText(rep->cardRef) : QString()));
        // Set is the free-text slack column: carry the full value as a tooltip so it
        // stays readable when the width elides it.
        auto* setCell = cell(rep ? QString::fromStdString(rep->cardRef.setName) : QString());
        if (rep) {
            setCell->setToolTip(QString::fromStdString(rep->cardRef.setName));
        }
        table_->setItem(i, 3, setCell);
        table_->setItem(i, 4, cell(rep && rep->condition ? conditionAbbrev(*rep->condition)
                                                         : QString()));
        table_->setItem(i, 5, cell(rep && rep->rarity ? rarityLabel(*rep->rarity) : QString()));
        table_->setItem(i, 6, cell(rep && rep->foil ? foilLabel(*rep->foil) : QString()));
        table_->setItem(i, 7, cell(statusLabel(entry.status)));
    }

    // Move the highlight to the row the selected species landed on after the sort, so
    // the selection follows the record rather than the row index. Block signals so
    // setCurrentCell doesn't re-fire showRow (which re-rolls a random copy); the panel
    // is re-driven explicitly below with the SAME copy that was on screen.
    if (selectedDex >= 0) {
        for (int i = 0; i < static_cast<int>(entries_.size()); ++i) {
            if (entries_[i].pokemon.dexNumber == selectedDex) {
                table_->blockSignals(true);
                table_->setCurrentCell(i, 1);
                table_->blockSignals(false);
                break;
            }
        }
    }

    applyFilter(search_->text());  // preserve the current filter across a refresh

    // Re-drive the panel from the (identity-restored) current row so the highlight and
    // panel stay in agreement, mirroring OwnedCardsView. Re-show the SAME copy that was
    // on screen (shownCopyBefore) rather than calling showRow(), which re-rolls a random
    // copy of the species. openEditCopy re-selects the just-edited copy after its own
    // refresh(), which overrides this.
    const int current = table_->currentRow();
    if (current >= 0 && !table_->isRowHidden(current) && !table_->selectedItems().isEmpty()) {
        QTableWidgetItem* number = table_->item(current, 0);
        QTableWidgetItem* name = table_->item(current, 1);
        if (number && name) {
            shownDex_ = number->text().toInt();
            showSpeciesInPanel(shownDex_, name->text(), shownCopyBefore);
        }
    }
}

void BinderView::sortEntries() {
    // Precompute each row's keys once (via sortByKeys) rather than rebuilding them for
    // both operands on every comparison — the name and the copy-derived text columns
    // allocate QStrings, and each row does a representativeCopy() lookup. A sortColumn_ < 0
    // keeps the guide's natural (dex) order. The copy columns are keyed as std::optional so
    // a species with no copy filed here sinks to the bottom in either direction (see
    // compareOptional), not just ascending. Condition/rarity/foil rank by enum value
    // (best-to-worst condition; declaration order for rarity/foil) so the sort matches My
    // Cards' semantics rather than the labels' alphabetical order.
    struct Key {
        int dexNumber;
        QString name;
        std::optional<QString> card;
        std::optional<QString> setName;
        std::optional<int> conditionRank;
        std::optional<int> rarityRank;
        std::optional<int> foilRank;
        int statusRank;
    };
    const bool ascending = sortOrder_ == Qt::AscendingOrder;
    sortByKeys(
        entries_, sortColumn_, sortOrder_,
        [this](const CardBinderEntry& e) {
            Key key;
            key.dexNumber = e.pokemon.dexNumber;
            key.name = QString::fromStdString(e.pokemon.name);
            key.statusRank = static_cast<int>(e.status);
            if (const CardCopy* rep = representativeCopy(e.pokemon.dexNumber)) {
                key.card = cardText(rep->cardRef);
                key.setName = QString::fromStdString(rep->cardRef.setName);
                if (rep->condition) {
                    key.conditionRank = static_cast<int>(*rep->condition);
                }
                if (rep->rarity) {
                    key.rarityRank = static_cast<int>(*rep->rarity);
                }
                if (rep->foil) {
                    key.foilRank = static_cast<int>(*rep->foil);
                }
            }
            return key;
        },
        [ascending](const Key& a, const Key& b, int column) -> int {
            const auto text = [](const QString& x, const QString& y) {
                return x.localeAwareCompare(y);
            };
            const auto rank = [](int x, int y) { return compareValues(x, y); };
            switch (column) {
                case 0:
                    return compareValues(a.dexNumber, b.dexNumber);
                case 1:
                    return a.name.localeAwareCompare(b.name);
                case 2:
                    return compareOptional(a.card, b.card, ascending, text);
                case 3:
                    return compareOptional(a.setName, b.setName, ascending, text);
                case 4:
                    return compareOptional(a.conditionRank, b.conditionRank, ascending, rank);
                case 5:
                    return compareOptional(a.rarityRank, b.rarityRank, ascending, rank);
                case 6:
                    return compareOptional(a.foilRank, b.foilRank, ascending, rank);
                case 7:
                    // CollectionStatus enum values are the documented precedence order —
                    // a more meaningful grouping than the status labels' alphabetical order.
                    return compareValues(a.statusRank, b.statusRank);
            }
            return 0;
        });
}

const CardCopy* BinderView::representativeCopy(int dex) const {
    const auto it = ownedHere_.find(dex);
    if (it == ownedHere_.end() || it->second.empty()) {
        return nullptr;
    }
    return &it->second.front();
}

void BinderView::applyFilter(const QString& filter) {
    bool shownStillVisible = false;
    for (int i = 0; i < static_cast<int>(entries_.size()); ++i) {
        const QString name = QString::fromStdString(entries_[i].pokemon.name);
        const bool visible = filter.isEmpty() || name.contains(filter, Qt::CaseInsensitive);
        table_->setRowHidden(i, !visible);
        if (visible && entries_[i].pokemon.dexNumber == shownDex_) {
            shownStillVisible = true;
        }
    }
    // If the Pokémon shown in the detail panel is now hidden (an empty result, or
    // a filter that excludes it), clear the panel so it never shows a species with
    // no visible row.
    if (!shownStillVisible) {
        detail_->clear();
        shownDex_ = -1;
    }
}

void BinderView::showRow(int row) {
    if (row < 0) {
        return;
    }
    QTableWidgetItem* number = table_->item(row, 0);
    QTableWidgetItem* name = table_->item(row, 1);
    if (!number || !name) {
        return;
    }
    shownDex_ = number->text().toInt();
    showSpeciesInPanel(shownDex_, name->text());
}

void BinderView::showSpeciesInPanel(int dex, const QString& name, const QString& preferCopyId) {
    showSpeciesCopiesInPanel(detail_, ownedHere_, dex, name, preferCopyId);
}

void BinderView::activateRow(int row) {
    if (row < 0) {
        return;
    }
    QTableWidgetItem* number = table_->item(row, 0);
    QTableWidgetItem* name = table_->item(row, 1);
    if (!number || !name) {
        return;
    }
    const int dex = number->text().toInt();
    const QString species = name->text();

    const auto it = ownedHere_.find(dex);
    const bool ownedHere = it != ownedHere_.end() && !it->second.empty();
    const QString copyId = detail_->shownCopyId();
    activateCopyRow(
        {this, ownedHere, species, copyId,
         tr("%1 has no cards filed in this binder yet.\nAdd one now?").arg(species),
         [this, dex, species]() { openAddCopy(dex, species); },
         [this, copyId]() { openEditCopy(copyId); }});
}

void BinderView::openAddCopy(int dexNumber, const QString& name) {
    // Scoped to this binder: the copy is filed here and the picker is locked to it.
    auto* page =
        new AddCardCopyPage(cardSearch_, cardCopies_, binders_, cardImages_, dexNumber, name,
                            binder_.id);
    // Adding a copy recomputes the guide: the copy is filed in this binder (so it
    // becomes "Completed" here) and submit auto-returns, so refresh so the guide
    // isn't stale on the way back.
    connect(page, &AddCardCopyPage::copyAdded, this, &BinderView::refresh);
    connect(page, &AddCardCopyPage::backRequested, this, [this, page]() {
        stack_->setCurrentIndex(0);
        stack_->removeWidget(page);
        page->deleteLater();
    });
    stack_->addWidget(page);
    stack_->setCurrentWidget(page);
}

void BinderView::openEditCopy(const QString& copyId) {
    openEditCopyFromBuckets(stack_, cardSearch_, priceLookup_, cardImages_, cardCopies_,
                            ownedHere_, shownDex_,
                            copyId, binders_.list(), [this, copyId]() {
                                // Capture the shown species before refresh(), which may
                                // clear shownDex_. An edit can change the guide (a comment, a
                                // binder move that removes the copy from here, a new image),
                                // so recompute, then re-show the SAME copy — not a fresh
                                // random pick.
                                const int dex = shownDex_;
                                refresh();
                                reselectSpecies(dex, copyId);
                            });
}

void BinderView::reselectSpecies(int dex, const QString& copyId) {
    for (int i = 0; i < static_cast<int>(entries_.size()); ++i) {
        if (entries_[i].pokemon.dexNumber == dex) {
            table_->blockSignals(true);  // setCurrentCell would re-fire showRow (random)
            table_->setCurrentCell(i, 1);
            table_->blockSignals(false);
            shownDex_ = dex;
            // If the copy left the binder (moved/removed), showSpeciesInPanel falls back
            // to plain artwork.
            showSpeciesInPanel(dex, QString::fromStdString(entries_[i].pokemon.name), copyId);
            return;
        }
    }
    // The species left the guide entirely (its only copy moved away).
    detail_->clear();
    shownDex_ = -1;
}

}  // namespace pokedex
