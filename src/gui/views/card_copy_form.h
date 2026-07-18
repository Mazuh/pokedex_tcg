#pragma once

#include <QWidget>

#include <optional>
#include <string>
#include <vector>

#include "core/domain/card_condition.h"
#include "core/domain/card_copy.h"
#include "core/domain/card_ownership.h"
#include "core/domain/card_reference.h"
#include "core/domain/types.h"

class QComboBox;
class QHBoxLayout;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;

namespace pokedex {

struct CardBinder;

// GUI — the shared "card copy details" pane: the printed-identity fields (expansion
// code, set name, language, collector number), condition, ownership, the binder
// picker, and a comments box, plus a bottom row where the host drops its own action
// button(s). Extracted from AddCardCopyPage so both "Add copy" and "Edit card" render
// the same fields the same way — the second shared building block alongside
// CardFinderPanel (the two together are what made the pages look alike).
//
// The one axis the two hosts differ on is editability, so it is a parameter:
// setReferenceEditable(false) turns the identity/condition/ownership fields read-only
// for the "view an existing copy" (edit) case, while comments stay editable in both.
// The binder combo's editability is set with the picker (locked for the scoped-add
// and edit cases). The form is pure fields — it holds no service, reads no clock, and
// creates/saves nothing; the host reads the values back (cardReference(), ownership(),
// …) and performs the write.
class CardCopyForm : public QWidget {
    Q_OBJECT

public:
    explicit CardCopyForm(QWidget* parent = nullptr);

    // Populate the binder combo and select `selected` (nullopt → "— None —"). When
    // `enabled` is false the combo is shown but locked (scoped-add and edit cases).
    void setupBinderPicker(const std::vector<CardBinder>& binders,
                           std::optional<CardBinderId> selected, bool enabled);

    // Toggle whether the printed-identity / language / condition / ownership fields
    // can be edited. Comments are always editable. Read-only is the edit case.
    void setReferenceEditable(bool editable);

    // Fill the printed-identity fields from a picked card (leaves language / condition
    // / ownership alone). Uses setText, so it does NOT emit referenceEdited().
    void setCardReference(const CardReference& ref);

    // Fill every stored field from an existing copy (the edit case): identity,
    // language, condition, ownership, and comments. Does not touch the binder combo
    // (use setupBinderPicker for that). Silent — emits no signals.
    void loadCopy(const CardCopy& copy);

    // Append a host action button (e.g. "Add copy" / "Save comments") to the bottom
    // row, left-aligned in insertion order. The form takes ownership.
    void addAction(QPushButton* button);

    // Read the current field values.
    CardReference cardReference() const;
    CardOwnership ownership() const;
    std::optional<CardCondition> condition() const;
    std::optional<CardBinderId> binderId() const;
    std::string comments() const;

Q_SIGNALS:
    // A printed-identity field (expansion / set / collector) was edited by the USER
    // (textEdited, not setCardReference). Lets the Add host drop a stale finder pick.
    void referenceEdited();
    // The comments text changed (any source). Lets a host enable its Save button.
    void commentsChanged();

private:
    QLineEdit* expansionCode_;
    QLineEdit* setName_;
    QComboBox* language_;
    QLineEdit* collectorNumber_;
    QComboBox* condition_;
    QComboBox* ownership_;
    QComboBox* binder_;
    QPlainTextEdit* comments_;
    QHBoxLayout* actions_;  // bottom row the host fills via addAction()
};

}  // namespace pokedex
