#include "gui/views/settings_view.h"

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

#include "core/app/install_service.h"
#include "core/storage/workspace.h"
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

    auto* form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignLeft);
    form->addRow(tr("Workspace folder"), workspaceRow);
    form->addRow(QString(), workspaceHelp);

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
    // The single source of truth for "where the workspace lives" is the config file.
    const std::optional<std::filesystem::path> configured = readConfiguredWorkspacePath();
    savedWorkspace_ =
        configured ? QString::fromStdString(configured->string()) : QString();
    workspaceEdit_->setText(savedWorkspace_);  // triggers refreshDirtyState → clean
}

bool SettingsView::isDirty() const {
    return workspaceEdit_->text().trimmed() != savedWorkspace_;
}

void SettingsView::refreshDirtyState() {
    const bool dirty = isDirty();
    saveButton_->setEnabled(dirty);
    dirtyHint_->setVisible(dirty);
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

    try {
        // Validate + set up the target like the relaunch path: openWorkspace is
        // permissive (it creates/migrates the folder), so this both proves the path is
        // usable now and prepares an existing workspace to be switched to. Only after
        // it succeeds do we record the path in the config file.
        openWorkspace(std::filesystem::path(text.toStdString()));
        writeConfiguredWorkspacePath(std::filesystem::path(text.toStdString()));
    } catch (const std::exception& e) {
        QMessageBox::critical(
            this, tr("Settings"),
            tr("Could not use that workspace folder:\n%1").arg(QString::fromUtf8(e.what())));
        return false;  // leave the form dirty so the user can correct it
    }

    const bool changed = (text != savedWorkspace_);
    savedWorkspace_ = text;
    workspaceEdit_->setText(text);  // normalize the field to the saved value
    refreshDirtyState();
    showToast(this, changed ? tr("Saved — restart the app to open the new workspace.")
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
