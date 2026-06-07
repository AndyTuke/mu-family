# mu-on — μ-family groove sequencer.
#
# A 909-style step sequencer with four fixed instrument channels: Kick (synth),
# Bass (deep synth), Hat (sample), Snare (sample). Built on the shared platform
# (ProcessorBase + MixerEngine + FXChain + the shared editor shell / sidebar /
# mixer); the bass↔kick side-chain ducking is the shared MixerEngine's
# channel-to-channel sidechain, so it's wiring + defaults, not new DSP.

# ── mu-On — Standalone + VST3 + CLAP (+ AU on macOS) ────────────────────────
juce_add_plugin(mu-on
    COMPANY_NAME "Transwarp Development Project"
    BUNDLE_ID "com.transwarp.mu-on"
    PLUGIN_MANUFACTURER_CODE TDP1
    PLUGIN_CODE MuOn
    FORMATS VST3 Standalone ${MUFAMILY_AU_FORMAT}   # AU on macOS only (family rule)
    PRODUCT_NAME "mu-On"
    PLUGIN_NAME  "μ-On"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VERSION         "${MUFAMILY_PLUGIN_VERSION}"
    COMPANY_COPYRIGHT "${MUFAMILY_COPYRIGHT}"
)

target_sources(mu-on
    PRIVATE
        Source/Plugin/PluginProcessor.cpp
        Source/Plugin/PluginEditor.cpp
)

target_include_directories(mu-on
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/Source"
    SYSTEM PRIVATE
        "${CMAKE_SOURCE_DIR}/ThirdParty/signalsmith-basics/include"
        "${CMAKE_SOURCE_DIR}/ThirdParty/signalsmith-dsp/include"
)

target_compile_definitions(mu-on
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(mu-on
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

# Custom standalone app — shared "Close μ-On?" confirmation on quit (mirrors
# mu-clid / mu-tant / mu-toni). Compiled only into the Standalone target.
target_compile_definitions(mu-on_Standalone PRIVATE JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1)
target_sources(mu-on_Standalone PRIVATE
    Source/Plugin/StandaloneApp.cpp)

# CLAP format via clap-juce-extensions (mirrors the family). Creates mu-on_CLAP.
clap_juce_extensions_plugin(TARGET mu-on
    CLAP_ID "com.transwarp.mu-on"
    CLAP_FEATURES instrument drum-machine sequencer)

# Family build-counter (shared increment target defined in mu-clid/CMakeLists.txt).
add_dependencies(mu-on increment_build_number)

# ── mu-on-tests — console UnitTests ─────────────────────────────────────────
# Built as part of ALL_BUILD (mirrors mu-tant-tests / mu-toni-tests: small + local).
juce_add_console_app(mu-on-tests PRODUCT_NAME "mu-on-tests")

target_sources(mu-on-tests PRIVATE
    Source/Tests/TestMain.cpp
    Source/Tests/ApvtsLayoutTests.cpp
)

target_compile_features(mu-on-tests PRIVATE cxx_std_17)

target_include_directories(mu-on-tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Source"
    "${CMAKE_SOURCE_DIR}/mu-core"
)

target_link_libraries(mu-on-tests PRIVATE
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

target_compile_definitions(mu-on-tests PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)

add_dependencies(mu-on mu-on-tests)
