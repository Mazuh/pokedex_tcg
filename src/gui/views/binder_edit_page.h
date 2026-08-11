#pragma once

#include <QWidget>

#include <optional>
#include <vector>

#include "core/domain/card_binder.h"
#include "core/domain/region.h"

class QCheckBox;
class QLabel;
class QLineEdit;
class QSpinBox;

namespace pokedex {

class BinderService;

// GUI — the "new / edit binder" screen: a full in-window page with the binder's
// name and region, pushed onto a host's QStackedWidget (BindersPage or BinderView)
// rather than shown as a modal dialog. It replaces the old BinderEditorDialog and
// the rename QInputDialog — binder CRUD is done on dedicated screens now (see the
// "screens over modals for CRUD" convention in CLAUDE.md), so create and edit share
// the same form.
//
// The region is multivalued — a binder may span several regions — so it is picked
// with a checkbox per region rather than a single-select combo.
//
// It also records the album's optional PHYSICAL layout: how many cards it holds
// (capacity) and the shape of one page (rows × columns of pockets), which is what lets
// the binder guide say where a card sits. Both are optional and entered with QSpinBoxes
// whose minimum (0) is shown as "Not set" — the same 0-is-unset sentinel storage uses,
// made a visible UI state rather than an empty field indistinguishable from a cleared
// one. A spinbox rather than a validated line edit so an invalid value can't be typed
// at all and submit() needs no parse branch.
//
// It writes straight through BinderService on submit (create for a new binder,
// update for an existing one), shows a toast, and emits backRequested() so the host
// pops + disposes it and re-reads the binder(s). Back also emits backRequested()
// (an unsaved cancel). Mirrors WishlistEditPage's push/return contract.
class BinderEditPage : public QWidget {
    Q_OBJECT

public:
    // Create mode: a blank form whose submit creates a new binder.
    BinderEditPage(BinderService& service, QWidget* parent = nullptr);
    // Edit mode: the form is pre-filled from `existing` and submit updates it.
    BinderEditPage(BinderService& service, const CardBinder& existing,
                   QWidget* parent = nullptr);

Q_SIGNALS:
    // Emitted when the user leaves this page — after a successful save, or on Back
    // (an unsaved cancel). The host pops + disposes the page and re-reads state.
    void backRequested();

    // Emitted right before backRequested() on a successful save, carrying the WHOLE
    // persisted binder as storage now holds it. A host already showing this binder
    // (BinderView) keeps a by-value copy and never re-reads it, so it replaces that copy
    // wholesale rather than patching the fields it happens to know about — which is what
    // stops a newly added field from being silently dropped on that path.
    void saved(const CardBinder& binder);

private:
    // Shared ctor body: builds the form, seeding it from `existing` when editing.
    void build(const std::optional<CardBinder>& existing);
    // Validate the form, then create or update the binder through the service.
    void submit();
    // The pocket grid as entered, or nullopt when both sides are left unset.
    std::optional<CardBinderPocketGrid> enteredGrid() const;
    // Keep the "· N pockets per page" hint in step with the two spinboxes.
    void updatePocketHint();

    BinderService& service_;
    // Set only in edit mode — the id of the binder being edited (empty = create).
    std::string editingId_;
    QLineEdit* nameEdit_;
    // One checkbox per region, in canonical kRegions order (so regionChecks_[i]
    // toggles kRegions[i]). The checked set is the binder's region set.
    std::vector<QCheckBox*> regionChecks_;
    // The optional physical layout. 0 means "not recorded" in all three.
    QSpinBox* capacityEdit_;
    QSpinBox* pocketRowsEdit_;
    QSpinBox* pocketColumnsEdit_;
    QLabel* pocketHint_;
};

}  // namespace pokedex
