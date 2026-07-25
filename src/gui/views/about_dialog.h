#pragma once

#include <QDialog>

namespace pokedex {

// GUI — the "About Pokédex TCG" modal: the app heading + "by Mazuh", the build
// version (the last commit's short hash, with a -dev suffix outside a Release
// build), a one-line description, the repository link, the MIT license line, and
// the fan-project legal/trademark disclaimer (verbatim from the README). A
// deliberate modal dialog (like FirstRunDialog), reached from the About menu item
// — which macOS relocates into the application menu via the action's AboutRole.
class AboutDialog : public QDialog {
    Q_OBJECT

public:
    explicit AboutDialog(QWidget* parent = nullptr);
};

}  // namespace pokedex
