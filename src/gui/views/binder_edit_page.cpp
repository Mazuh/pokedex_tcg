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
        tr("Name your binder, and optionally scope it to one or more regions."), this);
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

    if (editing) {
        nameEdit_->setText(QString::fromStdString(existing->name));
        for (std::size_t i = 0; i < kRegions.size(); ++i) {
            const bool scoped = std::find(existing->pokemonRegions.begin(),
                                          existing->pokemonRegions.end(),
                                          kRegions[i]) != existing->pokemonRegions.end();
            regionChecks_[i]->setChecked(scoped);
        }
    }

    auto* form = new QFormLayout;
    // macOS's style centers a QFormLayout by default; on this full-width page that
    // strands the fields mid-screen, so pin the form to the top-left like the other
    // in-window pages.
    form->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignTop);
    form->addRow(tr("Name"), nameEdit_);
    form->addRow(tr("Regions"), regionsBox);

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

    try {
        if (editingId_.empty()) {
            service_.create(name.toStdString(), regions);
            showToast(this, tr("Binder “%1” created.").arg(name));
        } else {
            service_.update(editingId_, name.toStdString(), regions);
            showToast(this, tr("Binder “%1” saved.").arg(name));
        }
    } catch (const std::exception& e) {
        QMessageBox::critical(this, tr("Pokedex TCG"),
                              tr("Could not save the binder:\n%1")
                                  .arg(QString::fromUtf8(e.what())));
        return;  // stay on the page so the user can correct and retry
    }
    // Hand the committed values back before leaving, so a host showing this binder
    // can update its heading/guide in place without re-querying storage.
    Q_EMIT saved(name, regions);
    Q_EMIT backRequested();
}

}  // namespace pokedex
