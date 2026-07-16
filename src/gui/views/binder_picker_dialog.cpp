#include "gui/views/binder_picker_dialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QString>
#include <QVBoxLayout>
#include <QVariant>

#include "gui/views/region_labels.h"

namespace pokedex {

namespace {

// Display text for a binder in the combo — name, plus its region when it has one,
// so two binders with the same name are still distinguishable.
QString binderText(const CardBinder& binder) {
    const QString name = QString::fromStdString(binder.name);
    if (binder.pokemonRegion) {
        return QStringLiteral("%1 — %2").arg(name, regionLabel(*binder.pokemonRegion));
    }
    return name;
}

}  // namespace

BinderPickerDialog::BinderPickerDialog(const std::vector<CardBinder>& binders,
                                       const std::optional<CardBinderId>& current,
                                       QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(tr("Assign to Binder"));

    combo_ = new QComboBox(this);
    // The first entry means "filed nowhere"; its data is an empty string.
    combo_->addItem(tr("— None —"), QString());
    for (const CardBinder& binder : binders) {
        combo_->addItem(binderText(binder), QString::fromStdString(binder.id));
    }
    // Preselect the copy's current binder, if it still exists in the list.
    if (current) {
        const int index = combo_->findData(QString::fromStdString(*current));
        if (index >= 0) {
            combo_->setCurrentIndex(index);
        }
    }

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
    const QString id = combo_->currentData().toString();
    if (id.isEmpty()) {
        return std::nullopt;
    }
    return id.toStdString();
}

}  // namespace pokedex
