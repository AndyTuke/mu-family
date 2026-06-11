# Packages Release build artifacts into a customer zip in build/dist/.
# Called as a POST_BUILD script (or custom target) from each product's CMakeLists.
# Skips silently for Debug builds.
#
# Required parameters (pass via cmake -D):
#   CONFIG        — $<CONFIG> from the calling build rule
#   PRODUCT       — slug used in the zip filename, e.g. "mu-Clid", "mu-Clid-Lite", "mu-Tant"
#   VST3_BUNDLE   — path to the built .vst3 directory
#   CLAP_FILE     — path to the built .clap file
#   DIST_DIR      — output directory (typically <build>/dist)
#   SOURCE_DIR    — repo root (to read build_number.txt)
#
# Optional parameters:
#   STANDALONE    — path to the built Standalone .exe (omit for Lite/MIDI-effect variants)
#   PRESETS_DIR   — path to a factory Presets folder to bundle (omit if none yet)
#
# Output: <DIST_DIR>/<PRODUCT>-v1.0.<build>-Windows.zip

if(NOT CONFIG STREQUAL "Release")
    return()
endif()

# Read the current build number.
file(READ "${SOURCE_DIR}/build_number.txt" _bn)
string(STRIP "${_bn}" _bn)
set(_version "1.0.${_bn}")

message(STATUS "Packaging ${PRODUCT} v${_version}...")

# Stage into a clean temp directory so the zip has a flat top level.
set(_staging "${DIST_DIR}/staging/${PRODUCT}")
file(REMOVE_RECURSE "${_staging}")
file(MAKE_DIRECTORY "${_staging}")

# VST3 bundle (directory).
if(IS_DIRECTORY "${VST3_BUNDLE}")
    get_filename_component(_vst3_name "${VST3_BUNDLE}" NAME)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${VST3_BUNDLE}" "${_staging}/${_vst3_name}"
        RESULT_VARIABLE _r)
    if(_r EQUAL 0)
        message(STATUS "  + ${_vst3_name}")
    else()
        message(WARNING "  VST3 copy failed: ${VST3_BUNDLE}")
    endif()
else()
    message(WARNING "  VST3 bundle not found: ${VST3_BUNDLE}")
endif()

# CLAP file.
if(EXISTS "${CLAP_FILE}")
    get_filename_component(_clap_name "${CLAP_FILE}" NAME)
    file(COPY_FILE "${CLAP_FILE}" "${_staging}/${_clap_name}" ONLY_IF_DIFFERENT RESULT _r)
    if(_r STREQUAL "0" OR _r STREQUAL "")
        message(STATUS "  + ${_clap_name}")
    else()
        message(WARNING "  CLAP copy failed: ${CLAP_FILE}")
    endif()
else()
    message(WARNING "  CLAP not found: ${CLAP_FILE}")
endif()

# Standalone exe (optional).
if(DEFINED STANDALONE AND EXISTS "${STANDALONE}")
    get_filename_component(_sa_name "${STANDALONE}" NAME)
    file(COPY_FILE "${STANDALONE}" "${_staging}/${_sa_name}" ONLY_IF_DIFFERENT RESULT _r)
    if(_r STREQUAL "0" OR _r STREQUAL "")
        message(STATUS "  + ${_sa_name}")
    else()
        message(WARNING "  Standalone copy failed: ${STANDALONE}")
    endif()
endif()

# Factory Presets folder (optional).
if(DEFINED PRESETS_DIR AND IS_DIRECTORY "${PRESETS_DIR}")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${PRESETS_DIR}" "${_staging}/Presets"
        RESULT_VARIABLE _r)
    if(_r EQUAL 0)
        message(STATUS "  + Presets/")
    else()
        message(WARNING "  Presets copy failed: ${PRESETS_DIR}")
    endif()
endif()

# Create the zip from the staging dir so paths inside the zip are flat/relative.
file(MAKE_DIRECTORY "${DIST_DIR}")
set(_zip "${DIST_DIR}/${PRODUCT}-v${_version}-Windows.zip")
file(REMOVE "${_zip}")

file(GLOB _entries RELATIVE "${_staging}" "${_staging}/*")
if(_entries)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E tar cvf "${_zip}" --format=zip -- ${_entries}
        WORKING_DIRECTORY "${_staging}"
        RESULT_VARIABLE _tar_result
    )
    if(_tar_result EQUAL 0)
        message(STATUS "  => ${_zip}")
    else()
        message(WARNING "  zip creation failed (result: ${_tar_result})")
    endif()
else()
    message(WARNING "  Nothing staged for ${PRODUCT} — zip skipped.")
endif()
