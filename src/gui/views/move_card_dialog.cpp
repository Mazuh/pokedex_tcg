#include "gui/views/move_card_dialog.h"

#include <QDialogButtonBox>
#include <QFont>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>

#include "gui/views/binder_layout_labels.h"
#include "gui/views/primary_button.h"

namespace pokedex {

namespace {

// The 0-based pocket a row occupies, or -1 for a row that holds none. Counts exactly the
// rows the guide's Page/Pocket columns number, so the two can't disagree about which
// sleeve the user is pointing at.
int pocketOfRow(std::span<const CardBinderEntry> rows, int row) {
    int pocket = 0;
    for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
        if (!holdsPocket(rows[i])) {
            continue;
        }
        if (i == row) {
            return pocket;
        }
        ++pocket;
    }
    return -1;
}

int pocketOfCopy(std::span<const CardBinderEntry> rows, const CardCopyId& copyId) {
    for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
        if (rows[i].cardCopyId == copyId) {
            return pocketOfRow(rows, i);
        }
    }
    return -1;
}

int countPockets(std::span<const CardBinderEntry> rows) {
    int n = 0;
    for (const CardBinderEntry& row : rows) {
        n += holdsPocket(row) ? 1 : 0;
    }
    return n;
}

}  // namespace

MoveCardDialog::MoveCardDialog(const CardBinder& binder, std::span<const CardBinderEntry> rows,
                               const std::vector<QString>& rowLabels, const CardCopyId& copyId,
                               const QString& heading, bool placed, QWidget* parent)
    : QDialog(parent),
      rows_(rows.begin(), rows.end()),
      rowLabels_(rowLabels),
      pocketsPerPage_(binder.pocketGrid ? pocketsPerPage(*binder.pocketGrid) : 1),
      columns_(binder.pocketGrid ? binder.pocketGrid->columns : 1),
      pocketCount_(countPockets(rows)) {
    setWindowTitle(tr("Move card"));

    auto* title = new QLabel(heading, this);
    QFont titleFont = title->font();
    titleFont.setBold(true);
    title->setFont(titleFont);

    const int currentPocket = std::max(pocketOfCopy(rows, copyId), 0);
    const int currentInPage = currentPocket % pocketsPerPage_;
    auto* now = new QLabel(tr("Currently at page %1 · pocket %2.")
                               .arg(currentPocket / pocketsPerPage_ + 1)
                               .arg(pocketLabel(currentInPage, columns_)),
                           this);
    now->setEnabled(false);  // muted: context, not an input

    // How far the page numbers may go. The album's recorded capacity is the real answer
    // when there is one; otherwise the last page in use plus one, so a card can always be
    // sent past the end without inventing a size the binder never claimed. Whichever is
    // larger wins, because an over-full binder really does run past its stated capacity.
    const int pagesInUse = (std::max(pocketCount_, 1) - 1) / pocketsPerPage_ + 1;
    const int pagesByCapacity =
        binder.capacity ? (*binder.capacity + pocketsPerPage_ - 1) / pocketsPerPage_ : 0;
    const int maxPage = std::max(pagesInUse + 1, pagesByCapacity);

    pageEdit_ = new QSpinBox(this);
    pageEdit_->setRange(1, maxPage);
    pageEdit_->setValue(currentPocket / pocketsPerPage_ + 1);
    pageEdit_->setAccelerated(true);

    rowEdit_ = new QSpinBox(this);
    rowEdit_->setRange(1, binder.pocketGrid ? binder.pocketGrid->rows : 1);
    rowEdit_->setValue(currentInPage / columns_ + 1);  // as pocketLabel spells it

    columnEdit_ = new QSpinBox(this);
    columnEdit_->setRange(1, columns_);
    columnEdit_->setValue(currentInPage % columns_ + 1);

    for (QSpinBox* box : {pageEdit_, rowEdit_, columnEdit_}) {
        box->setMaximumWidth(110);
        connect(box, &QSpinBox::valueChanged, this, &MoveCardDialog::updateTargetPreview);
    }

    // [row] × [column], laid out the way the Pocket column reads it.
    auto* pocketBox = new QWidget(this);
    auto* pocketRow = new QHBoxLayout(pocketBox);
    pocketRow->setContentsMargins(0, 0, 0, 0);
    pocketRow->addWidget(rowEdit_);
    pocketRow->addWidget(new QLabel(QStringLiteral("×"), pocketBox));
    pocketRow->addWidget(columnEdit_);
    pocketRow->addStretch();

    auto* form = new QFormLayout;
    form->addRow(tr("Page"), pageEdit_);
    form->addRow(tr("Pocket"), pocketBox);

    // Says what is in the sleeve being aimed at — the piece the coordinates alone can't
    // tell the user, and what makes "the blank is replaced" legible before committing.
    preview_ = new QLabel(this);
    preview_->setWordWrap(true);
    preview_->setEnabled(false);

    auto* buttons = new QDialogButtonBox(this);
    auto* moveButton = buttons->addButton(tr("Move"), QDialogButtonBox::AcceptRole);
    applyPrimaryButtonStyle(moveButton);  // the commit action of this form
    buttons->addButton(QDialogButtonBox::Cancel);
    if (placed) {
        auto* resetButton =
            buttons->addButton(tr("Return to natural order"), QDialogButtonBox::ResetRole);
        resetButton->setToolTip(
            tr("Put this card back where its Pokédex number places it. Any empty pockets "
               "in front of it stay where they are, and the sleeve it leaves closes up — "
               "so this returns the card, it doesn't undo the move."));
        connect(resetButton, &QPushButton::clicked, this, [this] {
            resetRequested_ = true;
            accept();
        });
    }
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(title);
    layout->addWidget(now);
    layout->addSpacing(8);
    layout->addLayout(form);
    layout->addWidget(preview_);
    layout->addStretch();
    layout->addWidget(buttons);

    updateTargetPreview();
}

int MoveCardDialog::targetPocket() const {
    return (pageEdit_->value() - 1) * pocketsPerPage_ + (rowEdit_->value() - 1) * columns_ +
           (columnEdit_->value() - 1);
}

int MoveCardDialog::rowAtEnteredPocket() const {
    const int target = targetPocket();
    int pocket = 0;
    for (int i = 0; i < static_cast<int>(rows_.size()); ++i) {
        if (!holdsPocket(rows_[i])) {
            continue;
        }
        if (pocket == target) {
            return i;
        }
        ++pocket;
    }
    return -1;
}

void MoveCardDialog::updateTargetPreview() {
    const int row = rowAtEnteredPocket();
    if (row < 0) {
        // "the arranged cards", not "the binder": the caller hands this dialog the rows up
        // to the loose run (cards with no fixed position), which the guide keeps after
        // everything else — so a card sent past the end lands ahead of them, not last.
        preview_->setText(tr("That pocket is past the last card — this one will go at the "
                             "end of the arranged cards."));
        return;
    }
    if (isBlankPocket(rows_[row])) {
        preview_->setText(tr("That pocket is empty — this card will fill it, and nothing "
                             "else will move."));
        return;
    }
    const QString label =
        row < static_cast<int>(rowLabels_.size()) ? rowLabels_[row] : QString();
    preview_->setText(tr("That pocket holds: %1").arg(label));
}

}  // namespace pokedex
