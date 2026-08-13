#pragma once

#include <QString>
#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;

namespace pokedex {

class BackupService;

// GUI — the Settings section of the main window: the app's configuration screen.
// Today it holds four settings — the collection *workspace* folder, the *default
// card language* pre-selected when adding a new copy, the *AI assistant API key*,
// and the *backup folder* — persisted to (and loaded from) the same `config` file
// the app already uses (storage/workspace.h, a small key=value store). More settings
// can join them as new rows on the same form. (The API-key field is deliberately
// provider-neutral: it stores whatever the active assistant provider needs, read at
// call time.)
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
//
// The backup rows are the one place where a staged setting sits beside IMMEDIATE
// actions: the folder is saved with the rest of the form, but "Back up now" writes a
// file there and then. The two are reconciled by disabling both action buttons while
// the backup-folder field is dirty (with a tooltip saying to save first), so a backup
// can never land somewhere other than the folder the user is looking at.
class SettingsView : public QWidget {
    Q_OBJECT

public:
    explicit SettingsView(BackupService& backups, QWidget* parent = nullptr);

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
    // The same, for the backup folder.
    void browseBackup();
    // Recompute the dirty state from the fields vs. the saved baselines and update the
    // Save button's enabled state, the restart hint's visibility, and whether the
    // backup action buttons are available.
    void refreshDirtyState();
    // Run one backup synchronously (there is no threading in this app), showing a wait
    // cursor and reporting the outcome. `kind` picks which verb runs.
    void runBackup(bool full);
    // Re-read the last-run times from the saved backup folder into the label.
    void refreshLastRunLabel();

    BackupService& backups_;
    QLineEdit* workspaceEdit_;
    QComboBox* languageEdit_;
    QLineEdit* assistantKeyEdit_;
    QLineEdit* backupEdit_;
    QPushButton* dataBackupButton_;
    QPushButton* fullBackupButton_;
    QLabel* lastRunLabel_;
    QPushButton* saveButton_;
    QLabel* dirtyHint_;
    // The values last written/loaded — the clean baselines the fields are compared
    // against to decide dirtiness.
    QString savedWorkspace_;
    QString savedLanguage_;
    QString savedAssistantKey_;
    // The RESOLVED backup folder (configured value, else the default sibling), so the
    // field always shows a real absolute path rather than a blank meaning "somewhere".
    QString savedBackupFolder_;
    // True while a backup is running. Guards against a queued second click re-entering
    // the synchronous write; see runBackup for why disabling the buttons cannot.
    bool backupRunning_ = false;
};

}  // namespace pokedex
