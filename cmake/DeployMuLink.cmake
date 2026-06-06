# Deploys the mu-link Release exe to the OneDrive tester share (bare exe — mu-link is a
# standalone GUI app with no plugin formats and no installer). Called from
# mu-link/CMakeLists.txt as a POST_BUILD on the `mu-link` target. Skips Debug silently —
# Debug binaries must not reach the tester folder (run those from the build output).
#
# Required parameters (pass via cmake -D):
#   CONFIG    — $<CONFIG> from the calling build rule
#   APP_EXE   — absolute path to the built mu-link.exe ($<TARGET_FILE:mu-link>)
#   DIST_WIN  — OneDrive tester drop folder root

if(NOT CONFIG STREQUAL "Release")
    return()
endif()

message(STATUS "Deploying mu-link Release exe to: ${DIST_WIN}")

# Capture RESULT so a locked destination (a tester running mu-link, antivirus, OneDrive
# mid-sync) downgrades from "abort the build" to a warning — mirrors DeployRelease.cmake.
if(EXISTS "${APP_EXE}")
    file(COPY_FILE "${APP_EXE}" "${DIST_WIN}/mu-link.exe"
         ONLY_IF_DIFFERENT
         RESULT copy_result)
    if(copy_result STREQUAL "0" OR copy_result STREQUAL "")   # "0" = success (incl. no-op)
        message(STATUS "  mu-link -> ${DIST_WIN}/mu-link.exe")
    else()
        message(WARNING "  mu-link deploy skipped (${copy_result}). Locked? Close any running mu-link.")
    endif()
else()
    message(WARNING "  mu-link exe not found: ${APP_EXE}")
endif()
