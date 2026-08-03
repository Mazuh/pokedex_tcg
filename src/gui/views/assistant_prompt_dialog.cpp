#include "gui/views/assistant_prompt_dialog.h"

#include <QFont>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include "gui/services/assistant_service.h"
#include "gui/views/primary_button.h"

namespace pokedex {

AssistantPromptDialog::AssistantPromptDialog(AssistantService& service, QWidget* parent)
    : QDialog(parent), service_(service) {
    setWindowTitle(tr("AI Assistant"));
    setModal(true);
    resize(480, 420);

    auto* heading = new QLabel(tr("Ask the assistant"), this);
    QFont headingFont = heading->font();
    headingFont.setBold(true);
    headingFont.setPointSize(headingFont.pointSize() + 2);
    heading->setFont(headingFont);

    auto* subtitle = new QLabel(
        tr("Type a question and press Send. Uses the API key from Settings."), this);
    subtitle->setEnabled(false);
    subtitle->setWordWrap(true);

    promptEdit_ = new QPlainTextEdit(this);
    promptEdit_->setPlaceholderText(tr("e.g. What set is Charizard 4/102 from?"));
    promptEdit_->setTabChangesFocus(true);

    sendButton_ = new QPushButton(tr("Send"), this);
    sendButton_->setAutoDefault(true);
    sendButton_->setDefault(true);
    applyPrimaryButtonStyle(sendButton_);
    connect(sendButton_, &QPushButton::clicked, this, &AssistantPromptDialog::send);

    statusLabel_ = new QLabel(this);
    statusLabel_->setEnabled(false);
    statusLabel_->setWordWrap(true);

    auto* answerHeading = new QLabel(tr("Answer"), this);
    answerView_ = new QPlainTextEdit(this);
    answerView_->setReadOnly(true);
    answerView_->setPlaceholderText(tr("The assistant's reply will appear here."));

    connect(&service_, &AssistantService::answerReady, this, [this](const QString& text) {
        answerView_->setPlainText(text);
        statusLabel_->clear();
        setBusy(false);
    });
    connect(&service_, &AssistantService::failed, this, [this](const QString& message) {
        statusLabel_->setText(tr("Error: %1").arg(message));
        setBusy(false);
    });

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(heading);
    layout->addWidget(subtitle);
    layout->addWidget(promptEdit_, 1);
    layout->addWidget(sendButton_);
    layout->addWidget(statusLabel_);
    layout->addSpacing(6);
    layout->addWidget(answerHeading);
    layout->addWidget(answerView_, 2);
}

void AssistantPromptDialog::send() {
    answerView_->clear();
    statusLabel_->setText(tr("Thinking…"));
    setBusy(true);
    service_.ask(promptEdit_->toPlainText());
}

void AssistantPromptDialog::setBusy(bool busy) {
    sendButton_->setEnabled(!busy);
    promptEdit_->setReadOnly(busy);
}

}  // namespace pokedex
