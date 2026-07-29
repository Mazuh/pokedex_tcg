#pragma once

#include <QWidget>

#include <optional>
#include <vector>

#include "core/domain/card_binder.h"
#include "core/domain/region.h"

class QCheckBox;
class QLineEdit;

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

    // Emitted right before backRequested() on a successful save, carrying the
    // committed name and region set. A host already showing this binder (BinderView)
    // uses it to update in place — no storage re-read — so the heading and guide
    // reflect the edit without a full list() round-trip.
    void saved(const QString& name, const std::vector<Region>& regions);

private:
    // Shared ctor body: builds the form, seeding it from `existing` when editing.
    void build(const std::optional<CardBinder>& existing);
    // Validate the name, then create or update the binder through the service.
    void submit();

    BinderService& service_;
    // Set only in edit mode — the id of the binder being edited (empty = create).
    std::string editingId_;
    QLineEdit* nameEdit_;
    // One checkbox per region, in canonical kRegions order (so regionChecks_[i]
    // toggles kRegions[i]). The checked set is the binder's region set.
    std::vector<QCheckBox*> regionChecks_;
};

}  // namespace pokedex
