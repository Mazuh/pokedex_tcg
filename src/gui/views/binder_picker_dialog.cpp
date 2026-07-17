#include "gui/views/binder_picker_dialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QVBoxLayout>

#include "gui/views/binder_combo.h"

namespace pokedex {

BinderPickerDialog::BinderPickerDialog(const std::vector<CardBinder>& binders,
                                       const std::optional<CardBinderId>& current,
                                       QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(tr("Assign to Binder"));

    combo_ = new QComboBox(this);
    fillBinderCombo(*combo_, binders, current);

    auto* form = new QFormLayout;
    form->addRow(tr("Binder"), combo_);

    auto* buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
}

std::optional<CardBinderId> BinderPickerDialog::selectedBinderId() const {
    return binderComboSelection(*combo_);
}

}  // namespace pokedex
