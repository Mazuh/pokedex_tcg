#include "gui/views/about_dialog.h"

#include <QDialogButtonBox>
#include <QFont>
#include <QFrame>
#include <QLabel>
#include <QVBoxLayout>

#include "gui/version.h"  // generated: pokedex::kAppVersion (short commit hash)
#include "gui/views/muted_text.h"

namespace pokedex {

namespace {

// A word-wrapped label whose embedded <a> links open in the system browser. Used
// for every text block so the repo/license links are clickable and the long
// disclaimer wraps to the dialog width.
QLabel* richText(const QString& html) {
    auto* label = new QLabel(html);
    label->setWordWrap(true);
    label->setTextFormat(Qt::RichText);
    label->setOpenExternalLinks(true);
    label->setTextInteractionFlags(Qt::TextBrowserInteraction);
    return label;
}

QFrame* horizontalRule() {
    auto* line = new QFrame;
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    return line;
}

}  // namespace

AboutDialog::AboutDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("About Pokédex TCG"));
    setModal(true);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 20);
    layout->setSpacing(10);

    // Heading — the app name, large and bold.
    auto* heading = new QLabel(tr("Pokédex TCG"));
    QFont headingFont = heading->font();
    headingFont.setPointSize(headingFont.pointSize() + 8);
    headingFont.setBold(true);
    heading->setFont(headingFont);
    layout->addWidget(heading);

    // "by Mazuh" — the deliberately un-unique identity the user wants shown.
    auto* byline = new QLabel(tr("by Mazuh"));
    QFont bylineFont = byline->font();
    bylineFont.setPointSize(bylineFont.pointSize() + 1);
    byline->setFont(bylineFont);
    applyMutedText(byline);  // muted, secondary
    layout->addWidget(byline);

    // Version — the last commit's short hash (kAppVersion carries the -dev suffix
    // itself when this isn't a Release build). Not semver by design: it's just an
    // indicator of which build you're running.
    auto* version = new QLabel(tr("Version %1").arg(QString::fromUtf8(kAppVersion)));
    version->setTextInteractionFlags(Qt::TextSelectableByMouse);  // easy to copy into a report
    applyMutedText(version);  // greyed but still selectable (setEnabled(false) would swallow the clicks)
    layout->addWidget(version);

    layout->addSpacing(4);
    layout->addWidget(horizontalRule());
    layout->addSpacing(4);

    // Short description (from the README's tagline).
    layout->addWidget(richText(
        tr("Just another Pokédex, but specifically designed to manage a physical "
           "card collection using local files.")));

    // Repository link.
    layout->addWidget(richText(
        tr("Source: <a href=\"https://github.com/Mazuh/pokedex_tcg\">"
           "github.com/Mazuh/pokedex_tcg</a>")));

    // License line — MIT, matching the repo's LICENSE and its copyright holder.
    layout->addWidget(richText(
        tr("The original source code is licensed under the "
           "<a href=\"https://github.com/Mazuh/pokedex_tcg/blob/main/LICENSE\">MIT "
           "License</a>.<br>© 2026 Marcell \"Mazuh\" G. C. da Silva.")));

    layout->addSpacing(4);
    layout->addWidget(horizontalRule());
    layout->addSpacing(4);

    // Legal / trademark disclaimer — verbatim from the README, so the fan-project
    // non-affiliation and third-party-IP notices are visible in the app itself.
    auto* disclaimer = richText(
        tr("This is an unofficial, non-commercial, fan-made project. It is not "
           "affiliated with, endorsed by, sponsored by, or otherwise associated "
           "with The Pokémon Company, Nintendo, Creatures Inc., or GAME FREAK inc."
           "<br><br>"
           "Pokémon, the Pokémon Trading Card Game, and all related names, "
           "characters, artwork, images, trademarks, and other intellectual "
           "property are the property of their respective owners. No ownership of "
           "such third-party intellectual property is claimed, and it is not "
           "covered by this project's MIT License."));
    applyMutedText(disclaimer);  // fine-print styling
    layout->addWidget(disclaimer);

    layout->addSpacing(8);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);
    layout->addWidget(buttons);

    // A fixed, readable column width so the wrapped text has a stable measure;
    // height follows the wrapped content via each label's heightForWidth.
    setFixedWidth(460);
}

}  // namespace pokedex
