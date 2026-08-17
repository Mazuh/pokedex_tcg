#include "gui/views/card_copy_form.h"

#include <QComboBox>
#include <QEvent>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPalette>
#include <QPlainTextEdit>
#include <QPoint>
#include <QPushButton>
#include <QStringList>
#include <QTextCursor>
#include <QToolButton>
#include <QToolTip>
#include <QVBoxLayout>

#include "gui/views/binder_combo.h"
#include "gui/views/condition_labels.h"
#include "gui/views/empty_option.h"
#include "gui/views/foil_labels.h"
#include "gui/views/language_codes.h"
#include "gui/views/muted_text.h"
#include "gui/views/ownership_labels.h"
#include "gui/views/rarity_labels.h"

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

// The three "info" popovers are constant markup over compile-time enum ranges, so
// each is built once (function-local static) and returned by reference — every
// CardCopyForm (a fresh one per Add/Edit page open) then reuses it instead of
// re-escaping and re-concatenating the same definition list. The app has no
// runtime language switching, so freezing the translation at first build is fine.

// The condition-picker "info" popover: every grade, each with its description.
const QString& conditionInfoHtml() {
    static const QString html =
        QStringLiteral("<p><b>What the condition grades mean</b></p>") +
        definitionListHtml(kConditions, conditionLabel, conditionDescription);
    return html;
}

// The rarity-picker "info" popover: the modern scale, then a "Legacy rarities"
// subheading with the older-era rarities — mirroring the two tables the terms come
// from, so the legacy ones read as a distinct, secondary group.
const QString& rarityInfoHtml() {
    static const QString html =
        QStringLiteral("<p><b>What the rarities mean</b></p>") +
        definitionListHtml(kModernRarities, rarityLabel, rarityDescription) +
        QStringLiteral("<p><b>Legacy rarities</b> (older eras)</p>") +
        definitionListHtml(kLegacyRarities, rarityLabel, rarityDescription);
    return html;
}

// The foil-treatment "info" popover: every finish, each with its description.
const QString& foilInfoHtml() {
    static const QString html =
        QStringLiteral("<p><b>What the foil treatments mean</b></p>") +
        definitionListHtml(kFoils, foilLabel, foilDescription);
    return html;
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
    connect(language_, &QComboBox::activated, this, [this](int) { Q_EMIT detailsChanged(); });

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
    connect(condition_, &QComboBox::activated, this, [this](int) { Q_EMIT detailsChanged(); });

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
    connect(rarity_, &QComboBox::activated, this, [this](int) { Q_EMIT detailsChanged(); });

    foil_ = new QComboBox(this);
    foil_->addItem(noneOptionLabel(), -1);
    for (const CardFoil f : kFoils) {
        foil_->addItem(foilLabel(f), static_cast<int>(f));
    }
    connect(foil_, &QComboBox::activated, this, [this](int) { Q_EMIT detailsChanged(); });

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
        Q_EMIT binderChanged();
    });

    comments_ = new QPlainTextEdit(this);
    comments_->setPlaceholderText(
        tr("Capture story, price, seller, imperfections, dates…"));
    connect(comments_, &QPlainTextEdit::textChanged, this,
            [this]() { Q_EMIT commentsChanged(); });

    // Guard every combo against accidental wheel changes: give them StrongFocus (so a
    // wheel-over doesn't focus them) and filter their wheel events (eventFilter eats an
    // unfocused wheel). A stray scroll while scanning the form must not silently flip a
    // recorded copy's language/condition/rarity/foil/ownership or move it between binders.
    for (QComboBox* combo : {language_, condition_, rarity_, foil_, ownership_, binder_}) {
        combo->setFocusPolicy(Qt::StrongFocus);
        combo->installEventFilter(this);
    }

    form->addRow(tr("Card name"), cardName_);
    form->addRow(tr("Expansion code"), expansionCode_);
    form->addRow(tr("Set name"), setName_);
    form->addRow(tr("Language"), language_);
    form->addRow(tr("Collector number"), collectorNumber_);

    // Condition, rarity, and foil treatment each pair their combo with a small "ⓘ"
    // that explains the terms (opaque abbreviations / jargon otherwise). The same rich
    // text is the button's tooltip (hover) and is shown on click via QToolTip, so it
    // works with both mouse habits; the WhatsThis cursor signals "this reveals help";
    // the popover text is the same source as the picker's options. One lambda builds
    // all three rows so the affordance can't drift between them.
    const auto attributeRow = [this, form](const QString& label, QComboBox* combo,
                                           const QString& infoHtml, const QString& accessibleName) {
        auto* info = new QToolButton(this);
        info->setText(QStringLiteral("ⓘ"));
        info->setAutoRaise(true);
        info->setFocusPolicy(Qt::NoFocus);
        info->setCursor(Qt::WhatsThisCursor);
        info->setToolTip(infoHtml);
        info->setAccessibleName(accessibleName);
        connect(info, &QToolButton::clicked, this, [info, infoHtml]() {
            QToolTip::showText(info->mapToGlobal(QPoint(0, info->height())), infoHtml, info);
        });
        auto* row = new QHBoxLayout;
        row->setContentsMargins(0, 0, 0, 0);
        row->addWidget(combo, 1);
        row->addWidget(info);
        form->addRow(label, row);
    };
    attributeRow(tr("Condition"), condition_, conditionInfoHtml(),
                 tr("What the condition grades mean"));
    attributeRow(tr("Rarity"), rarity_, rarityInfoHtml(), tr("What the rarities mean"));
    attributeRow(tr("Foil treatment"), foil_, foilInfoHtml(), tr("What the foil treatments mean"));

    form->addRow(tr("Ownership"), ownership_);
    // The binder combo pairs with an optional "Remove from binder" button (hidden unless
    // setBinderRemovable(true)); a hidden button takes no layout space, so the add flow's
    // row is unchanged.
    auto* binderRow = new QHBoxLayout;
    binderRow->setContentsMargins(0, 0, 0, 0);
    binderRow->addWidget(binder_, 1);
    binderRow->addWidget(unassignBinder_);
    form->addRow(tr("Binder"), binderRow);
    form->addRow(tr("Comments"), comments_);

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

void CardCopyForm::setupBinderPicker(const std::vector<CardBinder>& binders,
                                     std::optional<CardBinderId> selected, bool enabled) {
    fillBinderCombo(*binder_, binders, selected);
    binder_->setEnabled(enabled);
    updateBinderRemoveEnabled();  // reflect the loaded selection on the Remove button
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
        return;
    }
    int li = language_->findData(code);
    if (li < 0) {
        language_->addItem(code, code);
        li = language_->count() - 1;
    }
    language_->setCurrentIndex(li);
}

void CardCopyForm::setCondition(std::optional<CardCondition> condition) {
    const int data = condition ? static_cast<int>(*condition) : -1;
    const int index = condition_->findData(data);
    condition_->setCurrentIndex(index >= 0 ? index : 0);  // unmapped → "— None —"
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

std::string CardCopyForm::comments() const {
    return comments_->toPlainText().toStdString();
}

}  // namespace pokedex
