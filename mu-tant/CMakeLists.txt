# mu-tant — wavetable drone synth (first-stab engine).
#
# Debug-only for now (not shipped to testers). Plugin-specific sources live under
# Source/; shared audio/UI/ProcessorBase come from mu-core via the link.

# ── mu-Tant — Standalone (first stab; VST3/CLAP added when it's worth shipping) ──
juce_add_plugin(mu-tant
    COMPANY_NAME "Transwarp Development Project"
    BUNDLE_ID "com.transwarp.mu-tant"
    PLUGIN_MANUFACTURER_CODE TDP1
    PLUGIN_CODE MTnt
    FORMATS Standalone
    PRODUCT_NAME "mu-Tant"
    PLUGIN_NAME  "μ-Tant"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
)

target_sources(mu-tant
    PRIVATE
        Source/Plugin/PluginProcessor.cpp
        Source/Plugin/PluginEditor.cpp
        Source/UI/VoicePanel.cpp
        Source/UI/VoiceSidebar.cpp
        Source/UI/GatingDesigner.cpp
        Source/Audio/WavetableBank.cpp
        Source/Audio/SynthVoice.cpp
)

target_include_directories(mu-tant
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/Source"
    SYSTEM PRIVATE
        "${CMAKE_SOURCE_DIR}/ThirdParty/signalsmith-basics/include"
        "${CMAKE_SOURCE_DIR}/ThirdParty/signalsmith-dsp/include"
)

target_compile_definitions(mu-tant
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(mu-tant
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

# Family build-counter: depend on the shared increment target (defined in
# mu-clid/CMakeLists.txt) so mu-tant builds also tick the session counter.
# Without this, building only mu-tant would reuse a stale BuildNumber.h.
add_dependencies(mu-tant increment_build_number)

# ── mu-tant-tests — console UnitTests over the new synth DSP ─────────────────
# Tests only mu-tant's self-contained DSP (Scales + WavetableOscillator +
# WavetableBank) — no mu-core deps needed. EXCLUDE_FROM_ALL like mu-clid-tests.
juce_add_console_app(mu-tant-tests PRODUCT_NAME "mu-tant-tests")

target_sources(mu-tant-tests PRIVATE
    Source/Tests/TestMain.cpp
    Source/Tests/SynthDSPTests.cpp
    Source/Tests/ModulatorTests.cpp
    Source/Tests/GatingDesignerTests.cpp
    Source/UI/GatingDesigner.cpp
    Source/Audio/WavetableBank.cpp
    # mu-core .cpps the modulator + gating tests depend on (the console-app
    # target doesn't share JUCE-plugin context, so it can't link mu-core directly).
    "${CMAKE_SOURCE_DIR}/mu-core/Modulation/ModulationMatrix.cpp"
    "${CMAKE_SOURCE_DIR}/mu-core/Sequencer/ControlSequence.cpp"
    "${CMAKE_SOURCE_DIR}/mu-core/UI/Components/DropdownSelect.cpp"
    "${CMAKE_SOURCE_DIR}/mu-core/UI/Components/MuLookAndFeel.cpp"
    "${CMAKE_SOURCE_DIR}/mu-core/UI/Components/MuTheme.cpp"
)

target_compile_features(mu-tant-tests PRIVATE cxx_std_17)

target_include_directories(mu-tant-tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Source"
    "${CMAKE_SOURCE_DIR}/mu-core"
)

target_link_libraries(mu-tant-tests PRIVATE
    juce::juce_core
    juce::juce_audio_basics
    juce::juce_data_structures
    juce::juce_events
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)

target_compile_definitions(mu-tant-tests PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)

# Built as part of ALL_BUILD from day 1 — mu-tant's DSP changes get caught
# every build, matching mu-clid's full-coverage discipline.
add_dependencies(mu-tant mu-tant-tests)
