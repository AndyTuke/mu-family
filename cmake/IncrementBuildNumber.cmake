# Runs at build time (not configure time) via add_custom_target.
# CMAKE_CURRENT_LIST_DIR is the cmake/ subdirectory, so .. is the project root.
set(ROOT_DIR     "${CMAKE_CURRENT_LIST_DIR}/..")
set(COUNTER_FILE "${ROOT_DIR}/build_number.txt")
set(HEADER_FILE  "${ROOT_DIR}/Source/BuildNumber.h")

file(READ "${COUNTER_FILE}" BUILD_NUMBER)
string(STRIP "${BUILD_NUMBER}" BUILD_NUMBER)

# Only Debug increments the counter; Release reuses the same number so a full build stays in sync.
if(NOT BUILD_CONFIG STREQUAL "Release")
    math(EXPR BUILD_NUMBER "${BUILD_NUMBER} + 1")
    file(WRITE "${COUNTER_FILE}" "${BUILD_NUMBER}")
endif()

file(WRITE "${HEADER_FILE}"
    "#pragma once\n#define BUILD_NUMBER ${BUILD_NUMBER}\n")

message(STATUS "Build number: Beta ${BUILD_NUMBER} (${BUILD_CONFIG})")
