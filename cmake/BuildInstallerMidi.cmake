# Builds mu-Clid-MIDI-Setup-v<major>.<minor>.<N>.exe using Inno Setup.
# Called via: cmake -P BuildInstallerMidi.cmake

file(READ "${SOURCE_DIR}/build_number.txt" BUILD_NUM)
string(STRIP "${BUILD_NUM}" BUILD_NUM)
file(READ "${SOURCE_DIR}/version.txt" VERSION_BASE)
string(STRIP "${VERSION_BASE}" VERSION_BASE)

# The installer packages the Release artefacts; skip quietly until a Release build has produced them under the current name.
if(NOT EXISTS "${BUILD_DIR}/mu-clid-midi/mu-clid-midi_artefacts/Release/CLAP/mu-Clid MIDI.clap")
    message(STATUS "mu-Clid MIDI installer skipped (no Release build yet)")
    return()
endif()

execute_process(
    COMMAND "${ISCC_EXE}"
            "/DBuildNum=${BUILD_NUM}" "/DVersionBase=${VERSION_BASE}"
            "${SOURCE_DIR}/mu-clid-midi/installer/mu-Clid-MIDI.iss"
    RESULT_VARIABLE ISCC_RESULT
)

if(NOT ISCC_RESULT EQUAL 0)
    message(FATAL_ERROR "Inno Setup failed with code ${ISCC_RESULT}")
endif()

message(STATUS "mu-Clid MIDI installer built (build ${BUILD_NUM})")
