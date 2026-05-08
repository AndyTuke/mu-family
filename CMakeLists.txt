cmake_minimum_required(VERSION 3.22)
project(mu-clid VERSION 1.0.0)

# Embed the VC++ runtime so the plugin runs without the Redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

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
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    ICON_BIG   "${CMAKE_SOURCE_DIR}/resources/Icon.png"
    ICON_SMALL "${CMAKE_SOURCE_DIR}/resources/Icon.png"
)


target_sources(mu-clid
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/MidiPresetMap.cpp
        Source/Sequencer/EuclideanGenerator.cpp
        Source/Sequencer/HitGenerator.cpp
        Source/Sequencer/Rhythm.cpp
        Source/Sequencer/SequencerEngine.cpp
        Source/Sequencer/ControlSequence.cpp
        Source/Audio/SamplePlayer.cpp
        Source/Audio/InsertProcessor.cpp
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
        Source/UI/MidiPresetsPanel.cpp
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

# Installer — built automatically as part of every Release build.
# Reads build_number.txt at build time (after increment) so the filename is always correct.
# Requires Inno Setup 6 at its default install location.
find_program(ISCC_EXE ISCC
    PATHS "C:/Program Files (x86)/Inno Setup 6"
    NO_DEFAULT_PATH)
set(MUCLID_INSTALLER_DIST "D:/Andy Tuke/Music - Music Software/TDP/Windows/Installer")
if(ISCC_EXE)
    add_custom_target(mu-clid_installer ALL
        COMMAND "${CMAKE_COMMAND}"
                "-DISCC_EXE=${ISCC_EXE}"
                "-DSOURCE_DIR=${CMAKE_SOURCE_DIR}"
                "-DDIST_DIR=${MUCLID_INSTALLER_DIST}"
                "-DCMAKE_COMMAND=${CMAKE_COMMAND}"
                -P "${CMAKE_SOURCE_DIR}/cmake/BuildInstaller.cmake"
        COMMENT "Building mu-Clid installer"
        VERBATIM
    )
    add_dependencies(mu-clid_installer mu-clid)
else()
    message(STATUS "Inno Setup not found — mu-clid_installer target unavailable")
endif()

# Deploy Release artifacts to the OneDrive folder shared with testers.
# Debug builds skip this — see cmake/DeployRelease.cmake.
set(MUCLID_WIN_DIST "D:/Andy Tuke/Music - Music Software/TDP/Windows")
add_custom_command(TARGET mu-clid_Standalone POST_BUILD
    COMMAND "${CMAKE_COMMAND}"
            "-DCONFIG=$<CONFIG>"
            "-DSTANDALONE=$<TARGET_FILE:mu-clid_Standalone>"
            "-DVST3_BUNDLE=${CMAKE_BINARY_DIR}/mu-clid_artefacts/$<CONFIG>/VST3/mu-Clid.vst3"
            "-DCLAP_FILE=${CMAKE_BINARY_DIR}/mu-clid_artefacts/$<CONFIG>/CLAP/mu-Clid.clap"
            "-DDIST_WIN=${MUCLID_WIN_DIST}"
            -P "${CMAKE_SOURCE_DIR}/cmake/DeployRelease.cmake"
    COMMENT "Deploying Release artifacts to OneDrive (Debug skipped)"
    VERBATIM
)

clap_juce_extensions_plugin(TARGET mu-clid
    CLAP_ID "com.transwarp.mu-clid"
    CLAP_FEATURES instrument sampler drum-machine)

# ── mu-Clid Lite — single-rhythm MIDI-effect VST3/CLAP ──────────────────────
juce_add_plugin(mu-clid-lite
    COMPANY_NAME "Transwarp Development Project"
    BUNDLE_ID "com.transwarp.mu-clid-lite"
    PLUGIN_MANUFACTURER_CODE TDP1
    PLUGIN_CODE MCLt
    FORMATS VST3
    PRODUCT_NAME "mu-Clid Lite"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT TRUE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    ICON_BIG   "${CMAKE_SOURCE_DIR}/resources/Icon.png"
    ICON_SMALL "${CMAKE_SOURCE_DIR}/resources/Icon.png"
)

target_sources(mu-clid-lite
    PRIVATE
        Source/PluginProcessor.cpp
        Source/LiteEditor.cpp
        Source/MidiPresetMap.cpp
        Source/Sequencer/EuclideanGenerator.cpp
        Source/Sequencer/HitGenerator.cpp
        Source/Sequencer/Rhythm.cpp
        Source/Sequencer/SequencerEngine.cpp
        Source/Sequencer/ControlSequence.cpp
        Source/Audio/SamplePlayer.cpp
        Source/Audio/InsertProcessor.cpp
        Source/Audio/VoiceEngine.cpp
        Source/Audio/MidiOutputEngine.cpp
        Source/Audio/MixerEngine.cpp
        Source/Modulation/ModulationMatrix.cpp
        Source/FX/EffectSlot.cpp
        Source/FX/DelaySlot.cpp
        Source/FX/ReverbSlot.cpp
        Source/FX/FXChain.cpp
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
        Source/UI/EuclideanPanel.cpp
        Source/UI/AboutPanel.cpp
        Source/License/LicenseChecker.cpp
        ThirdParty/monocypher/monocypher.c
)

target_include_directories(mu-clid-lite
    SYSTEM PRIVATE
        "${CMAKE_SOURCE_DIR}/ThirdParty/signalsmith-basics/include"
        "${CMAKE_SOURCE_DIR}/ThirdParty/signalsmith-dsp/include"
        "${CMAKE_SOURCE_DIR}/ThirdParty/monocypher"
)

target_compile_definitions(mu-clid-lite
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_USE_MP3AUDIOFORMAT=1
        MUCLID_LITE_BUILD=1
)

target_link_libraries(mu-clid-lite
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

add_dependencies(mu-clid-lite increment_build_number)

# Lite installer — Release only, requires Inno Setup.
if(ISCC_EXE)
    add_custom_target(mu-clid-lite_installer ALL
        COMMAND "${CMAKE_COMMAND}"
                "-DISCC_EXE=${ISCC_EXE}"
                "-DSOURCE_DIR=${CMAKE_SOURCE_DIR}"
                "-DBUILD_DIR=${CMAKE_BINARY_DIR}"
                "-DCMAKE_COMMAND=${CMAKE_COMMAND}"
                -P "${CMAKE_SOURCE_DIR}/cmake/BuildInstallerLite.cmake"
        COMMENT "Building mu-Clid Lite installer"
        VERBATIM
    )
    add_dependencies(mu-clid-lite_installer mu-clid-lite)
endif()

clap_juce_extensions_plugin(TARGET mu-clid-lite
    CLAP_ID "com.transwarp.mu-clid-lite"
    CLAP_FEATURES midi-effect)

# Deploy Lite Release artifacts to the OneDrive tester folder alongside the main plugin.
# Both VST3 and CLAP targets get a POST_BUILD hook so each artifact is copied as soon
# as it is built, regardless of which target was rebuilt.
add_custom_command(TARGET mu-clid-lite_VST3 POST_BUILD
    COMMAND "${CMAKE_COMMAND}"
            "-DCONFIG=$<CONFIG>"
            "-DVST3_BUNDLE=${CMAKE_BINARY_DIR}/mu-clid-lite_artefacts/$<CONFIG>/VST3/mu-Clid Lite.vst3"
            "-DCLAP_FILE=${CMAKE_BINARY_DIR}/mu-clid-lite_artefacts/$<CONFIG>/CLAP/mu-Clid Lite.clap"
            "-DDIST_WIN=${MUCLID_WIN_DIST}"
            -P "${CMAKE_SOURCE_DIR}/cmake/DeployReleaseLite.cmake"
    COMMENT "Deploying mu-Clid Lite VST3 to OneDrive (Debug skipped)"
    VERBATIM
)
add_custom_command(TARGET mu-clid-lite_CLAP POST_BUILD
    COMMAND "${CMAKE_COMMAND}"
            "-DCONFIG=$<CONFIG>"
            "-DVST3_BUNDLE=${CMAKE_BINARY_DIR}/mu-clid-lite_artefacts/$<CONFIG>/VST3/mu-Clid Lite.vst3"
            "-DCLAP_FILE=${CMAKE_BINARY_DIR}/mu-clid-lite_artefacts/$<CONFIG>/CLAP/mu-Clid Lite.clap"
            "-DDIST_WIN=${MUCLID_WIN_DIST}"
            -P "${CMAKE_SOURCE_DIR}/cmake/DeployReleaseLite.cmake"
    COMMENT "Deploying mu-Clid Lite CLAP to OneDrive (Debug skipped)"
    VERBATIM
)