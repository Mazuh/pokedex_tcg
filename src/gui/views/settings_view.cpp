#include "gui/views/settings_view.h"

#include <QApplication>
#include <QComboBox>
#include <QCursor>
#include <QDir>
#include <QFileDialog>
#include <QFont>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include <exception>
#include <filesystem>
#include <optional>
#include <string>

#include "core/app/backup_service.h"
#include "core/app/install_service.h"
#include "core/storage/workspace.h"
#include "gui/services/assistant_service.h"  // kAssistantApiKeyConfigKey
#include "gui/views/datetime_label.h"
#include "gui/views/empty_option.h"
#include "gui/views/language_codes.h"
#include "gui/views/primary_button.h"
#include "gui/views/toast.h"

namespace pokedex {

namespace {

// Show a wait cursor for the duration of a synchronous operation, restoring it on
// every exit path including an exception. There is no threading anywhere in this app,
// so a full backup of a large media cache genuinely blocks the event loop; this is the
// sanctioned way to say so rather than looking frozen for no reason.
class WaitCursor {
public:
    WaitCursor() { QApplication::setOverrideCursor(Qt::WaitCursor); }
    WaitCursor(const WaitCursor&) = delete;
    WaitCursor& operator=(const WaitCursor&) = delete;
    ~WaitCursor() { QApplication::restoreOverrideCursor(); }
};

}  // namespace

SettingsView::SettingsView(BackupService& backups, QWidget* parent)
    : QWidget(parent), backups_(backups) {
    // Page heading, matching the other in-window pages' bold, slightly larger title.
    auto* heading = new QLabel(tr("Settings"), this);
    QFont headingFont = heading->font();
    headingFont.setBold(true);
    headingFont.setPointSize(headingFont.pointSize() + 3);
    heading->setFont(headingFont);

    auto* subtitle =
        new QLabel(tr("These are saved to your app configuration and applied when you press "
                      "“Save changes”."),
                   this);
    subtitle->setEnabled(false);  // muted: a hint, not content
    subtitle->setWordWrap(true);

    // --- Workspace setting -------------------------------------------------
    workspaceEdit_ = new QLineEdit(this);
    workspaceEdit_->setPlaceholderText(tr("Path to your collection folder"));
    connect(workspaceEdit_, &QLineEdit::textChanged, this, &SettingsView::refreshDirtyState);

    auto* browseButton = new QPushButton(tr("Browse…"), this);
    browseButton->setAutoDefault(false);
    connect(browseButton, &QPushButton::clicked, this, &SettingsView::browse);

    auto* workspaceRow = new QHBoxLayout;
    workspaceRow->addWidget(workspaceEdit_, 1);
    workspaceRow->addWidget(browseButton);

    auto* workspaceHelp = new QLabel(
        tr("The folder holding your collection (its database and cached images). It can live "
           "in a local directory or inside iCloud, Dropbox, or a NAS you sync yourself."),
        this);
    workspaceHelp->setEnabled(false);
    workspaceHelp->setWordWrap(true);

    // --- Default language setting -----------------------------------------
    // The language pre-selected in the "Add copy" form for a fresh copy — handy when a
    // whole collection (or booster) is one language. Shares the card form's code list so
    // the two pickers never drift; the leading blank entry means "no default".
    languageEdit_ = new QComboBox(this);
    for (const QString& code : languageCodes()) {
        languageEdit_->addItem(code.isEmpty() ? noneOptionLabel() : code, code);
    }
    connect(languageEdit_, &QComboBox::activated, this, &SettingsView::refreshDirtyState);

    auto* languageHelp = new QLabel(
        tr("Pre-selected as the language when you add a new card. You can still change it "
           "per card. Takes effect on the next card you add — no restart needed."),
        this);
    languageHelp->setEnabled(false);
    languageHelp->setWordWrap(true);

    // --- AI assistant API key ---------------------------------------------
    // The secret for the app's AI-assistant provider. Masked (Password echo) so it
    // isn't shoulder-surfed; stored under a provider-neutral config key and read at
    // call time, so it applies live (no restart) and survives a provider swap.
    assistantKeyEdit_ = new QLineEdit(this);
    assistantKeyEdit_->setEchoMode(QLineEdit::Password);
    assistantKeyEdit_->setPlaceholderText(tr("Paste your API key"));
    connect(assistantKeyEdit_, &QLineEdit::textChanged, this, &SettingsView::refreshDirtyState);

    auto* assistantHelp = new QLabel(
        tr("Used by the AI Assistant tool (in the sidebar). Stored in your app configuration "
           "on this computer and sent only to the assistant provider. Takes effect "
           "immediately — no restart needed."),
        this);
    assistantHelp->setEnabled(false);
    assistantHelp->setWordWrap(true);

    // --- Backups -----------------------------------------------------------
    // The folder is a staged setting like the ones above; the two buttons beside it
    // are immediate actions. They are disabled while the folder field is dirty (see
    // refreshDirtyState) so a backup can never land somewhere other than the folder
    // the user is looking at.
    backupEdit_ = new QLineEdit(this);
    backupEdit_->setPlaceholderText(tr("Path to your backup folder"));
    connect(backupEdit_, &QLineEdit::textChanged, this, &SettingsView::refreshDirtyState);

    auto* backupBrowseButton = new QPushButton(tr("Browse…"), this);
    backupBrowseButton->setAutoDefault(false);
    connect(backupBrowseButton, &QPushButton::clicked, this, &SettingsView::browseBackup);

    auto* backupRow = new QHBoxLayout;
    backupRow->addWidget(backupEdit_, 1);
    backupRow->addWidget(backupBrowseButton);

    auto* backupHelp = new QLabel(
        tr("Where manual backups and the automatic pre-upgrade backup are written. "
           "To recover: quit the app, then either copy a “pokedex-data-….db” file over "
           "“pokedex.db” in your workspace folder, or unzip a “pokedex-full-….zip” into a "
           "new empty folder and point “Workspace folder” above at it. The app never "
           "deletes a backup, so old ones pile up until you remove them yourself. Backing "
           "up everything can take a while for a large collection — the app stays busy "
           "until it finishes."),
        this);
    backupHelp->setEnabled(false);
    backupHelp->setWordWrap(true);

    dataBackupButton_ = new QPushButton(tr("Back up data"), this);
    dataBackupButton_->setAutoDefault(false);
    connect(dataBackupButton_, &QPushButton::clicked, this, [this]() { runBackup(false); });

    fullBackupButton_ = new QPushButton(tr("Back up everything"), this);
    fullBackupButton_->setAutoDefault(false);
    connect(fullBackupButton_, &QPushButton::clicked, this, [this]() { runBackup(true); });

    auto* backupActions = new QHBoxLayout;
    backupActions->setContentsMargins(0, 0, 0, 0);
    backupActions->addWidget(dataBackupButton_);
    backupActions->addWidget(fullBackupButton_);
    backupActions->addStretch();

    lastRunLabel_ = new QLabel(this);
    lastRunLabel_->setEnabled(false);
    lastRunLabel_->setWordWrap(true);

    auto* form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignLeft);
    form->addRow(tr("Workspace folder"), workspaceRow);
    form->addRow(QString(), workspaceHelp);
    form->addRow(tr("Default language"), languageEdit_);
    form->addRow(QString(), languageHelp);
    form->addRow(tr("AI assistant API key"), assistantKeyEdit_);
    form->addRow(QString(), assistantHelp);
    form->addRow(tr("Backup folder"), backupRow);
    form->addRow(QString(), backupHelp);
    form->addRow(QString(), backupActions);
    form->addRow(QString(), lastRunLabel_);

    // A muted note that a workspace change is a next-launch change, shown only while
    // the form is dirty (there's nothing pending to restart for otherwise).
    dirtyHint_ = new QLabel(
        tr("Note: switching the workspace folder takes effect the next time you start the app."),
        this);
    dirtyHint_->setEnabled(false);
    dirtyHint_->setWordWrap(true);
    dirtyHint_->setVisible(false);

    saveButton_ = new QPushButton(tr("Save changes"), this);
    saveButton_->setAutoDefault(false);
    applyPrimaryButtonStyle(saveButton_);
    connect(saveButton_, &QPushButton::clicked, this, [this]() { save(); });

    auto* buttonRow = new QHBoxLayout;
    buttonRow->addStretch();
    buttonRow->addWidget(saveButton_);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 12, 16, 12);  // match the other sections' padding
    layout->addWidget(heading);
    layout->addWidget(subtitle);
    layout->addSpacing(8);
    layout->addLayout(form);
    layout->addWidget(dirtyHint_);
    layout->addStretch();  // keep the form at the top; the extra room stays below
    layout->addLayout(buttonRow);

    loadFromConfig();
}

void SettingsView::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    // Reflect the on-disk config when the section is shown — but never clobber unsaved
    // edits. Navigating here always arrives clean (the leave-guard clears the form on
    // the way out), so this refreshes on a normal visit; the isDirty() gate matters for
    // a *spontaneous* re-show of an already-open dirty form (Cmd+H / minimize-restore /
    // app-switch), which must keep the user's typed value rather than reset it.
    if (!isDirty()) {
        loadFromConfig();
    }
}

void SettingsView::loadFromConfig() {
    // The config file is the single source of truth for both settings.
    const std::optional<std::filesystem::path> configured = readConfiguredWorkspacePath();
    savedWorkspace_ =
        configured ? QString::fromStdString(configured->string()) : QString();

    const std::optional<std::string> lang = readConfigValue(kDefaultLanguageConfigKey);
    const QString langCode = lang ? QString::fromStdString(*lang) : QString();
    int langIndex = languageEdit_->findData(langCode);
    if (langIndex < 0) {
        langIndex = 0;  // an unrecognized stored code falls back to "no default"
    }
    // Set the language baseline before touching any widget, so a stray signal (e.g. the
    // workspace setText below) computing dirtiness sees a consistent, clean baseline.
    savedLanguage_ = languageEdit_->itemData(langIndex).toString();

    const std::optional<std::string> assistantKey =
        readConfigValue(kAssistantApiKeyConfigKey);
    savedAssistantKey_ = assistantKey ? QString::fromStdString(*assistantKey) : QString();

    // The baseline is the RESOLVED folder (configured value, else the default sibling
    // of the workspace), not the raw config value: the default is dot-prefixed and so
    // hidden in Finder, and this pre-filled field is how the user learns where it is.
    // Because the baseline is the resolved path, leaving it untouched writes no config
    // key at all — so the default keeps tracking the workspace if the workspace moves.
    //
    // It is asked of the SERVICE, i.e. resolved against the workspace actually open,
    // never against the staged one in the field above. The buttons here write through
    // that same service, so deriving it from a staged workspace would make the field and
    // the tooltip name a folder no backup would go to until the next launch.
    savedBackupFolder_ = QString::fromStdString(backups_.folder().string());

    languageEdit_->setCurrentIndex(langIndex);  // activated-only, so no dirty signal here
    assistantKeyEdit_->setText(savedAssistantKey_);  // triggers refreshDirtyState
    backupEdit_->setText(savedBackupFolder_);        // triggers refreshDirtyState
    workspaceEdit_->setText(savedWorkspace_);   // triggers refreshDirtyState → clean
    refreshDirtyState();
    refreshLastRunLabel();
}

bool SettingsView::isDirty() const {
    return workspaceEdit_->text().trimmed() != savedWorkspace_ ||
           languageEdit_->currentData().toString() != savedLanguage_ ||
           assistantKeyEdit_->text() != savedAssistantKey_ ||
           backupEdit_->text().trimmed() != savedBackupFolder_;
}

void SettingsView::refreshDirtyState() {
    saveButton_->setEnabled(isDirty());
    // The restart caveat is workspace-only (the language applies to the next add live),
    // so show it only while the workspace field itself differs from what's saved.
    dirtyHint_->setVisible(workspaceEdit_->text().trimmed() != savedWorkspace_);

    // Gate the immediate backup actions on the BACKUP FOLDER field only — not on
    // isDirty(), which would block a backup merely because the API key was being
    // edited. Disabled-with-an-explaining-tooltip mirrors the binder guide's
    // Insert-blank / Move… buttons: say what is wrong and how to fix it.
    const bool backupFolderDirty = backupEdit_->text().trimmed() != savedBackupFolder_;
    const QString tip =
        backupFolderDirty
            ? tr("Press “Save changes” first — backups are written to the saved folder.")
            : tr("Write a backup now to %1.").arg(savedBackupFolder_);
    for (QPushButton* button : {dataBackupButton_, fullBackupButton_}) {
        button->setEnabled(!backupFolderDirty);
        button->setToolTip(tip);
    }
}

void SettingsView::refreshLastRunLabel() {
    const auto label = [](const std::optional<Timestamp>& when) {
        return when ? dateTimeLabel(*when) : tr("never");
    };
    lastRunLabel_->setText(tr("Last data backup: %1 · Last full backup: %2")
                               .arg(label(backups_.lastRun(BackupKind::Data)),
                                    label(backups_.lastRun(BackupKind::Full))));
}

void SettingsView::runBackup(bool full) {
    // A re-entrancy flag, not setEnabled(false): the work below is synchronous with no
    // event pumping, so a disable would never be painted OR acted on, and refreshDirtyState
    // re-enables both buttons before the queued second click is ever dispatched. An
    // impatient double-click during a multi-second archive would then run the whole thing
    // twice. The flag is what actually stops that.
    if (backupRunning_) {
        return;
    }
    backupRunning_ = true;
    struct Reset {
        bool& flag;
        ~Reset() { flag = false; }
    } reset{backupRunning_};

    try {
        std::filesystem::path written;
        {
            WaitCursor busy;
            written = full ? backups_.runFullBackup() : backups_.runDataBackup();
        }
        refreshDirtyState();
        refreshLastRunLabel();
        showToast(this, full ? tr("Full backup saved: %1")
                                   .arg(QString::fromStdString(written.filename().string()))
                             : tr("Data backup saved: %1")
                                   .arg(QString::fromStdString(written.filename().string())));
    } catch (const std::exception& e) {
        refreshDirtyState();
        QMessageBox::critical(this, tr("Backup"),
                              tr("Could not write the backup:\n%1").arg(QString::fromUtf8(e.what())));
    }
}

void SettingsView::browse() {
    const QString start =
        workspaceEdit_->text().trimmed().isEmpty() ? QDir::homePath() : workspaceEdit_->text();
    const QString dir =
        QFileDialog::getExistingDirectory(this, tr("Choose workspace folder"), start);
    if (!dir.isEmpty()) {
        workspaceEdit_->setText(dir);
    }
}

void SettingsView::browseBackup() {
    const QString start =
        backupEdit_->text().trimmed().isEmpty() ? QDir::homePath() : backupEdit_->text();
    const QString dir =
        QFileDialog::getExistingDirectory(this, tr("Choose backup folder"), start);
    if (!dir.isEmpty()) {
        backupEdit_->setText(dir);
    }
}

bool SettingsView::save() {
    const QString text = workspaceEdit_->text().trimmed();
    if (text.isEmpty()) {
        QMessageBox::warning(this, tr("Settings"), tr("Please choose a workspace folder."));
        return false;
    }

    const QString language = languageEdit_->currentData().toString();
    const QString assistantKey = assistantKeyEdit_->text().trimmed();
    const QString backupFolder = backupEdit_->text().trimmed();
    if (backupFolder.isEmpty()) {
        QMessageBox::warning(this, tr("Settings"), tr("Please choose a backup folder."));
        return false;
    }
    const bool workspaceChanged = (text != savedWorkspace_);
    const bool languageChanged = (language != savedLanguage_);
    const bool keyChanged = (assistantKey != savedAssistantKey_);
    const bool backupChanged = (backupFolder != savedBackupFolder_);

    try {
        // Validate the backup folder BEFORE any write. Every writeConfig* below commits
        // to disk immediately, so validating last meant a rejected backup folder left the
        // workspace/language/key changes already persisted while the dialog said the save
        // had failed — and a later "Discard" would then reveal them as saved after all.
        if (backupChanged) {
            // Checked against the workspace being SAVED, not the one currently open, so a
            // folder nested inside the new workspace is caught in the same press. Also
            // creates it, so the path in the field is real from here on.
            ensureUsableBackupFolder(Workspace(std::filesystem::path(text.toStdString())),
                                     std::filesystem::path(backupFolder.toStdString()));
        }

        // Persist only the settings that actually changed — each writeConfig* call is a
        // full read-modify-write of the config file, so writing untouched keys is wasted
        // I/O. Crucially, only a real workspace change re-runs openWorkspace: it is the
        // heavy, permissive validate+create+migrate the relaunch path uses, and opening a
        // second connection to the DB the running app already holds is both needless and a
        // file-lock contender. An unchanged path is already open and proven usable.
        if (workspaceChanged) {
            openWorkspace(std::filesystem::path(text.toStdString()));
            writeConfiguredWorkspacePath(std::filesystem::path(text.toStdString()));
        }
        if (languageChanged) {
            writeConfigValue(kDefaultLanguageConfigKey, language.toStdString());
        }
        if (keyChanged) {
            writeConfigValue(kAssistantApiKeyConfigKey, assistantKey.toStdString());
        }
        if (backupChanged) {
            writeConfigValue(kBackupFolderConfigKey, backupFolder.toStdString());
        }
    } catch (const std::exception& e) {
        QMessageBox::critical(
            this, tr("Settings"),
            tr("Could not save settings:\n%1").arg(QString::fromUtf8(e.what())));
        return false;  // leave the form dirty so the user can correct it
    }

    savedWorkspace_ = text;
    savedLanguage_ = language;
    savedAssistantKey_ = assistantKey;
    savedBackupFolder_ = backupFolder;
    workspaceEdit_->setText(text);           // normalize the field to the saved value
    assistantKeyEdit_->setText(assistantKey);  // normalize (trimmed)
    backupEdit_->setText(backupFolder);        // normalize (trimmed)
    refreshDirtyState();
    refreshLastRunLabel();  // a new folder has its own history (usually none yet)
    // The workspace change is the only one needing a restart; a language-only change is
    // live, so don't nag about restarting when the folder didn't move.
    showToast(this, workspaceChanged
                        ? tr("Saved — restart the app to open the new workspace.")
                        : tr("Settings saved."));
    return true;
}

bool SettingsView::confirmLeave(QWidget* dialogParent) {
    if (!isDirty()) {
        return true;
    }

    QMessageBox box(dialogParent);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(tr("Unsaved changes"));
    box.setText(tr("You have unsaved changes in Settings."));
    box.setInformativeText(tr("Do you want to save them before leaving?"));
    QPushButton* saveBtn = box.addButton(tr("Save"), QMessageBox::AcceptRole);
    QPushButton* discardBtn = box.addButton(tr("Discard"), QMessageBox::DestructiveRole);
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(saveBtn);
    box.exec();

    if (box.clickedButton() == saveBtn) {
        return save();  // only leave if the write actually succeeded
    }
    if (box.clickedButton() == discardBtn) {
        loadFromConfig();  // throw the edits away, back to the clean baseline
        return true;
    }
    return false;  // Cancel / dialog dismissed: stay on the page
}

}  // namespace pokedex
