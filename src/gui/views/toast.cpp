#include "gui/views/toast.h"

#include <QAbstractAnimation>
#include <QGraphicsOpacityEffect>
#include <QLabel>
#include <QLatin1String>
#include <QPropertyAnimation>
#include <QTimer>
#include <QWidget>

namespace pokedex {

namespace {

// So at most one toast lives per window: a new one replaces any still on screen,
// found by this object name, rather than stacking up.
constexpr QLatin1String kToastName("pokedexToast");
constexpr int kVisibleMs = 1900;  // how long it holds at full opacity
constexpr int kFadeMs = 450;      // the fade-out duration
constexpr int kBottomMargin = 32;  // gap from the window's bottom edge

}  // namespace

void showToast(QWidget* anchor, const QString& message) {
    if (!anchor) {
        return;
    }
    QWidget* window = anchor->window();
    if (!window) {
        return;
    }

    // Replace any in-flight toast so rapid successive confirmations don't stack.
    if (QWidget* existing =
            window->findChild<QWidget*>(kToastName, Qt::FindDirectChildrenOnly)) {
        existing->deleteLater();
    }

    auto* toast = new QLabel(message, window);
    toast->setObjectName(kToastName);
    toast->setAttribute(Qt::WA_TransparentForMouseEvents);  // never intercept clicks
    toast->setAttribute(Qt::WA_DeleteOnClose);
    toast->setAlignment(Qt::AlignCenter);
    // A dark translucent HUD pill (macOS-style) — legible over either theme.
    toast->setStyleSheet(
        "QLabel {"
        " background: rgba(28, 28, 30, 0.92);"
        " color: white;"
        " padding: 10px 20px;"
        " border-radius: 11px;"
        " font-size: 13px;"
        "}");

    // Fade the whole pill (background included) via an opacity effect.
    auto* opacity = new QGraphicsOpacityEffect(toast);
    opacity->setOpacity(1.0);
    toast->setGraphicsEffect(opacity);

    // Center near the bottom of the window. The toast is transient, so a fixed
    // position at show-time is fine (no need to track window resizes).
    toast->adjustSize();
    const int x = (window->width() - toast->width()) / 2;
    const int y = window->height() - toast->height() - kBottomMargin;
    toast->move(qMax(0, x), qMax(0, y));
    toast->show();
    toast->raise();

    // Hold, then fade out and close (WA_DeleteOnClose disposes it). The timer is
    // parented to the toast, so replacing the toast cancels its pending fade.
    QTimer::singleShot(kVisibleMs, toast, [toast, opacity]() {
        auto* fade = new QPropertyAnimation(opacity, "opacity", toast);
        fade->setDuration(kFadeMs);
        fade->setStartValue(1.0);
        fade->setEndValue(0.0);
        QObject::connect(fade, &QPropertyAnimation::finished, toast, &QWidget::close);
        fade->start(QAbstractAnimation::DeleteWhenStopped);
    });
}

}  // namespace pokedex
