#pragma once

#include <QImage>
#include <QPixmap>
#include <QWidget>

#include <functional>

#include "core/app/card_scan.h"

class QCamera;
class QImageCapture;
class QLabel;
class QLineEdit;
class QMediaCaptureSession;
class QPushButton;
class QHideEvent;
class QResizeEvent;
class QShowEvent;
class QStackedWidget;
class QVideoWidget;

namespace pokedex {

class AssistantService;

// GUI — the "Scan a card" screen: a live webcam preview with a Scan button. The user
// positions a physical card in front of the default camera, presses Scan, and the
// snapshot is sent to the assistant, which reads the card and returns a search string
// (see ScannedCard / card_scan.h). The screen shows what was read as editable fields;
// on "Search my cards" it emits cardResolved() so the host can drive the app's own
// deterministic search — the assistant only reads the print, it never "finds" the card.
//
// It is an in-window page (pushed into the shell's section stack with a Back top bar),
// not a modal — matching the app's preference for full screens over dialogs for real
// workflows. The camera runs only while the page is in use: startScan() resets it for a
// fresh scan and starts the camera; hideEvent stops it (so navigating away or minimizing
// turns the device light off) and, on an explicit hide only, cancels any pending reply;
// a spontaneous show (restore-from-minimize) resumes a live scan. When no camera is
// available (or access is denied) the preview shows a message and Scan is disabled.
//
// It depends only on the vendor-neutral AssistantService (for the vision call) and the
// Qt-free card_scan helpers (for the prompt + reply parse) — it knows nothing of the
// concrete LLM provider.
class ScanCardView : public QWidget {
    Q_OBJECT

public:
    // Given a card name the reader produced, how many of the user's cards could be it —
    // a quick "have I already added this?" estimate shown beside the Card field. The
    // host supplies it (a snapshot of owned names), keeping the view free of storage.
    // An empty function means "no estimate" (the label stays blank).
    using OwnedNameMatcher = std::function<int(const QString& cardName)>;

    // `service` must outlive the view (it is app-owned, like the window). It is the
    // shared assistant transport. `ownedNameMatcher` is captured by value (may be empty).
    explicit ScanCardView(AssistantService& service, OwnedNameMatcher ownedNameMatcher = {},
                          QWidget* parent = nullptr);
    ~ScanCardView() override;

    // Replace the owned-name matcher (see OwnedNameMatcher). The view is created once but
    // the estimate needs a fresh snapshot of the collection per open, so the host sets it
    // right before startScan(); passing {} disables the estimate.
    void setOwnedNameMatcher(OwnedNameMatcher matcher);

    // Reset to a fresh scan (clear the last reading, return to the live view) and start
    // the camera. Call this each time the page is (re)opened from the sidebar/menu so a
    // new scan doesn't inherit the last result — and so re-opening while the page is
    // already current still restarts the camera (a no-op setCurrentIndex fires no show).
    void startScan();

Q_SIGNALS:
    // The user accepted a reading: route `scanned.query` into the app's search. Only
    // emitted for a reading with a non-empty query (the button is disabled otherwise).
    void cardResolved(const ScannedCard& scanned);

    // The user pressed Back — the host returns to the section it came from.
    void backRequested();

protected:
    // Start the camera when the page is shown, stop it (and cancel any pending reply)
    // when it is hidden — so the camera runs only while this screen is visible.
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    // Rescale the frozen still to the (possibly new) preview size, so the captured
    // frame stays fit-scaled when the window is resized while it is shown.
    void resizeEvent(QResizeEvent* event) override;

private:
    void startCamera();          // check/request camera permission, then open the camera
    void openCamera();           // actually wire the session against the default camera
    void showCameraBlocked(const QString& message);  // permission denied / unavailable UI
    void stopCamera();           // stop capture — safe to call twice
    void resetForNewScan();      // return to the live view, clear the last reading
    void requestScan();          // capture a still, then hand it to identify()
    void freezeFrame(const QImage& frame);  // stop the camera, show the captured still
    void retake();               // discard the still, resume the live preview
    void identify(const QImage& frame);  // encode + send the frame to the assistant
    void onAnswer(const QString& text);  // parse the reply → show it
    void onFailed(const QString& message);
    void showResult();           // fill the editable fields from lastScan_
    // Recompute the right-hand "first impressions" from the Card field: the owned-name
    // match count and the detected Pokémon (name/dex #/region, cached in detectedDex_).
    void updateFirstImpressions();
    // The current reading as edited in the fields (trimmed) — what cardResolved()
    // carries, so a fix the user typed (a misread letter, a wrong number) is honored.
    ScannedCard currentReading() const;
    void setBusy(bool busy);     // disable Scan / show progress while a call is in flight
    bool isFrozen() const;       // true while the captured still (not the camera) is shown

    AssistantService& service_;

    QCamera* camera_ = nullptr;
    QMediaCaptureSession* session_ = nullptr;
    QImageCapture* capture_ = nullptr;

    QStackedWidget* viewStack_ = nullptr;  // page 0: live camera preview; page 1: still
    QVideoWidget* preview_ = nullptr;      // the live camera output (page 0)
    QLabel* frozen_ = nullptr;             // the captured still, shown after Scan (page 1)
    QPixmap capturedPixmap_;      // the last captured frame, kept so it can be rescaled
                                  // on resize without re-converting from the QImage
    QLabel* status_;        // "Point a card at the camera" / "Identifying…" / errors
    // The reading, shown as editable fields (hidden until there is one) so the user can
    // correct a misread letter/number before searching or adding.
    QWidget* resultForm_ = nullptr;
    QLineEdit* cardNameEdit_ = nullptr;
    QLineEdit* setNameEdit_ = nullptr;
    QLineEdit* setCodeEdit_ = nullptr;
    QLineEdit* numberEdit_ = nullptr;
    QLineEdit* queryEdit_ = nullptr;  // the actual search string driving My Cards
    // Right-hand "first impressions": owned-name match count + the detected Pokémon line.
    QLabel* matchEstimate_ = nullptr;
    QLabel* speciesLabel_ = nullptr;
    OwnedNameMatcher ownedNameMatcher_;  // supplied by the host; empty = no estimate
    QPushButton* scanButton_;
    QPushButton* retakeButton_;  // "Retake" — resume the live camera after a capture
    QPushButton* useButton_;  // "Search my cards" — enabled once a card is identified

    ScannedCard lastScan_;  // the most recent reading, carried to cardResolved()
    bool hasCamera_ = false;
    bool active_ = false;   // true between startScan() and leaving the page; gates whether
                            // showEvent should (re)start the camera
};

}  // namespace pokedex
