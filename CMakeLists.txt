cmake_minimum_required(VERSION 3.22)
project(mu-clid VERSION 1.0.0)

add_subdirectory($ENV{JUCE_PATH} JUCE)
add_subdirectory(ThirdParty/clap-juce-extensions)

juce_add_plugin(mu-clid
    COMPANY_NAME "Transwarp Development Project"
    BUNDLE_ID "com.transwarp.mu-clid"
    PLUGIN_MANUFACTURER_CODE TDP1
    PLUGIN_CODE MCld
    FORMATS VST3 Standalone
    PRODUCT_NAME "mu-Clid"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
)


target_sources(mu-clid
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/Sequencer/EuclideanGenerator.cpp
        Source/Sequencer/HitGenerator.cpp
        Source/Sequencer/Rhythm.cpp
        Source/Sequencer/SequencerEngine.cpp
        Source/Sequencer/ControlSequence.cpp
        Source/Audio/SamplePlayer.cpp
        Source/Audio/VoiceEngine.cpp
        Source/Audio/MidiOutputEngine.cpp
        Source/Modulation/ModulationMatrix.cpp
        Source/UI/Components/MuClidLookAndFeel.cpp
        Source/UI/Components/KnobWithLabel.cpp
        Source/UI/Components/SegmentControl.cpp
        Source/UI/Components/NudgeInput.cpp
        Source/UI/Components/DropdownSelect.cpp
        Source/UI/Components/StepEditor.cpp
        Source/UI/Components/LFOEditor.cpp
        Source/UI/Components/AddButton.cpp
        Source/UI/Components/StatusBar.cpp
        Source/UI/TransportBar.cpp
        Source/UI/RhythmCircle.cpp
        Source/UI/SidebarItem.cpp
        Source/UI/RhythmSidebar.cpp
        Source/UI/EuclideanPanel.cpp
        Source/UI/VoiceSection.cpp
        Source/UI/ModulatorEditor.cpp
        Source/UI/ModMatrixPanel.cpp
        Source/UI/ModulatorPanel.cpp
        Source/UI/RhythmPanel.cpp
        Source/FX/EffectSlot.cpp
        Source/FX/DelaySlot.cpp
        Source/FX/ReverbSlot.cpp
        Source/FX/FXChain.cpp
        Source/UI/FXRow.cpp
        Source/UI/DelayRow.cpp
        Source/Audio/MixerEngine.cpp
        Source/UI/Components/VUMeter.cpp
        Source/UI/MixerChannel.cpp
        Source/UI/MixerOverlay.cpp
        Source/UI/AboutPanel.cpp
        Source/UI/SaveDialog.cpp
        Source/UI/PresetBrowser.cpp
        Source/UI/SettingsOverlay.cpp
        Source/License/LicenseChecker.cpp
        ThirdParty/monocypher/monocypher.c
)

target_include_directories(mu-clid
    SYSTEM PRIVATE
        "${CMAKE_SOURCE_DIR}/ThirdParty/signalsmith-basics/include"
        "${CMAKE_SOURCE_DIR}/ThirdParty/signalsmith-dsp/include"
        "${CMAKE_SOURCE_DIR}/ThirdParty/monocypher"
)

target_compile_definitions(mu-clid
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_USE_MP3AUDIOFORMAT=1
)

target_link_libraries(mu-clid
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_gui_basics
        juce::juce_gui_extra
        juce::juce_audio_formats
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# Increment build_number.txt and regenerate Source/BuildNumber.h before every build.
add_custom_target(increment_build_number ALL
    COMMAND ${CMAKE_COMMAND} -P "${CMAKE_SOURCE_DIR}/cmake/IncrementBuildNumber.cmake"
    COMMENT "Incrementing build number"
    VERBATIM
)
add_dependencies(mu-clid increment_build_number)

# Installer — run manually after a Release build: cmake --build build --target mu-clid_installer --config Release
# Requires Inno Setup 6 at its default install location.
file(READ "${CMAKE_SOURCE_DIR}/build_number.txt" MUCLID_BUILD_NUMBER)
string(STRIP "${MUCLID_BUILD_NUMBER}" MUCLID_BUILD_NUMBER)
find_program(ISCC_EXE ISCC
    PATHS "C:/Program Files (x86)/Inno Setup 6"
    NO_DEFAULT_PATH)
set(MUCLID_INSTALLER_DIST "D:/Andy Tuke/Music - Music Software/TDP/Windows/Installer")
if(ISCC_EXE)
    add_custom_target(mu-clid_installer
        COMMAND "${ISCC_EXE}"
                "/DBuildNum=${MUCLID_BUILD_NUMBER}"
                "${CMAKE_SOURCE_DIR}/installer/mu-Clid.iss"
        COMMAND "${CMAKE_COMMAND}" -E copy
                "${CMAKE_SOURCE_DIR}/build/installer/mu-Clid-Setup-v1.0.${MUCLID_BUILD_NUMBER}.exe"
                "${MUCLID_INSTALLER_DIST}/mu-Clid-Setup-v1.0.${MUCLID_BUILD_NUMBER}.exe"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/installer"
        COMMENT "Building mu-Clid installer (build ${MUCLID_BUILD_NUMBER}) and copying to OneDrive"
        VERBATIM
    )
else()
    message(STATUS "Inno Setup not found — mu-clid_installer target unavailable")
endif()

clap_juce_extensions_plugin(TARGET mu-clid
    CLAP_ID "com.transwarp.mu-clid"
    CLAP_FEATURES instrument sampler drum-machine)