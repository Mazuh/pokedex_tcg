#include "gui/views/card_copy_form.h"

#include <QColor>
#include <QComboBox>
#include <QEvent>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPalette>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStringList>
#include <QVBoxLayout>

#include "gui/views/binder_combo.h"
#include "gui/views/condition_labels.h"
#include "gui/views/ownership_labels.h"

namespace pokedex {

namespace {

// The card languages this project recognizes (English-only source can't fill this,
// so it is always the user's choice). Leading blank = "unspecified".
const QStringList& languageCodes() {
    static const QStringList codes = {"", "EN", "FR", "DE", "IT", "ES",
                                      "LA", "PT", "C",  "F",  "T",  "I"};
    return codes;
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
    language_->addItems(languageCodes());
    connect(language_, &QComboBox::activated, this, [this](int) { Q_EMIT detailsChanged(); });

    collectorNumber_ = new QLineEdit(this);
    collectorNumber_->setPlaceholderText(tr("e.g. 125/197"));
    connect(collectorNumber_, &QLineEdit::textEdited, this,
            [this](const QString&) { Q_EMIT referenceEdited(); });

    condition_ = new QComboBox(this);
    // Condition is optional (a copy can be recorded ungraded) — default to blank, so
    // nothing is claimed unless the user picks a grade. The -1 sentinel means "none".
    // Labels come from conditionLabel() (the app's single source, so the picker reads
    // the same as the table and a new grade fails its exhaustive -Wswitch).
    condition_->addItem(tr("(Unspecified)"), -1);
    for (const CardCondition c :
         {CardCondition::NearMint, CardCondition::LightlyPlayed, CardCondition::ModeratelyPlayed,
          CardCondition::HeavilyPlayed, CardCondition::Damaged}) {
        condition_->addItem(conditionLabel(c), static_cast<int>(c));
    }
    connect(condition_, &QComboBox::activated, this, [this](int) { Q_EMIT detailsChanged(); });

    ownership_ = new QComboBox(this);
    for (const CardOwnership o :
         {CardOwnership::Incoming, CardOwnership::Owned, CardOwnership::Removed}) {
        ownership_->addItem(ownershipLabel(o), static_cast<int>(o));
    }
    ownership_->setCurrentIndex(ownership_->findData(static_cast<int>(CardOwnership::Owned)));
    connect(ownership_, &QComboBox::activated, this, [this](int) { Q_EMIT detailsChanged(); });

    // The binder the copy is filed in — populated + enabled via setupBinderPicker().
    // activated() fires only on a USER pick (not the programmatic setCurrentIndex in
    // setupBinderPicker), so a host can persist a reassignment without echoing loads.
    binder_ = new QComboBox(this);
    connect(binder_, &QComboBox::activated, this,
            [this](int) { Q_EMIT binderChanged(); });

    comments_ = new QPlainTextEdit(this);
    comments_->setPlaceholderText(
        tr("Capture story, price, seller, imperfections, dates…"));
    connect(comments_, &QPlainTextEdit::textChanged, this,
            [this]() { Q_EMIT commentsChanged(); });

    // Guard every combo against accidental wheel changes: give them StrongFocus (so a
    // wheel-over doesn't focus them) and filter their wheel events (eventFilter eats an
    // unfocused wheel). A stray scroll while scanning the form must not silently flip a
    // recorded copy's language/condition/ownership or move it between binders.
    for (QComboBox* combo : {language_, condition_, ownership_, binder_}) {
        combo->setFocusPolicy(Qt::StrongFocus);
        combo->installEventFilter(this);
    }

    form->addRow(tr("Card name"), cardName_);
    form->addRow(tr("Expansion code"), expansionCode_);
    form->addRow(tr("Set name"), setName_);
    form->addRow(tr("Language"), language_);
    form->addRow(tr("Collector number"), collectorNumber_);
    form->addRow(tr("Condition"), condition_);
    form->addRow(tr("Ownership"), ownership_);
    form->addRow(tr("Binder"), binder_);
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
            QPalette pal = field->palette();
            const QColor muted = pal.color(QPalette::Disabled, QPalette::Text);
            pal.setColor(QPalette::Active, QPalette::Text, muted);
            pal.setColor(QPalette::Inactive, QPalette::Text, muted);
            field->setPalette(pal);
        }
    }
    // Only the printing identity locks here. language / condition / ownership describe
    // the physical copy, so they stay editable in both cases — a recorded copy's grade,
    // state, or language can be corrected without re-picking the printing. comments_
    // stays editable too; binder_ is governed by setupBinderPicker().
}

void CardCopyForm::setCardReference(const CardReference& ref) {
    // Autofill the printed identity. setText() does not fire textEdited, so this does
    // not emit referenceEdited(). Language / condition / ownership are left to the user.
    cardName_->setText(QString::fromStdString(ref.name));
    expansionCode_->setText(QString::fromStdString(ref.expansionCode));
    setName_->setText(QString::fromStdString(ref.setName));
    collectorNumber_->setText(QString::fromStdString(ref.collectorNumber));
}

void CardCopyForm::loadCopy(const CardCopy& copy) {
    cardName_->setText(QString::fromStdString(copy.cardRef.name));
    expansionCode_->setText(QString::fromStdString(copy.cardRef.expansionCode));
    setName_->setText(QString::fromStdString(copy.cardRef.setName));
    collectorNumber_->setText(QString::fromStdString(copy.cardRef.collectorNumber));
    const int li = language_->findText(QString::fromStdString(copy.cardRef.language));
    language_->setCurrentIndex(li >= 0 ? li : 0);
    const int cd = copy.condition ? static_cast<int>(*copy.condition) : -1;
    const int ci = condition_->findData(cd);
    condition_->setCurrentIndex(ci >= 0 ? ci : 0);
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
    ref.language = language_->currentText().toStdString();  // "" when unspecified
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

std::optional<CardBinderId> CardCopyForm::binderId() const {
    return binderComboSelection(*binder_);
}

std::string CardCopyForm::comments() const {
    return comments_->toPlainText().toStdString();
}

}  // namespace pokedex
