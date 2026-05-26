# Runs at build time (not configure time) via add_custom_target.
# CMAKE_CURRENT_LIST_DIR is the cmake/ subdirectory, so .. is the project root.
set(ROOT_DIR     "${CMAKE_CURRENT_LIST_DIR}/..")
set(COUNTER_FILE "${ROOT_DIR}/build_number.txt")
# mu-family monorepo: BuildNumber.h lives in the mu-clid plugin subtree since
# the build number is a per-plugin artefact (mu-tant will have its own).
set(HEADER_FILE  "${ROOT_DIR}/mu-clid/Source/BuildNumber.h")

file(READ "${COUNTER_FILE}" BUILD_NUMBER)
string(STRIP "${BUILD_NUMBER}" BUILD_NUMBER)

# Only Release increments the counter; Debug reuses the same number so a full build stays in sync.
if(BUILD_CONFIG STREQUAL "Release")
    math(EXPR BUILD_NUMBER "${BUILD_NUMBER} + 1")
    file(WRITE "${COUNTER_FILE}" "${BUILD_NUMBER}")
endif()

file(WRITE "${HEADER_FILE}"
    "#pragma once\n#define BUILD_NUMBER ${BUILD_NUMBER}\n")

message(STATUS "Build number: Beta ${BUILD_NUMBER} (${BUILD_CONFIG})")
