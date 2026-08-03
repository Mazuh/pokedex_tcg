#include "gui/views/scan_card_dialog.h"

#include <QApplication>
#include <QBuffer>
#include <QByteArray>
#include <QCamera>
#include <QCameraDevice>
#include <QFont>
#include <QHBoxLayout>
#include <QImageCapture>
#include <QLabel>
#include <QMediaCaptureSession>
#include <QMediaDevices>
#include <QPermissions>
#include <QPushButton>
#include <QString>
#include <QVBoxLayout>
#include <QVideoWidget>

#include <string>

#include "core/app/card_scan.h"
#include "gui/services/assistant_service.h"
#include "gui/views/primary_button.h"

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

ScanCardDialog::ScanCardDialog(AssistantService& service, QWidget* parent)
    : QDialog(parent), service_(service) {
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

    auto* preview = new QVideoWidget(this);
    preview->setMinimumHeight(320);
    preview->setStyleSheet("background: black; border-radius: 6px;");

    status_ = new QLabel(this);
    status_->setWordWrap(true);

    result_ = new QLabel(this);
    result_->setWordWrap(true);
    result_->setTextFormat(Qt::RichText);
    result_->setVisible(false);

    scanButton_ = new QPushButton(tr("Scan"), this);
    scanButton_->setAutoDefault(true);
    scanButton_->setDefault(true);
    applyPrimaryButtonStyle(scanButton_);
    connect(scanButton_, &QPushButton::clicked, this, &ScanCardDialog::requestScan);

    useButton_ = new QPushButton(tr("Search my cards"), this);
    useButton_->setEnabled(false);
    connect(useButton_, &QPushButton::clicked, this, [this]() {
        if (lastScan_.identified) {
            Q_EMIT cardResolved(lastScan_);
            accept();
        }
    });

    auto* closeButton = new QPushButton(tr("Close"), this);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::reject);

    auto* buttons = new QHBoxLayout;
    buttons->addWidget(scanButton_);
    buttons->addWidget(useButton_);
    buttons->addStretch(1);
    buttons->addWidget(closeButton);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(heading);
    layout->addWidget(subtitle);
    layout->addWidget(preview, 1);
    layout->addWidget(status_);
    layout->addWidget(result_);
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
    session_->setVideoOutput(preview);
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
    result_->setVisible(false);
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
    result_->setVisible(false);
    useButton_->setEnabled(false);
    capture_->capture();  // → imageCaptured → identify()
}

void ScanCardDialog::identify(const QImage& frame) {
    const std::string base64 = encodeJpegBase64(frame);
    if (base64.empty()) {
        onFailed(tr("Could not read the captured image."));
        return;
    }
    status_->setText(tr("Identifying…"));
    service_.ask(buildCardScanPrompt(base64));
}

void ScanCardDialog::onAnswer(const QString& text) {
    lastScan_ = parseScannedCard(text.toStdString());
    setBusy(false);
    if (lastScan_.identified) {
        status_->setText(tr("Read a card — review it below, then search your cards."));
        useButton_->setEnabled(true);
        showResult();
    } else {
        result_->setVisible(false);
        const QString note = lastScan_.note.empty()
                                 ? tr("Couldn't read a card.")
                                 : QString::fromStdString(lastScan_.note);
        status_->setText(tr("%1 Reposition the card and press Scan again.").arg(note));
    }
}

void ScanCardDialog::onFailed(const QString& message) {
    setBusy(false);
    result_->setVisible(false);
    useButton_->setEnabled(false);
    status_->setText(tr("Error: %1").arg(message));
}

void ScanCardDialog::showResult() {
    // A compact summary of what was read; the query is what actually drives the search.
    QString html = tr("<b>Read from the card</b><br>");
    const auto row = [&html](const QString& label, const std::string& value) {
        if (!value.empty()) {
            html += label + QStringLiteral(": ") + QString::fromStdString(value).toHtmlEscaped() +
                    QStringLiteral("<br>");
        }
    };
    row(tr("Card"), lastScan_.cardName);
    row(tr("Set"), lastScan_.setName);
    row(tr("Set code"), lastScan_.setCode);
    row(tr("Number"), lastScan_.collectorNumber);
    html += QStringLiteral("<br>") + tr("Search: <b>%1</b>")
                                         .arg(QString::fromStdString(lastScan_.query).toHtmlEscaped());
    result_->setText(html);
    result_->setVisible(true);
}

void ScanCardDialog::setBusy(bool busy) {
    // Keep Scan disabled while a capture/identify round-trip is in flight.
    scanButton_->setEnabled(!busy && hasCamera_);
}

}  // namespace pokedex
