#pragma once

#include <QWidget>

#include <optional>
#include <string>
#include <vector>

#include "core/domain/card_condition.h"
#include "core/domain/card_copy.h"
#include "core/domain/card_foil.h"
#include "core/domain/card_ownership.h"
#include "core/domain/card_rarity.h"
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
// setReferenceEditable(false) turns the printed-identity fields read-only for the
// "view an existing copy" (edit) case. The physical-copy attributes (language,
// condition, ownership), the comments, and the binder pick all stay editable in both
// cases — a recorded copy's grade/state/language can be corrected on the edit page.
// The binder combo's editability is set with the picker (locked for the scoped-add
// and edit cases). The form is pure fields — it holds no service, reads no clock, and
// creates/saves nothing; the host reads the values back (cardReference(), ownership(),
// …) and performs the write.
class CardCopyForm : public QWidget {
    Q_OBJECT

public:
    explicit CardCopyForm(QWidget* parent = nullptr);

protected:
    // Swallow wheel events on the combos unless one is focused (see the .cpp), so
    // scrolling the form never silently changes a copy's language / condition /
    // ownership / binder — a footgun now that these fields are editable on the edit page.
    bool eventFilter(QObject* watched, QEvent* event) override;

public:
    // Populate the binder combo and select `selected` (nullopt → "— None —"). When
    // `enabled` is false the combo is shown but locked (scoped-add and edit cases).
    void setupBinderPicker(const std::vector<CardBinder>& binders,
                           std::optional<CardBinderId> selected, bool enabled);

    // Reveal an explicit "Remove from binder" button beside the binder combo (default
    // hidden). Clicking it selects "— None —" and emits binderChanged(), so a host that
    // persists the combo already handles the unassign — the button is a discoverable
    // shortcut for the "pick — None —" gesture. Enabled only while a binder is actually
    // selected (nothing to remove otherwise). The edit page opts in; the add flow leaves
    // it hidden.
    void setBinderRemovable(bool removable);

    // Toggle whether the printed-identity fields (card name, expansion, set, collector
    // number) can be edited. The physical-copy attributes (language, condition,
    // ownership), comments, and binder pick are always editable. Read-only is the edit
    // case, where the printing identity is the record but the copy's own fields aren't.
    void setReferenceEditable(bool editable);

    // Fill the printed-identity fields from a picked card (leaves language / condition
    // / ownership alone). Uses setText, so it does NOT emit referenceEdited().
    void setCardReference(const CardReference& ref);

    // Pre-fill the rarity picker (e.g. from a picked card's catalog rarity). nullopt
    // selects "— None —". Silent — uses setCurrentIndex, so it emits no signals,
    // and the user can still change it.
    void setRarity(std::optional<CardRarity> rarity);

    // Overwrite the comments box outright, resetting its undo history with it — for
    // filling a box the user hasn't touched yet (a load, a prefill).
    void setComments(const std::string& comments);

    // Overwrite the comments box as a single UNDOABLE edit, so Ctrl/Cmd+Z brings back
    // whatever was there. Use this instead of setComments for a host BUTTON that
    // replaces the note (the add page's "Reuse comments"): the user may have typed a
    // long note and mis-clicked, and setComments' reset would put it out of reach.
    void replaceComments(const std::string& comments);

    // Pre-fill the language / condition pickers (e.g. reusing the last card's physical
    // attributes across a booster — the card search can't supply these). Silent (uses
    // setCurrentIndex, so no detailsChanged()); an empty/unset value or an unmapped code
    // falls back to "— None —" (setLanguage adds an unknown code so it round-trips, as
    // loadCopy does). The user can still change either.
    void setLanguage(const std::string& language);
    void setCondition(std::optional<CardCondition> condition);

    // Fill every stored field from an existing copy (the edit case): identity,
    // language, condition, rarity, foil, ownership, and comments. Does not touch the
    // binder combo (use setupBinderPicker for that). Silent — emits no signals.
    void loadCopy(const CardCopy& copy);

    // Append a host action button (e.g. "Add copy" / "Save comments") to the bottom
    // row, left-aligned in insertion order. The form takes ownership.
    void addAction(QPushButton* button);

    // Read the current field values.
    CardReference cardReference() const;
    CardOwnership ownership() const;
    std::optional<CardCondition> condition() const;
    std::optional<CardRarity> rarity() const;
    std::optional<CardFoil> foil() const;
    std::optional<CardBinderId> binderId() const;
    std::string comments() const;

Q_SIGNALS:
    // A printed-identity field (expansion / set / collector) was edited by the USER
    // (textEdited, not setCardReference). Lets the Add host drop a stale finder pick.
    void referenceEdited();
    // The comments text changed (any source). Lets a host enable its Save button.
    void commentsChanged();
    // A physical-copy attribute (language / condition / ownership) was changed by the
    // USER (activated, not a programmatic load). Lets an edit host enable its Save button.
    void detailsChanged();
    // The user picked a different binder in the combo (not a programmatic load). Lets
    // an edit host persist the reassignment immediately.
    void binderChanged();

private:
    // Enable the "Remove from binder" button only while the combo is editable and a
    // binder is actually selected — there is nothing to remove from "— None —".
    void updateBinderRemoveEnabled();

    QLineEdit* cardName_;
    QLineEdit* expansionCode_;
    QLineEdit* setName_;
    QComboBox* language_;
    QLineEdit* collectorNumber_;
    QComboBox* condition_;
    QComboBox* rarity_;
    QComboBox* foil_;
    QComboBox* ownership_;
    QComboBox* binder_;
    QPushButton* unassignBinder_;  // "Remove from binder", hidden until setBinderRemovable(true)
    QPlainTextEdit* comments_;
    QHBoxLayout* actions_;  // bottom row the host fills via addAction()
};

}  // namespace pokedex
