#include "gui/views/scan_card_dialog.h"

#include <QApplication>
#include <QBuffer>
#include <QByteArray>
#include <QCamera>
#include <QCameraDevice>
#include <QFont>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QImageCapture>
#include <QLabel>
#include <QLineEdit>
#include <QMediaCaptureSession>
#include <QMediaDevices>
#include <QPermissions>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QString>
#include <QVBoxLayout>
#include <QVideoWidget>

#include <string>
#include <utility>

#include "core/app/card_scan.h"
#include "gui/services/assistant_service.h"
#include "gui/views/primary_button.h"
#include "gui/views/scaled_pixmap.h"

namespace pokedex {

namespace {

// Encode a captured frame as a base64 JPEG for the vision request. Downscale first so
// the upload stays small (cost + latency): a card fills a fraction of a HD frame, and
// the model reads the print fine at ~1024px. Returns "" if encoding fails.
std::string encodeJpegBase64(const QImage& frame) {
    QImage image = frame;
    constexpr int kMaxDim = 1024;
    if (image.width() > kMaxDim || image.height() > kMaxDim) {
        image = image.scaled(kMaxDim, kMaxDim, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    QByteArray bytes;
    QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "JPEG", 85)) {
        return "";
    }
    return QString::fromLatin1(bytes.toBase64()).toStdString();
}

}  // namespace

ScanCardDialog::ScanCardDialog(AssistantService& service, OwnedNameMatcher ownedNameMatcher,
                               QWidget* parent)
    : QDialog(parent), service_(service), ownedNameMatcher_(std::move(ownedNameMatcher)) {
    setWindowTitle(tr("Scan a card"));
    setModal(true);
    resize(560, 620);

    auto* heading = new QLabel(tr("Scan a card"), this);
    QFont headingFont = heading->font();
    headingFont.setBold(true);
    headingFont.setPointSize(headingFont.pointSize() + 2);
    heading->setFont(headingFont);

    auto* subtitle = new QLabel(
        tr("Hold a card in front of the camera, then press Scan. The assistant reads its "
           "set and collector number so you can check whether you already have it."),
        this);
    subtitle->setEnabled(false);
    subtitle->setWordWrap(true);

    // The image area swaps between the live camera and the frozen still: after a Scan
    // the camera turns off and the captured frame is shown in its place, so the user
    // sees exactly what was sent (and that a blurry shot is worth retaking) instead of
    // a still-moving preview that gives no feedback while the assistant thinks.
    preview_ = new QVideoWidget;
    preview_->setMinimumHeight(320);
    preview_->setStyleSheet("background: black; border-radius: 6px;");

    frozen_ = new QLabel;
    frozen_->setMinimumHeight(320);
    frozen_->setAlignment(Qt::AlignCenter);
    frozen_->setStyleSheet("background: black; border-radius: 6px;");

    viewStack_ = new QStackedWidget(this);
    viewStack_->addWidget(preview_);  // page 0 — live
    viewStack_->addWidget(frozen_);   // page 1 — frozen still

    status_ = new QLabel(this);
    status_->setWordWrap(true);

    // The reading is shown as editable fields, not fixed labels: the reader is usually
    // right but occasionally off by a letter or a digit, and the user can fix that here
    // before it drives the search / the add-form prefill instead of rescanning.
    resultForm_ = new QWidget(this);
    resultForm_->setVisible(false);
    cardNameEdit_ = new QLineEdit(resultForm_);
    setNameEdit_ = new QLineEdit(resultForm_);
    setCodeEdit_ = new QLineEdit(resultForm_);
    numberEdit_ = new QLineEdit(resultForm_);
    queryEdit_ = new QLineEdit(resultForm_);
    queryEdit_->setToolTip(
        tr("The search string used to look through your cards — set (or set code) plus "
           "the collector number."));

    // The Card row pairs the name field with a muted match estimate ("(3 possible
    // matches)") — a quick "have I already added this?" read of how many owned cards
    // share this name, updated as the reader fills it in or the user corrects it.
    matchEstimate_ = new QLabel(resultForm_);
    matchEstimate_->setEnabled(false);
    auto* cardRow = new QWidget(resultForm_);
    auto* cardRowLayout = new QHBoxLayout(cardRow);
    cardRowLayout->setContentsMargins(0, 0, 0, 0);
    cardRowLayout->addWidget(cardNameEdit_, 1);
    cardRowLayout->addWidget(matchEstimate_);

    auto* formLayout = new QFormLayout(resultForm_);
    formLayout->setContentsMargins(0, 0, 0, 0);
    auto* readHeading = new QLabel(tr("Read from the card (edit to correct):"), resultForm_);
    readHeading->setEnabled(false);
    formLayout->addRow(readHeading);
    formLayout->addRow(tr("Card"), cardRow);
    formLayout->addRow(tr("Set"), setNameEdit_);
    formLayout->addRow(tr("Set code"), setCodeEdit_);
    formLayout->addRow(tr("Number"), numberEdit_);
    formLayout->addRow(tr("Search"), queryEdit_);

    // "Search my cards" needs a non-empty query; keep it in step as the user edits it.
    connect(queryEdit_, &QLineEdit::textChanged, this, [this](const QString& text) {
        if (resultForm_->isVisible()) {
            useButton_->setEnabled(!text.trimmed().isEmpty());
        }
    });
    // Recount the estimate whenever the card name changes (reader fill-in or a correction).
    connect(cardNameEdit_, &QLineEdit::textChanged, this,
            [this](const QString&) { updateMatchEstimate(); });

    scanButton_ = new QPushButton(tr("Scan"), this);
    scanButton_->setAutoDefault(true);
    scanButton_->setDefault(true);
    applyPrimaryButtonStyle(scanButton_);
    connect(scanButton_, &QPushButton::clicked, this, &ScanCardDialog::requestScan);

    // Hidden until a still is frozen; turns the camera back on to reposition and shoot
    // again. It's a secondary action, so it keeps the plain button look (not the accent).
    retakeButton_ = new QPushButton(tr("Retake"), this);
    retakeButton_->setVisible(false);
    connect(retakeButton_, &QPushButton::clicked, this, &ScanCardDialog::retake);

    useButton_ = new QPushButton(tr("Search my cards"), this);
    useButton_->setEnabled(false);
    connect(useButton_, &QPushButton::clicked, this, [this]() {
        const ScannedCard reading = currentReading();
        if (!reading.query.empty()) {
            Q_EMIT cardResolved(reading);
            accept();
        }
    });

    auto* closeButton = new QPushButton(tr("Close"), this);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::reject);

    auto* buttons = new QHBoxLayout;
    buttons->addWidget(scanButton_);
    buttons->addWidget(retakeButton_);
    buttons->addWidget(useButton_);
    buttons->addStretch(1);
    buttons->addWidget(closeButton);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(heading);
    layout->addWidget(subtitle);
    layout->addWidget(viewStack_, 1);
    layout->addWidget(status_);
    layout->addWidget(resultForm_);
    layout->addLayout(buttons);

    connect(&service_, &AssistantService::answerReady, this, &ScanCardDialog::onAnswer);
    connect(&service_, &AssistantService::failed, this, &ScanCardDialog::onFailed);

    // Stop the camera as soon as the dialog is dismissed (either button, Esc, or the
    // window close), so the device light goes off promptly rather than at destruction.
    // Also orphan any in-flight identify() reply so its raw JSON can't be delivered to a
    // later-opened caller of the shared assistant service.
    connect(this, &QDialog::finished, this, [this](int) {
        stopCamera();
        service_.cancelPending();
    });

    // Wire the capture session against the default camera and start the preview.
    session_ = new QMediaCaptureSession(this);
    session_->setVideoOutput(preview_);
    startCamera();
}

ScanCardDialog::~ScanCardDialog() { stopCamera(); }

void ScanCardDialog::startCamera() {
    if (QMediaDevices::defaultVideoInput().isNull()) {
        hasCamera_ = false;
        status_->setText(tr("No camera was found. Connect a webcam and reopen this window."));
        scanButton_->setEnabled(false);
        return;
    }

    // macOS/iOS gate camera access behind a permission. Without asking, the camera just
    // fails to a log line the user never sees ("Access to camera not granted"), so check
    // the permission first and, when it isn't decided yet, request it (which shows the
    // native prompt). Elsewhere checkPermission returns Granted and this is a no-op.
    const QCameraPermission permission;
    switch (qApp->checkPermission(permission)) {
        case Qt::PermissionStatus::Granted:
            break;
        case Qt::PermissionStatus::Undetermined:
            status_->setText(tr("Waiting for camera permission…"));
            scanButton_->setEnabled(false);
            qApp->requestPermission(permission, this, [this](const QPermission& result) {
                if (result.status() == Qt::PermissionStatus::Granted) {
                    openCamera();
                } else {
                    showCameraBlocked(
                        tr("Camera access was denied. To scan cards, allow it in System "
                           "Settings ▸ Privacy & Security ▸ Camera, then reopen this window."));
                }
            });
            return;
        case Qt::PermissionStatus::Denied:
            showCameraBlocked(
                tr("Camera access is blocked. Enable it for Pokedex TCG in System Settings ▸ "
                   "Privacy & Security ▸ Camera, then reopen this window."));
            return;
    }

    openCamera();
}

void ScanCardDialog::showCameraBlocked(const QString& message) {
    hasCamera_ = false;
    scanButton_->setEnabled(false);
    resultForm_->setVisible(false);
    status_->setText(message);
}

void ScanCardDialog::openCamera() {
    hasCamera_ = true;
    scanButton_->setEnabled(true);

    camera_ = new QCamera(QMediaDevices::defaultVideoInput(), this);
    capture_ = new QImageCapture(this);
    session_->setCamera(camera_);
    session_->setImageCapture(capture_);

    connect(camera_, &QCamera::errorOccurred, this,
            [this](QCamera::Error error, const QString& message) {
                if (error != QCamera::NoError) {
                    onFailed(tr("Camera error: %1").arg(message));
                }
            });
    connect(capture_, &QImageCapture::imageCaptured, this,
            [this](int, const QImage& image) { identify(image); });
    connect(capture_, &QImageCapture::errorOccurred, this,
            [this](int, QImageCapture::Error, const QString& message) {
                onFailed(tr("Could not capture a frame: %1").arg(message));
            });

    camera_->start();
    status_->setText(tr("Point a card at the camera, then press Scan."));
}

void ScanCardDialog::stopCamera() {
    if (camera_) {
        camera_->stop();
    }
}

void ScanCardDialog::requestScan() {
    if (!hasCamera_ || !capture_) {
        return;
    }
    if (!capture_->isReadyForCapture()) {
        status_->setText(tr("The camera isn't ready yet — try again in a moment."));
        return;
    }
    setBusy(true);
    status_->setText(tr("Capturing…"));
    resultForm_->setVisible(false);
    useButton_->setEnabled(false);
    capture_->capture();  // → imageCaptured → identify()
}

void ScanCardDialog::freezeFrame(const QImage& frame) {
    // Turn the camera off and show the captured still in its place: the user gets clear
    // feedback that the shot was taken (and can judge whether it's blurry) while the
    // assistant works, instead of a live preview that keeps moving with no result.
    capturedPixmap_ = QPixmap::fromImage(frame);
    stopCamera();
    viewStack_->setCurrentWidget(frozen_);  // make it the current page before scaling,
    setScaledPixmap(frozen_, capturedPixmap_);  // so it scales to the shown geometry
    retakeButton_->setVisible(true);
}

void ScanCardDialog::retake() {
    // Resume the live preview so the user can reposition and shoot again. Cancel any
    // still-pending identify() reply first, so a late answer from the discarded capture
    // can't land on (and render a result over) the resumed live preview.
    service_.cancelPending();
    viewStack_->setCurrentWidget(preview_);
    retakeButton_->setVisible(false);
    resultForm_->setVisible(false);
    useButton_->setEnabled(false);
    frozen_->clear();
    capturedPixmap_ = QPixmap();
    if (camera_) {
        camera_->start();
    }
    setBusy(false);
    status_->setText(tr("Point a card at the camera, then press Scan."));
}

void ScanCardDialog::identify(const QImage& frame) {
    freezeFrame(frame);
    const std::string base64 = encodeJpegBase64(frame);
    if (base64.empty()) {
        onFailed(tr("Could not read the captured image."));
        return;
    }
    setBusy(true);  // frozen + in flight: Scan and Retake both wait for the reply
    status_->setText(tr("Identifying…"));
    service_.ask(buildCardScanPrompt(base64));
}

void ScanCardDialog::onAnswer(const QString& text) {
    lastScan_ = parseScannedCard(text.toStdString());
    setBusy(false);
    if (lastScan_.identified) {
        status_->setText(tr("Read a card — correct any field below, then search your cards."));
        showResult();  // fills the fields; the query field's change enables "Search my cards"
    } else {
        resultForm_->setVisible(false);
        const QString note = lastScan_.note.empty()
                                 ? tr("Couldn't read a card.")
                                 : QString::fromStdString(lastScan_.note);
        status_->setText(tr("%1 Press Retake to reposition and try again.").arg(note));
    }
}

void ScanCardDialog::onFailed(const QString& message) {
    setBusy(false);
    resultForm_->setVisible(false);
    useButton_->setEnabled(false);
    // The camera is off and a still is frozen at this point, so Scan is greyed —
    // point the user at Retake as the way to try again (it's the only live action).
    const QString retry = isFrozen() ? tr(" Press Retake to try again.") : QString();
    status_->setText(tr("Error: %1").arg(message) + retry);
}

void ScanCardDialog::showResult() {
    // Fill the editable fields from the reading. Block the fields' textChanged during
    // population so it doesn't drive the button/estimate updates — those are done once,
    // explicitly, below. (Signals alone are unreliable here anyway: setText emits nothing
    // when the value is unchanged, e.g. rescanning the same card.) The connections still
    // fire for later manual edits.
    resultForm_->setVisible(true);
    {
        const QSignalBlocker blockName(cardNameEdit_);
        const QSignalBlocker blockQuery(queryEdit_);
        cardNameEdit_->setText(QString::fromStdString(lastScan_.cardName));
        setNameEdit_->setText(QString::fromStdString(lastScan_.setName));
        setCodeEdit_->setText(QString::fromStdString(lastScan_.setCode));
        numberEdit_->setText(QString::fromStdString(lastScan_.collectorNumber));
        queryEdit_->setText(QString::fromStdString(lastScan_.query));
    }
    useButton_->setEnabled(!queryEdit_->text().trimmed().isEmpty());
    updateMatchEstimate();
}

void ScanCardDialog::updateMatchEstimate() {
    if (!ownedNameMatcher_) {
        matchEstimate_->clear();  // host gave no matcher — no estimate to show
        return;
    }
    const QString name = cardNameEdit_->text().trimmed();
    if (name.isEmpty()) {
        matchEstimate_->clear();
        return;
    }
    const int matches = ownedNameMatcher_(name);
    matchEstimate_->setText(matches <= 0
                                ? tr("none in your cards yet")
                                : tr("%n possible match(es) in your cards", "", matches));
}

ScannedCard ScanCardDialog::currentReading() const {
    // The reading as it now stands in the fields — the user may have corrected a misread
    // letter or digit. identified is true because the form is only shown for a read card.
    ScannedCard reading;
    reading.identified = true;
    reading.cardName = cardNameEdit_->text().trimmed().toStdString();
    reading.setName = setNameEdit_->text().trimmed().toStdString();
    reading.setCode = setCodeEdit_->text().trimmed().toStdString();
    reading.collectorNumber = numberEdit_->text().trimmed().toStdString();
    reading.query = queryEdit_->text().trimmed().toStdString();
    return reading;
}

void ScanCardDialog::setBusy(bool busy) {
    // Scan is live-only (the camera is off while a still is frozen). Retake stays
    // enabled whenever a still is frozen — including mid-flight, where it doubles as
    // an abort (retake() cancels the pending reply) so the dialog can never look dead.
    const bool frozen = isFrozen();
    scanButton_->setEnabled(!busy && hasCamera_ && !frozen);
    retakeButton_->setEnabled(frozen);
}

bool ScanCardDialog::isFrozen() const {
    // Null-safe: a QResizeEvent can reach this override before the ctor wires the stack.
    return viewStack_ && viewStack_->currentWidget() == frozen_;
}

void ScanCardDialog::resizeEvent(QResizeEvent* event) {
    QDialog::resizeEvent(event);
    // Keep the frozen still fit-scaled to the (new) preview size while it is shown.
    // Rescale the cached pixmap — never re-convert the QImage — so a resize drag is cheap.
    if (isFrozen() && !capturedPixmap_.isNull()) {
        setScaledPixmap(frozen_, capturedPixmap_);
    }
}

}  // namespace pokedex
