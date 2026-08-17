#include "gui/views/info_dialog.h"

#include <QDialogButtonBox>
#include <QFont>
#include <QFrame>
#include <QGuiApplication>
#include <QLabel>
#include <QRect>
#include <QScreen>
#include <QStyle>
#include <QTextBrowser>
#include <QTextDocument>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cmath>

namespace pokedex {

namespace {

// A readable text column for a definition list, narrowed on a small display so the
// dialog keeps margins there too.
constexpr int kPreferredWidth = 560;
constexpr int kLayoutMargin = 20;
constexpr int kMinBodyHeight = 120;
// The whole point of the change: the explanation must FIT on the screen (a tooltip that
// didn't is what this replaced), so the body never claims more than this share of the
// available height — beyond it the text scrolls instead of growing the window.
constexpr double kMaxBodyScreenShare = 0.7;

// The display the host window is actually on — the same one QDialog centers us over and
// clamps us to, so it is the one whose size we must respect.
const QScreen* screenFor(const QWidget* parent) {
    return parent != nullptr ? parent->screen() : QGuiApplication::primaryScreen();
}

}  // namespace

InfoDialog::InfoDialog(const QString& title, const QString& bodyHtml, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(title);
    setModal(true);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(kLayoutMargin, kLayoutMargin, kLayoutMargin, 16);
    layout->setSpacing(10);

    auto* heading = new QLabel(title, this);
    heading->setWordWrap(true);
    QFont headingFont = heading->font();
    headingFont.setPointSize(headingFont.pointSize() + 3);
    headingFont.setBold(true);
    heading->setFont(headingFont);
    layout->addWidget(heading);

    // A read-only QTextBrowser rather than a QLabel: the body must both SCROLL and be
    // measurable (see the header). It drives the same QTextDocument a QLabel would, so the
    // callers' <dl>/<dt>/<dd> and any <a href> render exactly as they did in the tooltip.
    // Frameless with a transparent viewport so it reads as dialog prose, not a text field.
    body_ = new QTextBrowser(this);
    body_->setOpenExternalLinks(true);
    body_->setFrameShape(QFrame::NoFrame);
    body_->viewport()->setAutoFillBackground(false);
    body_->setHtml(bodyHtml);
    layout->addWidget(body_, /*stretch=*/1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);
    layout->addWidget(buttons);

    // --- Size to the content, capped to the screen ---------------------------------
    const QScreen* screen = screenFor(parent);
    const QRect available =
        screen != nullptr ? screen->availableGeometry() : QRect(0, 0, 1024, 768);
    const int width = std::min(kPreferredWidth, available.width() - 80);

    // Lay the text out at the width it will really have and ask how tall it came out. Measure
    // through a SEPARATE QTextDocument, never body_->document(): a QTextEdit in its default
    // WidgetWidth wrap mode owns its document's text width and snaps it back to the (still
    // ~100 px, unlaid-out) viewport the moment the layout changes — which silently measured
    // every body at a sliver of its real width and so reported it 3-6× too tall, sending even
    // a five-entry list to the screen cap. A scrollbar's width is reserved unconditionally:
    // if none ends up showing, the measure is merely conservative — which errs toward a
    // little slack rather than toward clipping the last line.
    const int scrollbar = style()->pixelMetric(QStyle::PM_ScrollBarExtent, nullptr, body_);
    QTextDocument measured;
    measured.setDefaultFont(body_->font());
    measured.setDocumentMargin(body_->document()->documentMargin());
    measured.setHtml(bodyHtml);
    measured.setTextWidth(width - 2 * kLayoutMargin - scrollbar);
    const int contentHeight = static_cast<int>(std::ceil(measured.size().height())) + 2;

    // Not std::clamp: its lo <= hi precondition is UB, and a degenerate screen (an empty
    // availableGeometry from a display being disconnected, a headless or virtual one) would
    // put the cap under the floor. Floor last, so a tiny screen yields a too-tall dialog
    // rather than undefined behaviour.
    const int maxBody = static_cast<int>(available.height() * kMaxBodyScreenShare);
    const int bodyHeight = std::max(kMinBodyHeight, std::min(contentHeight, maxBody));

    // Pin the body and read the layout's total MINIMUM — with the body pinned that is exactly
    // "heading + buttons + margins + this body", so it hands us the window height directly.
    // (sizeHint() is no use here: a QTextBrowser's own hint is a generic default that ignores
    // the content, and the widget's cached hint doesn't even see the pin.) Then relax the pin
    // before resizing, so the user can still shrink the dialog afterwards.
    // Do NOT reach for adjustSize() here: QWidgetPrivate::adjustedSize() inflates any WINDOW
    // whose layout is expanding to two thirds of the screen in BOTH dimensions, and a
    // QTextBrowser is Expanding — that is precisely the oversized box being removed.
    body_->setFixedHeight(bodyHeight);
    const int windowHeight = layout->totalMinimumSize().height();
    body_->setMinimumHeight(kMinBodyHeight);
    body_->setMaximumHeight(QWIDGETSIZE_MAX);
    resize(width, std::min(windowHeight, static_cast<int>(available.height() * 0.9)));

    // Take the floor from the layout too, now that the pin is off — an EXPLICIT minimum
    // suppresses the one QLayout::SetDefaultConstraint would have installed, so a hardcoded
    // pair shorter than the layout needs (heading + a 120 px body + the button box) would let
    // the user drag the dialog down until Close is squeezed out of reach.
    const QSize floor = layout->totalMinimumSize();
    setMinimumSize(std::max(320, floor.width()), floor.height());
    body_->setFocus();  // so PageDown/arrows scroll a long list at once; Esc still closes
}

}  // namespace pokedex
