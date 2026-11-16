# Invoked at build time by the mu-clid_installer target.
# The installer packs the Release binaries, so any other configuration (a Debug build of everything) skips it: otherwise a Debug build
# fails when no Release build exists, or packs stale Release files and replaces the installer in the distribution folder.
if(NOT BUILD_CONFIG STREQUAL "Release")
    message(STATUS "mu-Clid installer: skipped (${BUILD_CONFIG} build; it is built with Release)")
    return()
endif()
# Reads build_number.txt after increment_build_number has already run,
# so the installer filename matches the build number baked into the plugin.
file(READ "${SOURCE_DIR}/build_number.txt" BUILD_NUM)
string(STRIP "${BUILD_NUM}" BUILD_NUM)
file(READ "${SOURCE_DIR}/version.txt" VERSION_BASE)
string(STRIP "${VERSION_BASE}" VERSION_BASE)

execute_process(
    COMMAND "${ISCC_EXE}" "/DBuildNum=${BUILD_NUM}" "/DVersionBase=${VERSION_BASE}" "${SOURCE_DIR}/mu-clid/installer/mu-Clid.iss"
    WORKING_DIRECTORY "${SOURCE_DIR}/mu-clid/installer"
    RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Inno Setup failed with exit code ${result}")
endif()

# Highlander: there can be only one. Sweep any pre-existing
# installer exes from DIST_DIR before placing the new one,
# so testers always see exactly one installer (the latest build).
# Cover BOTH the old ASCII (mu-Clid-Setup-v*.exe) and new Unicode
# (μ-Clid-Setup-v*.exe) names so a build after the rename still
# cleans up stale older-name installers left in the deploy folder.
file(GLOB previous_installers
    "${DIST_DIR}/mu-Clid-Setup-v*.exe"
    "${DIST_DIR}/μ-Clid-Setup-v*.exe")
foreach(stale ${previous_installers})
    file(REMOVE "${stale}")
endforeach()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E copy
        "${SOURCE_DIR}/build/installer/mu-Clid-Setup-v${VERSION_BASE}.${BUILD_NUM}.exe"
        "${DIST_DIR}/mu-Clid-Setup-v${VERSION_BASE}.${BUILD_NUM}.exe"
    RESULT_VARIABLE copy_result
)
if(NOT copy_result EQUAL 0)
    message(WARNING "Could not copy installer to distribution folder: ${DIST_DIR}")
endif()
