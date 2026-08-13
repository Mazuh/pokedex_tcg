#include "gui/views/main_window.h"

#include <QAction>
#include <QCloseEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QSet>
#include <QSplitter>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QWidget>

#include <memory>
#include <vector>

#include "core/app/card_catalog_dto.h"
#include "core/app/card_copy_service.h"
#include "core/domain/card_copy.h"
#include "core/domain/card_ownership.h"
#include "core/domain/card_reference.h"
#include "gui/services/card_search_service.h"
#include "gui/views/about_dialog.h"
#include "gui/views/assistant_prompt_dialog.h"
#include "gui/views/binders_page.h"
#include "gui/views/card_copy_labels.h"
#include "gui/views/owned_cards_view.h"
#include "gui/views/pokemon_list_view.h"
#include "gui/views/scan_card_view.h"
#include "gui/views/settings_view.h"
#include "gui/views/splitter_style.h"
#include "gui/views/wishlist_view.h"

namespace pokedex {

namespace {

// The single set in the loaded set table that the read code/name unambiguously names, or
// null. Exact printed code (unique per set) wins; then an exact set name; then, only if the
// read name substring-matches EXACTLY ONE set (either direction, 3+ chars), that set. An
// ambiguous or absent match returns null, so the scan panel says "not recognized" rather
// than confidently naming an arbitrary printing (unlike the search-narrowing
// resolveSetFilterToIds, which is deliberately many-matching). Precise by design: a misread
// set reads as unrecognized instead of matching an unrelated one by substring.
const CardSetInfo* matchReadSet(const std::vector<CardSetInfo>& sets, const QString& code,
                                const QString& name) {
    if (!code.isEmpty()) {
        for (const CardSetInfo& s : sets) {
            if (!s.ptcgoCode.empty() &&
                QString::fromStdString(s.ptcgoCode).compare(code, Qt::CaseInsensitive) == 0) {
                return &s;
            }
        }
    }
    if (name.isEmpty()) {
        return nullptr;
    }
    for (const CardSetInfo& s : sets) {
        if (QString::fromStdString(s.name).compare(name, Qt::CaseInsensitive) == 0) {
            return &s;
        }
    }
    // No exact hit — accept a substring match only when it identifies exactly one set, so a
    // near-miss reading still resolves but an ambiguous fragment (e.g. "Base") does not.
    if (name.size() < 3) {
        return nullptr;
    }
    const QString needle = name.toLower();
    const CardSetInfo* only = nullptr;
    for (const CardSetInfo& s : sets) {
        const QString candidate = QString::fromStdString(s.name).toLower();
        if (candidate.contains(needle) || needle.contains(candidate)) {
            if (only != nullptr) {
                return nullptr;  // ambiguous — more than one set matches
            }
            only = &s;
        }
    }
    return only;
}

}  // namespace

MainWindow::MainWindow(BinderService& binderService, BinderGuideService& guide,
                       PokemonBrowseService& browse, WishlistService& wishlist,
                       MediaService& media, CardSearchService& cardSearch,
                       CardPriceLookupService& priceLookup, CardCopyService& cardCopies,
                       CardImageStore& cardImages, AssistantService& assistant,
                       BackupService& backups, const QString& collectionPath, QWidget* parent)
    : QWidget(parent) {
    setWindowTitle(tr("Pokedex TCG"));
    resize(900, 600);

    // Opens the modal About box, parented to the window. Shared by the menu-bar
    // item (macOS's application menu) and the in-window sidebar footer button, so
    // the About box is reachable without relying on the native menu bar.
    const auto showAbout = [this]() { AboutDialog(this).exec(); };

    // Opens the AI-assistant demo, parented to the window. Shared by the sidebar
    // footer button and the Tools menu action.
    const auto showAssistant = [this, &assistant]() {
        AssistantPromptDialog(assistant, this).exec();
    };

    // The left sidebar: a macOS-style source list whose rows select the section
    // shown on the right. No frame, so it reads as a pane rather than a boxed list.
    auto* sidebar = new QListWidget(this);
    sidebar->setFrameShape(QFrame::NoFrame);
    // Keep it a narrow, fixed-purpose pane: never a horizontal scrollbar (it
    // would clip labels like "All Pokémon"), and a bounded width so the split
    // divider can't be dragged to swallow the whole window or collapse the pane.
    sidebar->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // Breathing room so rows don't hug the window edge, and rounded selection
    // pills — the macOS source-list look.
    sidebar->setStyleSheet(
        "QListWidget { padding: 8px 6px; }"
        "QListWidget::item { padding: 6px 8px; border-radius: 6px; }"
        "QListWidget::item:selected { background: palette(highlight);"
        " color: palette(highlighted-text); }"
        // When the window isn't focused, macOS greys the selection — but keep it
        // clearly legible (a plain palette(midlight) is nearly invisible in dark
        // mode), so the current section always reads.
        "QListWidget::item:selected:!active { background: rgba(128, 128, 128, 0.32);"
        " color: palette(text); }");
    new QListWidgetItem(tr("Binders"), sidebar);
    new QListWidgetItem(tr("All Pokémon"), sidebar);
    new QListWidgetItem(tr("My Cards"), sidebar);
    new QListWidgetItem(tr("Wishlist"), sidebar);
    new QListWidgetItem(tr("Settings"), sidebar);

    // An "About" affordance pinned at the bottom of the sidebar, so the same dialog
    // is reachable inside the window (not only via the native macOS menu). Flat, so
    // it reads as a footer action rather than a primary button, but in the normal
    // text color (a divider above sets it apart) so it's plainly visible.
    auto* aboutButton = new QPushButton(tr("ⓘ  About"));
    aboutButton->setFlat(true);
    aboutButton->setCursor(Qt::PointingHandCursor);
    aboutButton->setStyleSheet(
        "QPushButton { border: none; text-align: left; padding: 14px 20px;"
        " color: palette(text); }"
        "QPushButton:hover { background: rgba(128, 128, 128, 0.20); }");

    // A sibling footer action opening the card scanner (webcam → assistant reads the
    // card → search), styled to match About. This is the primary entry to the scan flow;
    // the AI-assistant demo stays reachable from the Tools menu.
    auto* scanButton = new QPushButton(tr("✦  Scan card"));
    scanButton->setFlat(true);
    scanButton->setCursor(Qt::PointingHandCursor);
    scanButton->setStyleSheet(
        "QPushButton { border: none; text-align: left; padding: 14px 20px;"
        " color: palette(text); }"
        "QPushButton:hover { background: rgba(128, 128, 128, 0.20); }");

    // A hairline above the footer separates it from the source list.
    auto* footerDivider = new QFrame;
    footerDivider->setFrameShape(QFrame::HLine);
    footerDivider->setFrameShadow(QFrame::Plain);
    footerDivider->setStyleSheet("color: rgba(128, 128, 128, 0.35);");
    connect(aboutButton, &QPushButton::clicked, this, showAbout);
    // scanButton is connected below, once the "My Cards" section it routes into exists.

    // Wrap the list + footer into one pane; the pane (not the list) now carries the
    // sidebar's width bounds so the splitter treats the whole column as one band.
    auto* sidebarPanel = new QWidget(this);
    sidebarPanel->setMinimumWidth(160);
    sidebarPanel->setMaximumWidth(280);
    auto* sidebarLayout = new QVBoxLayout(sidebarPanel);
    sidebarLayout->setContentsMargins(0, 0, 0, 0);
    sidebarLayout->setSpacing(0);
    sidebarLayout->addWidget(sidebar, 1);  // the list takes all the slack
    sidebarLayout->addWidget(footerDivider);
    sidebarLayout->addWidget(scanButton);
    sidebarLayout->addWidget(aboutButton);

    // Section order must match the sidebar row order above: a row selects the
    // stack page at the same index.
    sections_ = new QStackedWidget(this);
    sections_->addWidget(new BindersPage(binderService, guide, wishlist, media, cardSearch,
                                         priceLookup, cardCopies, cardImages, collectionPath));
    auto* pokemonView = new PokemonListView(browse, wishlist, media, cardSearch, priceLookup,
                                            cardCopies, cardImages, binderService);
    sections_->addWidget(pokemonView);
    auto* ownedView = new OwnedCardsView(cardCopies, binderService, cardImages, cardSearch,
                                         priceLookup, media, wishlist);
    sections_->addWidget(ownedView);
    sections_->addWidget(new WishlistView(wishlist));
    settings_ = new SettingsView(backups);
    const int settingsRow = sections_->addWidget(settings_);

    // The webcam Scan-a-card screen is an extra page in the section stack with no sidebar
    // row: it's opened on demand from the sidebar footer / Tools menu and returns (Back)
    // to the section it was opened from. Its signals are wired once here; showScan (below)
    // refreshes its owned-name matcher and shows it.
    auto* scanView = new ScanCardView(assistant);
    // Tell the scan panel whether the read set is one the catalog knows — but only from the
    // set table ALREADY loaded in memory (querying the shared search service live, never
    // triggering a fetch). Set once: the service outlives the view.
    scanView->setSetMatcher([&cardSearch](const QString& setCode, const QString& setName) {
        ScanCardView::SetLookup lookup;
        const std::vector<CardSetInfo>& sets = cardSearch.sets();
        if (sets.empty()) {
            return lookup;  // set table not loaded yet — loaded=false, no fetch
        }
        lookup.loaded = true;
        const CardSetInfo* found = matchReadSet(sets, setCode.trimmed(), setName.trimmed());
        lookup.matched = found != nullptr;
        if (found != nullptr) {
            // Reuse the shared "Base Set (BS)" formatter so the panel and the card tables'
            // Set column never render the same set differently.
            CardReference ref;
            ref.setName = found->name;
            ref.expansionCode = found->ptcgoCode;
            lookup.canonicalLabel = setLabel(ref);
        }
        return lookup;
    });
    const int scanRow = sections_->addWidget(scanView);
    // Make section `row` the visible one, coming FROM the scan page. Normally this is a
    // sidebar row change; but when the sidebar already highlights `row` (the scan was
    // opened from that very section), currentRowChanged won't fire, so switch the page
    // directly. Leaving Settings was already reconciled when the scan opened (showScan
    // runs confirmLeave), so no dirty-form prompt can fire here.
    const auto goToSection = [this, sidebar](int row) {
        if (sidebar->currentRow() == row) {
            currentRow_ = row;
            sections_->setCurrentIndex(row);
        } else {
            sidebar->setCurrentRow(row);
        }
    };
    connect(scanView, &ScanCardView::cardResolved, this,
            [goToSection, ownedView](const ScannedCard& scanned) {
                // Route the reading into My Cards (fills its search + stashes the scan for
                // the next "Add a card") and show that section.
                ownedView->applyScannedCard(scanned);
                goToSection(2);
            });
    connect(scanView, &ScanCardView::addRequested, this,
            [goToSection, pokemonView, ownedView](const ScannedCard& reading, int dex,
                                                  bool copyFieldsToForm) {
                // Go straight to creation. A detected species opens that species' add-copy
                // page; otherwise the species-free "add a card" page. The target section
                // must be current BEFORE the inner add page is pushed, so switch first.
                // copyFieldsToForm picks the prefill: paste the read fields (no search) vs
                // seed the finder search (by set for a species, by name otherwise).
                if (dex > 0) {
                    const QString species = speciesName(dex);
                    goToSection(1);  // Pokémon browser
                    if (copyFieldsToForm) {
                        pokemonView->openAddCopyWithFields(dex, species, reading);
                    } else {
                        // Prefer the printed set code (abbreviation), else the full set
                        // name. The finder only auto-searches at 3+ chars, so a short code
                        // (e.g. "BS") also falls back to the name so the set still lists.
                        QString setQuery = QString::fromStdString(reading.setCode).trimmed();
                        if (setQuery.size() < 3) {
                            setQuery = QString::fromStdString(reading.setName);
                        }
                        pokemonView->openAddCopyBySet(dex, species, setQuery);
                    }
                } else {
                    goToSection(2);  // My Cards
                    ownedView->startAddScannedCard(reading, copyFieldsToForm);
                }
            });
    connect(scanView, &ScanCardView::backRequested, this,
            [this]() { sections_->setCurrentIndex(currentRow_); });

    // The Settings section is the one page with a manually-applied form, so leaving
    // it must not silently drop unsaved edits. Route section switches through a guard
    // that, when leaving Settings with a dirty form, prompts Save/Discard/Cancel — and
    // on Cancel snaps the sidebar selection back without re-prompting (guarding_).
    connect(sidebar, &QListWidget::currentRowChanged, this,
            [this, sidebar, settingsRow](int row) {
                if (row < 0 || guarding_) {
                    return;
                }
                if (row == currentRow_) {
                    sections_->setCurrentIndex(row);
                    return;
                }
                if (currentRow_ == settingsRow && !settings_->confirmLeave(this)) {
                    // Snap the selection back to Settings. Deferred (queued) because the
                    // list is still mid-keypress/mid-click here — reverting inline gets
                    // overwritten when that handler finishes moving the selection, leaving
                    // the highlight on the new row while the page stays on Settings.
                    QMetaObject::invokeMethod(
                        this,
                        [this, sidebar]() {
                            guarding_ = true;
                            sidebar->setCurrentRow(currentRow_);  // no re-prompt
                            guarding_ = false;
                        },
                        Qt::QueuedConnection);
                    return;
                }
                currentRow_ = row;
                sections_->setCurrentIndex(row);
            });

    // Double-clicking an owned species in the Pokémon browser jumps to "My Cards"
    // pre-filtered to that species: set the filter first (it persists through the
    // reload the section's showEvent triggers), then select the sidebar row — which
    // switches sections and highlights it, keeping the sidebar in sync. Row 2 = My
    // Cards, matching the sidebar/section order built above.
    connect(pokemonView, &PokemonListView::searchInMyCardsRequested, this,
            [sidebar, ownedView](const QString& species) {
                ownedView->searchFor(species);
                sidebar->setCurrentRow(2);
            });

    // Opens the webcam Scan-a-card screen: refresh its owned-name match estimate from a
    // fresh snapshot of the collection, reset it for a new scan, and show the page.
    const auto showScan = [this, &cardCopies, scanView, scanRow, settingsRow]() {
        // Opening Scan is a section switch, so honor the Settings dirty-form guard (which
        // the sidebar routes every switch through): leaving a dirty Settings form prompts
        // Save/Discard/Cancel, and Cancel aborts opening Scan. This also leaves the form
        // clean before entering the scan flow, so returning to My Cards on a resolved
        // reading can't trip a stale prompt.
        if (currentRow_ == settingsRow && !settings_->confirmLeave(this)) {
            return;
        }
        // A fresh snapshot of the live (non-Removed) collection's display names, taken at
        // open so the view can estimate name matches without re-querying per keystroke.
        // Collapse duplicate copies of the same printing to one entry (keyed by name +
        // printed identity), so owning three of a card counts as one possible match, not
        // three — the estimate answers "which cards could this be?", not "how many copies".
        auto ownedNames = std::make_shared<std::vector<QString>>();
        QSet<QString> seenPrintings;
        for (const CardCopy& copy : cardCopies.listAll()) {
            if (copy.ownership == CardOwnership::Removed) {
                continue;  // history, not part of "do I already have this?"
            }
            const QString name = speciesOrCardName(copy).trimmed().toLower();
            if (name.isEmpty()) {
                continue;
            }
            const QString printing = name + QLatin1Char('|') +
                                     QString::fromStdString(copy.cardRef.setName).toLower() +
                                     QLatin1Char('|') +
                                     QString::fromStdString(copy.cardRef.collectorNumber).toLower();
            if (seenPrintings.contains(printing)) {
                continue;  // another copy of a printing already counted
            }
            seenPrintings.insert(printing);
            ownedNames->push_back(name);
        }
        scanView->setOwnedNameMatcher([ownedNames](const QString& cardName) {
            const QString needle = cardName.trimmed().toLower();
            if (needle.isEmpty()) {
                return 0;
            }
            int count = 0;
            for (const QString& owned : *ownedNames) {
                if (owned.contains(needle)) {  // an owned name that contains the read name
                    ++count;
                }
            }
            return count;
        });

        scanView->startScan();  // reset to a fresh scan; showEvent starts the camera
        sections_->setCurrentIndex(scanRow);
    };
    connect(scanButton, &QPushButton::clicked, this, showScan);

    // A draggable split: a narrow sidebar that keeps its size while the section
    // area takes the slack when the window resizes.
    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(sidebarPanel);
    splitter->addWidget(sections_);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    // Neither pane may be dragged shut; the sidebar's min/max width keep it a
    // narrow band so a fast drag can't flip it to full-window width.
    splitter->setChildrenCollapsible(false);
    splitter->setSizes({180, 720});
    thinDivider(splitter);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(splitter);

    // A menu bar carrying a single About item. On macOS Qt promotes this to the
    // native global menu bar, and the AboutRole moves the item into the bold
    // "Pokédex TCG" application menu (the platform-standard spot for About);
    // elsewhere it renders in-window under Help. The dialog is parented to the
    // window so it centers over it and shares its lifetime.
    auto* menuBar = new QMenuBar(this);
    QMenu* toolsMenu = menuBar->addMenu(tr("Tools"));
    QAction* scanAction = toolsMenu->addAction(tr("Scan a card…"));
    connect(scanAction, &QAction::triggered, this, showScan);
    QAction* assistantAction = toolsMenu->addAction(tr("AI Assistant…"));
    connect(assistantAction, &QAction::triggered, this, showAssistant);
    QMenu* helpMenu = menuBar->addMenu(tr("Help"));
    QAction* aboutAction = helpMenu->addAction(tr("About Pokédex TCG"));
    aboutAction->setMenuRole(QAction::AboutRole);
    connect(aboutAction, &QAction::triggered, this, showAbout);
    layout->setMenuBar(menuBar);

    sidebar->setCurrentRow(0);  // start on Binders
}

void MainWindow::closeEvent(QCloseEvent* event) {
    // Only relevant while Settings is the visible section; when it's not, its form is
    // clean (leaving it required saving/discarding), so confirmLeave() is a no-op.
    if (!settings_->confirmLeave(this)) {
        event->ignore();
        return;
    }
    QWidget::closeEvent(event);
}

}  // namespace pokedex
