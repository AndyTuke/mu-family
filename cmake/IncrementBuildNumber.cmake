# Runs at build time (not configure time) via add_custom_target.
# CMAKE_CURRENT_LIST_DIR is the cmake/ subdirectory, so .. is the project root.
set(ROOT_DIR     "${CMAKE_CURRENT_LIST_DIR}/..")
set(COUNTER_FILE "${ROOT_DIR}/build_number.txt")
# mu-family monorepo: BuildNumber.h lives in mu-core so every product reads the
# same header (build counter is shared family-wide — single root build_number.txt).
set(HEADER_FILE  "${ROOT_DIR}/mu-core/BuildNumber.h")

file(READ "${COUNTER_FILE}" BUILD_NUMBER)
string(STRIP "${BUILD_NUMBER}" BUILD_NUMBER)

# Family rule: a build "session" produces matched Debug + Release artefacts at
# the same build number — testers reporting "v1.0.659" must be reproducible
# locally without ambiguity over which config they were on. Bump the counter
# on the first build of a session (regardless of which config goes first);
# subsequent builds within the same session reuse the value. A session ends
# when no build has run for kSessionWindowSeconds (5 minutes).
set(kSessionWindowSeconds 300)

file(TIMESTAMP "${COUNTER_FILE}" LAST_BUMP_S "%s")
string(TIMESTAMP NOW_S "%s")
math(EXPR ELAPSED "${NOW_S} - ${LAST_BUMP_S}")

if(ELAPSED GREATER ${kSessionWindowSeconds})
    math(EXPR BUILD_NUMBER "${BUILD_NUMBER} + 1")
    file(WRITE "${COUNTER_FILE}" "${BUILD_NUMBER}")
    set(BUMP_NOTE "bumped")
else()
    set(BUMP_NOTE "session reuse")
endif()

file(WRITE "${HEADER_FILE}"
    "#pragma once\n#define BUILD_NUMBER ${BUILD_NUMBER}\n")

message(STATUS "Build number: Beta ${BUILD_NUMBER} (${BUILD_CONFIG}, ${BUMP_NOTE})")
