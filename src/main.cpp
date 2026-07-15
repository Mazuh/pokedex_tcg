#include <QApplication>
#include <QDialog>
#include <QFont>
#include <QLabel>
#include <QMessageBox>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>

#include <exception>
#include <optional>

#include "core/app/install_service.h"
#include "gui/views/first_run_dialog.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    std::optional<pokedex::Workspace> workspace;
    try {
        if (std::optional<pokedex::Workspace> configured = pokedex::configuredWorkspace()) {
            // Relaunch: open + migrate the recorded workspace (so a future schema
            // bump is applied before the DB is used).
            workspace = pokedex::openWorkspace(configured->root());
        } else {
            // First run: the wizard creates + migrates the workspace itself, so we
            // just adopt the folder it chose — no second open.
            pokedex::FirstRunDialog dialog;
            if (dialog.exec() != QDialog::Accepted) {
                return 0;  // user cancelled setup: nothing to open
            }
            workspace = pokedex::Workspace(dialog.chosenWorkspace());
        }
    } catch (const std::exception &e) {
        // The workspace folder became unavailable (NAS/iCloud unmounted, path
        // unwritable, …). Report it instead of letting the exception crash the app.
        QMessageBox::critical(
            nullptr, QStringLiteral("Pokedex TCG"),
            QStringLiteral("Could not open your workspace:\n%1").arg(QString::fromUtf8(e.what())));
        return 1;
    }

    QWidget window;
    window.setWindowTitle("Pokedex TCG");
    window.resize(360, 160);

    auto *label = new QLabel(QStringLiteral("Hello, World! — Pokedex TCG"));
    label->setAlignment(Qt::AlignCenter);
    QFont font = label->font();
    font.setPointSize(20);
    label->setFont(font);

    auto *workspaceLabel = new QLabel(
        QStringLiteral("Workspace: %1")
            .arg(QString::fromStdString(workspace->root().string())));
    workspaceLabel->setAlignment(Qt::AlignCenter);

    auto *layout = new QVBoxLayout(&window);
    layout->addWidget(label);
    layout->addWidget(workspaceLabel);

    window.show();

    return app.exec();
}
