#pragma once

#include <QDialog>

#include <optional>
#include <string>

#include "core/domain/region.h"

class QComboBox;
class QLineEdit;

namespace pokedex {

// GUI — the "new binder" form: a name and an optional region. The region is only
// an initializer chosen at creation, so this dialog is used for create, not
// rename (renaming touches the name alone). Getters are valid after accept().
class BinderEditorDialog : public QDialog {
    Q_OBJECT

public:
    explicit BinderEditorDialog(QWidget* parent = nullptr);

    std::string name() const;
    std::optional<Region> region() const;

private:
    void submit();

    QLineEdit* nameEdit_;
    QComboBox* regionCombo_;
};

}  // namespace pokedex
