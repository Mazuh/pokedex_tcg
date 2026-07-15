#pragma once

#include <QDialog>

#include <filesystem>

class QLineEdit;

namespace pokedex {

// GUI — first-run setup wizard. Lets the user choose where the collection
// workspace lives (a local folder, or one inside iCloud / Dropbox / a NAS) and
// creates it through the Qt-free install service. On accept, chosenWorkspace()
// returns the created workspace root.
class FirstRunDialog : public QDialog {
    Q_OBJECT

public:
    explicit FirstRunDialog(QWidget* parent = nullptr);

    // Valid only after the dialog was accepted.
    std::filesystem::path chosenWorkspace() const { return chosen_; }

private:
    void browse();
    void create();

    QLineEdit* pathEdit_;
    std::filesystem::path chosen_;
};

}  // namespace pokedex
