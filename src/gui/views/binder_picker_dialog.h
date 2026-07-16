#pragma once

#include <QDialog>

#include <optional>
#include <vector>

#include "core/domain/card_binder.h"
#include "core/domain/types.h"

class QComboBox;

namespace pokedex {

// GUI — a small reusable picker for choosing which binder a card is filed in (or
// none). It is presented modally; selectedBinderId() is valid after accept() and
// returns nullopt for the "— None —" choice. Kept generic (takes the binder list
// and the current selection) so any flow that files a copy can reuse it.
class BinderPickerDialog : public QDialog {
    Q_OBJECT

public:
    // `binders` is the choices to offer; `current` preselects the copy's present
    // binder (nullopt → the "— None —" entry).
    BinderPickerDialog(const std::vector<CardBinder>& binders,
                       const std::optional<CardBinderId>& current, QWidget* parent = nullptr);

    // The chosen binder id, or nullopt for "— None —". Valid after accept().
    std::optional<CardBinderId> selectedBinderId() const;

private:
    QComboBox* combo_;
};

}  // namespace pokedex
