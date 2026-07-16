#include <QApplication>
#include <QDialog>
#include <QMessageBox>
#include <QString>

#include <exception>
#include <optional>

#include "core/app/binder_guide_service.h"
#include "core/app/binder_service.h"
#include "core/app/card_copy_service.h"
#include "core/app/install_service.h"
#include "core/app/poke_api.h"
#include "core/app/pokemon_browse_service.h"
#include "core/app/pokemon_tcg_io_api.h"
#include "core/app/wishlist_service.h"
#include "core/storage/card_binder_repository.h"
#include "core/storage/card_copy_repository.h"
#include "core/storage/database.h"
#include "core/storage/wishlist_repository.h"
#include "gui/services/card_search_service.h"
#include "gui/services/media_service.h"
#include "gui/views/first_run_dialog.h"
#include "gui/views/main_window.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    std::optional<pokedex::Workspace> workspace;
    try {
        if (std::optional<pokedex::Workspace> configured = pokedex::configuredWorkspace()) {
            // Relaunch: open + migrate the recorded workspace (so a future schema
            // bump is applied before the DB is used).
            workspace = pokedex::openWorkspace(configured->root());
        } else {
            // First run: the wizard creates + migrates the workspace itself, so we
            // just adopt the folder it chose — no second open.
            pokedex::FirstRunDialog dialog;
            if (dialog.exec() != QDialog::Accepted) {
                return 0;  // user cancelled setup: nothing to open
            }
            workspace = pokedex::Workspace(dialog.chosenWorkspace());
        }
    } catch (const std::exception &e) {
        // The workspace folder became unavailable (NAS/iCloud unmounted, path
        // unwritable, …). Report it instead of letting the exception crash the app.
        QMessageBox::critical(
            nullptr, QStringLiteral("Pokedex TCG"),
            QStringLiteral("Could not open your workspace:\n%1").arg(QString::fromUtf8(e.what())));
        return 1;
    }

    // Open the workspace database for the app's lifetime and wire the binder
    // CRUD stack on top of it: repository (storage) -> service (verbs) -> window
    // (GUI). Database is non-movable, so it lives here as a local that outlives
    // app.exec(). migrate() is a no-op when already current (it also covers the
    // first-run branch, which adopts the folder without re-opening the DB).
    try {
        pokedex::Database db(workspace->dbPath());
        db.migrate();
        pokedex::CardBinderRepository repository(db);
        pokedex::BinderService service(repository);

        // The read side behind "open binder": copies + wishlist feed the guide
        // that resolves each Pokémon's CollectionStatus. All locals here so they
        // outlive app.exec(), matching the "service outlives the window" contract.
        pokedex::CardCopyRepository copyRepository(db);
        pokedex::WishlistRepository wishlistRepository(db);
        pokedex::BinderGuideService guide(copyRepository, wishlistRepository);

        // The unscoped Pokédex browser reads the same copy repository to count
        // owned cards per species.
        pokedex::PokemonBrowseService browse(copyRepository);

        // The copy CRUD verbs behind the "Add copy" flow (and later, copy
        // management), over the same repository.
        pokedex::CardCopyService cardCopies(copyRepository);

        // The wishlist verbs, backing both the unscoped Wishlist section and the
        // per-Pokémon editor in every detail panel.
        pokedex::WishlistService wishlist(wishlistRepository);

        // The external-API adapter (swap this line to change image sources) and the
        // media fetch/cache service, rooted at the workspace media dir. Locals here
        // so they outlive app.exec(), matching the "service outlives the window"
        // contract; one shared instance serves every section.
        pokedex::PokeApi externalApi;
        pokedex::MediaService media(
            externalApi, QString::fromStdString(workspace->mediaDir().string()));

        // The card-catalog adapter (swap this line to change card sources) and the
        // search/transport service backing the "Add copy" flow. Locals here so they
        // outlive app.exec(); one shared instance serves every section. It caches
        // nothing to disk — search results are display-only.
        pokedex::PokemonTcgIoApi cardApi;
        pokedex::CardSearchService cardSearch(cardApi);

        pokedex::MainWindow window(
            service, guide, browse, wishlist, media, cardSearch, cardCopies,
            QString::fromStdString(workspace->root().string()));
        window.show();

        return app.exec();
    } catch (const std::exception &e) {
        QMessageBox::critical(
            nullptr, QStringLiteral("Pokedex TCG"),
            QStringLiteral("Could not open your collection:\n%1").arg(QString::fromUtf8(e.what())));
        return 1;
    }
}
