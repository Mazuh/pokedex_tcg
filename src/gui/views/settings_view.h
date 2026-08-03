#pragma once

#include <QString>
#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;

namespace pokedex {

// GUI — the Settings section of the main window: the app's configuration screen.
// Today it holds a single setting — the collection *workspace* folder — which is
// persisted to (and loaded from) the same one-line `config` file the app already
// uses to remember where the collection lives (storage/workspace.h). More settings
// can join it later as new rows on the same form.
//
// The whole page is one form applied MANUALLY: edits are staged in the fields and
// only committed when the user presses "Save changes" (the primary button). This
// is deliberately unlike the rest of the app's write-straight-through pages —
// switching the workspace is a heavyweight change (it only takes effect on the
// next launch), so it should never happen as a side effect of typing.
//
// Because edits are staged, leaving the section with unsaved changes would silently
// lose them; the host (MainWindow) guards every section switch by calling
// confirmLeave() first, which prompts to Save / Discard / Cancel.
class SettingsView : public QWidget {
    Q_OBJECT

public:
    explicit SettingsView(QWidget* parent = nullptr);

    // True when the form holds edits not yet written to the config file. The host
    // reads this before allowing the user to navigate away from this section.
    bool isDirty() const;

    // Called by the host before leaving this section (and before closing the window).
    // If the form is clean it returns true immediately. Otherwise it prompts the user:
    //   Save    → attempt to persist; return true only if the write succeeded,
    //   Discard → revert the fields to the saved values and return true,
    //   Cancel  → return false so the host keeps the user on this section.
    bool confirmLeave(QWidget* dialogParent);

protected:
    // Re-load from the config file each time the section is shown, so the form
    // reflects the on-disk truth (and starts clean) on every visit.
    void showEvent(QShowEvent* event) override;

private:
    // Load the workspace path from the config file into the field and take that as
    // the clean baseline (clears the dirty state).
    void loadFromConfig();
    // Persist the form. Validates + opens the chosen workspace (creating/migrating it,
    // like the relaunch path) before recording it in the config file. Returns false —
    // leaving the form dirty — when the folder is empty/unusable (an error is shown).
    bool save();
    // Open a folder picker seeded at the current field value.
    void browse();
    // Recompute the dirty state from the field vs. the saved baseline and update the
    // Save button's enabled state + the restart hint's visibility.
    void refreshDirtyState();

    QLineEdit* workspaceEdit_;
    QPushButton* saveButton_;
    QLabel* dirtyHint_;
    // The workspace path last written/loaded — the clean baseline the field is
    // compared against to decide dirtiness.
    QString savedWorkspace_;
};

}  // namespace pokedex
