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

# --- Core library (Qt-free, unit-testable) -----------------------------------
# All non-GUI logic lives here so it can be tested headlessly in CI.
add_library(pokedex_core STATIC
    src/greeting.cpp
)
target_include_directories(pokedex_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# --- GUI application (Qt 6 Widgets) ------------------------------------------
find_package(Qt6 REQUIRED COMPONENTS Widgets)

qt_standard_project_setup()

qt_add_executable(pokedex_tcg
    src/main.cpp
)
target_link_libraries(pokedex_tcg PRIVATE pokedex_core Qt6::Widgets)

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

    add_executable(pokedex_tests tests/test_greeting.cpp)
    target_link_libraries(pokedex_tests PRIVATE pokedex_core GTest::gtest_main)

    include(GoogleTest)
    gtest_discover_tests(pokedex_tests)
endif()
