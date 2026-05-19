cmake_minimum_required(VERSION 3.22)
project(mu-clid VERSION 1.0.0)

# Embed the VC++ runtime so the plugin runs without the Redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_subdirectory($ENV{JUCE_PATH} JUCE)
add_subdirectory(ThirdParty/clap-juce-extensions)

juce_add_binary_data(mu-clid-resources
    SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/mu-clid.png"
)

# ── mu-core: shared audio/FX/modulation/UI source set ───────────────────────
# An INTERFACE library that propagates shared source files to consuming plugin
# targets. Sources compile in each consuming target's context (where JUCE modules
# are fully initialised by juce_add_plugin), avoiding the need for a separate
# STATIC library that would require duplicating JUCE's module setup.
# Future mu-tant adds: target_link_libraries(mu-tant PRIVATE mu-core)
add_library(mu-core INTERFACE)

# Pin C++ standard explicitly so a future JUCE bump cannot silently change it.
target_compile_features(mu-core INTERFACE cxx_std_17)

target_sources(mu-core INTERFACE
    # Modulation
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/Modulation/ModulationMatrix.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/Sequencer/ControlSequence.cpp"
    # Audio
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/Audio/VoiceEngine.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/Audio/InsertProcessor.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/Audio/MultiModeFilter.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/Audio/SamplePlayer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/Audio/MixerEngine.cpp"
    # FX
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/Audio/FX/Slots/EffectSlot.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/Audio/FX/Slots/DelaySlot.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/Audio/FX/Slots/ReverbSlot.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/Audio/FX/Slots/FXChain.cpp"
    # UI — shared components
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/UI/Components/MuLookAndFeel.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/UI/Components/MuTheme.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/UI/Components/BipolarSliderRow.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/UI/Components/KnobWithLabel.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/UI/Components/SegmentControl.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/UI/Components/NudgeInput.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/UI/Components/DropdownSelect.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/UI/Components/StepEditor.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/UI/Components/LFOEditor.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/UI/Components/AddButton.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/UI/Components/StatusBar.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/UI/Components/VUMeter.cpp"
    # UI — shared mixer/FX panels
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/UI/FXRow.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/UI/DelayRow.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/UI/MixerChannel.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/UI/MixerChannel_Bindings.cpp"   # #408 partial TU
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/UI/MixerChannel_Insert.cpp"     # #408 partial TU
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/UI/MixerOverlay.cpp"
    # ProcessorBase
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/Plugin/ProcessorBase.cpp"
)

target_include_directories(mu-core INTERFACE
    "${CMAKE_SOURCE_DIR}/Source"
)

# ── mu-Clid — main plugin ────────────────────────────────────────────────────
juce_add_plugin(mu-clid
    COMPANY_NAME "Transwarp Development Project"
    BUNDLE_ID "com.transwarp.mu-clid"
    PLUGIN_MANUFACTURER_CODE TDP1
    PLUGIN_CODE MCld
    FORMATS VST3 Standalone
    PRODUCT_NAME "μ-Clid"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    ICON_BIG   "${CMAKE_SOURCE_DIR}/resources/Icon.png"
    ICON_SMALL "${CMAKE_SOURCE_DIR}/resources/Icon.png"
)


target_sources(mu-clid
    PRIVATE
        Source/Plugin/PluginProcessor.cpp
        Source/Plugin/PluginProcessor_APVTS.cpp
        Source/Plugin/PresetIO.cpp
        Source/Plugin/HotSwapStager.cpp
        Source/Plugin/SamplePreview.cpp
        Source/Plugin/PluginEditor.cpp
        Source/Persistence/MidiPresetMap.cpp
        Source/Sequencer/EuclideanGenerator.cpp
        Source/Sequencer/HitGenerator.cpp
        Source/Sequencer/Rhythm.cpp
        Source/Sequencer/SequencerEngine.cpp
        Source/Audio/MidiOutputEngine.cpp
        Source/UI/TransportBar.cpp
        Source/UI/RhythmCircle.cpp
        Source/UI/SidebarItem.cpp
        Source/UI/RhythmSidebar.cpp
        Source/UI/EuclideanPanel.cpp
        Source/UI/VoiceSection.cpp
        Source/UI/Voice/PitchSubsection.cpp
        Source/UI/Voice/FilterSubsection.cpp
        Source/UI/Voice/AmpSubsection.cpp
        Source/UI/Voice/InsertSubsection.cpp
        Source/UI/ModulatorEditor.cpp
        Source/UI/ModMatrixPanel.cpp
        Source/UI/ModulatorPanel.cpp
        Source/UI/RhythmPanel.cpp
        Source/UI/AboutPanel.cpp
        Source/UI/SaveDialog.cpp
        Source/UI/SampleBrowser.cpp
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
        mu-core
        mu-clid-resources
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

# Standalone-only: custom app class with close confirmation dialog (#167).
target_compile_definitions(mu-clid_Standalone PRIVATE JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1)
target_sources(mu-clid_Standalone PRIVATE Source/Plugin/StandaloneApp.cpp)

# Increment build_number.txt only on Release; Debug reads the same value so both configs share a number.
add_custom_target(increment_build_number ALL
    COMMAND ${CMAKE_COMMAND} -DBUILD_CONFIG=$<CONFIG> -P "${CMAKE_SOURCE_DIR}/cmake/IncrementBuildNumber.cmake"
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
    # #349 follow-up: depend on the actual format outputs, not just the shared-code
    # target. `mu-clid` is the JUCE shared lib; the installer needs the artifact
    # files produced by `mu-clid_VST3`, `mu-clid_CLAP`, and `mu-clid_Standalone`.
    # Without listing them explicitly, MSBuild can run the installer in parallel
    # with the Standalone build and fail because `mu-Clid.exe` doesn't exist yet.
    add_dependencies(mu-clid_installer mu-clid mu-clid_VST3 mu-clid_CLAP mu-clid_Standalone)
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
            "-DVST3_BUNDLE=${CMAKE_BINARY_DIR}/mu-clid_artefacts/$<CONFIG>/VST3/μ-Clid.vst3"
            "-DCLAP_FILE=${CMAKE_BINARY_DIR}/mu-clid_artefacts/$<CONFIG>/CLAP/μ-Clid.clap"
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
    PRODUCT_NAME "μ-Clid Lite"
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
        Source/Plugin/PluginProcessor.cpp
        Source/Plugin/PluginProcessor_APVTS.cpp
        Source/Plugin/PresetIO.cpp
        Source/Plugin/HotSwapStager.cpp
        Source/Plugin/SamplePreview.cpp
        Source/Plugin/LiteEditor.cpp
        Source/Persistence/MidiPresetMap.cpp
        Source/Sequencer/EuclideanGenerator.cpp
        Source/Sequencer/HitGenerator.cpp
        Source/Sequencer/Rhythm.cpp
        Source/Sequencer/SequencerEngine.cpp
        Source/Audio/MidiOutputEngine.cpp
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
        mu-core
        mu-clid-resources
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
    # #349 follow-up: depend on the format outputs explicitly (see comment above on
    # mu-clid_installer). Lite ships only VST3 + CLAP — no Standalone target.
    add_dependencies(mu-clid-lite_installer mu-clid-lite mu-clid-lite_VST3 mu-clid-lite_CLAP)
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
            "-DVST3_BUNDLE=${CMAKE_BINARY_DIR}/mu-clid-lite_artefacts/$<CONFIG>/VST3/μ-Clid Lite.vst3"
            "-DCLAP_FILE=${CMAKE_BINARY_DIR}/mu-clid-lite_artefacts/$<CONFIG>/CLAP/μ-Clid Lite.clap"
            "-DDIST_WIN=${MUCLID_WIN_DIST}"
            -P "${CMAKE_SOURCE_DIR}/cmake/DeployReleaseLite.cmake"
    COMMENT "Deploying mu-Clid Lite VST3 to OneDrive (Debug skipped)"
    VERBATIM
)
add_custom_command(TARGET mu-clid-lite_CLAP POST_BUILD
    COMMAND "${CMAKE_COMMAND}"
            "-DCONFIG=$<CONFIG>"
            "-DVST3_BUNDLE=${CMAKE_BINARY_DIR}/mu-clid-lite_artefacts/$<CONFIG>/VST3/μ-Clid Lite.vst3"
            "-DCLAP_FILE=${CMAKE_BINARY_DIR}/mu-clid-lite_artefacts/$<CONFIG>/CLAP/μ-Clid Lite.clap"
            "-DDIST_WIN=${MUCLID_WIN_DIST}"
            -P "${CMAKE_SOURCE_DIR}/cmake/DeployReleaseLite.cmake"
    COMMENT "Deploying mu-Clid Lite CLAP to OneDrive (Debug skipped)"
    VERBATIM
)

# ── mu-clid-tests — data-layer regression tests (#442) ───────────────────────
# Console executable that runs every juce::UnitTest defined in Source/Tests/.
# Built on-demand only: `cmake --build build --target mu-clid-tests`.
# Not part of the default ALL_BUILD, so day-to-day plugin development isn't
# slowed by test compilation. Run the resulting exe — non-zero exit = failures.
juce_add_console_app(mu-clid-tests
    PRODUCT_NAME "mu-clid-tests"
)

target_sources(mu-clid-tests PRIVATE
    Source/Tests/TestMain.cpp
    Source/Tests/RhythmParamRoundTripTests.cpp
    Source/Tests/KindedPropertyRoundTripTests.cpp
    Source/Tests/GlobalParamDefsTests.cpp
    Source/Tests/ModulatorSerialiseTests.cpp
    Source/Tests/AlgorithmNameTableTests.cpp
    Source/Modulation/ModulationMatrix.cpp
    Source/Sequencer/ControlSequence.cpp
)

target_compile_features(mu-clid-tests PRIVATE cxx_std_17)

target_include_directories(mu-clid-tests PRIVATE
    "${CMAKE_SOURCE_DIR}/Source"
)

target_link_libraries(mu-clid-tests PRIVATE
    juce::juce_core
    juce::juce_data_structures
    juce::juce_events
    juce::juce_audio_basics
    juce::juce_audio_processors
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

target_compile_definitions(mu-clid-tests PRIVATE
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
)

# Exclude from default build so the test target only builds when asked.
set_target_properties(mu-clid-tests PROPERTIES EXCLUDE_FROM_ALL TRUE)
