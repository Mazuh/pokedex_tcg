#pragma once

#include <QDialog>

class QLabel;
class QPlainTextEdit;
class QPushButton;

namespace pokedex {

class AssistantService;

// GUI — a small modal demo of the AI-assistant module: a prompt box, a Send
// button, and a read-only answer area. The user types a question, presses Send,
// and sees the assistant's reply. It is a deliberately thin exerciser of the
// vendor-neutral AssistantService (and, through it, the AiAssistant seam) — it
// knows nothing about the concrete provider.
//
// A modal dialog is the right shape here (like About / the binder picker): a
// transient tool the user opens, uses, and dismisses — not a CRUD screen, which
// the app hosts as an in-window page instead.
class AssistantPromptDialog : public QDialog {
    Q_OBJECT

public:
    // `service` must outlive the dialog (it is owned by the app, like the window).
    explicit AssistantPromptDialog(AssistantService& service, QWidget* parent = nullptr);

private:
    void send();
    void setBusy(bool busy);

    AssistantService& service_;
    QPlainTextEdit* promptEdit_;
    QPlainTextEdit* answerView_;
    QPushButton* sendButton_;
    QLabel* statusLabel_;
};

}  // namespace pokedex
