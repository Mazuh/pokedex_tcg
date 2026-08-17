#include "gui/views/region_progress_panel.h"

#include <QColor>
#include <QEnterEvent>
#include <QEvent>
#include <QFont>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QRectF>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cstddef>
#include <functional>

#include "gui/views/binder_layout_labels.h"  // percentLabel
#include "gui/views/region_labels.h"

namespace pokedex {

namespace {

// The progress strip beside a region's name. Hand-painted rather than a stylesheet'd
// QProgressBar for the reason primary_button.h already records: a Qt style sheet can't
// reference palette roles dynamically, so a `palette(highlight)` chunk goes stale across
// a live theme or accent change, while palette().color() here is re-read on every repaint.
// (Any stylesheet on a QProgressBar drops it out of native rendering anyway, so there is
// no native look to preserve.) Declares no signals, so it needs no Q_OBJECT.
class BarStrip : public QWidget {
public:
    explicit BarStrip(QWidget* parent) : QWidget(parent) {
        // The row beneath owns the mouse: a child receiving it would end the row's
        // hover and swallow the click.
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setFixedHeight(6);
        // Capped rather than fixed: the breakdown is a compact block to compare regions
        // down a column, so the bar must stop growing well before the pane's width (the
        // figures belong NEXT to it, not a splitter away) — but it must still be able to
        // shrink, or these nine rows would put a hard minimum width on the whole browse
        // pane and stop the user dragging the splitter to enlarge the detail panel.
        setMinimumWidth(48);
        setMaximumWidth(160);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

    void setRatio(int part, int whole) {
        part_ = part;
        whole_ = whole;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const qreal radius = height() / 2.0;
        painter.setPen(Qt::NoPen);
        painter.setBrush(palette().color(QPalette::Mid));
        painter.drawRoundedRect(QRectF(rect()), radius, radius);
        if (part_ <= 0 || whole_ <= 0) {
            return;  // nothing captured: an empty track, not a sliver of fill
        }
        // A tiny nonzero ratio must still be visible — the same rule percentLabel
        // follows when it renders "<1%" rather than a flat 0%. So the fill is never
        // narrower than its own rounded cap.
        const qreal full = width();
        const qreal filled =
            std::min(full, std::max(full * part_ / whole_, 2.0 * radius));
        painter.setBrush(palette().color(QPalette::Highlight));
        painter.drawRoundedRect(QRectF(0.0, 0.0, filled, height()), radius, radius);
    }

private:
    int part_ = 0;
    int whole_ = 0;
};

}  // namespace

// One region's row: name, bar, "95 of 151", "(63%)". Clickable across its whole width
// (the click is what filters the list), hovering and active-highlighting by painting
// rather than a stylesheet — same reasoning as BarStrip, plus it spares us
// WA_StyledBackground and an id selector to keep the sheet off the child labels.
// It reports the click through a std::function rather than a signal, so it needs no
// Q_OBJECT of its own (the select_all_line_edit.h precedent) — a class defined in a .cpp
// that declared one would need an explicit `#include "….moc"`, a pattern this repo has
// nowhere. (The file is moc'd regardless, for RegionProgressPanel itself.)
struct RegionProgressPanel::Row : QWidget {
    Row(Region regionArg, int nameWidth, int countsWidth, int percentWidth, QWidget* parent)
        : QWidget(parent), region(regionArg) {
        setCursor(Qt::PointingHandCursor);

        name = new QLabel(regionLabel(region), this);
        name->setFixedWidth(nameWidth);
        counts = new QLabel(this);
        counts->setFixedWidth(countsWidth);
        counts->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        percent = new QLabel(this);
        percent->setFixedWidth(percentWidth);
        percent->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        bar = new BarStrip(this);

        // Every child must be transparent to the mouse, or the row loses both the
        // click (which would land on a label) and its hover (a child taking the
        // pointer sends the row a HoverLeave, flickering the highlight off over
        // each label).
        for (QWidget* child : {static_cast<QWidget*>(name), static_cast<QWidget*>(counts),
                               static_cast<QWidget*>(percent)}) {
            child->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        }

        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(6, 2, 6, 2);
        layout->setSpacing(8);
        layout->addWidget(name);
        layout->addWidget(bar, 1);  // grows first, up to its cap; the stretch takes the rest
        layout->addWidget(counts);
        layout->addWidget(percent);
        layout->addStretch();  // the block stays left; the row stays clickable full-width

        setActive(false);  // seeds the tooltip
    }

    void setProgress(const RegionProgress& progress) {
        bar->setRatio(progress.capturedSpecies, progress.totalSpecies);
        counts->setText(RegionProgressPanel::tr("%1 of %2")
                            .arg(progress.capturedSpecies)
                            .arg(progress.totalSpecies));
        // percentLabel requires a positive whole; a region with no catalog species
        // has no ratio to state.
        percent->setText(progress.totalSpecies > 0
                             ? QStringLiteral("(%1)").arg(percentLabel(
                                   progress.capturedSpecies, progress.totalSpecies))
                             : QString());
    }

    void setActive(bool active) {
        active_ = active;
        QFont font = name->font();
        font.setBold(active);
        name->setFont(font);
        setToolTip(active ? RegionProgressPanel::tr("Stop showing only %1")
                                .arg(regionLabel(region))
                          : RegionProgressPanel::tr("Show only %1").arg(regionLabel(region)));
        update();
    }

    std::function<void()> onClick;
    Region region;
    QLabel* name;
    BarStrip* bar;
    QLabel* counts;
    QLabel* percent;

protected:
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && onClick) {
            onClick();
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);
    }

    void enterEvent(QEnterEvent* event) override {
        hovered_ = true;
        update();
        QWidget::enterEvent(event);
    }

    void leaveEvent(QEvent* event) override {
        hovered_ = false;
        update();
        QWidget::leaveEvent(event);
    }

    void paintEvent(QPaintEvent*) override {
        if (!hovered_ && !active_) {
            return;  // an untouched row is just text on the window background
        }
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        QColor wash = palette().color(QPalette::Highlight);
        wash.setAlpha(active_ ? 60 : 28);  // the active region reads stronger than a hover
        painter.setPen(Qt::NoPen);
        painter.setBrush(wash);
        painter.drawRoundedRect(QRectF(rect()), 4.0, 4.0);
    }

private:
    bool hovered_ = false;
    bool active_ = false;
};

RegionProgressPanel::RegionProgressPanel(QWidget* parent) : QWidget(parent) {
    // The headline, built as one QLabel PER FIGURE rather than a single rich-text line,
    // because each needs its own hover tooltip and Qt has no per-span tooltip inside a
    // label (the BinderView stats-row idiom). The muted colour is declared once on this
    // container and cascades to its labels — it must NOT go on the panel, or the region
    // names below grey out too. Each label but the first carries its own leading " · ",
    // so hiding one takes its separator with it. Filled by setProgress().
    auto* statsRow = new QWidget(this);
    statsRow->setStyleSheet(QStringLiteral("QLabel { color: gray; }"));
    auto* statsLayout = new QHBoxLayout(statsRow);
    statsLayout->setContentsMargins(0, 0, 0, 0);
    statsLayout->setSpacing(0);
    capturedStat_ = new QLabel(statsRow);
    capturedStat_->setToolTip(
        tr("Pokémon species you own at least one card of, out of every species in the "
           "National Pokédex. Duplicates don't add to this — the Cards figure counts those. "
           "Cards on their way to you and removed ones don't count."));
    cardsStat_ = new QLabel(statsRow);
    cardsStat_->setToolTip(
        tr("Owned card copies that depict a Pokémon species. Cards on their way to you and "
           "removed ones aren't counted, and neither are Trainer/Energy cards, which depict "
           "no species — so this can be lower than the row count in My Cards."));
    statsLayout->addWidget(capturedStat_);
    statsLayout->addWidget(cardsStat_);
    statsLayout->addStretch();

    // A native disclosure control (arrow + text), not a unicode "▾", whose size and
    // baseline drift with the font. Flat, like the price surfaces' ⓘ button — and
    // deliberately NOT accented: that affordance is reserved for a form's commit button.
    // Deliberately NOT checkable either: a checked auto-raise tool button paints a
    // pressed background, which reads as a boxed control rather than a disclosure
    // triangle. The arrow already says which state it is in, so the expanded/collapsed
    // state is read back off rowsBox_'s visibility instead.
    toggle_ = new QToolButton(this);
    toggle_->setText(tr("By region"));
    toggle_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);  // else the text vanishes
    toggle_->setAutoRaise(true);
    toggle_->setFocusPolicy(Qt::NoFocus);
    toggle_->setToolTip(tr("Show or hide the per-region breakdown"));

    // The nine rows are built once: the regions are compile-time constant, so a refresh
    // only re-texts them. Without a shared grid (a clickable row has to own its cells)
    // the columns line up by fixed widths, measured here from the widest content each
    // will ever hold. A QLabel doesn't elide, it clips, so each measurement has to cover
    // the WIDEST rendering the cell can ever take:
    //  - the name in BOLD, since setActive bolds the active region's label in place;
    //  - the percentage as "(>99%)", percentLabel's near-complete guard, which is not
    //    guaranteed to fit the width of "(100%)".
    QFont boldFont = font();
    boldFont.setBold(true);
    const QFontMetrics boldMetrics(boldFont);
    int nameWidth = 0;
    for (const Region region : kRegions) {
        nameWidth = std::max(nameWidth, boldMetrics.horizontalAdvance(regionLabel(region)));
    }
    const int countsWidth = fontMetrics().horizontalAdvance(QStringLiteral("0000 of 0000"));
    const int percentWidth =
        std::max(fontMetrics().horizontalAdvance(QStringLiteral("(100%)")),
                 fontMetrics().horizontalAdvance(
                     QStringLiteral("(%1)").arg(percentLabel(150, 151))));

    rowsBox_ = new QWidget(this);
    auto* rowsLayout = new QVBoxLayout(rowsBox_);
    rowsLayout->setContentsMargins(0, 2, 0, 0);
    rowsLayout->setSpacing(1);
    for (std::size_t i = 0; i < kRegions.size(); ++i) {
        auto* row = new Row(kRegions[i], nameWidth, countsWidth, percentWidth, rowsBox_);
        // The panel holds no idea of which region is active — it just reports the click
        // and lets the host, which owns the search box, decide what that means.
        row->onClick = [this, region = kRegions[i]]() { emit regionActivated(region); };
        rows_[i] = row;
        rowsLayout->addWidget(row);
    }

    auto* header = new QHBoxLayout;
    header->setContentsMargins(0, 0, 0, 0);
    header->addWidget(statsRow, 1);
    header->addWidget(toggle_);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 4);
    layout->setSpacing(0);
    layout->addLayout(header);
    layout->addWidget(rowsBox_);

    connect(toggle_, &QToolButton::clicked, this, [this]() {
        const bool expanded = !rowsBox_->isVisible();
        rowsBox_->setVisible(expanded);
        toggle_->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
    });
    toggle_->setArrowType(Qt::DownArrow);  // starts expanded — the breakdown is the point
}

void RegionProgressPanel::setProgress(std::span<const RegionProgress> regions) {
    // Fold the rows for the headline rather than counting again, so the total above
    // the breakdown always agrees with the breakdown.
    const CollectionProgress total = totalProgress(regions);
    capturedStat_->setText(
        total.totalSpecies > 0
            ? tr("Captured %1 of %2 (%3)")
                  .arg(total.capturedSpecies)
                  .arg(total.totalSpecies)
                  .arg(percentLabel(total.capturedSpecies, total.totalSpecies))
            : tr("Captured %1").arg(total.capturedSpecies));
    cardsStat_->setText(tr(" · Cards %1").arg(total.cards));

    // regionProgress reports every region in kRegions order, so the rows and the
    // spans line up positionally; a short span (never produced today) leaves the
    // trailing rows as they were rather than reading past the end.
    const std::size_t shown = std::min(regions.size(), rows_.size());
    for (std::size_t i = 0; i < shown; ++i) {
        rows_[i]->setProgress(regions[i]);
    }
}

void RegionProgressPanel::setActiveRegion(std::optional<Region> region) {
    for (Row* row : rows_) {
        row->setActive(region && *region == row->region);
    }
}

}  // namespace pokedex
