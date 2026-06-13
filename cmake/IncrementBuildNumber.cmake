# Runs at build time (not configure time) via add_custom_target.
# CMAKE_CURRENT_LIST_DIR is the cmake/ subdirectory, so .. is the project root.
set(ROOT_DIR     "${CMAKE_CURRENT_LIST_DIR}/..")
set(COUNTER_FILE "${ROOT_DIR}/build_number.txt")
# Witness of the most recent Debug build number — written only by the Debug path
# so Release can detect a counter that advanced without a real Debug build.
set(LASTDEBUG_FILE "${ROOT_DIR}/.last_debug_build")
# mu-family monorepo: BuildNumber.h lives in mu-core so every product reads the
# same header (build counter is shared family-wide — single root build_number.txt).
set(HEADER_FILE  "${ROOT_DIR}/mu-core/BuildNumber.h")

file(READ "${COUNTER_FILE}" BUILD_NUMBER)
string(STRIP "${BUILD_NUMBER}" BUILD_NUMBER)

# Versioning policy (owner rules):
#   Debug   — EVERY Debug build increments by exactly 1. No time/session throttle:
#             each Debug build is a distinct, testable artefact and gets its own
#             number. Records the value to .last_debug_build as the witness.
#   Release — NEVER increments. Reuses the current number (= the last Debug build)
#             and rebuilds only the Release artefacts; the Debug artefacts are
#             untouched. A shipped Release therefore equals the last Debug at ship
#             time, and naturally sits BELOW the counter once development moves on.
#             Guard: a Release number GREATER than .last_debug_build is impossible
#             unless the counter advanced without a Debug build — stop and let the
#             owner decide rather than ship a mismatched build.
if(BUILD_CONFIG STREQUAL "Release")
    if(EXISTS "${LASTDEBUG_FILE}")
        file(READ "${LASTDEBUG_FILE}" LAST_DEBUG)
        string(STRIP "${LAST_DEBUG}" LAST_DEBUG)
        if(BUILD_NUMBER GREATER LAST_DEBUG)
            message(FATAL_ERROR
                "Release build number ${BUILD_NUMBER} is HIGHER than the last Debug "
                "build ${LAST_DEBUG}. Release must never exceed Debug — the counter "
                "advanced without a Debug build. STOPPING so a decision can be made.")
        endif()
    endif()
    set(BUMP_NOTE "release — reusing #${BUILD_NUMBER}")
else()
    math(EXPR BUILD_NUMBER "${BUILD_NUMBER} + 1")
    file(WRITE "${COUNTER_FILE}"   "${BUILD_NUMBER}")
    file(WRITE "${LASTDEBUG_FILE}" "${BUILD_NUMBER}")
    set(BUMP_NOTE "debug — bumped to #${BUILD_NUMBER}")
endif()

# Runtime number for the in-app About panel.
file(WRITE "${HEADER_FILE}"
    "#pragma once\n#define BUILD_NUMBER ${BUILD_NUMBER}\n")

# ── Regenerate the Windows version resources from THIS (post-increment) number ──
# The .rc is otherwise baked at configure time (before the build-time bump), which
# left the Windows file-properties version one behind the About panel on Debug.
# Regenerating here — with the *_rc_lib targets depending on increment_build_number —
# makes Windows file properties == BuildNumber.h == About panel, every build.
set(MUFAMILY_PLUGIN_VERSION "1.0.0.${BUILD_NUMBER}")
string(REPLACE "." "," MUFAMILY_FILEVERSION "${MUFAMILY_PLUGIN_VERSION}")
if(NOT DEFINED MUFAMILY_COPYRIGHT)
    string(TIMESTAMP _yr "%Y")
    set(MUFAMILY_COPYRIGHT "Copyright (c) ${_yr} Transwarp Development Project")
endif()
foreach(_n 1 2 3 4)
    if(DEFINED RC${_n}_IN AND DEFINED RC${_n}_OUT AND EXISTS "${RC${_n}_IN}")
        configure_file("${RC${_n}_IN}" "${RC${_n}_OUT}" @ONLY NEWLINE_STYLE CRLF)
    endif()
endforeach()

message(STATUS "Build number: ${BUILD_NUMBER} (${BUILD_CONFIG}, ${BUMP_NOTE})")
