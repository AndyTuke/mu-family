# Invoked at build time by the mu-clid_installer target.
# Reads build_number.txt after increment_build_number has already run,
# so the installer filename matches the build number baked into the plugin.
file(READ "${SOURCE_DIR}/build_number.txt" BUILD_NUM)
string(STRIP "${BUILD_NUM}" BUILD_NUM)

execute_process(
    COMMAND "${ISCC_EXE}" "/DBuildNum=${BUILD_NUM}" "${SOURCE_DIR}/installer/mu-Clid.iss"
    WORKING_DIRECTORY "${SOURCE_DIR}/installer"
    RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Inno Setup failed with exit code ${result}")
endif()

# Highlander: there can be only one. Sweep any pre-existing
# mu-Clid-Setup-v*.exe from DIST_DIR before placing the new one,
# so testers always see exactly one installer (the latest build).
file(GLOB previous_installers "${DIST_DIR}/mu-Clid-Setup-v*.exe")
foreach(stale ${previous_installers})
    file(REMOVE "${stale}")
endforeach()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E copy
        "${SOURCE_DIR}/build/installer/mu-Clid-Setup-v1.0.${BUILD_NUM}.exe"
        "${DIST_DIR}/mu-Clid-Setup-v1.0.${BUILD_NUM}.exe"
    RESULT_VARIABLE copy_result
)
if(NOT copy_result EQUAL 0)
    message(WARNING "Could not copy installer to distribution folder: ${DIST_DIR}")
endif()
