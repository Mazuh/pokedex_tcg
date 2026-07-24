#include "gui/views/owned_cards_view.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QShowEvent>
#include <QSplitter>
#include <QStackedWidget>
#include <QString>
#include <QStyle>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <exception>
#include <optional>
#include <unordered_map>
#include <vector>

#include "core/app/binder_service.h"
#include "core/app/card_copy_service.h"
#include "core/domain/card_binder.h"
#include "core/domain/card_copy.h"
#include "core/domain/pokemon.h"
#include "gui/services/card_image_store.h"
#include "gui/views/add_card_copy_page.h"
#include "gui/views/binder_picker_dialog.h"
#include "gui/views/card_copy_labels.h"
#include "gui/views/card_image_panel.h"
#include "gui/views/edit_card_copy_page.h"
#include "gui/views/condition_labels.h"
#include "gui/views/foil_labels.h"
#include "gui/views/ownership_labels.h"
#include "gui/views/rarity_labels.h"
#include "gui/views/region_labels.h"
#include "gui/views/select_all_line_edit.h"
#include "gui/views/sortable_table.h"
#include "gui/views/splitter_style.h"
#include "gui/views/table_cell.h"
#include "gui/views/toast.h"

namespace pokedex {

namespace {

// The region label for a dex number, from the compile-time catalog. Not shown in
// any column, but appended to the search haystack so a copy is findable by its
// species' region. Empty for an out-of-range number (defensive). (The copy label
// helpers speciesName/cardText/speciesOrCardName/titleFor live in card_copy_labels.h,
// shared with the binder guide's detail panel.)
QString speciesRegionLabel(PokemonDexNum dexNumber) {
    const Pokemon* entry = catalogEntry(dexNumber);
    return entry ? regionLabel(entry->region) : QString();
}

// A soft-Removed copy is history, not part of the live collection: it sorts to the
// bottom band, grays out, and is the only kind that can be permanently deleted.
// One predicate so the sort, the graying, the button gate, and the delete guard
// can never drift apart.
bool isRemoved(const CardCopy& copy) { return copy.ownership == CardOwnership::Removed; }

// Three-way compare (compareValues shape) for an optional-valued column
// (Condition / Rarity / Foil) where an unset value must always sink to the
// BOTTOM — in both sort directions, because "no data" isn't a low value. The
// shared sort shell flips the comparator's sign for a descending sort, so we
// pre-invert the set-vs-unset case here: `ascending` decides which sign keeps
// the unset operand last after that flip. Two set values compare normally; two
// unset values are equal.
int compareOptionalRank(std::optional<int> a, std::optional<int> b, bool ascending) {
    if (a && b) {
        return compareValues(*a, *b);
    }
    if (!a && !b) {
        return 0;
    }
    // Exactly one is unset. Set-before-unset is cmp<0 ascending / cmp>0
    // descending (the shell's flip turns the latter back into "last").
    const int setBeforeUnset = ascending ? -1 : 1;
    return a ? setBeforeUnset : -setBeforeUnset;
}

}  // namespace

OwnedCardsView::OwnedCardsView(CardCopyService& copies, BinderService& binders,
                               CardImageStore& images, CardSearchService& cardSearch,
                               QWidget* parent)
    : QWidget(parent),
      copies_(copies),
      binders_(binders),
      images_(images),
      cardSearch_(cardSearch) {
    search_ = new SelectAllLineEdit(this);
    search_->setPlaceholderText(
        tr("Search copy by Pokémon, collector number, set or binder…"));
    search_->setClearButtonEnabled(true);
    connect(search_, &QLineEdit::textChanged, this, [this](const QString&) { applyFilter(); });

    // A read-only nine-column table: Pokémon, card ref, set, language, condition,
    // rarity, foil, ownership, binder. Whole-row selection, no editing; the Pokémon
    // column sizes to its content (so the species name is never truncated) while the
    // Set column takes up the slack. The Set column carries the human set name, which
    // for code-less sets (McDonald's, POP…) is the only disambiguator.
    table_ = new QTableWidget(this);
    table_->setColumnCount(9);
    table_->setHorizontalHeaderLabels({tr("Pokémon"), tr("Card"), tr("Set"), tr("Lang"),
                                       tr("Cond."), tr("Rarity"), tr("Foil"), tr("Ownership"),
                                       tr("Binder")});
    table_->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->verticalHeader()->setVisible(false);
    auto* header = table_->horizontalHeader();
    // Pokémon (col 0) sizes to its content so the species name is never truncated —
    // names are short and bounded, so this stays a modest, stable width even with the
    // image panel narrowing the table. Set (col 2) is the flexible slack absorber
    // instead: free text that grows when there's room and elides (full value on a
    // tooltip) when space is tight, so Pokémon keeps its width and, when very narrow,
    // there is still overflow to scroll to. Binder (col 8) is content-sized-then-capped
    // in reload() so a long user-named binder can't crowd the table; the short
    // metadata columns size to content.
    for (const int col : {0, 1, 3, 4, 5, 6, 7}) {  // Pokémon, Card, Lang, Cond., Rarity, Foil, Ownership
        header->setSectionResizeMode(col, QHeaderView::ResizeToContents);
    }
    header->setSectionResizeMode(2, QHeaderView::Stretch);      // Set — flexible slack absorber
    header->setSectionResizeMode(8, QHeaderView::Interactive);  // Binder — free text, capped
    table_->setStyleSheet("QTableView::item { padding-left: 8px; padding-right: 16px; }");
    connect(table_, &QTableWidget::itemSelectionChanged, this, [this]() {
        updateButtonState();
        showSelectedImage();
    });
    // Double-clicking a row opens that copy's Edit page — the double-click selects the
    // row first, so editSelectedCard() reads the intended row.
    connect(table_, &QTableWidget::cellDoubleClicked, this,
            [this](int, int) { editSelectedCard(); });
    // Clicking a header sorts by that column; store the choice and repopulate from the
    // cached data (a pure reorder, no re-query) — keeping loaded_/haystacks_ aligned
    // with the reordered rows and the selection on the same copy.
    installHeaderSort(table_, [this](int column, Qt::SortOrder order) {
        sortColumn_ = column;
        sortOrder_ = order;
        repopulate(selectedCopyId());
    });

    assignButton_ = new QPushButton(tr("Assign to binder…"), this);
    assignButton_->setIcon(style()->standardIcon(QStyle::SP_DirOpenIcon));
    connect(assignButton_, &QPushButton::clicked, this, &OwnedCardsView::assignSelected);

    removeButton_ = new QPushButton(tr("Remove…"), this);
    removeButton_->setIcon(style()->standardIcon(QStyle::SP_TrashIcon));
    connect(removeButton_, &QPushButton::clicked, this, &OwnedCardsView::removeSelected);

    // "Delete permanently…" drops a soft-Removed copy's row for good — enabled only
    // when the selected copy is already Removed (a two-step gate: soft-remove first,
    // then permanently delete), and always confirmed. Removed copies sort last and
    // gray out (see repopulate), so this action targets that bottom band.
    deleteButton_ = new QPushButton(tr("Delete permanently…"), this);
    deleteButton_->setIcon(style()->standardIcon(QStyle::SP_DialogDiscardButton));
    connect(deleteButton_, &QPushButton::clicked, this, &OwnedCardsView::deletePermanently);

    editButton_ = new QPushButton(tr("Edit card…"), this);
    editButton_->setIcon(style()->standardIcon(QStyle::SP_FileDialogDetailedView));
    connect(editButton_, &QPushButton::clicked, this, &OwnedCardsView::editSelectedCard);

    // "Add a card…" records a species-free card (a Trainer/Energy card) — needs no row
    // selection, so it stays available even when the collection is empty. Pokémon
    // copies are still added from "All Pokémon" (scoped to a species).
    addButton_ = new QPushButton(tr("Add a card…"), this);
    addButton_->setIcon(style()->standardIcon(QStyle::SP_FileDialogNewFolder));
    connect(addButton_, &QPushButton::clicked, this, &OwnedCardsView::addNewCard);

    // Shown in place of the table (and search) when the collection is empty.
    emptyLabel_ = new QLabel(
        tr("No cards yet. Use “Add a card…” below for a Trainer or Energy card, or open "
           "a Pokémon in “All Pokémon” and use “Add copy…”."),
        this);
    emptyLabel_->setWordWrap(true);
    emptyLabel_->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    emptyLabel_->setEnabled(false);  // muted: a hint, not content

    countLabel_ = new QLabel(this);
    countLabel_->setEnabled(false);  // muted: a status detail, not an action

    auto* buttons = new QHBoxLayout;
    buttons->addWidget(addButton_);
    buttons->addWidget(assignButton_);
    buttons->addWidget(removeButton_);
    buttons->addWidget(deleteButton_);
    buttons->addWidget(editButton_);
    buttons->addStretch();

    // The list pane (left) holds everything the section had before; the card-image
    // panel (right) shows the selected copy's image — the PokemonListView splitter
    // idiom, so "My Cards" reads like "All Pokémon".
    auto* listPane = new QWidget(this);
    auto* listLayout = new QVBoxLayout(listPane);
    listLayout->setContentsMargins(16, 12, 16, 12);  // match the other sections' padding
    listLayout->addWidget(search_);
    listLayout->addWidget(table_);
    listLayout->addWidget(emptyLabel_);
    listLayout->addLayout(buttons);
    listLayout->addWidget(countLabel_);

    panel_ = new CardImagePanel;

    // A deferred fetchAndSave() download (or any image write) for the copy currently
    // shown must re-render the panel: the copy id is unchanged, so showSelectedImage's
    // shownCopyId_ dedup guard would otherwise suppress the reload and the new art
    // would never appear. Reset the guard and re-show when the shown copy changes.
    connect(&images_, &CardImageStore::imageChanged, this, [this](const QString& copyId) {
        if (copyId.toStdString() == shownCopyId_) {
            shownCopyId_.clear();
            showSelectedImage();
        }
    });

    auto* splitter = new QSplitter(Qt::Horizontal);
    splitter->addWidget(listPane);
    splitter->addWidget(panel_);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 0);
    splitter->setSizes({560, 240});
    thinDivider(splitter);

    // An inner stack so an in-window "Edit card" page can be pushed over the list,
    // then popped back — the PokemonListView list⇄page idiom, so My Cards navigates
    // within the section rather than opening a separate window.
    stack_ = new QStackedWidget(this);
    stack_->addWidget(splitter);  // page 0: the list ⇄ image panel

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(stack_);

    updateButtonState();
}

void OwnedCardsView::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    reload();  // reflect any copies added since this section was last visible
}

std::string OwnedCardsView::selectedCopyId() const {
    const int sel = table_->currentRow();
    if (sel < 0 || sel >= static_cast<int>(loaded_.size()) ||
        table_->selectedItems().isEmpty()) {
        return {};
    }
    return loaded_[sel].id;
}

const CardCopy* OwnedCardsView::selectedVisibleCopy() const {
    const int row = table_->currentRow();
    if (row < 0 || row >= static_cast<int>(loaded_.size()) || table_->isRowHidden(row)) {
        return nullptr;
    }
    return &loaded_[row];
}

void OwnedCardsView::reload() {
    // Capture the selection by copy id before loaded_ is replaced (the current row
    // still indexes the old vector), so repopulate() can re-select it at its new row —
    // otherwise a header-sort would leave the highlight on a different copy and
    // Remove/Assign would silently act on the wrong card.
    const std::string keepSelected = selectedCopyId();
    // (Re)load the inventory and the binders once; a header-sort re-sort goes through
    // repopulate() directly, so reordering never re-hits storage.
    loaded_ = copies_.listAll();
    binderList_ = binders_.list();
    repopulate(keepSelected);
}

void OwnedCardsView::repopulate(const std::string& keepSelectedId) {
    // Resolve a binder id to its display name (shown in the Binder column) and its
    // region (search-only) from the cached binder list — rebuilt each repopulate but
    // never re-queried. A binder renamed/removed in the Binders section is picked up on
    // the next reload(). Built before the sort below so sorting by the Binder column
    // can key on the display name.
    struct BinderInfo {
        QString name;
        QString region;  // empty when the binder wasn't scoped to a region
    };
    std::unordered_map<std::string, BinderInfo> binderInfo;
    for (const CardBinder& binder : binderList_) {
        binderInfo.emplace(binder.id,
                           BinderInfo{QString::fromStdString(binder.name),
                                      binder.pokemonRegion ? regionLabel(*binder.pokemonRegion)
                                                           : QString()});
    }
    const auto binderName = [&](const CardCopy& c) -> QString {
        if (!c.binderId) {
            return QString();
        }
        const auto it = binderInfo.find(*c.binderId);
        return it != binderInfo.end() ? it->second.name : QString();
    };

    if (sortColumn_ < 0) {
        // Default (unsorted) order: group a species' copies together, oldest first.
        std::sort(loaded_.begin(), loaded_.end(), [](const CardCopy& a, const CardCopy& b) {
            if (a.pokemonDexNum != b.pokemonDexNum) {
                return a.pokemonDexNum < b.pokemonDexNum;
            }
            return a.insertedAt < b.insertedAt;
        });
    } else {
        // Precompute each copy's sort keys once (via sortByKeys) rather than rebuilding
        // them for both operands on every comparison — column 0's speciesOrCardName does
        // a catalog lookup + allocation, and the text columns allocate.
        struct Key {
            QString species, card, setName, language, ownership, binderName;
            std::optional<int> conditionRank, rarityRank, foilRank;
        };
        const bool ascending = sortOrder_ == Qt::AscendingOrder;
        const int column = sortColumn_;
        sortByKeys(
            loaded_, sortColumn_, sortOrder_,
            [&](const CardCopy& c) {
                // Condition ranks best-to-worst by enum value; rarity and foil rank by
                // enum declaration order. An unset value stays nullopt so it can sink to
                // the bottom in either direction (see compareOptionalRank), not just
                // ascending.
                const auto rank = [](auto opt) -> std::optional<int> {
                    return opt ? std::optional<int>(static_cast<int>(*opt)) : std::nullopt;
                };
                // Build ONLY the clicked column's key: keyCompare reads a single field,
                // so materializing the other columns (column 0's catalog lookup, five
                // QString allocations) for every row on each header click is pure waste
                // that grows with the collection. Unset fields keep their default
                // (empty QString / nullopt), which keyCompare never consults.
                Key key;
                switch (column) {
                    case 0: key.species = speciesOrCardName(c); break;
                    case 1: key.card = cardText(c.cardRef); break;
                    case 2: key.setName = QString::fromStdString(c.cardRef.setName); break;
                    case 3: key.language = QString::fromStdString(c.cardRef.language); break;
                    case 4: key.conditionRank = rank(c.condition); break;
                    case 5: key.rarityRank = rank(c.rarity); break;
                    case 6: key.foilRank = rank(c.foil); break;
                    case 7: key.ownership = ownershipLabel(c.ownership); break;
                    case 8: key.binderName = binderName(c); break;
                }
                return key;
            },
            [ascending](const Key& a, const Key& b, int column) -> int {
                switch (column) {
                    case 0: return a.species.localeAwareCompare(b.species);
                    case 1: return a.card.localeAwareCompare(b.card);
                    case 2: return a.setName.localeAwareCompare(b.setName);
                    case 3: return a.language.localeAwareCompare(b.language);
                    case 4: return compareOptionalRank(a.conditionRank, b.conditionRank, ascending);
                    case 5: return compareOptionalRank(a.rarityRank, b.rarityRank, ascending);
                    case 6: return compareOptionalRank(a.foilRank, b.foilRank, ascending);
                    case 7: return a.ownership.localeAwareCompare(b.ownership);
                    case 8: return a.binderName.localeAwareCompare(b.binderName);
                }
                return 0;
            });
    }

    // Soft-Removed copies always sink to the bottom, regardless of the active sort:
    // they're history, not part of the live collection. std::stable_partition keeps
    // the just-computed order within each band, so the live copies stay sorted as
    // above and the removed ones keep their relative order beneath them.
    std::stable_partition(loaded_.begin(), loaded_.end(),
                          [](const CardCopy& c) { return !isRemoved(c); });

    // Removed rows render grayed out (the palette's disabled text color), so the
    // bottom band reads as inactive history at a glance.
    const QBrush removedForeground = table_->palette().brush(QPalette::Disabled, QPalette::Text);

    table_->setRowCount(static_cast<int>(loaded_.size()));
    haystacks_.assign(loaded_.size(), QString());
    for (int row = 0; row < static_cast<int>(loaded_.size()); ++row) {
        const CardCopy& c = loaded_[row];
        // Column 0 identifies the row: the species name, or (for a species-free
        // Trainer/Energy card) its printed card name so the row isn't blank.
        table_->setItem(row, 0, cell(speciesOrCardName(c)));
        table_->setItem(row, 1, cell(cardText(c.cardRef)));
        // Set and Binder are the free-text, capped columns: carry the full value as
        // a tooltip so it stays readable when the cap elides it.
        auto* setCell = cell(QString::fromStdString(c.cardRef.setName));
        setCell->setToolTip(QString::fromStdString(c.cardRef.setName));
        table_->setItem(row, 2, setCell);
        table_->setItem(row, 3, cell(QString::fromStdString(c.cardRef.language)));
        // Condition is optional (ungraded copies) — blank renders as an em-dash.
        table_->setItem(row, 4, cell(c.condition ? conditionAbbrev(*c.condition) : QString()));
        // Rarity and foil are optional too — blank when unset. Full labels (no
        // abbreviation), since many legacy rarities lack a standard short form.
        table_->setItem(row, 5, cell(c.rarity ? rarityLabel(*c.rarity) : QString()));
        table_->setItem(row, 6, cell(c.foil ? foilLabel(*c.foil) : QString()));
        table_->setItem(row, 7, cell(ownershipLabel(c.ownership)));
        // Resolve the copy's binder once — its name goes in the cell, its region
        // into the search haystack below.
        const BinderInfo* binder = nullptr;
        if (c.binderId) {
            const auto it = binderInfo.find(*c.binderId);
            if (it != binderInfo.end()) {
                binder = &it->second;
            }
        }
        const QString binderName = binder ? binder->name : QString();
        auto* binderCell = cell(binderName);
        binderCell->setToolTip(binderName);
        table_->setItem(row, 8, binderCell);

        // Gray out a Removed copy's whole row so the (bottom-sorted) history band
        // reads as inactive.
        if (isRemoved(c)) {
            for (int col = 0; col < table_->columnCount(); ++col) {
                table_->item(row, col)->setForeground(removedForeground);
            }
        }

        // Precompute this row's lowercased search text from its cells, plus a few
        // fields that don't show verbatim in the table yet users still expect to
        // filter by: the dex number (the "#" column was dropped), the full
        // condition label (the column abbreviates to NM/LP/…), and both regions —
        // the species' region and, if filed, the binder's region.
        QString hay;
        if (c.pokemonDexNum) {
            hay += QString::number(*c.pokemonDexNum) + QLatin1Char(' ');
        }
        for (int col = 0; col < table_->columnCount(); ++col) {
            hay += table_->item(row, col)->text() + QLatin1Char(' ');
        }
        if (c.condition) {
            hay += conditionLabel(*c.condition) + QLatin1Char(' ');
        }
        if (c.pokemonDexNum) {
            hay += speciesRegionLabel(*c.pokemonDexNum) + QLatin1Char(' ');
        }
        if (binder && !binder->region.isEmpty()) {
            hay += binder->region + QLatin1Char(' ');
        }
        haystacks_[row] = hay.toLower();
    }
    // Cap the Binder column: size it to content, then clamp so a long user-named
    // binder elides (…) — with the full value on its tooltip and the column still
    // draggable — rather than crowding the table. (Set is the Stretch column now, so
    // it manages its own width.) The cap is a representative long name, so ordinary
    // names show whole.
    const int freeTextCap =
        table_->fontMetrics().horizontalAdvance(QStringLiteral("McDonald's Collection 2021")) + 32;
    table_->resizeColumnToContents(8);
    if (table_->columnWidth(8) > freeTextCap) {
        table_->setColumnWidth(8, freeTextCap);
    }
    // Empty state: swap the table + search + row actions (and the card-image panel)
    // for a friendly hint when nothing is stored — otherwise the "select a card"
    // panel sits beside a "no cards yet" hint with nothing to select.
    const bool empty = loaded_.empty();
    table_->setVisible(!empty);
    search_->setVisible(!empty);
    countLabel_->setVisible(!empty);
    assignButton_->setVisible(!empty);
    removeButton_->setVisible(!empty);
    deleteButton_->setVisible(!empty);
    editButton_->setVisible(!empty);
    emptyLabel_->setVisible(empty);
    panel_->setVisible(!empty);

    // Restore the selection at its new row (a no-op if the copy is gone).
    if (!keepSelectedId.empty()) {
        for (int row = 0; row < static_cast<int>(loaded_.size()); ++row) {
            if (loaded_[row].id == keepSelectedId) {
                table_->selectRow(row);
                break;
            }
        }
    }

    applyFilter();  // re-hide non-matches, set the count, and re-sync the panel
    updateButtonState();
}

void OwnedCardsView::applyFilter() {
    // Compare the lowercased needle against each row's precomputed haystack (built
    // in reload()) — a plain substring test per row, no per-keystroke allocation.
    const QString needle = search_->text().trimmed().toLower();
    int visible = 0;
    for (int row = 0; row < table_->rowCount(); ++row) {
        const bool match = needle.isEmpty() ||
                           (row < static_cast<int>(haystacks_.size()) &&
                            haystacks_[row].contains(needle));
        table_->setRowHidden(row, !match);
        if (match) {
            ++visible;
        }
    }
    countLabel_->setText(tr("Showing %1 of %2 cards").arg(visible).arg(table_->rowCount()));
    // A filter that hides the selected row doesn't deselect it, so re-sync the panel
    // (it clears when the current row is hidden) and the row actions — most importantly
    // "Delete permanently…", which must not stay enabled over a now-hidden row.
    showSelectedImage();
    updateButtonState();
}

void OwnedCardsView::updateButtonState() {
    const int row = table_->currentRow();
    // A row the search filter has hidden keeps its selection (applyFilter only toggles
    // row visibility), so it stays currentRow() while off-screen. Treat that as "no
    // actionable selection": every row action (Remove/Assign/Edit/Delete) reads
    // loaded_[currentRow], so an enabled button over a hidden row would silently mutate
    // a card the user can't see and didn't mean to touch. Gate them all on visibility.
    const bool hasSelection = row >= 0 && !table_->selectedItems().isEmpty() &&
                              !table_->isRowHidden(row);
    // A soft-Removed copy is frozen history: it can only be permanently deleted (or
    // left as-is), never edited or refiled. So Edit/Assign are disabled over one —
    // mirroring CardCopyService, which rejects editDetails/assignToBinder on a Removed
    // copy. Remove stays available (re-removing appends a fresh history note).
    const bool liveSelection = hasSelection && row < static_cast<int>(loaded_.size()) &&
                               !isRemoved(loaded_[row]);
    assignButton_->setEnabled(liveSelection);
    removeButton_->setEnabled(hasSelection);
    editButton_->setEnabled(liveSelection);
    // Permanent deletion is only offered for a copy that's already soft-Removed —
    // you remove first, then (optionally) purge it from history. (hasSelection already
    // excludes a filter-hidden row, so an off-screen copy can't be purged.)
    const bool removedSelected = hasSelection && row < static_cast<int>(loaded_.size()) &&
                                 isRemoved(loaded_[row]);
    deleteButton_->setEnabled(removedSelected);
}

void OwnedCardsView::showSelectedImage() {
    const int row = table_->currentRow();
    const bool valid = row >= 0 && row < static_cast<int>(loaded_.size()) &&
                       !table_->isRowHidden(row) && !table_->selectedItems().isEmpty();
    // The copy the panel should show, or "" when nothing valid is selected. Skip if
    // it hasn't changed — this runs on every keystroke (applyFilter), and load()
    // does a synchronous PNG decode we don't want to repeat for the same image.
    const std::string target = valid ? loaded_[row].id : std::string();
    if (target == shownCopyId_) {
        return;
    }
    shownCopyId_ = target;
    if (!valid) {
        panel_->clear();
        return;
    }
    const CardCopy& copy = loaded_[row];
    // A synchronous local disk read (like MediaService's cache-hit path); a null
    // pixmap (older copy, or one added without a preview) shows the panel placeholder.
    // The copy's comments show beneath the image (hidden when blank).
    panel_->showImage(titleFor(copy), images_.load(copy.id),
                      QString::fromStdString(copy.comments));
}

void OwnedCardsView::assignSelected() {
    const CardCopy* selected = selectedVisibleCopy();
    if (!selected || isRemoved(*selected)) {
        return;  // no visible selection, or frozen history (button disabled; re-check)
    }
    const CardCopy& copy = *selected;
    BinderPickerDialog dialog(binders_.list(), copy.binderId, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    try {
        const std::optional<CardBinderId> target = dialog.selectedBinderId();
        copies_.assignToBinder(copy.id, target);
        showToast(this, target ? tr("Card filed in its binder.") : tr("Card removed from its binder."));
    } catch (const std::exception& e) {
        QMessageBox::critical(this, tr("Pokedex TCG"),
                              tr("Could not file the card:\n%1").arg(QString::fromUtf8(e.what())));
    }
    reload();
}

void OwnedCardsView::removeSelected() {
    // Remove is allowed on an already-Removed copy too (re-removing appends a fresh
    // history note), so this guards only bounds+visibility, not ownership.
    const CardCopy* selected = selectedVisibleCopy();
    if (!selected) {
        return;
    }
    const CardCopy& copy = *selected;
    // One dialog serves as both the confirmation and the optional note: OK removes
    // (the copy is kept as Removed for auditable history), Cancel aborts. A blank
    // note just removes without appending.
    bool ok = false;
    const QString note = QInputDialog::getMultiLineText(
        this, tr("Remove card"),
        tr("Removing keeps the card in your history as Removed.\n"
           "Optionally add a note (why it left — sold, traded, lost…):"),
        QString(), &ok);
    if (!ok) {
        return;
    }
    try {
        copies_.remove(copy.id, note.toStdString());
        showToast(this, tr("Card removed, kept in your history."));
    } catch (const std::exception& e) {
        QMessageBox::critical(
            this, tr("Pokedex TCG"),
            tr("Could not remove the card:\n%1").arg(QString::fromUtf8(e.what())));
    }
    reload();
}

void OwnedCardsView::deletePermanently() {
    // Only a soft-Removed copy that's actually visible can be purged (the button is
    // disabled otherwise, but re-check in case state changed underneath us — a hidden
    // row's selection survives the search filter).
    const CardCopy* selected = selectedVisibleCopy();
    if (!selected || !isRemoved(*selected)) {
        return;
    }
    const CardCopy& copy = *selected;
    // Always confirm — a hard delete drops the row for good, with no history kept.
    const auto answer = QMessageBox::warning(
        this, tr("Delete permanently"),
        tr("Permanently delete “%1” from your history?\n\nThis cannot be undone.")
            .arg(titleFor(copy)),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) {
        return;
    }
    try {
        const std::string copyId = copy.id;  // copy.id before reload() invalidates loaded_
        copies_.hardDelete(copyId);
        // Reclaim the copy's on-disk card image so it isn't orphaned in cards/.
        images_.remove(copyId);
        showToast(this, tr("Card permanently deleted."));
    } catch (const std::exception& e) {
        QMessageBox::critical(
            this, tr("Pokedex TCG"),
            tr("Could not delete the card:\n%1").arg(QString::fromUtf8(e.what())));
    }
    reload();
}

void OwnedCardsView::editSelectedCard() {
    const CardCopy* selected = selectedVisibleCopy();
    if (!selected || isRemoved(*selected)) {
        return;  // no visible selection, or frozen history (button disabled; re-check)
    }
    const CardCopy& copy = *selected;
    const std::string copyId = copy.id;
    auto* page = new EditCardCopyPage(cardSearch_, images_, copies_, copy, binders_.list(),
                                      titleFor(copy));
    stack_->addWidget(page);
    // The image refresh is driven by CardImageStore::imageChanged (connected in the
    // ctor). On return, reload so an edited comment shows in the panel, and reselect
    // the same copy so the panel stays on it (reload rebuilds the table, clearing the
    // selection).
    connect(page, &EditCardCopyPage::backRequested, this, [this, page, copyId]() {
        stack_->setCurrentIndex(0);   // back to the list ⇄ panel
        stack_->removeWidget(page);
        page->deleteLater();
        reload();  // pick up an edited comment (and any image change)
        for (int i = 0; i < static_cast<int>(loaded_.size()); ++i) {
            if (loaded_[i].id == copyId) {
                table_->selectRow(i);
                break;
            }
        }
        // Force a panel re-render: the copy id is unchanged and its row/selection can
        // survive the reload, so showSelectedImage()'s shownCopyId_ dedup guard would
        // otherwise skip re-showing an edited comment (a comment save fires no
        // CardImageStore::imageChanged to clear the guard, unlike an image change).
        shownCopyId_.clear();
        showSelectedImage();
    });
    stack_->setCurrentWidget(page);
}

void OwnedCardsView::addNewCard() {
    // Species-free (dexNumber = nullopt): the finder searches by card name and the copy
    // depicts no Pokémon. Free binder choice (no locked binder). Reuses the same in-
    // window push/pop idiom as editSelectedCard.
    auto* page = new AddCardCopyPage(cardSearch_, copies_, binders_, images_, std::nullopt,
                                     /*speciesName=*/QString());
    stack_->addWidget(page);
    const auto pop = [this, page]() {
        stack_->setCurrentIndex(0);
        stack_->removeWidget(page);
        page->deleteLater();
    };
    // copyAdded fires before backRequested; reload so the new card is in the list the
    // moment we return to it.
    connect(page, &AddCardCopyPage::copyAdded, this, &OwnedCardsView::reload);
    connect(page, &AddCardCopyPage::backRequested, this, pop);
    stack_->setCurrentWidget(page);
}

}  // namespace pokedex
