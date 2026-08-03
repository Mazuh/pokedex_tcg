#pragma once

#include <QDialog>
#include <QImage>

#include "core/app/card_scan.h"

class QCamera;
class QImageCapture;
class QLabel;
class QMediaCaptureSession;
class QPushButton;

namespace pokedex {

class AssistantService;

// GUI — the "Scan a card" dialog: a live webcam preview with a Scan button. The user
// positions a physical card in front of the default camera, presses Scan, and the
// snapshot is sent to the assistant, which reads the card and returns a search string
// (see ScannedCard / card_scan.h). The dialog shows what was read; on "Search my
// cards" it emits cardResolved() so the host can drive the app's own deterministic
// search — the assistant only reads the print, it never "finds" the card.
//
// A modal dialog is the right shape (like About / the assistant demo): a transient
// tool opened, used, and dismissed — not a CRUD screen. The camera starts when the
// dialog opens and stops when it closes. When no camera is available (or access is
// denied) the preview shows a message and Scan is disabled; the user can still cancel.
//
// It depends only on the vendor-neutral AssistantService (for the vision call) and the
// Qt-free card_scan helpers (for the prompt + reply parse) — it knows nothing of the
// concrete LLM provider.
class ScanCardDialog : public QDialog {
    Q_OBJECT

public:
    // `service` must outlive the dialog (it is app-owned, like the window). It is the
    // shared assistant transport; the dialog is modal, so no other caller competes.
    explicit ScanCardDialog(AssistantService& service, QWidget* parent = nullptr);
    ~ScanCardDialog() override;

Q_SIGNALS:
    // The user accepted a reading: route `scanned.query` into the app's search. Only
    // emitted for an identified card (the button is disabled otherwise).
    void cardResolved(const ScannedCard& scanned);

private:
    void startCamera();          // check/request camera permission, then open the camera
    void openCamera();           // actually wire the session against the default camera
    void showCameraBlocked(const QString& message);  // permission denied / unavailable UI
    void stopCamera();           // stop capture (on close) — safe to call twice
    void requestScan();          // capture a still, then hand it to identify()
    void identify(const QImage& frame);  // encode + send the frame to the assistant
    void onAnswer(const QString& text);  // parse the reply → show it
    void onFailed(const QString& message);
    void showResult();           // render lastScan_ into the result label
    void setBusy(bool busy);     // disable Scan / show progress while a call is in flight

    AssistantService& service_;

    QCamera* camera_ = nullptr;
    QMediaCaptureSession* session_ = nullptr;
    QImageCapture* capture_ = nullptr;

    QLabel* status_;        // "Point a card at the camera" / "Identifying…" / errors
    QLabel* result_;        // the parsed reading (rich text), hidden until there is one
    QPushButton* scanButton_;
    QPushButton* useButton_;  // "Search my cards" — enabled once a card is identified

    ScannedCard lastScan_;  // the most recent reading, carried to cardResolved()
    bool hasCamera_ = false;
};

}  // namespace pokedex
