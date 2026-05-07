# Deploys Release build artifacts to the OneDrive distribution folder shared with testers.
# Called from CMakeLists.txt POST_BUILD on mu-clid_Standalone.
# Skips silently for Debug builds — Debug binaries must NOT reach the tester folder.
# Run Debug builds directly from the build output folder.

if(NOT CONFIG STREQUAL "Release")
    return()
endif()

message(STATUS "Deploying Release artifacts to: ${DIST_WIN}")

if(EXISTS "${STANDALONE}")
    file(COPY_FILE "${STANDALONE}" "${DIST_WIN}/mu-Clid.exe" ONLY_IF_DIFFERENT)
    message(STATUS "  Standalone -> ${DIST_WIN}/mu-Clid.exe")
else()
    message(WARNING "  Standalone not found: ${STANDALONE}")
endif()

if(IS_DIRECTORY "${VST3_BUNDLE}")
    file(COPY "${VST3_BUNDLE}" DESTINATION "${DIST_WIN}")
    message(STATUS "  VST3 -> ${DIST_WIN}/mu-Clid.vst3")
else()
    message(WARNING "  VST3 bundle not found: ${VST3_BUNDLE}")
endif()

if(EXISTS "${CLAP_FILE}")
    file(COPY_FILE "${CLAP_FILE}" "${DIST_WIN}/mu-Clid.clap" ONLY_IF_DIFFERENT)
    message(STATUS "  CLAP -> ${DIST_WIN}/mu-Clid.clap")
else()
    message(WARNING "  CLAP not found: ${CLAP_FILE}")
endif()
