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

# --- Core library (Qt-free, unit-testable) -----------------------------------
# All non-GUI logic lives here so it can be tested headlessly in CI. The domain
# layer is header-only; storage/ and app/ contribute compiled sources, so
# pokedex_core is a STATIC library. SQLite is linked PRIVATE — it is an
# implementation detail that never appears in the public headers, keeping the
# library Qt- and SQLite-agnostic at its interface.
find_package(SQLite3 REQUIRED)

add_library(pokedex_core STATIC
    src/core/storage/workspace.cpp
    src/core/storage/database.cpp
    src/core/app/install_service.cpp
)
target_include_directories(pokedex_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(pokedex_core
    PRIVATE pokedex_warnings SQLite3::SQLite3)

# --- GUI application (Qt 6 Widgets) ------------------------------------------
find_package(Qt6 REQUIRED COMPONENTS Widgets)

qt_standard_project_setup()

qt_add_executable(pokedex_tcg
    src/main.cpp
    src/gui/views/first_run_dialog.cpp
)
target_link_libraries(pokedex_tcg PRIVATE pokedex_core pokedex_warnings Qt6::Widgets)

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
        tests/domain/card_binder_test.cpp
        tests/domain/wishlist_test.cpp
        tests/domain/pokemon_test.cpp
        tests/domain/model_smoke_test.cpp
        tests/storage/workspace_test.cpp
        tests/storage/database_test.cpp
        tests/app/install_service_test.cpp
    )
    target_link_libraries(pokedex_tests PRIVATE pokedex_core pokedex_warnings GTest::gtest_main)

    include(GoogleTest)
    gtest_discover_tests(pokedex_tests)
endif()
