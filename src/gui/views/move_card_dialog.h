#pragma once

#include <QDialog>

#include <QString>

#include <span>
#include <vector>

#include "core/domain/card_binder.h"
#include "core/domain/card_binder_entry.h"
#include "core/domain/types.h"

class QLabel;
class QSpinBox;

namespace pokedex {

// GUI — "put this card at page 17, pocket 3×2": a small modal for naming the sleeve a
// filed card should move to.
//
// A modal rather than a pushed page, which is the repo's default for CRUD (see the
// screens-over-modals convention). This is not a record being edited: it is one short,
// cancellable choice with an immediate result, the same shape BinderPickerDialog has.
// Pushing a whole screen to type three numbers would cost more navigation than it saves.
//
// It asks for COORDINATES rather than a target row because that matches how the gesture
// actually happens — the user is holding the album and knows which sleeve they want,
// not which card is currently in it. The live "that pocket holds…" line closes the loop
// by naming what is there. It needs the binder's pocket grid to exist at all; without one
// there are no coordinates to name, and the caller keeps its Move button disabled.
class MoveCardDialog : public QDialog {
    Q_OBJECT

public:
    // `rows` is the guide exactly as shown (natural filed order) and `rowLabels` names
    // each of them, 1:1 — supplied by the caller rather than derived here so the "that
    // pocket holds…" line reads identically to the table's own Name column. `copyId` is
    // the card being moved and `heading` its display name. `placed` says whether the card
    // currently carries a manual position, which is what the "Return to natural order"
    // button acts on — offering it for a card that was never moved would suggest a state
    // it isn't in.
    MoveCardDialog(const CardBinder& binder, std::span<const CardBinderEntry> rows,
                   const std::vector<QString>& rowLabels, const CardCopyId& copyId,
                   const QString& heading, bool placed, QWidget* parent = nullptr);

    // The chosen pocket, 0-based over the rows that hold one — the numbering
    // planCardMove() expects. Valid after accept().
    int targetPocket() const;

    // Whether the user chose to return the card to natural order instead of moving it.
    // Valid after accept(); when true, targetPocket() is meaningless.
    bool resetRequested() const { return resetRequested_; }

private:
    // Re-derive the target pocket from the three spinboxes and say what is in it.
    void updateTargetPreview();

    // The row currently at the entered pocket, or -1 when it is past the last one.
    int rowAtEnteredPocket() const;

    std::vector<CardBinderEntry> rows_;
    std::vector<QString> rowLabels_;
    int pocketsPerPage_;
    int columns_;
    int pocketCount_;  // how many sleeves the arrangement occupies right now
    QSpinBox* pageEdit_;
    QSpinBox* rowEdit_;
    QSpinBox* columnEdit_;
    QLabel* preview_;
    bool resetRequested_ = false;
};

}  // namespace pokedex
