cmake_minimum_required(VERSION 3.24)

project(pokedex_tcg
    VERSION 0.1.0
    DESCRIPTION "Pokedex TCG — visual Hello World bootstrap"
    LANGUAGES CXX)

# --- Language standard -------------------------------------------------------
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile_commands.json for clangd / editor tooling.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Warnings ----------------------------------------------------------------
# Applied only to our own targets (not fetched deps like GoogleTest, whose
# headers we mark SYSTEM below). -Wall -Wextra are always on so warnings show
# up locally; -Werror is opt-in via POKEDEX_WERROR so CI can make them fatal
# without breaking day-to-day local builds. CI (Linux/GCC + macOS/Clang)
# builds every target under this, so a second compiler polices the code.
option(POKEDEX_WERROR "Treat compiler warnings as errors" OFF)
add_library(pokedex_warnings INTERFACE)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    # -Wno-missing-field-initializers: our domain types are plain-data
    # aggregates built with designated initializers that deliberately omit
    # fields (notably the app-set insertedAt/updatedAt audit stamps), letting
    # them value-initialize. GCC's -Wextra flags every such omission as
    # "missing initializer" even though it is explicit and intentional; Clang
    # does not. Silencing this one sub-warning keeps designated-init idiom
    # usable without weakening the rest of -Wall -Wextra -Werror.
    target_compile_options(pokedex_warnings INTERFACE
        -Wall -Wextra -Wno-missing-field-initializers)
    if(POKEDEX_WERROR)
        target_compile_options(pokedex_warnings INTERFACE -Werror)
    endif()
endif()

# --- Sanitizers --------------------------------------------------------------
# Opt-in Address + UndefinedBehavior sanitizers, run as a dedicated CI gate.
# Because this app is developed largely by AI, these deterministic runtime
# checks are a cheap, high-value backstop: they turn the tests we already run
# into detectors for use-after-free, leaks, buffer overruns, and C++ UB — the
# exact class of bugs a green GoogleTest run says nothing about, and the exact
# risk surface of using the SQLite C API (raw handles, manual finalize/close)
# directly. -fno-sanitize-recover=all makes UBSan abort with a nonzero exit so
# CI actually fails on a violation rather than only printing. Leak detection is
# Linux-only (Apple Clang's ASan does not report leaks at exit), so the CI job
# runs this on Ubuntu.
#
# Carried as an INTERFACE target rather than directory-wide flags so only our
# own code is instrumented — NOT the fetched GoogleTest, whose internal UB
# would otherwise abort the gate on a third-party defect. Linked into every
# first-party target (core, tests, GUI) so a full ASan build links cleanly; CI
# builds only the test target, so the Qt GUI is never compiled under the
# sanitizer there.
option(POKEDEX_ASAN "Build with Address + UndefinedBehavior sanitizers" OFF)
add_library(pokedex_sanitizers INTERFACE)
if(POKEDEX_ASAN)
    target_compile_options(pokedex_sanitizers INTERFACE
        -fsanitize=address,undefined -fno-omit-frame-pointer
        -fno-sanitize-recover=all)
    target_link_options(pokedex_sanitizers INTERFACE
        -fsanitize=address,undefined)
endif()

# --- Core library (Qt-free, unit-testable) -----------------------------------
# All non-GUI logic lives here so it can be tested headlessly in CI. The domain
# layer is header-only apart from the generated Pokédex catalog table;
# storage/ and app/ contribute compiled sources, so pokedex_core is a STATIC
# library. SQLite is linked PRIVATE — it is an
# implementation detail that never appears in the public headers, keeping the
# library Qt- and SQLite-agnostic at its interface.
find_package(SQLite3 REQUIRED)

# FindSQLite3's success criterion is only the header (SQLite3_INCLUDE_DIR), and
# across CMake versions it does not always define the SQLite3::SQLite3 imported
# target even when the package is "found" — which then fails at generate time
# with "target not found". Synthesize the target from the variables the module
# does set, so linking is version-independent; fail loudly and legibly if the
# actual library is missing (on Debian/Ubuntu: install libsqlite3-dev).
if(NOT TARGET SQLite3::SQLite3)
    if(NOT SQLite3_LIBRARY)
        message(FATAL_ERROR
            "SQLite3 headers found but no import library. "
            "On Debian/Ubuntu, install libsqlite3-dev.")
    endif()
    add_library(SQLite3::SQLite3 UNKNOWN IMPORTED)
    set_target_properties(SQLite3::SQLite3 PROPERTIES
        IMPORTED_LOCATION "${SQLite3_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${SQLite3_INCLUDE_DIR}")
endif()

# --- JSON parsing (nlohmann/json) --------------------------------------------
# The card-catalog seam parses pokemontcg.io JSON payloads in Qt-free core, so a
# JSON library is needed here (SQLite gives us no JSON). Fetched from source like
# GoogleTest and linked PRIVATE into pokedex_core — it never appears in a public
# core header (the parsers take/return std::string + plain structs), so the core
# interface stays dependency-agnostic. JSON_SystemInclude marks its headers SYSTEM
# so our -Werror build is not tripped by the library's own headers; JSON_BuildTests
# keeps its test suite out of our build.
include(FetchContent)
set(JSON_BuildTests OFF CACHE INTERNAL "")
set(JSON_SystemInclude ON CACHE INTERNAL "")
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

add_library(pokedex_core STATIC
    src/core/domain/pokemon_catalog.cpp
    src/core/storage/workspace.cpp
    src/core/storage/database.cpp
    src/core/storage/statement.cpp
    src/core/storage/codecs.cpp
    src/core/storage/card_binder_repository.cpp
    src/core/storage/card_copy_repository.cpp
    src/core/storage/wishlist_repository.cpp
    src/core/app/install_service.cpp
    src/core/app/uuid.cpp
    src/core/app/binder_service.cpp
    src/core/app/binder_guide_service.cpp
    src/core/app/pokemon_browse_service.cpp
    src/core/app/card_copy_service.cpp
    src/core/app/wishlist_service.cpp
    src/core/app/poke_api.cpp
    src/core/app/pokemon_tcg_io_api.cpp
    src/core/app/gemini_assistant.cpp
    src/core/app/card_scan.cpp
    src/core/app/card_catalog_parse.cpp
    src/core/app/card_set_cache.cpp
    src/core/app/card_price_cache.cpp
    src/core/app/card_price_service.cpp
    src/core/app/media_cache_layout.cpp
    src/core/app/rate_limiter.cpp
)
target_include_directories(pokedex_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(pokedex_core
    PRIVATE pokedex_warnings pokedex_sanitizers SQLite3::SQLite3 nlohmann_json::nlohmann_json)

# --- GUI application (Qt 6 Widgets) ------------------------------------------
# Network is used only by the GUI's media-fetching service (MediaService); core
# stays Qt-free, so it is linked here and never into pokedex_core. Multimedia +
# MultimediaWidgets back the webcam Scan-a-card dialog (QCamera / QMediaCaptureSession
# / QImageCapture / QVideoWidget); on Ubuntu they need the qt6-multimedia-dev package.
find_package(Qt6 REQUIRED COMPONENTS Widgets Network Multimedia MultimediaWidgets)

qt_standard_project_setup()

# --- Build version (short commit hash shown in the About box) ----------------
# Generate gui/version.h at *build* time (not configure) so the hash tracks HEAD
# across commits without a reconfigure; dev.sh only reconfigures when build.ninja
# is absent. GenerateVersion.cmake writes the header only when the value changes,
# so an unchanged hash never triggers a rebuild. A Release build (install.sh) is
# prod; anything else gets a -dev suffix.
find_package(Git QUIET)
set(POKEDEX_VERSION_DIR "${CMAKE_BINARY_DIR}/generated")
set(POKEDEX_VERSION_HEADER "${POKEDEX_VERSION_DIR}/gui/version.h")
add_custom_target(pokedex_version_header
    BYPRODUCTS "${POKEDEX_VERSION_HEADER}"
    COMMAND ${CMAKE_COMMAND}
        -DGIT_EXECUTABLE=${GIT_EXECUTABLE}
        -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
        -DOUTPUT_FILE=${POKEDEX_VERSION_HEADER}
        -DBUILD_TYPE=$<CONFIG>
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateVersion.cmake
    COMMENT "Resolving build version from git"
    VERBATIM)

qt_add_executable(pokedex_tcg
    src/main.cpp
    src/gui/views/first_run_dialog.cpp
    src/gui/views/binder_edit_page.cpp
    src/gui/views/binder_picker_dialog.cpp
    src/gui/views/main_window.cpp
    src/gui/views/about_dialog.cpp
    src/gui/views/binders_page.cpp
    src/gui/views/binder_view.cpp
    src/gui/views/pokemon_list_view.cpp
    src/gui/views/pokemon_detail_panel.cpp
    src/gui/views/add_card_copy_page.cpp
    src/gui/views/card_copy_form.cpp
    src/gui/views/card_finder_panel.cpp
    src/gui/views/edit_card_copy_page.cpp
    src/gui/views/owned_cards_view.cpp
    src/gui/views/card_price_fetch_controller.cpp
    src/gui/views/bulk_refresh_controller.cpp
    src/gui/views/card_prices_panel.cpp
    src/gui/views/card_prices_summary.cpp
    src/gui/views/prices_edit_page.cpp
    src/gui/views/wishlist_sources_editor.cpp
    src/gui/views/wishlist_edit_page.cpp
    src/gui/views/wishlist_view.cpp
    src/gui/views/settings_view.cpp
    src/gui/views/assistant_prompt_dialog.cpp
    src/gui/views/scan_card_view.cpp
    src/gui/views/toast.cpp
    src/gui/services/network_log.cpp
    src/gui/services/media_service.cpp
    src/gui/services/card_image_store.cpp
    src/gui/services/card_search_service.cpp
    src/gui/services/card_price_lookup_service.cpp
    src/gui/services/assistant_service.cpp
)
target_link_libraries(pokedex_tcg PRIVATE pokedex_core pokedex_warnings pokedex_sanitizers Qt6::Widgets Qt6::Network Qt6::Multimedia Qt6::MultimediaWidgets)
# The generated gui/version.h is under the build tree; make it includable and
# ensure it's (re)generated before the GUI compiles.
add_dependencies(pokedex_tcg pokedex_version_header)
target_include_directories(pokedex_tcg PRIVATE ${POKEDEX_VERSION_DIR})

# macOS: embed an Info.plist (carrying NSCameraUsageDescription) directly into the
# plain executable via the linker, so the webcam Scan-a-card dialog can obtain camera
# permission. Without this key macOS TCC denies the camera and Qt's QCameraPermission
# plugin can't even prompt ("Could not find permission plugin ... include the required
# usage description in your Info.plist"). This keeps ./build/pokedex_tcg a plain binary
# (dev.sh / install.sh / docs still refer to it) rather than restructuring into a .app;
# a distributable build would be a proper signed bundle carrying the same key.
if(APPLE)
    # Build a real .app bundle so the webcam Scan-a-card dialog can obtain camera
    # permission. Two macOS facts force this:
    #   1. Qt's QCameraPermission request reads NSCameraUsageDescription from
    #      [NSBundle mainBundle].infoDictionary and only prompts when it's present. A
    #      bare executable (even with an embedded __TEXT,__info_plist section) does NOT
    #      reliably satisfy that lookup, nor does LaunchServices/TCC register it (tccutil
    #      can't even find its bundle id) — so the request fails and the app never appears
    #      in System Settings ▸ Camera. A proper Contents/Info.plist fixes all of that.
    #   2. Homebrew's Qt is a STATIC build, so permission backends are .a plugins that
    #      only exist at runtime when explicitly imported; Qt does NOT auto-import them
    #      (opt-in, each needs a usage description). Without this the camera plugin is
    #      absent and QCameraPermission has no handler at all.
    # (This is why ./build/pokedex_tcg is now ./build/pokedex_tcg.app on macOS — dev.sh /
    # install.sh / README launch the bundle accordingly.)
    set_target_properties(pokedex_tcg PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "Pokedex TCG"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.mazuh.pokedex-tcg"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/cmake/macos/Info.plist.in")
    qt_import_plugins(pokedex_tcg INCLUDE Qt6::QDarwinCameraPermissionPlugin)

    # Ad-hoc-sign the finished bundle (stable identifier) so TCC attributes and remembers
    # the camera grant. Ad-hoc (`-`), so no developer certificate is needed for a local
    # build; a distributable build would use a Developer ID identity instead.
    add_custom_command(TARGET pokedex_tcg POST_BUILD
        COMMAND codesign --force --sign - "$<TARGET_BUNDLE_DIR:pokedex_tcg>"
        COMMENT "Ad-hoc signing the .app bundle (macOS camera permission)"
        VERBATIM)
endif()

# --- Install (drop a `pokedex` command onto PATH) ----------------------------
# install.sh runs `cmake --install` after a Release build so the app is
# launchable from any directory.
include(GNUInstallDirs)
if(APPLE)
    # The macOS build is a .app bundle (needed for camera permission — see the if(APPLE)
    # block above), so install the WHOLE bundle and add a `pokedex` symlink on PATH that
    # points at the bundle's inner binary. Running the symlink resolves to the real path
    # inside the .app, so [NSBundle mainBundle] still finds Contents/Info.plist and the
    # camera permission keeps working from the installed command. (A bare copy of the
    # inner binary would run but couldn't scan cards.)
    install(DIRECTORY "$<TARGET_BUNDLE_DIR:pokedex_tcg>"
            DESTINATION "${CMAKE_INSTALL_BINDIR}" USE_SOURCE_PERMISSIONS)
    install(CODE "file(CREATE_LINK
        \"pokedex_tcg.app/Contents/MacOS/pokedex_tcg\"
        \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/pokedex\"
        SYMBOLIC)")
else()
    # Elsewhere it's a plain binary: install it renamed to `pokedex` (PROGRAMS + RENAME;
    # install(TARGETS) has no RENAME). The genex resolves to the built binary regardless
    # of the build directory.
    install(PROGRAMS "$<TARGET_FILE:pokedex_tcg>"
            DESTINATION "${CMAKE_INSTALL_BINDIR}"
            RENAME pokedex)
endif()

# --- Tests (GoogleTest + CTest) ----------------------------------------------
include(CTest)
if(BUILD_TESTING)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.15.2
        GIT_SHALLOW TRUE
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    # GoogleTest is built from source, so its headers are not treated as SYSTEM
    # by default the way imported targets (Qt) are. Mark them SYSTEM so our
    # -Werror build is not tripped by warnings inside gtest's own headers.
    foreach(_gt gtest gtest_main gmock gmock_main)
        if(TARGET ${_gt})
            get_target_property(_inc ${_gt} INTERFACE_INCLUDE_DIRECTORIES)
            if(_inc)
                set_target_properties(${_gt} PROPERTIES
                    INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_inc}")
            endif()
        endif()
    endforeach()

    add_executable(pokedex_tests
        tests/domain/card_reference_test.cpp
        tests/domain/card_copy_test.cpp
        tests/domain/card_condition_test.cpp
        tests/domain/card_rarity_test.cpp
        tests/domain/card_foil_test.cpp
        tests/domain/card_binder_test.cpp
        tests/domain/wishlist_test.cpp
        tests/domain/pokemon_test.cpp
        tests/domain/pokemon_catalog_test.cpp
        tests/domain/model_smoke_test.cpp
        tests/storage/workspace_test.cpp
        tests/storage/database_test.cpp
        tests/storage/codecs_test.cpp
        tests/storage/card_binder_repository_test.cpp
        tests/storage/card_copy_repository_test.cpp
        tests/storage/wishlist_repository_test.cpp
        tests/app/install_service_test.cpp
        tests/app/binder_service_test.cpp
        tests/app/binder_guide_service_test.cpp
        tests/app/pokemon_browse_service_test.cpp
        tests/app/card_copy_service_test.cpp
        tests/app/wishlist_service_test.cpp
        tests/app/pokemon_external_api_test.cpp
        tests/app/pokemon_tcg_io_api_test.cpp
        tests/app/gemini_assistant_test.cpp
        tests/app/card_scan_test.cpp
        tests/app/card_catalog_parse_test.cpp
        tests/app/card_set_cache_test.cpp
        tests/app/card_price_cache_test.cpp
        tests/app/card_price_service_test.cpp
        tests/app/rate_limiter_test.cpp
    )
    target_link_libraries(pokedex_tests PRIVATE pokedex_core pokedex_warnings pokedex_sanitizers GTest::gtest_main)

    include(GoogleTest)
    # PRE_TEST defers test enumeration to `ctest` time. Under POKEDEX_ASAN the
    # default POST_BUILD mode would run the instrumented binary during the build
    # step, where the sanitizer's runtime options (leak/symbolizer config) are
    # not set — a report there would fail the build with an unhelpful trace.
    gtest_discover_tests(pokedex_tests DISCOVERY_MODE PRE_TEST)
endif()
