# Resolve the short git commit hash and compose the app version string, then write
# it into a generated header — but only when the value changes, so an unchanged
# hash doesn't force a rebuild of everything that includes it.
#
# Invoked at build time (cmake -P) by the pokedex_version_header target, so the
# hash tracks HEAD across commits without needing a reconfigure. Inputs arrive as
# -D defines: GIT_EXECUTABLE, SOURCE_DIR, OUTPUT_FILE, BUILD_TYPE.

set(hash "unknown")
if(GIT_EXECUTABLE)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${SOURCE_DIR}" rev-parse --short=7 HEAD
        OUTPUT_VARIABLE hash
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE git_result
        ERROR_QUIET)
    if(NOT git_result EQUAL 0 OR hash STREQUAL "")
        set(hash "unknown")  # not a git checkout (e.g. an unpacked tarball)
    endif()
endif()

# A Release build is the "prod" install (install.sh); any other build — dev.sh, or
# a bare `cmake` configure — is a dev build and carries the -dev suffix.
if(BUILD_TYPE STREQUAL "Release")
    set(version "${hash}")
else()
    set(version "${hash}-dev")
endif()

set(content
"#pragma once
// Generated at build time from git — do not edit.
namespace pokedex {
inline constexpr const char* kAppVersion = \"${version}\";
}  // namespace pokedex
")

# Skip the write when nothing changed, so dependents don't recompile every build.
set(existing "")
if(EXISTS "${OUTPUT_FILE}")
    file(READ "${OUTPUT_FILE}" existing)
endif()
if(NOT existing STREQUAL content)
    file(WRITE "${OUTPUT_FILE}" "${content}")
endif()
