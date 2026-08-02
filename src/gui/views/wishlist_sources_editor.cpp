#include "gui/views/wishlist_sources_editor.h"

#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QToolButton>
#include <QVBoxLayout>

#include <exception>
#include <optional>
#include <string>

#include "core/app/wishlist_service.h"
#include "gui/views/primary_button.h"
#include "gui/views/source_label.h"
#include "gui/views/toast.h"
#include "gui/views/wishlist_edit.h"

namespace pokedex {

WishlistSourcesEditor::WishlistSourcesEditor(WishlistService& wishlist, QWidget* parent)
    : QWidget(parent), wishlist_(wishlist) {
    auto* heading = new QLabel(tr("Wishlist"), this);
    QFont headingFont = heading->font();
    headingFont.setBold(true);
    heading->setFont(headingFont);

    // The source rows live in their own layout so reload() can clear and rebuild
    // just this section without touching the heading or the add row.
    rows_ = new QVBoxLayout;
    rows_->setContentsMargins(0, 0, 0, 0);
    rows_->setSpacing(2);

    // The add row: a free-text field (a seller name or an "http…" link) and its
    // button. Enter in the field adds too, the usual shortcut.
    input_ = new QLineEdit(this);
    input_->setPlaceholderText(tr("Add a seller or link…"));
    input_->setClearButtonEnabled(true);
    auto* addButton = new QToolButton(this);
    addButton->setText(tr("Add"));
    // The commit action of this little add-a-source form — give it the shared accent so
    // it reads as the primary button. No icon: a default QToolButton is icon-only, which
    // would replace the "Add" text.
    applyPrimaryButtonStyle(addButton, /*withIcon=*/false);

    connect(input_, &QLineEdit::returnPressed, this, &WishlistSourcesEditor::addFromInput);
    connect(addButton, &QToolButton::clicked, this, &WishlistSourcesEditor::addFromInput);

    auto* addRow = new QHBoxLayout;
    addRow->setContentsMargins(0, 0, 0, 0);
    addRow->addWidget(input_);
    addRow->addWidget(addButton);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 8, 0, 0);
    layout->addWidget(heading);
    layout->addLayout(rows_);
    layout->addLayout(addRow);

    clear();
}

void WishlistSourcesEditor::setPokemon(int dexNumber) {
    currentDex_ = dexNumber;
    input_->setEnabled(true);
    reload();
}

void WishlistSourcesEditor::clear() {
    currentDex_ = -1;
    input_->clear();
    input_->setEnabled(false);
    reload();
}

void WishlistSourcesEditor::reload() {
    // Tear down the previous rows (widgets and nested layouts) so the section can
    // be redrawn from scratch — the source set is small, so a full redraw is cheap
    // and avoids tracking per-row widgets.
    while (QLayoutItem* item = rows_->takeAt(0)) {
        if (QWidget* w = item->widget()) {
            w->deleteLater();
        } else if (QLayout* child = item->layout()) {
            // A nested row layout: empty it (widgets deferred, item wrappers freed).
            // Do NOT delete `child` here — a QLayout *is* a QLayoutItem, so `item`
            // and `child` are the same object; the single `delete item` below frees
            // it exactly once. (Deleting both double-freed it and crashed on the
            // second, virtual, destructor call.)
            while (QLayoutItem* inner = child->takeAt(0)) {
                if (QWidget* iw = inner->widget()) {
                    iw->deleteLater();
                }
                delete inner;
            }
        }
        delete item;
    }

    if (currentDex_ < 0) {
        return;
    }

    std::optional<Wishlist> wishlist = wishlist_.forPokemon(currentDex_);
    if (!wishlist || wishlist->sources.empty()) {
        auto* empty = new QLabel(tr("No sources yet."), this);
        empty->setEnabled(false);  // muted: a hint, not content
        rows_->addWidget(empty);
        return;
    }

    const int dex = currentDex_;
    for (const std::string& source : wishlist->sources) {
        const QString text = QString::fromStdString(source);

        auto* label = sourceLabel(text, this);
        auto* editButton = new QToolButton(this);
        editButton->setText(tr("Edit"));
        auto* removeButton = new QToolButton(this);
        removeButton->setText(tr("✕"));
        removeButton->setToolTip(tr("Remove this source"));

        // Edit: prompt for a new value pre-filled with the current one; a blank or
        // unchanged entry is a no-op. Remove: drop the source outright.
        connect(editButton, &QToolButton::clicked, this, [this, dex, source] {
            if (promptEditWishlistSource(this, wishlist_, dex,
                                         QString::fromStdString(source))) {
                reload();
            }
        });
        connect(removeButton, &QToolButton::clicked, this, [this, dex, source] {
            try {
                wishlist_.removeSource(dex, source);
                showToast(this, tr("Wishlist source removed."));
            } catch (const std::exception& e) {
                QMessageBox::warning(this, tr("Pokedex TCG"), QString::fromUtf8(e.what()));
            }
            reload();
        });

        auto* row = new QHBoxLayout;
        row->setContentsMargins(0, 0, 0, 0);
        row->addWidget(label, /*stretch=*/1);
        row->addWidget(editButton);
        row->addWidget(removeButton);
        rows_->addLayout(row);
    }
}

void WishlistSourcesEditor::addFromInput() {
    if (currentDex_ < 0) {
        return;
    }
    const std::string source = input_->text().toStdString();
    try {
        wishlist_.addSource(currentDex_, source);
    } catch (const WishlistError&) {
        return;  // blank/whitespace-only: silently ignore, nothing to add
    } catch (const std::exception& e) {
        QMessageBox::warning(this, tr("Pokedex TCG"), QString::fromUtf8(e.what()));
        return;
    }
    input_->clear();
    showToast(this, tr("Added to your wishlist."));
    reload();
}

}  // namespace pokedex
