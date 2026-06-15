# mu-toni — μ-family product scaffold.
#
# The synth engine + sequencer are not yet defined; this target brings the shared
# platform online (ProcessorBase + MixerEngine + FXChain + the shared editor shell
# / sidebar / mixer) with a blank EnginePanel where the product-specific UI lands.
# Shared audio/UI/ProcessorBase come from mu-core via the link.

# ── mu-Toni — Standalone + VST3 + CLAP ──────────────────────────────────────
juce_add_plugin(mu-toni
    COMPANY_NAME "Transwarp Development Project"
    BUNDLE_ID "com.transwarp.mu-toni"
    PLUGIN_MANUFACTURER_CODE TDP1
    PLUGIN_CODE MTni
    FORMATS VST3 Standalone ${MUFAMILY_AU_FORMAT}   # AU on macOS only (family rule)
    PRODUCT_NAME "mu-Toni"
    PLUGIN_NAME  "μ-Toni"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VERSION         "${MUFAMILY_PLUGIN_VERSION}"
    COMPANY_COPYRIGHT "${MUFAMILY_COPYRIGHT}"
)

target_sources(mu-toni
    PRIVATE
        Source/Plugin/PluginProcessor.cpp
        Source/Plugin/PluginEditor.cpp
        Source/UI/SettingsOverlay.cpp
)

target_include_directories(mu-toni
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/Source"
    SYSTEM PRIVATE
        "${CMAKE_SOURCE_DIR}/ThirdParty/signalsmith-basics/include"
        "${CMAKE_SOURCE_DIR}/ThirdParty/signalsmith-dsp/include"
)

target_compile_definitions(mu-toni
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(mu-toni
    PRIVATE
        mu-core
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

# Custom standalone app — shared "Close μ-Toni?" confirmation on quit (mirrors
# mu-clid / mu-tant). Compiled only into the Standalone target.
target_compile_definitions(mu-toni_Standalone PRIVATE JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1)
target_sources(mu-toni_Standalone PRIVATE
    Source/Plugin/StandaloneApp.cpp)

# CLAP format via clap-juce-extensions (mirrors mu-clid / mu-tant). Creates mu-toni_CLAP.
clap_juce_extensions_plugin(TARGET mu-toni
    CLAP_ID "com.transwarp.mu-toni"
    CLAP_FEATURES instrument synthesizer)

# Family build-counter (shared increment target defined in mu-clid/CMakeLists.txt).
add_dependencies(mu-toni increment_build_number)

# ── Deploy Release artifacts to the tester drop folder ──────────────────────
# Mirrors mu-tant's deploy pattern. Guarded by MUFAMILY_DEPLOY_TESTERS (OFF by default).
if(MUFAMILY_DEPLOY_TESTERS)
add_custom_command(TARGET mu-toni_Standalone POST_BUILD
    COMMAND "${CMAKE_COMMAND}"
            "-DCONFIG=$<CONFIG>"
            "-DSTANDALONE=$<TARGET_FILE:mu-toni_Standalone>"
            "-DVST3_BUNDLE=${CMAKE_BINARY_DIR}/mu-toni/mu-toni_artefacts/$<CONFIG>/VST3/mu-Toni.vst3"
            "-DCLAP_FILE=${CMAKE_BINARY_DIR}/mu-toni/mu-toni_artefacts/$<CONFIG>/CLAP/mu-Toni.clap"
            "-DDIST_WIN=${MUFAMILY_WIN_DIST}"
            "-DPRODUCT_FILENAME=mu-Toni"
            -P "${CMAKE_SOURCE_DIR}/cmake/DeployRelease.cmake"
    COMMENT "Deploying mu-Toni Release artifacts to OneDrive (Debug skipped)"
    VERBATIM
)
endif()

# ── mu-toni-tests — console UnitTests ───────────────────────────────────────
# Built as part of ALL_BUILD (mirrors mu-tant-tests: small + local, not a tester
# deploy, so building every build is cheap — the speed concession that keeps
# mu-clid-tests EXCLUDE_FROM_ALL doesn't apply here).
juce_add_console_app(mu-toni-tests PRODUCT_NAME "mu-toni-tests")

target_sources(mu-toni-tests PRIVATE
    Source/Tests/TestMain.cpp
    Source/Tests/ApvtsLayoutTests.cpp
)

target_compile_features(mu-toni-tests PRIVATE cxx_std_17)

target_include_directories(mu-toni-tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Source"
    "${CMAKE_SOURCE_DIR}/mu-core"
)

target_link_libraries(mu-toni-tests PRIVATE
    juce::juce_core
    juce::juce_audio_basics
    juce::juce_audio_processors
    juce::juce_data_structures
    juce::juce_events
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)

target_compile_definitions(mu-toni-tests PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)

add_dependencies(mu-toni mu-toni-tests)
