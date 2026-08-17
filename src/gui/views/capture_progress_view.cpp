#include "gui/views/capture_progress_view.h"

#include <QColor>
#include <QEnterEvent>
#include <QEvent>
#include <QFont>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QRectF>
#include <QShowEvent>
#include <QVBoxLayout>

#include <algorithm>
#include <cstddef>
#include <exception>
#include <functional>
#include <vector>

#include "core/app/card_copy_service.h"
#include "gui/views/back_button.h"
#include "gui/views/binder_layout_labels.h"  // percentLabel
#include "gui/views/muted_text.h"
#include "gui/views/region_labels.h"

namespace pokedex {

// The progress strip: the whole-collection one across the top, and the narrower one
// beside each region's name. Hand-painted rather than a stylesheet'd QProgressBar for
// the reason primary_button.h already records: a Qt style sheet can't reference palette
// roles dynamically, so a `palette(highlight)` chunk goes stale across a live theme or
// accent change, while palette().color() here is re-read on every repaint. (Any
// stylesheet on a QProgressBar drops it out of native rendering anyway, so there is no
// native look to preserve.) Declares no signals, so it needs no Q_OBJECT.
class BarStrip : public QWidget {
public:
    BarStrip(int barHeight, int maxWidth, QWidget* parent) : QWidget(parent) {
        // The row beneath owns the mouse: a child receiving it would end the row's
        // hover and swallow the click. (Harmless on the global bar, which sits in no
        // clickable row.)
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setFixedHeight(barHeight);
        // Capped rather than fixed: the breakdown is a block to compare regions down a
        // column, so a region's bar must stop growing well before the pane's width (the
        // figures belong NEXT to it, not a splitter away) — but it must still be able to
        // shrink, or these nine rows would put a hard minimum width on the section. The
        // global bar is capped too, to the width of a whole region row (see below), so
        // the two line up rather than one running to the window edge.
        setMinimumWidth(48);
        setMaximumWidth(maxWidth);
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

namespace {

// How wide a region's bar may grow. Wider than the cramped header this screen replaced
// (160px) — a section has the room — but still capped, so the counts stay beside the
// bar rather than a screen away from it.
constexpr int kRegionBarWidth = 320;

}  // namespace

// One region's row: name, bar, "95 of 151", "(63%)". Clickable across its whole width
// (the click browses that region), hovering by painting rather than a stylesheet — same
// reasoning as BarStrip, plus it spares us WA_StyledBackground and an id selector to
// keep the sheet off the child labels. It reports the click through a std::function
// rather than a signal, so it needs no Q_OBJECT of its own (the select_all_line_edit.h
// precedent) — a class defined in a .cpp that declared one would need an explicit
// `#include "….moc"`, a pattern this repo has nowhere. (The file is moc'd regardless,
// for CaptureProgressView itself.)
struct CaptureProgressView::Row : QWidget {
    Row(Region regionArg, int nameWidth, int countsWidth, int percentWidth, QWidget* parent)
        : QWidget(parent), region(regionArg) {
        setCursor(Qt::PointingHandCursor);
        setToolTip(CaptureProgressView::tr("Browse %1 in the Pokémon list").arg(regionLabel(region)));

        name = new QLabel(regionLabel(region), this);
        name->setFixedWidth(nameWidth);
        counts = new QLabel(this);
        counts->setFixedWidth(countsWidth);
        counts->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        percent = new QLabel(this);
        percent->setFixedWidth(percentWidth);
        percent->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        bar = new BarStrip(6, kRegionBarWidth, this);

        // Every child must be transparent to the mouse, or the row loses both the
        // click (which would land on a label) and its hover (a child taking the
        // pointer sends the row a HoverLeave, flickering the highlight off over
        // each label).
        for (QWidget* child : {static_cast<QWidget*>(name), static_cast<QWidget*>(counts),
                               static_cast<QWidget*>(percent)}) {
            child->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        }

        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(6, 4, 6, 4);
        layout->setSpacing(12);
        layout->addWidget(name);
        layout->addWidget(bar, 1);  // grows first, up to its cap; the stretch takes the rest
        layout->addWidget(counts);
        layout->addWidget(percent);
        layout->addStretch();  // the block stays left; the row stays clickable full-width
    }

    void setProgress(const RegionProgress& progress) {
        bar->setRatio(progress.capturedSpecies, progress.totalSpecies);
        counts->setText(CaptureProgressView::tr("%1 of %2")
                            .arg(progress.capturedSpecies)
                            .arg(progress.totalSpecies));
        // percentLabel requires a positive whole; a region with no catalog species
        // has no ratio to state.
        percent->setText(progress.totalSpecies > 0
                             ? QStringLiteral("(%1)").arg(percentLabel(
                                   progress.capturedSpecies, progress.totalSpecies))
                             : QString());
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
        if (!hovered_) {
            return;  // an untouched row is just text on the window background
        }
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        QColor wash = palette().color(QPalette::Highlight);
        wash.setAlpha(28);
        painter.setPen(Qt::NoPen);
        painter.setBrush(wash);
        painter.drawRoundedRect(QRectF(rect()), 4.0, 4.0);
    }

private:
    bool hovered_ = false;
};

CaptureProgressView::CaptureProgressView(PokemonBrowseService& browse, CardCopyService& copies,
                                         QWidget* parent)
    : QWidget(parent), browse_(browse), copies_(copies) {
    // A pushed page, so it carries its own Back bar and title (the WishlistEditPage
    // idiom) — the sidebar no longer names this screen, so the heading is the only
    // thing that does.
    auto* backButton = makeBackButton(this);
    connect(backButton, &QPushButton::clicked, this, &CaptureProgressView::backRequested);

    auto* heading = new QLabel(tr("Gotta Catch 'Em All!"), this);
    QFont headingFont = heading->font();
    headingFont.setBold(true);
    headingFont.setPointSize(headingFont.pointSize() + 3);
    heading->setFont(headingFont);

    auto* topBar = new QHBoxLayout;
    topBar->addWidget(backButton);
    topBar->addWidget(heading);
    topBar->addStretch();

    // The headline, built as one QLabel PER FIGURE rather than a single rich-text line,
    // because each needs its own hover tooltip and Qt has no per-span tooltip inside a
    // label (the BinderView stats-row idiom). Each label but the first carries its own
    // leading " · ", so hiding one takes its separator with it. Filled by setProgress().
    auto* statsRow = new QWidget(this);
    auto* statsLayout = new QHBoxLayout(statsRow);
    statsLayout->setContentsMargins(0, 0, 0, 0);
    statsLayout->setSpacing(0);
    capturedStat_ = new QLabel(statsRow);
    QFont headlineFont = capturedStat_->font();
    headlineFont.setPointSize(headlineFont.pointSize() + 4);
    headlineFont.setBold(true);
    capturedStat_->setFont(headlineFont);
    capturedStat_->setToolTip(
        tr("Pokémon species you own at least one card of, out of every species in the "
           "National Pokédex. Duplicates don't add to this — the Cards figure counts those. "
           "Cards on their way to you and removed ones don't count."));
    cardsStat_ = new QLabel(statsRow);
    cardsStat_->setFont(headlineFont);
    // Muted per-label rather than through a stylesheet on the row, so the figure beside
    // it keeps the normal text colour: this is the screen's headline, not a footnote.
    applyMutedText(cardsStat_);
    cardsStat_->setToolTip(
        tr("Owned card copies that depict a Pokémon species. Cards on their way to you and "
           "removed ones aren't counted, and neither are Trainer/Energy cards, which depict "
           "no species — so this can be lower than the row count in My Cards."));
    statsLayout->addWidget(capturedStat_);
    statsLayout->addWidget(cardsStat_);
    statsLayout->addStretch();

    // The whole-collection bar, built below once the region rows' column widths are
    // measured: it is taller than a region's, and capped to the exact width of a region
    // row so the two line up at both ends instead of the total running off to the window
    // edge on a wide screen.

    auto* byRegionLabel = new QLabel(tr("By region"), this);
    QFont sectionFont = byRegionLabel->font();
    sectionFont.setBold(true);
    byRegionLabel->setFont(sectionFont);

    auto* hint = new QLabel(tr("Click a region to browse it in the Pokémon list."), this);
    applyMutedText(hint);

    // The nine rows are built once: the regions are compile-time constant, so a refresh
    // only re-texts them. Without a shared grid (a clickable row has to own its cells)
    // the columns line up by fixed widths, measured here from the widest content each
    // will ever hold. A QLabel doesn't elide, it clips, so each measurement has to cover
    // the WIDEST rendering the cell can ever take — the percentage as "(>99%)",
    // percentLabel's near-complete guard, which is not guaranteed to fit "(100%)".
    int nameWidth = 0;
    for (const Region region : kRegions) {
        nameWidth = std::max(nameWidth, fontMetrics().horizontalAdvance(regionLabel(region)));
    }
    const int countsWidth = fontMetrics().horizontalAdvance(QStringLiteral("0000 of 0000"));
    const int percentWidth =
        std::max(fontMetrics().horizontalAdvance(QStringLiteral("(100%)")),
                 fontMetrics().horizontalAdvance(
                     QStringLiteral("(%1)").arg(percentLabel(150, 151))));

    // A region row's VISIBLE extent: its four columns and the three 12px gaps between
    // them. Spelled from the same numbers the row's layout uses, so the total bar can't
    // drift out of alignment with the rows under it. NEITHER of the row's own horizontal
    // margins is counted here: the leading one is reproduced instead by indenting the bar
    // (below), so both ends line up rather than the left overhanging by 6px; the trailing
    // one sits at the far right of the row widget, past the addStretch() that absorbs the
    // slack after the percentage, so it is not between any two columns at all.
    totalBar_ = new BarStrip(
        12, nameWidth + 12 + kRegionBarWidth + 12 + countsWidth + 12 + percentWidth, this);

    auto* rowsBox = new QWidget(this);
    auto* rowsLayout = new QVBoxLayout(rowsBox);
    rowsLayout->setContentsMargins(0, 0, 0, 0);
    rowsLayout->setSpacing(2);
    for (std::size_t i = 0; i < kRegions.size(); ++i) {
        auto* row = new Row(kRegions[i], nameWidth, countsWidth, percentWidth, rowsBox);
        row->onClick = [this, region = kRegions[i]]() { emit regionActivated(region); };
        rows_[i] = row;
        rowsLayout->addWidget(row);
    }

    // Everything the figures live in, hidden as one block when the collection can't be
    // read — a screen of zeros would be a confident lie, where the message below says
    // what actually happened.
    body_ = new QWidget(this);
    auto* bodyLayout = new QVBoxLayout(body_);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(8);
    bodyLayout->addWidget(statsRow);
    // The bar grows to its cap and the stretch takes the rest, so it stays left-aligned
    // over the rows. (Adding it to the vertical layout directly would centre it once the
    // section is wider than the cap.) The 6px left margin is the region row's own leading
    // margin, restated so the bar starts exactly where a region's name does.
    auto* totalBarRow = new QHBoxLayout;
    totalBarRow->setContentsMargins(6, 0, 0, 0);
    totalBarRow->addWidget(totalBar_, 1);
    totalBarRow->addStretch();
    bodyLayout->addLayout(totalBarRow);
    bodyLayout->addSpacing(12);
    bodyLayout->addWidget(byRegionLabel);
    bodyLayout->addWidget(rowsBox);
    bodyLayout->addWidget(hint);

    errorLabel_ = new QLabel(tr("Couldn't read your collection just now — reopen this "
                                "section to try again."),
                             this);
    errorLabel_->setWordWrap(true);
    applyMutedText(errorLabel_);
    errorLabel_->setVisible(false);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 12, 16, 12);  // don't hug the section edges
    layout->setSpacing(0);
    layout->addLayout(topBar);
    layout->addSpacing(12);
    layout->addWidget(body_);
    layout->addWidget(errorLabel_);
    layout->addStretch();  // the figures sit at the top; the screen doesn't stretch them
}

void CaptureProgressView::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    refresh();
}

void CaptureProgressView::refresh() {
    // Skip the query when no copy has been added/edited/removed in any section since the
    // last successful read: the figures derive only from the catalog (compile-time
    // constant) and the copy inventory, so an unchanged revision means they can't have
    // moved.
    if (copies_.revision() == revision_) {
        return;
    }
    std::vector<PokemonBrowseEntry> entries;
    try {
        entries = browse_.listAll();
    } catch (const std::exception&) {
        // Hold the sentinel (never a real revision) so the next show retries.
        revision_ = -1;
        body_->setVisible(false);
        errorLabel_->setVisible(true);
        return;
    }
    revision_ = copies_.revision();
    errorLabel_->setVisible(false);
    body_->setVisible(true);
    setProgress(regionProgress(entries));
}

void CaptureProgressView::setProgress(std::span<const RegionProgress> regions) {
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
    totalBar_->setRatio(total.capturedSpecies, total.totalSpecies);

    // regionProgress reports every region in kRegions order, so the rows and the
    // spans line up positionally; a short span (never produced today) leaves the
    // trailing rows as they were rather than reading past the end.
    const std::size_t shown = std::min(regions.size(), rows_.size());
    for (std::size_t i = 0; i < shown; ++i) {
        rows_[i]->setProgress(regions[i]);
    }
}

}  // namespace pokedex
