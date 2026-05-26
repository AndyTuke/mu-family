# Deploys mu-Clid Lite Release artifacts to the OneDrive tester folder.
# Called from CMakeLists.txt POST_BUILD on mu-clid-lite_VST3.
# Skips silently for Debug builds — Debug binaries must NOT reach the tester folder.

if(NOT CONFIG STREQUAL "Release")
    return()
endif()

message(STATUS "Deploying mu-Clid Lite Release artifacts to: ${DIST_WIN}")

if(IS_DIRECTORY "${VST3_BUNDLE}")
    file(COPY "${VST3_BUNDLE}" DESTINATION "${DIST_WIN}")
    message(STATUS "  VST3 -> ${DIST_WIN}/mu-Clid Lite.vst3")
else()
    message(WARNING "  VST3 bundle not found: ${VST3_BUNDLE}")
endif()

if(EXISTS "${CLAP_FILE}")
    file(COPY_FILE "${CLAP_FILE}" "${DIST_WIN}/mu-Clid Lite.clap" ONLY_IF_DIFFERENT)
    message(STATUS "  CLAP -> ${DIST_WIN}/mu-Clid Lite.clap")
else()
    message(WARNING "  CLAP not found: ${CLAP_FILE}")
endif()
