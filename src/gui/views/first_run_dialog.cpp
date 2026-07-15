#include "gui/views/first_run_dialog.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QString>
#include <QVBoxLayout>

#include <exception>

#include "core/app/install_service.h"

namespace pokedex {

FirstRunDialog::FirstRunDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Welcome to Pokedex TCG"));

    auto* intro = new QLabel(
        tr("Choose a folder for your collection workspace. It can live in a local\n"
           "directory or inside iCloud, Dropbox, or a NAS you sync yourself."),
        this);

    // Suggest a default, but the user is free to point anywhere.
    pathEdit_ = new QLineEdit(QDir::home().filePath("pokedex-tcg"), this);

    auto* browseButton = new QPushButton(tr("Browse…"), this);
    connect(browseButton, &QPushButton::clicked, this, &FirstRunDialog::browse);

    auto* pathRow = new QHBoxLayout;
    pathRow->addWidget(pathEdit_);
    pathRow->addWidget(browseButton);

    auto* buttons = new QDialogButtonBox(this);
    auto* createButton = buttons->addButton(tr("Create"), QDialogButtonBox::AcceptRole);
    buttons->addButton(QDialogButtonBox::Cancel);
    connect(createButton, &QPushButton::clicked, this, &FirstRunDialog::create);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(intro);
    layout->addLayout(pathRow);
    layout->addWidget(buttons);
}

void FirstRunDialog::browse() {
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Choose workspace folder"), pathEdit_->text());
    if (!dir.isEmpty()) {
        pathEdit_->setText(dir);
    }
}

void FirstRunDialog::create() {
    const QString text = pathEdit_->text().trimmed();
    if (text.isEmpty()) {
        QMessageBox::warning(this, tr("Pokedex TCG"), tr("Please choose a folder."));
        return;
    }

    try {
        const Workspace ws = initWorkspace(std::filesystem::path(text.toStdString()));
        chosen_ = ws.root();
        accept();
    } catch (const std::exception& e) {
        QMessageBox::critical(
            this, tr("Pokedex TCG"),
            tr("Could not create the workspace:\n%1").arg(QString::fromUtf8(e.what())));
    }
}

}  // namespace pokedex
