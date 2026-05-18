# Deploys Release build artifacts to the OneDrive distribution folder shared with testers.
# Called from CMakeLists.txt POST_BUILD on mu-clid_Standalone.
# Skips silently for Debug builds — Debug binaries must NOT reach the tester folder.
# Run Debug builds directly from the build output folder.

if(NOT CONFIG STREQUAL "Release")
    return()
endif()

message(STATUS "Deploying Release artifacts to: ${DIST_WIN}")

# #428: capture RESULT on each copy so a locked destination (testers running
# the standalone, antivirus scanning, OneDrive sync mid-upload) downgrades
# the failure from "abort the build" to a warning. The artefact files
# themselves are still produced under build/mu-clid_artefacts/Release/ —
# the user can install from the local installer or copy manually.
if(EXISTS "${STANDALONE}")
    file(COPY_FILE "${STANDALONE}" "${DIST_WIN}/μ-Clid.exe"
         ONLY_IF_DIFFERENT
         RESULT copy_result)
    if(copy_result STREQUAL "")
        message(STATUS "  Standalone -> ${DIST_WIN}/μ-Clid.exe")
    else()
        message(WARNING "  Standalone deploy skipped (${copy_result}). Locked? Close any running μ-Clid.")
    endif()
else()
    message(WARNING "  Standalone not found: ${STANDALONE}")
endif()

if(IS_DIRECTORY "${VST3_BUNDLE}")
    # file(COPY) doesn't expose a RESULT var; wrap in a try-style execute_process
    # so a locked sub-file inside the bundle doesn't abort the build.
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${VST3_BUNDLE}" "${DIST_WIN}/μ-Clid.vst3"
        RESULT_VARIABLE vst3_result
        OUTPUT_QUIET ERROR_QUIET)
    if(vst3_result EQUAL 0)
        message(STATUS "  VST3 -> ${DIST_WIN}/μ-Clid.vst3")
    else()
        message(WARNING "  VST3 deploy skipped (likely a locked file inside the bundle).")
    endif()
else()
    message(WARNING "  VST3 bundle not found: ${VST3_BUNDLE}")
endif()

if(EXISTS "${CLAP_FILE}")
    file(COPY_FILE "${CLAP_FILE}" "${DIST_WIN}/μ-Clid.clap"
         ONLY_IF_DIFFERENT
         RESULT copy_result)
    if(copy_result STREQUAL "")
        message(STATUS "  CLAP -> ${DIST_WIN}/μ-Clid.clap")
    else()
        message(WARNING "  CLAP deploy skipped (${copy_result}).")
    endif()
else()
    message(WARNING "  CLAP not found: ${CLAP_FILE}")
endif()
