#include "gui/views/settings_view.h"

#include <QComboBox>
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

#include "core/app/install_service.h"
#include "core/storage/workspace.h"
#include "gui/services/assistant_service.h"  // kAssistantApiKeyConfigKey
#include "gui/views/empty_option.h"
#include "gui/views/language_codes.h"
#include "gui/views/primary_button.h"
#include "gui/views/toast.h"

namespace pokedex {

SettingsView::SettingsView(QWidget* parent) : QWidget(parent) {
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

    auto* form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignLeft);
    form->addRow(tr("Workspace folder"), workspaceRow);
    form->addRow(QString(), workspaceHelp);
    form->addRow(tr("Default language"), languageEdit_);
    form->addRow(QString(), languageHelp);
    form->addRow(tr("AI assistant API key"), assistantKeyEdit_);
    form->addRow(QString(), assistantHelp);

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

    languageEdit_->setCurrentIndex(langIndex);  // activated-only, so no dirty signal here
    assistantKeyEdit_->setText(savedAssistantKey_);  // triggers refreshDirtyState
    workspaceEdit_->setText(savedWorkspace_);   // triggers refreshDirtyState → clean
    refreshDirtyState();
}

bool SettingsView::isDirty() const {
    return workspaceEdit_->text().trimmed() != savedWorkspace_ ||
           languageEdit_->currentData().toString() != savedLanguage_ ||
           assistantKeyEdit_->text() != savedAssistantKey_;
}

void SettingsView::refreshDirtyState() {
    saveButton_->setEnabled(isDirty());
    // The restart caveat is workspace-only (the language applies to the next add live),
    // so show it only while the workspace field itself differs from what's saved.
    dirtyHint_->setVisible(workspaceEdit_->text().trimmed() != savedWorkspace_);
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

bool SettingsView::save() {
    const QString text = workspaceEdit_->text().trimmed();
    if (text.isEmpty()) {
        QMessageBox::warning(this, tr("Settings"), tr("Please choose a workspace folder."));
        return false;
    }

    const QString language = languageEdit_->currentData().toString();
    const QString assistantKey = assistantKeyEdit_->text().trimmed();
    const bool workspaceChanged = (text != savedWorkspace_);
    const bool languageChanged = (language != savedLanguage_);
    const bool keyChanged = (assistantKey != savedAssistantKey_);

    try {
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
    } catch (const std::exception& e) {
        QMessageBox::critical(
            this, tr("Settings"),
            tr("Could not save settings:\n%1").arg(QString::fromUtf8(e.what())));
        return false;  // leave the form dirty so the user can correct it
    }

    savedWorkspace_ = text;
    savedLanguage_ = language;
    savedAssistantKey_ = assistantKey;
    workspaceEdit_->setText(text);           // normalize the field to the saved value
    assistantKeyEdit_->setText(assistantKey);  // normalize (trimmed)
    refreshDirtyState();
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
