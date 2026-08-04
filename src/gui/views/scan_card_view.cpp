#include "gui/views/scan_card_view.h"

#include <QApplication>
#include <QBuffer>
#include <QByteArray>
#include <QCamera>
#include <QCameraDevice>
#include <QFont>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QHideEvent>
#include <QImageCapture>
#include <QLabel>
#include <QLineEdit>
#include <QMediaCaptureSession>
#include <QMediaDevices>
#include <QPermissions>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QString>
#include <QVBoxLayout>
#include <QVideoWidget>

#include <optional>
#include <string>
#include <utility>

#include "core/app/card_scan.h"
#include "core/domain/pokemon.h"
#include "gui/services/assistant_service.h"
#include "gui/views/card_copy_labels.h"
#include "gui/views/primary_button.h"
#include "gui/views/region_labels.h"
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

ScanCardView::ScanCardView(AssistantService& service, OwnedNameMatcher ownedNameMatcher,
                           QWidget* parent)
    : QWidget(parent), service_(service), ownedNameMatcher_(std::move(ownedNameMatcher)) {
    // A Back top bar (matching the other in-window pages) returns to the section the
    // user came from; the host wires it to pop back to that section.
    auto* backButton = new QPushButton(tr("‹ Back"), this);
    backButton->setFlat(true);
    backButton->setCursor(Qt::PointingHandCursor);
    connect(backButton, &QPushButton::clicked, this, &ScanCardView::backRequested);
    auto* topBar = new QHBoxLayout;
    topBar->addWidget(backButton);
    topBar->addStretch(1);

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

    // The reading section is split in half: on the LEFT the editable fields (the reader
    // is usually right but occasionally off by a letter or digit, so the user can fix it
    // here rather than rescanning), on the RIGHT the "first impressions" — how many owned
    // cards match the name, and which Pokémon (if any) it depicts (name, dex #, region).
    resultForm_ = new QWidget(this);
    resultForm_->setVisible(false);

    // --- Left: the editable printed-identity fields ---
    auto* leftPanel = new QWidget(resultForm_);
    cardNameEdit_ = new QLineEdit(leftPanel);
    setNameEdit_ = new QLineEdit(leftPanel);
    setCodeEdit_ = new QLineEdit(leftPanel);
    numberEdit_ = new QLineEdit(leftPanel);
    queryEdit_ = new QLineEdit(leftPanel);
    queryEdit_->setToolTip(
        tr("The search string used to look through your cards — set (or set code) plus "
           "the collector number."));

    auto* formLayout = new QFormLayout(leftPanel);
    formLayout->setContentsMargins(0, 0, 0, 0);
    auto* readHeading = new QLabel(tr("Read from the card (edit to correct):"), leftPanel);
    readHeading->setEnabled(false);
    formLayout->addRow(readHeading);
    formLayout->addRow(tr("Card"), cardNameEdit_);
    formLayout->addRow(tr("Set"), setNameEdit_);
    formLayout->addRow(tr("Set code"), setCodeEdit_);
    formLayout->addRow(tr("Number"), numberEdit_);
    formLayout->addRow(tr("Search"), queryEdit_);

    // --- Right: first impressions (owned-name matches + detected Pokémon) ---
    auto* rightPanel = new QWidget(resultForm_);
    auto* impressionsHeading = new QLabel(tr("First impressions"), rightPanel);
    QFont impressionsFont = impressionsHeading->font();
    impressionsFont.setBold(true);
    impressionsHeading->setFont(impressionsFont);
    matchEstimate_ = new QLabel(rightPanel);   // "N of your cards may match this name."
    matchEstimate_->setEnabled(false);         // muted — a secondary "first impression" hint
    matchEstimate_->setWordWrap(true);
    speciesLabel_ = new QLabel(rightPanel);    // "Pokémon: Charizard · #6 · Kanto"
    speciesLabel_->setWordWrap(true);

    // Skip "Search my cards" and go straight to creation, two ways (both plain buttons —
    // "Scan" is the view's one accent). (1) Find in catalog: seed the finder search (by
    // set for a species, by name otherwise) and let the picked printing autofill — the
    // deterministic, reliable-when-the-search-works path. (2) Copy to creation form: paste
    // the read fields verbatim with no search — the escape hatch when the card search is
    // flaky. The findButton_ label switches with the detected species (set below).
    findButton_ = new QPushButton(tr("Create by name"), rightPanel);
    findButton_->setEnabled(false);
    findButton_->setToolTip(
        tr("Open creation and search the catalog to autofill from a real printing."));
    connect(findButton_, &QPushButton::clicked, this,
            [this]() { Q_EMIT addRequested(currentReading(), detectedDex_, /*copyFieldsToForm=*/false); });

    copyButton_ = new QPushButton(tr("Copy to creation form"), rightPanel);
    copyButton_->setEnabled(false);
    copyButton_->setToolTip(
        tr("Open creation with these read fields pasted in — no catalog search (handy when "
           "the card search is flaky)."));
    connect(copyButton_, &QPushButton::clicked, this,
            [this]() { Q_EMIT addRequested(currentReading(), detectedDex_, /*copyFieldsToForm=*/true); });

    auto* rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(16, 0, 0, 0);
    rightLayout->addWidget(impressionsHeading);
    rightLayout->addWidget(matchEstimate_);
    rightLayout->addWidget(speciesLabel_);
    rightLayout->addSpacing(8);
    rightLayout->addWidget(findButton_, 0, Qt::AlignLeft);
    rightLayout->addWidget(copyButton_, 0, Qt::AlignLeft);
    rightLayout->addStretch(1);

    auto* resultLayout = new QHBoxLayout(resultForm_);
    resultLayout->setContentsMargins(0, 0, 0, 0);
    resultLayout->addWidget(leftPanel, 1);
    resultLayout->addWidget(rightPanel, 1);

    // "Search my cards" needs a non-empty query; keep it in step as the user edits it.
    connect(queryEdit_, &QLineEdit::textChanged, this, [this](const QString& text) {
        if (resultForm_->isVisible()) {
            useButton_->setEnabled(!text.trimmed().isEmpty());
        }
    });
    // Recompute the first impressions whenever the card name changes (reader fill-in or a
    // correction) — both the owned-name count and the detected species depend on it.
    connect(cardNameEdit_, &QLineEdit::textChanged, this,
            [this](const QString&) { updateFirstImpressions(); });

    scanButton_ = new QPushButton(tr("Scan"), this);
    scanButton_->setAutoDefault(true);
    scanButton_->setDefault(true);
    applyPrimaryButtonStyle(scanButton_);
    connect(scanButton_, &QPushButton::clicked, this, &ScanCardView::requestScan);

    // Hidden until a still is frozen; turns the camera back on to reposition and shoot
    // again. It's a secondary action, so it keeps the plain button look (not the accent).
    retakeButton_ = new QPushButton(tr("Retake"), this);
    retakeButton_->setVisible(false);
    connect(retakeButton_, &QPushButton::clicked, this, &ScanCardView::retake);

    useButton_ = new QPushButton(tr("Search my cards"), this);
    useButton_->setEnabled(false);
    connect(useButton_, &QPushButton::clicked, this, [this]() {
        const ScannedCard reading = currentReading();
        if (!reading.query.empty()) {
            Q_EMIT cardResolved(reading);  // host switches to My Cards → hideEvent stops us
        }
    });

    auto* buttons = new QHBoxLayout;
    buttons->addWidget(scanButton_);
    buttons->addWidget(retakeButton_);
    buttons->addWidget(useButton_);
    buttons->addStretch(1);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(topBar);
    layout->addWidget(heading);
    layout->addWidget(subtitle);
    layout->addWidget(viewStack_, 1);
    layout->addWidget(status_);
    layout->addWidget(resultForm_);
    layout->addLayout(buttons);

    connect(&service_, &AssistantService::answerReady, this, &ScanCardView::onAnswer);
    connect(&service_, &AssistantService::failed, this, &ScanCardView::onFailed);

    // The capture session outlives individual camera opens; the camera itself is opened
    // in showEvent (only while the page is visible) rather than here.
    session_ = new QMediaCaptureSession(this);
    session_->setVideoOutput(preview_);
}

ScanCardView::~ScanCardView() { stopCamera(); }

void ScanCardView::setOwnedNameMatcher(OwnedNameMatcher matcher) {
    ownedNameMatcher_ = std::move(matcher);
}

void ScanCardView::startScan() {
    // Reset to a fresh live scan and start the camera now — NOT from showEvent. The host
    // shows the page right after this; driving the camera off showEvent would fail to
    // restart it when Scan is re-opened while this page is already current (setCurrentIndex
    // is then a no-op, so no showEvent fires). active_ marks the scan flow as live so a
    // minimize/restore (a spontaneous show) knows to resume the camera.
    active_ = true;
    resetForNewScan();
    startCamera();
}

void ScanCardView::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    // Resume the live camera after a restore-from-minimize (a spontaneous show), and only
    // while a live scan is in progress. A frozen still under review keeps the camera off
    // (Retake resumes it); navigation *to* the page is driven by startScan(), which already
    // started the camera, so a non-spontaneous show must not double-start it.
    if (event->spontaneous() && active_ && !isFrozen()) {
        startCamera();
    }
}

void ScanCardView::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    // Turn the device off whenever the page isn't visible (navigating away OR minimizing),
    // so the camera indicator goes out. But only an explicit hide means we're actually
    // leaving the scan flow (a section switch or window close): cancel any in-flight
    // identify() reply and go inactive. A spontaneous hide (minimize) must NOT cancel — the
    // reply the user is waiting on should survive a minimize/restore.
    stopCamera();
    if (!event->spontaneous()) {
        service_.cancelPending();
        active_ = false;
    }
}

void ScanCardView::resetForNewScan() {
    viewStack_->setCurrentWidget(preview_);
    retakeButton_->setVisible(false);
    resultForm_->setVisible(false);
    useButton_->setEnabled(false);
    frozen_->clear();
    capturedPixmap_ = QPixmap();
    lastScan_ = ScannedCard{};
    status_->clear();
}

void ScanCardView::startCamera() {
    if (QMediaDevices::defaultVideoInput().isNull()) {
        hasCamera_ = false;
        status_->setText(tr("No camera was found. Connect a webcam and return to this screen."));
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
                if (result.status() != Qt::PermissionStatus::Granted) {
                    showCameraBlocked(
                        tr("Camera access was denied. To scan cards, allow it in System "
                           "Settings ▸ Privacy & Security ▸ Camera, then return to this screen."));
                    return;
                }
                // The grant can resolve after the user has left the page; only open the
                // camera if the scan is still active and on-screen, else a stale callback
                // would turn the camera on off-screen (the view is persistent, so unlike
                // the old modal it is never destroyed to auto-disconnect this callback).
                if (active_ && isVisible()) {
                    openCamera();
                }
            });
            return;
        case Qt::PermissionStatus::Denied:
            showCameraBlocked(
                tr("Camera access is blocked. Enable it for Pokedex TCG in System Settings ▸ "
                   "Privacy & Security ▸ Camera, then return to this screen."));
            return;
    }

    openCamera();
}

void ScanCardView::showCameraBlocked(const QString& message) {
    hasCamera_ = false;
    scanButton_->setEnabled(false);
    resultForm_->setVisible(false);
    status_->setText(message);
}

void ScanCardView::openCamera() {
    hasCamera_ = true;
    scanButton_->setEnabled(true);

    // Create the camera + capture once and reuse them across show/hide cycles — openCamera
    // now runs from every showEvent (the page is reopened repeatedly), so recreating them
    // each time would leak a QCamera per open and stack duplicate captures.
    if (!camera_) {
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
    }

    camera_->start();
    status_->setText(tr("Point a card at the camera, then press Scan."));
}

void ScanCardView::stopCamera() {
    if (camera_) {
        camera_->stop();
    }
}

void ScanCardView::requestScan() {
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

void ScanCardView::freezeFrame(const QImage& frame) {
    // Turn the camera off and show the captured still in its place: the user gets clear
    // feedback that the shot was taken (and can judge whether it's blurry) while the
    // assistant works, instead of a live preview that keeps moving with no result.
    capturedPixmap_ = QPixmap::fromImage(frame);
    stopCamera();
    viewStack_->setCurrentWidget(frozen_);  // make it the current page before scaling,
    setScaledPixmap(frozen_, capturedPixmap_);  // so it scales to the shown geometry
    retakeButton_->setVisible(true);
}

void ScanCardView::retake() {
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

void ScanCardView::identify(const QImage& frame) {
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

void ScanCardView::onAnswer(const QString& text) {
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

void ScanCardView::onFailed(const QString& message) {
    setBusy(false);
    resultForm_->setVisible(false);
    useButton_->setEnabled(false);
    // The camera is off and a still is frozen at this point, so Scan is greyed —
    // point the user at Retake as the way to try again (it's the only live action).
    const QString retry = isFrozen() ? tr(" Press Retake to try again.") : QString();
    status_->setText(tr("Error: %1").arg(message) + retry);
}

void ScanCardView::showResult() {
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
    // The direct-add buttons need no query — any read card can be created. They're children
    // of the (now visible) result form, so they're only reachable while a reading is shown.
    findButton_->setEnabled(true);
    copyButton_->setEnabled(true);
    updateFirstImpressions();
}

void ScanCardView::updateFirstImpressions() {
    const QString name = cardNameEdit_->text().trimmed();

    // How many owned cards could be this one (by name) — the "have I already added this?"
    // read. The matcher is a name-substring estimate, so the wording hedges ("may match")
    // rather than asserting an exact duplicate count. Blank with no matcher / no name.
    if (!ownedNameMatcher_ || name.isEmpty()) {
        matchEstimate_->clear();
    } else {
        const int matches = ownedNameMatcher_(name);
        matchEstimate_->setText(matches <= 0
                                    ? tr("No cards of yours seem to match this name yet.")
                                    : tr("%n of your cards may match this name.", "", matches));
    }

    // Which Pokémon (if any) the card depicts — name, dex number, and region. Cached in
    // detectedDex_ (0 = none) to route the direct-add and label the "find in catalog" button.
    const std::optional<PokemonDexNum> dex = detectScannedSpecies(name.toStdString());
    detectedDex_ = dex.value_or(0);
    if (dex) {
        const Pokemon* entry = catalogEntry(*dex);
        const QString species = entry ? QString::fromStdString(entry->name) : QString();
        const QString region = entry ? regionLabel(entry->region) : QString();
        speciesLabel_->setText(tr("Pokémon: %1 · #%2 · %3").arg(species).arg(*dex).arg(region));
    } else if (name.isEmpty()) {
        speciesLabel_->clear();
    } else {
        speciesLabel_->setText(tr("No matching Pokémon (a Trainer/Energy card, or unrecognized)."));
    }
    // A species is added by searching its set; a species-free card by searching its name.
    findButton_->setText(detectedDex_ > 0 ? tr("Create by set") : tr("Create by name"));
}

ScannedCard ScanCardView::currentReading() const {
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

void ScanCardView::setBusy(bool busy) {
    // Scan is live-only (the camera is off while a still is frozen). Retake stays
    // enabled whenever a still is frozen — including mid-flight, where it doubles as
    // an abort (retake() cancels the pending reply) so the dialog can never look dead.
    const bool frozen = isFrozen();
    scanButton_->setEnabled(!busy && hasCamera_ && !frozen);
    retakeButton_->setEnabled(frozen);
}

bool ScanCardView::isFrozen() const {
    // Null-safe: a QResizeEvent can reach this override before the ctor wires the stack.
    return viewStack_ && viewStack_->currentWidget() == frozen_;
}

void ScanCardView::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    // Keep the frozen still fit-scaled to the (new) preview size while it is shown.
    // Rescale the cached pixmap — never re-convert the QImage — so a resize drag is cheap.
    if (isFrozen() && !capturedPixmap_.isNull()) {
        setScaledPixmap(frozen_, capturedPixmap_);
    }
}

}  // namespace pokedex
