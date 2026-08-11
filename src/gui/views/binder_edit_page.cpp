#include "gui/views/binder_edit_page.h"

#include <QCheckBox>
#include <QFont>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <exception>

#include "core/app/binder_service.h"
#include "core/domain/region.h"
#include "gui/views/back_button.h"
#include "gui/views/primary_button.h"
#include "gui/views/region_labels.h"
#include "gui/views/toast.h"

namespace pokedex {

BinderEditPage::BinderEditPage(BinderService& service, QWidget* parent)
    : QWidget(parent), service_(service) {
    build(std::nullopt);
}

BinderEditPage::BinderEditPage(BinderService& service, const CardBinder& existing,
                               QWidget* parent)
    : QWidget(parent), service_(service), editingId_(existing.id) {
    build(existing);
}

void BinderEditPage::build(const std::optional<CardBinder>& existing) {
    const bool editing = existing.has_value();

    auto* backButton = makeBackButton(this);
    connect(backButton, &QPushButton::clicked, this, &BinderEditPage::backRequested);

    auto* heading = new QLabel(editing ? tr("Edit Binder") : tr("New Binder"), this);
    QFont headingFont = heading->font();
    headingFont.setBold(true);
    headingFont.setPointSize(headingFont.pointSize() + 3);
    heading->setFont(headingFont);

    auto* topBar = new QHBoxLayout;
    topBar->addWidget(backButton);
    topBar->addWidget(heading);
    topBar->addStretch();

    auto* subtitle = new QLabel(
        tr("Name your binder, optionally scope it to one or more regions, and record the "
           "album's size if you know it."),
        this);
    subtitle->setEnabled(false);  // muted: a hint, not content

    nameEdit_ = new QLineEdit(this);
    nameEdit_->setPlaceholderText(tr("e.g. Kanto Journey"));
    nameEdit_->setMaximumWidth(360);  // a full-width field on this wide page reads oddly

    // A checkbox per region — the region is multivalued, so a binder can span
    // several. Laid out in a compact grid (3 columns) in canonical kRegions order,
    // so regionChecks_[i] maps to kRegions[i].
    auto* regionsBox = new QWidget(this);
    auto* regionsGrid = new QGridLayout(regionsBox);
    regionsGrid->setContentsMargins(0, 0, 0, 0);
    constexpr int kColumns = 3;
    regionChecks_.reserve(kRegions.size());
    for (std::size_t i = 0; i < kRegions.size(); ++i) {
        auto* check = new QCheckBox(regionLabel(kRegions[i]), regionsBox);
        regionChecks_.push_back(check);
        regionsGrid->addWidget(check, static_cast<int>(i) / kColumns,
                               static_cast<int>(i) % kColumns);
    }

    // The album's physical size, both parts optional. A spinbox at its minimum (0) shows
    // "Not set" via setSpecialValueText, which maps the storage layer's 0-is-unset
    // sentinel onto a visible UI state — an empty line edit couldn't distinguish "not
    // recorded" from "cleared", and would need parsing on submit.
    capacityEdit_ = new QSpinBox(this);
    capacityEdit_->setRange(0, 100000);
    capacityEdit_->setSpecialValueText(tr("Not set"));
    capacityEdit_->setSuffix(tr(" cards"));  // superseded by the special text at 0
    capacityEdit_->setAccelerated(true);
    capacityEdit_->setMaximumWidth(180);
    capacityEdit_->setToolTip(
        tr("How many cards the whole album holds. Shown against the count filed here, so "
           "you can see how full it is; going over is never blocked."));

    pocketRowsEdit_ = new QSpinBox(this);
    pocketColumnsEdit_ = new QSpinBox(this);
    for (QSpinBox* box : {pocketRowsEdit_, pocketColumnsEdit_}) {
        box->setRange(0, 20);
        box->setSpecialValueText(tr("—"));
        box->setMaximumWidth(90);
    }
    pocketRowsEdit_->setToolTip(tr("Pocket rows on one page — 3 for a typical 3×3 album."));
    pocketColumnsEdit_->setToolTip(tr("Pockets across one page — 3 for a typical 3×3 album."));
    pocketHint_ = new QLabel(this);
    pocketHint_->setEnabled(false);  // muted: a derived figure, not an input

    // [rows] × [columns]   · 9 pockets per page
    auto* gridBox = new QWidget(this);
    auto* gridRow = new QHBoxLayout(gridBox);
    gridRow->setContentsMargins(0, 0, 0, 0);
    gridRow->addWidget(pocketRowsEdit_);
    gridRow->addWidget(new QLabel(QStringLiteral("×"), gridBox));
    gridRow->addWidget(pocketColumnsEdit_);
    gridRow->addWidget(pocketHint_);
    gridRow->addStretch();
    // The two sides are deliberately NOT linked (setting rows to 3 does not set columns):
    // silently coercing a value the user typed is worse than the submit-time message.
    for (QSpinBox* box : {pocketRowsEdit_, pocketColumnsEdit_}) {
        connect(box, &QSpinBox::valueChanged, this, &BinderEditPage::updatePocketHint);
    }

    if (editing) {
        nameEdit_->setText(QString::fromStdString(existing->name));
        for (std::size_t i = 0; i < kRegions.size(); ++i) {
            const bool scoped = std::find(existing->pokemonRegions.begin(),
                                          existing->pokemonRegions.end(),
                                          kRegions[i]) != existing->pokemonRegions.end();
            regionChecks_[i]->setChecked(scoped);
        }
        capacityEdit_->setValue(existing->capacity.value_or(0));
        if (existing->pocketGrid) {
            pocketRowsEdit_->setValue(existing->pocketGrid->rows);
            pocketColumnsEdit_->setValue(existing->pocketGrid->columns);
        }
    }
    updatePocketHint();

    auto* form = new QFormLayout;
    // macOS's style centers a QFormLayout by default; on this full-width page that
    // strands the fields mid-screen, so pin the form to the top-left like the other
    // in-window pages.
    form->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignTop);
    form->addRow(tr("Name"), nameEdit_);
    form->addRow(tr("Regions"), regionsBox);
    form->addRow(tr("Capacity"), capacityEdit_);
    form->addRow(tr("Pocket grid"), gridBox);

    auto* submitButton =
        new QPushButton(editing ? tr("Save changes") : tr("Create binder"), this);
    submitButton->setDefault(true);
    applyPrimaryButtonStyle(submitButton);  // the primary/commit action — accent + ✓
    connect(submitButton, &QPushButton::clicked, this, &BinderEditPage::submit);
    // Enter in the name field submits, the usual single-field form gesture.
    connect(nameEdit_, &QLineEdit::returnPressed, this, &BinderEditPage::submit);

    auto* actions = new QHBoxLayout;
    actions->addWidget(submitButton);
    actions->addStretch();  // keep the button left, under the form

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->addLayout(topBar);
    layout->addWidget(subtitle);
    layout->addLayout(form);
    layout->addLayout(actions);
    layout->addStretch();  // keep the form at the top; extra room stays below
}

void BinderEditPage::submit() {
    const QString name = nameEdit_->text().trimmed();
    if (name.isEmpty()) {
        // Guard here too so the page can't submit a blank name (the service also
        // enforces it, but this keeps the feedback immediate and in-page).
        QMessageBox::warning(this, tr("Binder"), tr("Please enter a name."));
        return;
    }

    // Gather the checked regions in canonical order (regionChecks_ mirrors kRegions).
    std::vector<Region> regions;
    for (std::size_t i = 0; i < kRegions.size(); ++i) {
        if (regionChecks_[i]->isChecked()) {
            regions.push_back(kRegions[i]);
        }
    }

    // A grid needs both sides to describe a page. Guard it here for an immediate,
    // in-page message; the service enforces the same rule as the record of truth.
    const bool halfGrid =
        (pocketRowsEdit_->value() > 0) != (pocketColumnsEdit_->value() > 0);
    if (halfGrid) {
        QMessageBox::warning(
            this, tr("Binder"),
            tr("Enter both the rows and the columns of the pocket grid, or leave both unset."));
        return;
    }

    const std::optional<int> capacity =
        capacityEdit_->value() > 0 ? std::optional<int>(capacityEdit_->value()) : std::nullopt;

    CardBinder persisted;
    try {
        if (editingId_.empty()) {
            persisted = service_.create(name.toStdString(), regions, capacity, enteredGrid());
            showToast(this, tr("Binder “%1” created.").arg(name));
        } else {
            persisted =
                service_.update(editingId_, name.toStdString(), regions, capacity, enteredGrid());
            showToast(this, tr("Binder “%1” saved.").arg(name));
        }
    } catch (const std::exception& e) {
        QMessageBox::critical(this, tr("Pokedex TCG"),
                              tr("Could not save the binder:\n%1")
                                  .arg(QString::fromUtf8(e.what())));
        return;  // stay on the page so the user can correct and retry
    }
    // Hand the whole persisted binder back before leaving, so a host showing it can
    // replace its by-value copy outright without re-querying storage.
    Q_EMIT saved(persisted);
    Q_EMIT backRequested();
}

std::optional<CardBinderPocketGrid> BinderEditPage::enteredGrid() const {
    if (pocketRowsEdit_->value() <= 0 || pocketColumnsEdit_->value() <= 0) {
        return std::nullopt;
    }
    return CardBinderPocketGrid{.rows = pocketRowsEdit_->value(),
                                .columns = pocketColumnsEdit_->value()};
}

void BinderEditPage::updatePocketHint() {
    // Spell out the product at the moment of entry, so "3 × 3" visibly means nine cards
    // to a page. Blank while either side is unset — there is no page to describe yet.
    const std::optional<CardBinderPocketGrid> grid = enteredGrid();
    pocketHint_->setText(grid ? tr(" · %1 pockets per page").arg(pocketsPerPage(*grid))
                              : QString());
}

}  // namespace pokedex
