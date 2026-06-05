# Deploys Release build artifacts to the OneDrive distribution folder shared with testers.
# Called from each product's CMakeLists.txt POST_BUILD on its Standalone target.
# Skips silently for Debug builds — Debug binaries must NOT reach the tester folder.
# Run Debug builds directly from the build output folder.
#
# Required parameters (pass via cmake -D):
#   CONFIG          — $<CONFIG> from the calling build rule
#   STANDALONE      — absolute path to the built Standalone .exe
#   VST3_BUNDLE     — absolute path to the built .vst3 bundle directory
#   CLAP_FILE       — absolute path to the built .clap file
#   DIST_WIN        — OneDrive tester drop folder root
#   PRODUCT_FILENAME — product filename stem used in the drop folder (e.g. "mu-Clid", "mu-Tant")

if(NOT CONFIG STREQUAL "Release")
    return()
endif()

message(STATUS "Deploying ${PRODUCT_FILENAME} Release artifacts to: ${DIST_WIN}")

# #428: capture RESULT on each copy so a locked destination (testers running
# the standalone, antivirus scanning, OneDrive sync mid-upload) downgrades
# the failure from "abort the build" to a warning. The artefact files
# themselves are still produced under build/<product>_artefacts/Release/ —
# the user can install from the local installer or copy manually.
if(EXISTS "${STANDALONE}")
    file(COPY_FILE "${STANDALONE}" "${DIST_WIN}/${PRODUCT_FILENAME}.exe"
         ONLY_IF_DIFFERENT
         RESULT copy_result)
    # file(COPY_FILE) RESULT is "0" on success (incl. an ONLY_IF_DIFFERENT no-op),
    # or a human-readable error string on real failure. Accept both "0" and ""
    # (older CMake) as success — checking only "" mis-reported every successful
    # deploy as "skipped (0)", which falsely looked like a locked/running app.
    if(copy_result STREQUAL "0" OR copy_result STREQUAL "")
        message(STATUS "  Standalone -> ${DIST_WIN}/${PRODUCT_FILENAME}.exe")
    else()
        message(WARNING "  Standalone deploy skipped (${copy_result}). Locked? Close any running ${PRODUCT_FILENAME}.")
    endif()
else()
    message(WARNING "  Standalone not found: ${STANDALONE}")
endif()

if(IS_DIRECTORY "${VST3_BUNDLE}")
    # file(COPY) doesn't expose a RESULT var; wrap in a try-style execute_process
    # so a locked sub-file inside the bundle doesn't abort the build.
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${VST3_BUNDLE}" "${DIST_WIN}/${PRODUCT_FILENAME}.vst3"
        RESULT_VARIABLE vst3_result
        OUTPUT_QUIET ERROR_QUIET)
    if(vst3_result EQUAL 0)
        message(STATUS "  VST3 -> ${DIST_WIN}/${PRODUCT_FILENAME}.vst3")
    else()
        message(WARNING "  VST3 deploy skipped (likely a locked file inside the bundle).")
    endif()
else()
    message(WARNING "  VST3 bundle not found: ${VST3_BUNDLE}")
endif()

if(EXISTS "${CLAP_FILE}")
    file(COPY_FILE "${CLAP_FILE}" "${DIST_WIN}/${PRODUCT_FILENAME}.clap"
         ONLY_IF_DIFFERENT
         RESULT copy_result)
    if(copy_result STREQUAL "0" OR copy_result STREQUAL "")   # "0" = success (see Standalone note)
        message(STATUS "  CLAP -> ${DIST_WIN}/${PRODUCT_FILENAME}.clap")
    else()
        message(WARNING "  CLAP deploy skipped (${copy_result}).")
    endif()
else()
    message(WARNING "  CLAP not found: ${CLAP_FILE}")
endif()
