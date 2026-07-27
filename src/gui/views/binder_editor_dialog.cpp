#include "gui/views/binder_editor_dialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <QVariant>

#include "gui/views/empty_option.h"
#include "gui/views/region_labels.h"

namespace pokedex {

namespace {
// Sentinel stored in the combo's first item: "no region chosen".
constexpr int kNoRegion = -1;
}  // namespace

BinderEditorDialog::BinderEditorDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("New Binder"));

    nameEdit_ = new QLineEdit(this);
    nameEdit_->setPlaceholderText(tr("e.g. Kanto Journey"));

    regionCombo_ = new QComboBox(this);
    // The region is optional: the first entry means "start empty" — the shared
    // noneOptionLabel() so it reads the same as every other form's empty picker.
    regionCombo_->addItem(noneOptionLabel(), kNoRegion);
    for (const Region region : kRegions) {
        regionCombo_->addItem(regionLabel(region), static_cast<int>(region));
    }

    auto* form = new QFormLayout;
    form->addRow(tr("Name"), nameEdit_);
    form->addRow(tr("Region"), regionCombo_);

    auto* buttons = new QDialogButtonBox(this);
    auto* createButton = buttons->addButton(tr("Create"), QDialogButtonBox::AcceptRole);
    buttons->addButton(QDialogButtonBox::Cancel);
    connect(createButton, &QPushButton::clicked, this, &BinderEditorDialog::submit);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
}

std::string BinderEditorDialog::name() const {
    return nameEdit_->text().trimmed().toStdString();
}

std::optional<Region> BinderEditorDialog::region() const {
    const int data = regionCombo_->currentData().toInt();
    if (data == kNoRegion) {
        return std::nullopt;
    }
    return static_cast<Region>(data);
}

void BinderEditorDialog::submit() {
    // Guard here too so the dialog can't accept a blank name (the service also
    // enforces it, but this keeps the feedback immediate and in-dialog).
    if (nameEdit_->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, tr("New Binder"), tr("Please enter a name."));
        return;
    }
    accept();
}

}  // namespace pokedex
