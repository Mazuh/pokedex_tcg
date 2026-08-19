#include "gui/views/card_copy_form.h"

#include <QCheckBox>
#include <QComboBox>
#include <QEvent>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPalette>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStringList>
#include <QTextCursor>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cstddef>
#include <functional>
#include <iterator>
#include <utility>

#include "gui/views/binder_combo.h"
#include "gui/views/condition_labels.h"
#include "gui/views/empty_option.h"
#include "gui/views/foil_labels.h"
#include "gui/views/glyph_button.h"
#include "gui/views/info_button.h"
#include "gui/views/language_codes.h"
#include "gui/views/muted_text.h"
#include "gui/views/ownership_labels.h"
#include "gui/views/rarity_labels.h"
#include "gui/views/warning_text.h"

namespace pokedex {

namespace {

// The enum values each picker and its info popover iterate — the single lists, so a
// new enumerator flows into both the combo and the explanation automatically. The
// rarity split matches the CardRarity docstring: the modern scale, then the legacy
// rarities from older eras (shown as a distinct popover section).
constexpr CardCondition kConditions[] = {
    CardCondition::NearMint, CardCondition::LightlyPlayed, CardCondition::ModeratelyPlayed,
    CardCondition::HeavilyPlayed, CardCondition::Damaged};
constexpr CardRarity kModernRarities[] = {
    CardRarity::Common,    CardRarity::Uncommon,        CardRarity::Rare,
    CardRarity::DoubleRare, CardRarity::IllustrationRare, CardRarity::UltraRare,
    CardRarity::SpecialIllustrationRare, CardRarity::HyperRare, CardRarity::Promo};
constexpr CardRarity kLegacyRarities[] = {
    CardRarity::RareHolo,   CardRarity::RareHoloEX,  CardRarity::RarePrime,
    CardRarity::RareLegend, CardRarity::AmazingRare, CardRarity::Shining,
    CardRarity::Radiant,    CardRarity::AceSpec};
constexpr CardFoil kFoils[] = {
    CardFoil::NonHolo,        CardFoil::Holo,          CardFoil::ReverseHolo,
    CardFoil::CosmosHolo,     CardFoil::MirrorHolo,    CardFoil::CrackedIceHolo,
    CardFoil::ConfettiHolo,   CardFoil::CrosshatchHolo, CardFoil::HDHolo,
    CardFoil::Textured};

// A rich-text <dl> definition list of label/description pairs over a range of enum
// values, built from the same label/description helpers the picker uses (so the
// explanation can never drift from the options). Shared by all three info popovers —
// the single home of the dt/dd markup and its HTML-escaping.
template <class Range, class LabelFn, class DescFn>
QString definitionListHtml(const Range& values, LabelFn label, DescFn desc) {
    QString html = QStringLiteral("<dl>");
    for (const auto value : values) {
        html += QStringLiteral("<dt><b>%1</b></dt><dd>%2</dd>")
                    .arg(label(value).toHtmlEscaped(), desc(value).toHtmlEscaped());
    }
    html += QStringLiteral("</dl>");
    return html;
}

// The three "info" bodies are constant markup over compile-time enum ranges, so each is
// built once (function-local static) and returned by reference — every CardCopyForm (a
// fresh one per Add/Edit page open) then reuses it instead of re-escaping and
// re-concatenating the same definition list. The app has no runtime language switching, so
// freezing the translation at first build is fine. Each is built LAZILY, on the first click
// that opens the dialog, since makeInfoButton takes a provider rather than a string.
//
// None of them repeats its own title: InfoDialog renders the title the button was given as
// the dialog's heading.

// The condition-picker explanation: every grade, each with its description.
const QString& conditionInfoHtml() {
    static const QString html = definitionListHtml(kConditions, conditionLabel,
                                                   conditionDescription);
    return html;
}

// The rarity-picker explanation: the modern scale, then a "Legacy rarities" subheading with
// the older-era rarities — mirroring the two tables the terms come from, so the legacy ones
// read as a distinct, secondary group. The longest of the three by far, and the reason
// these moved off QToolTip onto a scrollable dialog.
const QString& rarityInfoHtml() {
    static const QString html =
        definitionListHtml(kModernRarities, rarityLabel, rarityDescription) +
        QStringLiteral("<p><b>Legacy rarities</b> (older eras)</p>") +
        definitionListHtml(kLegacyRarities, rarityLabel, rarityDescription);
    return html;
}

// The foil-treatment explanation: every finish, each with its description.
const QString& foilInfoHtml() {
    static const QString html = definitionListHtml(kFoils, foilLabel, foilDescription);
    return html;
}

// What "no fixed position" means, for the ⓘ beside the checkbox. Long enough to want the
// dialog rather than a tooltip: it has to say where the card goes, what it gives up, and
// that nothing is lost by changing your mind.
const QString& noFixedPositionInfoHtml() {
    static const QString html = QStringLiteral(
        "<p>Some cards never get a home sleeve — duplicates, trade fodder, a Trainer card "
        "that moves around. Tick this and the card is still filed in the binder, but it is "
        "listed in a loose run at the very <b>end</b> of the binder's guide, where you can "
        "reshuffle it as often as you like.</p>"
        "<p>Such a card takes no page or pocket number, holds no place in the Pokédex "
        "checklist (its species keeps reading as missing until another copy fills that "
        "sleeve), and cannot be moved to a named pocket. It still counts as captured in the "
        "binder's totals — you do own it, it just lives at the back.</p>"
        "<p>Untick it at any time and the card returns to its derived place, along with any "
        "arrangement that was recorded for it.</p>");
    return html;
}

// The "⚠" glyph the form's field rows carry, flagging a field the card catalog left empty
// (makeHintButton — a tooltip, since these are a sentence). Its "ⓘ" neighbour explaining
// what a picker's options mean is infoGlyph() / makeInfoButton — a dialog, since those run
// long.
const QString kMissingGlyph = QStringLiteral("⚠");

// The "⚠" popovers. Each says the same two things — nothing filled this in, and it is
// optional anyway — but names the reason per field, since the reasons genuinely differ (an
// English-only catalog, a grade only the holder can judge, a finish a scan can't tell
// apart). Static authored markup, so it bypasses tooltipText() — these are now the form's
// only tooltip-borne text, the ⓘ explanations having moved into a dialog.
QString missingHintHtml(const QString& why) {
    return QStringLiteral("<p><b>Not filled in for you</b><br>%1</p>"
                          "<p>It's optional — a card can be recorded without it.</p>")
        .arg(why);
}

}  // namespace

CardCopyForm::CardCopyForm(QWidget* parent) : QWidget(parent) {
    auto* form = new QFormLayout;
    // Let the inputs grow to fill the column (macOS defaults to leaving them at their
    // small size hint, which clips values like "McDonald's Collection 2021").
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    // The reference fields are stored data (autofilled from a picked card, or typed).
    // A USER edit (textEdited, not the autofill's setText) is reported so a host can
    // drop a now-stale finder selection.
    cardName_ = new QLineEdit(this);
    cardName_->setPlaceholderText(tr("e.g. Charizard ex"));
    connect(cardName_, &QLineEdit::textEdited, this,
            [this](const QString&) { Q_EMIT referenceEdited(); });

    expansionCode_ = new QLineEdit(this);
    expansionCode_->setPlaceholderText(tr("e.g. OBF"));
    connect(expansionCode_, &QLineEdit::textEdited, this,
            [this](const QString&) { Q_EMIT referenceEdited(); });

    setName_ = new QLineEdit(this);
    setName_->setPlaceholderText(tr("e.g. Obsidian Flames"));
    connect(setName_, &QLineEdit::textEdited, this,
            [this](const QString&) { Q_EMIT referenceEdited(); });

    // language / condition / ownership describe the physical copy (which language
    // print you own, its grade, whether it's owned/incoming/removed) rather than the
    // printing identity, so they stay editable even in the read-only "edit a copy"
    // case. A USER pick (activated, not the programmatic setCurrentIndex in loadCopy)
    // is reported so an edit host can enable its Save button.
    language_ = new QComboBox(this);
    // Each item carries the stored code as its data (so the empty entry can show the
    // shared noneOptionLabel() while still mapping back to ""). The leading blank in
    // languageCodes() is that empty entry; the rest are the real codes.
    for (const QString& code : languageCodes()) {
        language_->addItem(code.isEmpty() ? noneOptionLabel() : code, code);
    }
    // Every marked picker refreshes the "⚠" markers before reporting the change, so a
    // pick clears its own marker the instant it is made (and re-raises it on "— None —").
    connect(language_, &QComboBox::activated, this, [this](int) {
        refreshMissingFieldHints();
        Q_EMIT detailsChanged();
    });

    collectorNumber_ = new QLineEdit(this);
    collectorNumber_->setPlaceholderText(tr("e.g. 125/197"));
    connect(collectorNumber_, &QLineEdit::textEdited, this,
            [this](const QString&) { Q_EMIT referenceEdited(); });

    condition_ = new QComboBox(this);
    // Condition is optional (a copy can be recorded ungraded) — default to blank, so
    // nothing is claimed unless the user picks a grade. The -1 sentinel means "none";
    // its label is the shared noneOptionLabel() so every form's empty entry reads alike.
    // Labels come from conditionLabel() (the app's single source, so the picker reads
    // the same as the table and a new grade fails its exhaustive -Wswitch).
    condition_->addItem(noneOptionLabel(), -1);
    for (const CardCondition c : kConditions) {
        condition_->addItem(conditionLabel(c), static_cast<int>(c));
    }
    connect(condition_, &QComboBox::activated, this, [this](int) {
        refreshMissingFieldHints();
        Q_EMIT detailsChanged();
    });

    // Rarity and foil treatment are optional physical-copy attributes (like condition):
    // each has a noneOptionLabel() -1 sentinel first, then one item per enum value with
    // its label from the app's single source. Rarity lists the modern scale followed by
    // the legacy rarities (the two info-popover sections). Foil lists every finish.
    rarity_ = new QComboBox(this);
    rarity_->addItem(noneOptionLabel(), -1);
    for (const CardRarity r : kModernRarities) {
        rarity_->addItem(rarityLabel(r), static_cast<int>(r));
    }
    for (const CardRarity r : kLegacyRarities) {
        rarity_->addItem(rarityLabel(r), static_cast<int>(r));
    }
    connect(rarity_, &QComboBox::activated, this, [this](int) {
        refreshMissingFieldHints();
        Q_EMIT detailsChanged();
    });

    foil_ = new QComboBox(this);
    foil_->addItem(noneOptionLabel(), -1);
    for (const CardFoil f : kFoils) {
        foil_->addItem(foilLabel(f), static_cast<int>(f));
    }
    connect(foil_, &QComboBox::activated, this, [this](int) {
        refreshMissingFieldHints();
        Q_EMIT detailsChanged();
    });

    ownership_ = new QComboBox(this);
    // Only the live states are pickable. "Removed" is a one-way lifecycle transition
    // owned by the dedicated Remove verb (a confirmation plus an optional history note),
    // never a value set by editing a form field — and a Removed copy is frozen history
    // that CardCopyService won't re-edit anyway, so it never loads back into this combo.
    for (const CardOwnership o : {CardOwnership::Incoming, CardOwnership::Owned}) {
        ownership_->addItem(ownershipLabel(o), static_cast<int>(o));
    }
    ownership_->setCurrentIndex(ownership_->findData(static_cast<int>(CardOwnership::Owned)));
    connect(ownership_, &QComboBox::activated, this, [this](int) { Q_EMIT detailsChanged(); });

    // The binder the copy is filed in — populated + enabled via setupBinderPicker().
    // activated() fires only on a USER pick (not the programmatic setCurrentIndex in
    // setupBinderPicker), so a host can persist a reassignment without echoing loads.
    binder_ = new QComboBox(this);
    connect(binder_, &QComboBox::activated, this, [this](int) {
        updateBinderRemoveEnabled();  // a manual pick may move to/from "— None —"
        updateNoFixedPositionEnabled();
        Q_EMIT binderChanged();
    });

    // Explicit "unassign" affordance beside the combo — hidden until a host opts in with
    // setBinderRemovable(true) (the edit page). Clicking it selects "— None —" and drives
    // the same binderChanged() path a manual pick would, so the host's persist logic is
    // reused verbatim.
    unassignBinder_ = new QPushButton(tr("Remove from binder"), this);
    unassignBinder_->setVisible(false);
    unassignBinder_->setEnabled(false);
    connect(unassignBinder_, &QPushButton::clicked, this, [this]() {
        // fillBinderCombo always inserts "— None —" first, so index 0 is the unassigned
        // entry. The button is disabled while already there, so this is a no-op guard.
        if (binder_->currentIndex() == 0) {
            return;
        }
        binder_->setCurrentIndex(0);
        updateBinderRemoveEnabled();
        updateNoFixedPositionEnabled();
        Q_EMIT binderChanged();
    });

    // "No fixed position" — the copy is filed in the binder but keeps no home sleeve, so
    // the guide lists it in a loose run at the end (see CardCopy). It sits with the binder
    // picker because it qualifies the same decision, and reports through its own signal so
    // an edit host can persist it the instant it is toggled, exactly as it does the binder.
    noFixedPosition_ = new QCheckBox(tr("No fixed position — keep at the end"), this);
    connect(noFixedPosition_, &QCheckBox::toggled, this,
            [this](bool) { Q_EMIT noFixedPositionChanged(); });
    updateNoFixedPositionEnabled();  // no binder picked yet, so nothing to keep at the end of

    comments_ = new QPlainTextEdit(this);
    comments_->setPlaceholderText(
        tr("Capture story, price, seller, imperfections, dates…"));
    // textChanged fires for every source — typing, setComments, replaceComments, loadCopy —
    // so the comments marker needs no per-setter refresh, unlike the silent combo setters.
    connect(comments_, &QPlainTextEdit::textChanged, this, [this]() {
        refreshMissingFieldHints();
        Q_EMIT commentsChanged();
    });

    // Guard every combo against accidental wheel changes: give them StrongFocus (so a
    // wheel-over doesn't focus them) and filter their wheel events (eventFilter eats an
    // unfocused wheel). A stray scroll while scanning the form must not silently flip a
    // recorded copy's language/condition/rarity/foil/ownership or move it between binders.
    for (QComboBox* combo : {language_, condition_, rarity_, foil_, ownership_, binder_}) {
        combo->setFocusPolicy(Qt::StrongFocus);
        combo->installEventFilter(this);
    }

    // One builder for every row that carries a glyph beside its field. Right of the field
    // come, in order, the "⚠" marker (this field was left empty — see setMissingFieldHints)
    // and the "ⓘ" explaining the picker's terms (opaque abbreviations / jargon otherwise,
    // and the same source as the options themselves). Either may be absent; both wear
    // makeGlyphButton's look, so the affordance can't drift row to row — but they reveal
    // themselves differently: the ⚠'s sentence is a tooltip, the ⓘ's reference opens the
    // modal InfoDialog (its body would be clipped unread as a tooltip). Hence the ⓘ is
    // named by a TITLE plus a body PROVIDER, which is what keeps each …InfoHtml() static
    // lazy until the first click.
    //
    // The marker is amber (a hint, not an error — applyWarningText rather than
    // setEnabled(false), which would swallow the click that pops the explanation, and rather
    // than red, which would claim an optional field is invalid) and keeps its size while
    // hidden, so raising one never shifts the ⓘ or resizes the field. Its colour is an
    // explicit palette entry, so changeEvent below re-applies it when the theme flips.
    // Returns the marker for the caller to keep; nullptr when the row has none.
    //
    // Both glyphs occupy a fixed-width slot, and a row missing one still reserves it — so
    // the "⚠" column stays a column even though only three of the five marked rows carry an
    // "ⓘ" after it, and the fields all end at the same edge.
    const int glyphSlot = [] {
        QToolButton probe;  // measured, never shown: one column width for every row
        probe.setAutoRaise(true);
        int width = 0;
        for (const QString& glyph : {infoGlyph(), kMissingGlyph}) {
            probe.setText(glyph);
            width = std::max(width, probe.sizeHint().width());
        }
        return width;
    }();
    const auto fieldRow = [this, form, glyphSlot](
                              const QString& label, QWidget* field, const QString& missingWhy,
                              const QString& missingName, const QString& infoTitle,
                              std::function<QString()> infoBody,
                              Qt::Alignment glyphAlign = Qt::Alignment()) -> QToolButton* {
        // An absent glyph leaves an empty WIDGET of the same width behind, not addSpacing:
        // a spacer item and a widget lay out a few pixels apart, which is visible as a
        // column that doesn't quite line up between rows. Equal widths alone aren't enough
        // either — QHBoxLayout takes its inter-item spacing from the two neighbours'
        // QSizePolicy::ControlType (QStyle::layoutSpacing), so a plain QWidget beside a
        // button is spaced differently from a button beside a button. The filler therefore
        // claims the button's control type as well as its width.
        const auto emptySlot = [this, glyphSlot] {
            auto* filler = new QWidget(this);
            filler->setFixedWidth(glyphSlot);
            QSizePolicy policy{QSizePolicy::Fixed, QSizePolicy::Preferred};
            policy.setControlType(QSizePolicy::ToolButton);
            filler->setSizePolicy(policy);
            return filler;
        };
        auto* row = new QHBoxLayout;
        row->setContentsMargins(0, 0, 0, 0);
        row->addWidget(field, 1);
        QToolButton* missing = nullptr;
        if (missingWhy.isEmpty()) {
            row->addWidget(emptySlot());
        } else {
            missing = makeHintButton(this, kMissingGlyph, missingHintHtml(missingWhy), missingName);
            applyWarningText(missing);
            missing->setFixedWidth(glyphSlot);
            QSizePolicy policy = missing->sizePolicy();
            policy.setRetainSizeWhenHidden(true);
            missing->setSizePolicy(policy);
            missing->hide();  // armed only by setMissingFieldHints(true)
            row->addWidget(missing, 0, glyphAlign);
        }
        if (!infoBody) {
            row->addWidget(emptySlot());
        } else {
            auto* info = makeInfoButton(this, infoTitle, std::move(infoBody));
            info->setFixedWidth(glyphSlot);
            row->addWidget(info, 0, glyphAlign);
        }
        form->addRow(label, row);
        return missing;
    };

    // The row order is the point of the form: everything a picked card can autofill comes
    // first (the printed identity, then the catalog's best-effort rarity), and everything
    // only the person holding the card can answer comes after. So after a pick the eye runs
    // straight down to the first thing still needing attention instead of hunting up and
    // down a form whose filled and unfilled fields are interleaved.
    form->addRow(tr("Card name"), cardName_);
    form->addRow(tr("Expansion code"), expansionCode_);
    form->addRow(tr("Set name"), setName_);
    form->addRow(tr("Collector number"), collectorNumber_);
    rarityHint_ = fieldRow(tr("Rarity"), rarity_,
                           tr("The card catalog gave no rarity for this printing."),
                           tr("Rarity was not filled in"), tr("What the rarities mean"),
                           [] { return rarityInfoHtml(); });

    form->addRow(tr("Ownership"), ownership_);
    // The binder combo pairs with an optional "Remove from binder" button (hidden unless
    // setBinderRemovable(true)); a hidden button takes no layout space, so the add flow's
    // row is unchanged.
    auto* binderRow = new QHBoxLayout;
    binderRow->setContentsMargins(0, 0, 0, 0);
    binderRow->addWidget(binder_, 1);
    binderRow->addWidget(unassignBinder_);
    form->addRow(tr("Binder"), binderRow);
    // No label of its own: the checkbox's own text reads as the sentence, and an empty
    // label keeps it visually attached to the binder row it qualifies.
    fieldRow(QString(), noFixedPosition_, QString(),
             QString(), tr("Cards with no fixed position"),
             [] { return noFixedPositionInfoHtml(); });

    languageHint_ = fieldRow(tr("Language"), language_,
                             tr("The card catalog is English-only, so it can't tell which "
                                "language print you hold. Settings can pre-select a default."),
                             tr("Language was not filled in"), QString(), nullptr);
    conditionHint_ = fieldRow(tr("Condition"), condition_,
                              tr("Only you can see the card in hand, so nothing can grade it "
                                 "for you."),
                              tr("Condition was not filled in"),
                              tr("What the condition grades mean"),
                              [] { return conditionInfoHtml(); });
    foilHint_ = fieldRow(tr("Foil treatment"), foil_,
                         tr("The card catalog doesn't record which finish a printing came in."),
                         tr("Foil treatment was not filled in"),
                         tr("What the foil treatments mean"),
                         [] { return foilInfoHtml(); });
    commentsHint_ = fieldRow(tr("Comments"), comments_,
                             tr("Nothing to autofill here — it's your own note about this copy."),
                             tr("Comments were not filled in"), QString(), nullptr, Qt::AlignTop);

    // Qt derives tab order from CONSTRUCTION order, which no longer matches the rows above,
    // so spell the traversal out — otherwise Tab jumps from Collector number back up to
    // Language. The glyph buttons are NoFocus and stay out of it.
    QWidget* const tabChain[] = {cardName_, expansionCode_,   setName_,  collectorNumber_,
                                 rarity_,   ownership_,       binder_,   unassignBinder_,
                                 noFixedPosition_, language_, condition_, foil_,
                                 comments_};
    for (std::size_t i = 1; i < std::size(tabChain); ++i) {
        setTabOrder(tabChain[i - 1], tabChain[i]);
    }

    actions_ = new QHBoxLayout;
    actions_->addStretch();  // host buttons insert before the stretch (left-aligned)

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addLayout(form);
    layout->addLayout(actions_);
}

bool CardCopyForm::eventFilter(QObject* watched, QEvent* event) {
    // A QComboBox consumes wheel events and changes its selection on scroll — an easy
    // way to silently mis-set a value while scrolling past the form. Swallow the wheel
    // unless the combo is focused: an unfocused wheel is scrolling by, not a choice.
    if (event->type() == QEvent::Wheel) {
        if (auto* combo = qobject_cast<QComboBox*>(watched); combo && !combo->hasFocus()) {
            return true;  // eat it — leave the selection untouched
        }
    }
    return QWidget::eventFilter(watched, event);
}

void CardCopyForm::changeEvent(QEvent* event) {
    QWidget::changeEvent(event);
    if (event->type() != QEvent::PaletteChange &&
        event->type() != QEvent::ApplicationPaletteChange) {
        return;
    }
    // Both of this form's hand-coloured looks pin a CONCRETE colour into a child's own
    // palette, and the new theme's palette does not replace one of those — so re-derive
    // them against the fresh background. Resetting first is what makes the widget inherit
    // the incoming theme before the tone is picked off it.
    for (QToolButton* hint :
         {rarityHint_, languageHint_, conditionHint_, foilHint_, commentsHint_}) {
        if (hint != nullptr) {
            hint->setPalette(QPalette{});
            applyWarningText(hint);
        }
    }
    if (!referenceEditable_) {
        // The locked printed-identity fields, greyed by setReferenceEditable(false) — their
        // grey came from the OLD theme's Disabled entry, which reads washed out (or too
        // dark) once the appearance flips.
        for (QLineEdit* field : {cardName_, expansionCode_, setName_, collectorNumber_}) {
            field->setPalette(QPalette{});
            applyMutedText(field);
        }
    }
}

void CardCopyForm::setupBinderPicker(const std::vector<CardBinder>& binders,
                                     std::optional<CardBinderId> selected, bool enabled) {
    fillBinderCombo(*binder_, binders, selected);
    binder_->setEnabled(enabled);
    updateBinderRemoveEnabled();  // reflect the loaded selection on the Remove button
    updateNoFixedPositionEnabled();
}

void CardCopyForm::setMissingFieldHints(bool armed) {
    missingHintsArmed_ = armed;
    refreshMissingFieldHints();
}

void CardCopyForm::refreshMissingFieldHints() {
    if (commentsHint_ == nullptr) {
        return;  // the field signals are wired before the rows (and their markers) exist
    }
    // Guarding on the LAST marker built covers the whole window, including the stretch of
    // the ctor between the first row and the last — where rarityHint_ is already set and
    // the other four are not.
    const auto mark = [this](QToolButton* hint, bool empty) {
        hint->setVisible(missingHintsArmed_ && empty);
    };
    mark(rarityHint_, !rarity().has_value());
    mark(languageHint_, cardReference().language.empty());
    mark(conditionHint_, !condition().has_value());
    mark(foilHint_, !foil().has_value());
    // Whitespace-only is empty for this purpose — a stray newline left by a prefill isn't
    // a note, and the marker would otherwise read as "you wrote something" when nobody did.
    mark(commentsHint_, comments_->toPlainText().trimmed().isEmpty());
}

void CardCopyForm::setBinderRemovable(bool removable) {
    unassignBinder_->setVisible(removable);
    updateBinderRemoveEnabled();
}

void CardCopyForm::updateBinderRemoveEnabled() {
    unassignBinder_->setEnabled(binder_->isEnabled() && binderId().has_value());
}

void CardCopyForm::setReferenceEditable(bool editable) {
    // Lock the printed-identity line edits with setReadOnly (not setEnabled) so their
    // text stays selectable/copyable — a user may want to copy a set name or collector
    // number out. A plain read-only QLineEdit renders as bright, editable-looking
    // text though, which reads as confusing; so when locked we also mute the text to
    // the theme's disabled colour, matching a greyed-out read-only look while keeping
    // copy-out.
    referenceEditable_ = editable;  // changeEvent has to re-grey these on a theme switch
    for (QLineEdit* field : {cardName_, expansionCode_, setName_, collectorNumber_}) {
        field->setReadOnly(!editable);
        if (editable) {
            field->setPalette(QPalette());  // inherit defaults (normal, bright text)
        } else {
            applyMutedText(field);  // greyed read-only look, still selectable/copyable
        }
    }
    // Only the printing identity locks here. language / condition / rarity / foil /
    // ownership describe the physical copy, so they stay editable in both cases — a
    // recorded copy's grade, rarity, finish, state, or language can be corrected without
    // re-picking the printing. comments_ stays editable too; binder_ is governed by
    // setupBinderPicker().
}

void CardCopyForm::setCardReference(const CardReference& ref) {
    // Autofill the printed identity. setText() does not fire textEdited, so this does
    // not emit referenceEdited(). Language / condition / ownership are left to the user.
    cardName_->setText(QString::fromStdString(ref.name));
    expansionCode_->setText(QString::fromStdString(ref.expansionCode));
    setName_->setText(QString::fromStdString(ref.setName));
    collectorNumber_->setText(QString::fromStdString(ref.collectorNumber));
}

void CardCopyForm::setRarity(std::optional<CardRarity> rarity) {
    // Silent (setCurrentIndex, not activated), so autofilling from a picked card emits
    // no detailsChanged(); nullopt / an unmapped value falls back to "— None —".
    const int data = rarity ? static_cast<int>(*rarity) : -1;
    const int index = rarity_->findData(data);
    rarity_->setCurrentIndex(index >= 0 ? index : 0);
    refreshMissingFieldHints();  // silent setters emit nothing, so refresh explicitly
}

void CardCopyForm::setComments(const std::string& comments) {
    comments_->setPlainText(QString::fromStdString(comments));
}

void CardCopyForm::replaceComments(const std::string& comments) {
    // Select-all + insert through a cursor, which QPlainTextEdit records as an ordinary
    // edit — unlike setPlainText, which CLEARS the undo stack, so a mis-click on the
    // host's button would put a typed note permanently out of reach. One edit block, so
    // a single undo restores the whole previous text rather than half of it.
    QTextCursor cursor = comments_->textCursor();
    cursor.beginEditBlock();
    cursor.select(QTextCursor::Document);
    cursor.insertText(QString::fromStdString(comments));
    cursor.endEditBlock();
}

void CardCopyForm::setLanguage(const std::string& language) {
    // Same resolution as loadCopy: blank → the "— None —" entry; a known code selects
    // it; an unknown code is added as a selectable item so it round-trips.
    const QString code = QString::fromStdString(language);
    if (code.isEmpty()) {
        language_->setCurrentIndex(0);
        refreshMissingFieldHints();
        return;
    }
    int li = language_->findData(code);
    if (li < 0) {
        language_->addItem(code, code);
        li = language_->count() - 1;
    }
    language_->setCurrentIndex(li);
    refreshMissingFieldHints();
}

void CardCopyForm::setCondition(std::optional<CardCondition> condition) {
    const int data = condition ? static_cast<int>(*condition) : -1;
    const int index = condition_->findData(data);
    condition_->setCurrentIndex(index >= 0 ? index : 0);  // unmapped → "— None —"
    refreshMissingFieldHints();
}

void CardCopyForm::setNoFixedPosition(bool noFixedPosition) {
    // Silent by design, like the other setters: toggled() would fire noFixedPositionChanged
    // and an edit host would persist a value it had just loaded.
    const QSignalBlocker blocker(noFixedPosition_);
    noFixedPosition_->setChecked(noFixedPosition);
}

void CardCopyForm::loadCopy(const CardCopy& copy) {
    cardName_->setText(QString::fromStdString(copy.cardRef.name));
    expansionCode_->setText(QString::fromStdString(copy.cardRef.expansionCode));
    setName_->setText(QString::fromStdString(copy.cardRef.setName));
    collectorNumber_->setText(QString::fromStdString(copy.cardRef.collectorNumber));
    // Delegate the language / condition / rarity resolution to the single setters (as this
    // already does for rarity), so an unknown-code / unmapped-value rule lives in exactly
    // one place and the edit and prefill paths can't resolve the same value differently.
    setLanguage(copy.cardRef.language);
    setCondition(copy.condition);
    setRarity(copy.rarity);
    const int fd = copy.foil ? static_cast<int>(*copy.foil) : -1;
    const int fi = foil_->findData(fd);
    foil_->setCurrentIndex(fi >= 0 ? fi : 0);
    ownership_->setCurrentIndex(ownership_->findData(static_cast<int>(copy.ownership)));
    comments_->setPlainText(QString::fromStdString(copy.comments));
    setNoFixedPosition(copy.noFixedPosition);
    refreshMissingFieldHints();  // foil was set inline above, so refresh after it too
}

void CardCopyForm::addAction(QPushButton* button) {
    // Insert before the trailing stretch so buttons pack to the left in order.
    actions_->insertWidget(actions_->count() - 1, button);
}

CardReference CardCopyForm::cardReference() const {
    CardReference ref;
    ref.expansionCode = expansionCode_->text().trimmed().toStdString();
    ref.language = language_->currentData().toString().toStdString();  // "" when unspecified
    ref.collectorNumber = collectorNumber_->text().trimmed().toStdString();
    ref.setName = setName_->text().trimmed().toStdString();
    ref.name = cardName_->text().trimmed().toStdString();
    return ref;
}

CardOwnership CardCopyForm::ownership() const {
    return static_cast<CardOwnership>(ownership_->currentData().toInt());
}

std::optional<CardCondition> CardCopyForm::condition() const {
    const int data = condition_->currentData().toInt();
    return data < 0 ? std::nullopt
                    : std::optional<CardCondition>(static_cast<CardCondition>(data));
}

std::optional<CardRarity> CardCopyForm::rarity() const {
    const int data = rarity_->currentData().toInt();
    return data < 0 ? std::nullopt
                    : std::optional<CardRarity>(static_cast<CardRarity>(data));
}

std::optional<CardFoil> CardCopyForm::foil() const {
    const int data = foil_->currentData().toInt();
    return data < 0 ? std::nullopt : std::optional<CardFoil>(static_cast<CardFoil>(data));
}

std::optional<CardBinderId> CardCopyForm::binderId() const {
    return binderComboSelection(*binder_);
}

bool CardCopyForm::noFixedPosition() const { return noFixedPosition_->isChecked(); }

void CardCopyForm::updateNoFixedPositionEnabled() {
    // "Keep at the end" names a position IN A BINDER, so with no binder picked there is
    // nothing for it to mean — and left live it would let the add page report itself dirty,
    // and the edit page toast "kept at the end of its binder", over a card that is in none.
    // Shown-but-disabled with the reason in the tooltip, the idiom the guide's row actions
    // use; the box keeps whatever it holds, so unfiling a loose card hides nothing and
    // refiling it makes the setting editable again.
    const bool filed = binderId().has_value();
    noFixedPosition_->setEnabled(filed);
    noFixedPosition_->setToolTip(
        filed ? QString()
              : tr("File this card in a binder first — this decides where it sits in one."));
}

std::string CardCopyForm::comments() const {
    return comments_->toPlainText().toStdString();
}

}  // namespace pokedex
