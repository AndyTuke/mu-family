# mu-Clid — Euclidean rhythm sequencer + sample trigger plugin.
#
# Plugin-specific sources live under Source/. Shared audio/UI infrastructure
# is linked in via mu-core (defined in the family-root CMakeLists).

# Binary resources (logo PNG). Both mu-clid and mu-clid-lite share this target
# since Lite is a build variant that keeps the same visual identity.
juce_add_binary_data(mu-clid-resources
    SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/mu-clid.png"
)

# ── mu-Clid — full plugin (VST3 + CLAP + Standalone) ────────────────────────
juce_add_plugin(mu-clid
    COMPANY_NAME "Transwarp Development Project"
    BUNDLE_ID "com.transwarp.mu-clid"
    PLUGIN_MANUFACTURER_CODE TDP1
    PLUGIN_CODE MCld
    FORMATS VST3 Standalone
    PRODUCT_NAME "mu-Clid"
    PLUGIN_NAME  "μ-Clid"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    ICON_BIG   "${CMAKE_CURRENT_SOURCE_DIR}/resources/Icon.png"
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/resources/Icon.png"
)

target_sources(mu-clid
    PRIVATE
        Source/Plugin/PluginProcessor.cpp
        Source/Plugin/PluginProcessor_APVTS.cpp
        Source/Plugin/PresetIO.cpp
        Source/Plugin/HotSwapStager.cpp
        Source/Plugin/SamplePreview.cpp
        Source/Plugin/PluginEditor.cpp
        Source/Sequencer/EuclideanGenerator.cpp
        Source/Sequencer/HitGenerator.cpp
        Source/Sequencer/Rhythm.cpp
        Source/Sequencer/SequencerEngine.cpp
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
        Source/UI/SettingsOverlay.cpp
        Source/License/LicenseChecker.cpp
        "${CMAKE_SOURCE_DIR}/ThirdParty/monocypher/monocypher.c"
)

# Plugin-local Source/ is on the include path so `#include "Sequencer/Rhythm.h"`
# resolves locally; mu-core adds its own include directory via the link.
target_include_directories(mu-clid
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/Source"
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

# Standalone-only: custom app class with close confirmation dialog (#167)
# plus headless --render CLI for the listening-test pipeline (#649).
target_compile_definitions(mu-clid_Standalone PRIVATE JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1)
target_sources(mu-clid_Standalone PRIVATE
    Source/Plugin/StandaloneApp.cpp
    Source/Plugin/RenderMode.cpp
)

# ── Build-number increment (Release only) ────────────────────────────────────
# Header lands at mu-clid/Source/BuildNumber.h; AboutPanel reads it for display.
# Counter file lives at the family root so all family plugins share the build
# number space — keeps tester deliveries time-aligned across the suite.
add_custom_target(increment_build_number ALL
    COMMAND ${CMAKE_COMMAND} -DBUILD_CONFIG=$<CONFIG> -P "${CMAKE_SOURCE_DIR}/cmake/IncrementBuildNumber.cmake"
    COMMENT "Incrementing build number"
    VERBATIM
)
add_dependencies(mu-clid increment_build_number)

# ── Installer (Release only, requires Inno Setup) ───────────────────────────
if(ISCC_EXE)
    add_custom_target(mu-clid_installer ALL
        COMMAND "${CMAKE_COMMAND}"
                "-DISCC_EXE=${ISCC_EXE}"
                "-DSOURCE_DIR=${CMAKE_SOURCE_DIR}"
                "-DDIST_DIR=${MUFAMILY_INSTALLER_DIST}"
                "-DCMAKE_COMMAND=${CMAKE_COMMAND}"
                -P "${CMAKE_SOURCE_DIR}/cmake/BuildInstaller.cmake"
        COMMENT "Building mu-Clid installer"
        VERBATIM
    )
    add_dependencies(mu-clid_installer mu-clid mu-clid_VST3 mu-clid_CLAP mu-clid_Standalone)
endif()

# ── Deploy Release artifacts to the tester drop folder ──────────────────────
add_custom_command(TARGET mu-clid_Standalone POST_BUILD
    COMMAND "${CMAKE_COMMAND}"
            "-DCONFIG=$<CONFIG>"
            "-DSTANDALONE=$<TARGET_FILE:mu-clid_Standalone>"
            "-DVST3_BUNDLE=${CMAKE_BINARY_DIR}/mu-clid/mu-clid_artefacts/$<CONFIG>/VST3/mu-Clid.vst3"
            "-DCLAP_FILE=${CMAKE_BINARY_DIR}/mu-clid/mu-clid_artefacts/$<CONFIG>/CLAP/mu-Clid.clap"
            "-DDIST_WIN=${MUFAMILY_WIN_DIST}"
            -P "${CMAKE_SOURCE_DIR}/cmake/DeployRelease.cmake"
    COMMENT "Deploying Release artifacts to OneDrive (Debug skipped)"
    VERBATIM
)

clap_juce_extensions_plugin(TARGET mu-clid
    CLAP_ID "com.transwarp.mu-clid"
    CLAP_FEATURES instrument sampler drum-machine)

# Keep deployed formats version-matched. The OneDrive deploy is a POST_BUILD on
# mu-clid_Standalone; without this, building the Standalone could deploy before
# the VST3/CLAP finished building (build-order race), shipping a stale plugin at
# an older build number. Forcing the Standalone to depend on VST3 + CLAP means
# they're always built first, so a deploy ships a matched set. (Placed after the
# CLAP target is created.) Trade-off: also rebuilds VST3/CLAP on a Standalone-only
# build — negligible for the usual full `cmake --build` (ALL_BUILD) flow.
add_dependencies(mu-clid_Standalone mu-clid_VST3 mu-clid_CLAP)


# ── mu-Clid Lite — single-rhythm MIDI-effect VST3/CLAP ──────────────────────
juce_add_plugin(mu-clid-lite
    COMPANY_NAME "Transwarp Development Project"
    BUNDLE_ID "com.transwarp.mu-clid-lite"
    PLUGIN_MANUFACTURER_CODE TDP1
    PLUGIN_CODE MCLt
    FORMATS VST3
    PRODUCT_NAME "mu-Clid Lite"
    PLUGIN_NAME  "μ-Clid Lite"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT TRUE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    ICON_BIG   "${CMAKE_CURRENT_SOURCE_DIR}/resources/Icon.png"
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/resources/Icon.png"
)

target_sources(mu-clid-lite
    PRIVATE
        Source/Plugin/PluginProcessor.cpp
        Source/Plugin/PluginProcessor_APVTS.cpp
        Source/Plugin/PresetIO.cpp
        Source/Plugin/HotSwapStager.cpp
        Source/Plugin/SamplePreview.cpp
        Source/Plugin/LiteEditor.cpp
        Source/Sequencer/EuclideanGenerator.cpp
        Source/Sequencer/HitGenerator.cpp
        Source/Sequencer/Rhythm.cpp
        Source/Sequencer/SequencerEngine.cpp
        Source/UI/TransportBar.cpp
        Source/UI/RhythmCircle.cpp
        Source/UI/EuclideanPanel.cpp
        Source/UI/AboutPanel.cpp
        Source/License/LicenseChecker.cpp
        "${CMAKE_SOURCE_DIR}/ThirdParty/monocypher/monocypher.c"
)

target_include_directories(mu-clid-lite
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/Source"
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
    add_dependencies(mu-clid-lite_installer mu-clid-lite mu-clid-lite_VST3 mu-clid-lite_CLAP)
endif()

clap_juce_extensions_plugin(TARGET mu-clid-lite
    CLAP_ID "com.transwarp.mu-clid-lite"
    CLAP_FEATURES midi-effect)

add_custom_command(TARGET mu-clid-lite_VST3 POST_BUILD
    COMMAND "${CMAKE_COMMAND}"
            "-DCONFIG=$<CONFIG>"
            "-DVST3_BUNDLE=${CMAKE_BINARY_DIR}/mu-clid/mu-clid-lite_artefacts/$<CONFIG>/VST3/mu-Clid Lite.vst3"
            "-DCLAP_FILE=${CMAKE_BINARY_DIR}/mu-clid/mu-clid-lite_artefacts/$<CONFIG>/CLAP/mu-Clid Lite.clap"
            "-DDIST_WIN=${MUFAMILY_WIN_DIST}"
            -P "${CMAKE_SOURCE_DIR}/cmake/DeployReleaseLite.cmake"
    COMMENT "Deploying mu-Clid Lite VST3 to OneDrive (Debug skipped)"
    VERBATIM
)
add_custom_command(TARGET mu-clid-lite_CLAP POST_BUILD
    COMMAND "${CMAKE_COMMAND}"
            "-DCONFIG=$<CONFIG>"
            "-DVST3_BUNDLE=${CMAKE_BINARY_DIR}/mu-clid/mu-clid-lite_artefacts/$<CONFIG>/VST3/mu-Clid Lite.vst3"
            "-DCLAP_FILE=${CMAKE_BINARY_DIR}/mu-clid/mu-clid-lite_artefacts/$<CONFIG>/CLAP/mu-Clid Lite.clap"
            "-DDIST_WIN=${MUFAMILY_WIN_DIST}"
            -P "${CMAKE_SOURCE_DIR}/cmake/DeployReleaseLite.cmake"
    COMMENT "Deploying mu-Clid Lite CLAP to OneDrive (Debug skipped)"
    VERBATIM
)

# ── mu-clid-tests — data-layer regression tests (#442) ──────────────────────
# Console executable that runs every juce::UnitTest defined in Source/Tests/.
# Built on-demand only — not in the default ALL build.
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
    Source/Tests/HitGeneratorTests.cpp
    Source/Tests/ModulationMatrixTests.cpp
    Source/Tests/ApvtsLayoutTests.cpp
    Source/Tests/InsertDSPSmokeTests.cpp
    Source/Tests/FilterDSPSmokeTests.cpp
    Source/Tests/RhythmParamBoundaryTests.cpp
    Source/Tests/UIScalingConsistencyTests.cpp
    Source/Tests/SendFXSmokeTests.cpp
    Source/Tests/PresetXMLRoundTripTests.cpp
    Source/Tests/InsertAlgoTableTests.cpp
    Source/Sequencer/HitGenerator.cpp
    Source/Sequencer/EuclideanGenerator.cpp
    # Pull in mu-core .cpp's the tests need (they can't link mu-core directly
    # because the INTERFACE library propagates sources via target_sources, and
    # the console-app target doesn't share JUCE-plugin context).
    "${CMAKE_SOURCE_DIR}/mu-core/Audio/FX/Slots/EffectSlot.cpp"
    "${CMAKE_SOURCE_DIR}/mu-core/Audio/FX/Slots/DelaySlot.cpp"
    "${CMAKE_SOURCE_DIR}/mu-core/Audio/FX/Slots/ReverbSlot.cpp"
    "${CMAKE_SOURCE_DIR}/mu-core/Modulation/ModulationMatrix.cpp"
    "${CMAKE_SOURCE_DIR}/mu-core/Sequencer/ControlSequence.cpp"
)

target_compile_features(mu-clid-tests PRIVATE cxx_std_17)

target_include_directories(mu-clid-tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Source"
    "${CMAKE_SOURCE_DIR}/mu-core"
    "${CMAKE_SOURCE_DIR}/ThirdParty/signalsmith-basics/include"
    "${CMAKE_SOURCE_DIR}/ThirdParty/signalsmith-dsp/include"
)

target_link_libraries(mu-clid-tests PRIVATE
    juce::juce_core
    juce::juce_data_structures
    juce::juce_events
    juce::juce_audio_basics
    juce::juce_audio_processors
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

target_compile_definitions(mu-clid-tests PRIVATE
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
)

set_target_properties(mu-clid-tests PROPERTIES EXCLUDE_FROM_ALL TRUE)
