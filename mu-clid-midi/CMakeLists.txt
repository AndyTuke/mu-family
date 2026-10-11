# mu-Clid MIDI — single-rhythm Euclidean MIDI effect (VST3 + CLAP, no audio, no Standalone).
#
# The former mu-Clid Lite, now its own product. Plugin IDs are unchanged from Lite (PLUGIN_CODE MCLt,
# BUNDLE_ID / CLAP_ID com.transwarp.mu-clid-lite, state id MuClidState) so existing DAW projects keep
# finding it; only the folder, target and display name are new.
#
# Known exception: Rhythm, SequencerEngine, EuclideanPanel and RhythmCircle still live in mu-clid/
# (they move to mu-core with the layer container) and are compiled here from there. See CLAUDE.md.

set(MU_CLID_SOURCE "${CMAKE_SOURCE_DIR}/mu-clid/Source")

# ── mu-Clid MIDI — VST3 + CLAP (+ AU on macOS) ──────────────────────────────
juce_add_plugin(mu-clid-midi
    COMPANY_NAME "Transwarp Development Project"
    BUNDLE_ID "com.transwarp.mu-clid-lite"
    PLUGIN_MANUFACTURER_CODE TDP1
    PLUGIN_CODE MCLt
    FORMATS VST3 ${MUFAMILY_AU_FORMAT}   # AU on macOS only (family rule); no Standalone
    PRODUCT_NAME "mu-Clid MIDI"
    PLUGIN_NAME  "μ-Clid MIDI"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT TRUE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VERSION         "${MUFAMILY_PLUGIN_VERSION}"
    COMPANY_COPYRIGHT "${MUFAMILY_COPYRIGHT}"
    ICON_BIG   "${CMAKE_CURRENT_SOURCE_DIR}/resources/IconLite.png"
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/resources/IconLite.png"
)

# Custom VERSIONINFO with the Greek mu. Windows-only: the .rc and the JUCE `<target>_rc_lib` target
# exist only there. The increment target (defined in mu-clid/CMakeLists.txt) regenerates it each build.
if(WIN32)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/versioninfo.rc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/versioninfo.rc"
        @ONLY
        NEWLINE_STYLE CRLF
    )
    target_compile_definitions(mu-clid-midi        PRIVATE "JUCE_USER_DEFINED_RC_FILE=\"${CMAKE_CURRENT_BINARY_DIR}/versioninfo.rc\"")
    target_compile_definitions(mu-clid-midi_rc_lib PRIVATE "JUCE_USER_DEFINED_RC_FILE=\"${CMAKE_CURRENT_BINARY_DIR}/versioninfo.rc\"")
    # Compile the freshly regenerated .rc (post-increment number).
    add_dependencies(mu-clid-midi_rc_lib increment_build_number)
endif()

target_sources(mu-clid-midi
    PRIVATE
        Source/Plugin/PluginProcessor.cpp
        Source/Sequencer/MidiRhythmCore.cpp
        Source/Plugin/PluginEditor.cpp
        Source/Persistence/SessionState.cpp
        # Shared with mu-Clid until Rhythm / SequencerEngine move to mu-core.
        "${MU_CLID_SOURCE}/Sequencer/Rhythm.cpp"
        "${MU_CLID_SOURCE}/Sequencer/SequencerEngine.cpp"
        "${MU_CLID_SOURCE}/UI/EuclideanPanel.cpp"
        "${MU_CLID_SOURCE}/UI/RhythmCircle.cpp"
)

# This product's Source/ first, so its own Plugin/PluginProcessor.h wins over mu-Clid's of the same
# name; mu-Clid's Source/ is on the path only for the shared headers above.
target_include_directories(mu-clid-midi
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/Source"
        "${MU_CLID_SOURCE}"
    SYSTEM PRIVATE
        "${CMAKE_SOURCE_DIR}/ThirdParty/signalsmith-basics/include"
        "${CMAKE_SOURCE_DIR}/ThirdParty/signalsmith-dsp/include"
)

target_compile_definitions(mu-clid-midi
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_USE_MP3AUDIOFORMAT=1
)

target_link_libraries(mu-clid-midi
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

# Family build counter (the shared increment target is defined in mu-clid/CMakeLists.txt).
add_dependencies(mu-clid-midi increment_build_number)

clap_juce_extensions_plugin(TARGET mu-clid-midi
    CLAP_ID "com.transwarp.mu-clid-lite"
    CLAP_FEATURES midi-effect)

# ── Installer (Release only, requires Inno Setup) ───────────────────────────
if(ISCC_EXE)
    add_custom_target(mu-clid-midi_installer ALL
        COMMAND "${CMAKE_COMMAND}"
                "-DISCC_EXE=${ISCC_EXE}"
                "-DSOURCE_DIR=${CMAKE_SOURCE_DIR}"
                "-DBUILD_DIR=${CMAKE_BINARY_DIR}"
                "-DCMAKE_COMMAND=${CMAKE_COMMAND}"
                -P "${CMAKE_SOURCE_DIR}/cmake/BuildInstallerMidi.cmake"
        COMMENT "Building mu-Clid MIDI installer"
        VERBATIM
    )
    add_dependencies(mu-clid-midi_installer mu-clid-midi mu-clid-midi_VST3 mu-clid-midi_CLAP)
endif()

# ── Deploy Release artifacts to the tester drop folder ──────────────────────
# Guarded by MUFAMILY_DEPLOY_TESTERS (OFF by default) so no build pushes to the tester share
# unless explicitly opted in. See root CMakeLists.txt.
if(MUFAMILY_DEPLOY_TESTERS)
add_custom_command(TARGET mu-clid-midi_VST3 POST_BUILD
    COMMAND "${CMAKE_COMMAND}"
            "-DCONFIG=$<CONFIG>"
            "-DVST3_BUNDLE=${CMAKE_BINARY_DIR}/mu-clid-midi/mu-clid-midi_artefacts/$<CONFIG>/VST3/mu-Clid MIDI.vst3"
            "-DCLAP_FILE=${CMAKE_BINARY_DIR}/mu-clid-midi/mu-clid-midi_artefacts/$<CONFIG>/CLAP/mu-Clid MIDI.clap"
            "-DDIST_WIN=${MUFAMILY_WIN_DIST}"
            -P "${CMAKE_SOURCE_DIR}/cmake/DeployReleaseMidi.cmake"
    COMMENT "Deploying mu-Clid MIDI VST3 to OneDrive (Debug skipped)"
    VERBATIM
)
add_custom_command(TARGET mu-clid-midi_CLAP POST_BUILD
    COMMAND "${CMAKE_COMMAND}"
            "-DCONFIG=$<CONFIG>"
            "-DVST3_BUNDLE=${CMAKE_BINARY_DIR}/mu-clid-midi/mu-clid-midi_artefacts/$<CONFIG>/VST3/mu-Clid MIDI.vst3"
            "-DCLAP_FILE=${CMAKE_BINARY_DIR}/mu-clid-midi/mu-clid-midi_artefacts/$<CONFIG>/CLAP/mu-Clid MIDI.clap"
            "-DDIST_WIN=${MUFAMILY_WIN_DIST}"
            -P "${CMAKE_SOURCE_DIR}/cmake/DeployReleaseMidi.cmake"
    COMMENT "Deploying mu-Clid MIDI CLAP to OneDrive (Debug skipped)"
    VERBATIM
)
endif()

# Package the Release zip (VST3 + CLAP, no Standalone). A dedicated custom target (rather than
# POST_BUILD) lets us declare explicit deps on both format targets.
add_custom_target(mu-clid-midi-zip ALL
    COMMAND "${CMAKE_COMMAND}"
            "-DCONFIG=$<CONFIG>"
            "-DPRODUCT=mu-Clid-MIDI"
            "-DVST3_BUNDLE=${CMAKE_BINARY_DIR}/mu-clid-midi/mu-clid-midi_artefacts/$<CONFIG>/VST3/mu-Clid MIDI.vst3"
            "-DCLAP_FILE=${CMAKE_BINARY_DIR}/mu-clid-midi/mu-clid-midi_artefacts/$<CONFIG>/CLAP/mu-Clid MIDI.clap"
            "-DDIST_DIR=${CMAKE_BINARY_DIR}/dist"
            "-DSOURCE_DIR=${CMAKE_SOURCE_DIR}"
            -P "${CMAKE_SOURCE_DIR}/cmake/PackageRelease.cmake"
    COMMENT "Packaging mu-Clid MIDI Release zip"
    VERBATIM
)
add_dependencies(mu-clid-midi-zip mu-clid-midi_VST3 mu-clid-midi_CLAP increment_build_number)

# ── mu-clid-midi-tests — the product's own unit tests ───────────────────────
# Built on demand only (not in the default ALL build), like mu-clid-tests.
juce_add_console_app(mu-clid-midi-tests
    PRODUCT_NAME "mu-clid-midi-tests"
)

target_sources(mu-clid-midi-tests PRIVATE
    Source/Tests/TestMain.cpp
    Source/Tests/AccentVelocityTests.cpp
    Source/Tests/ParameterLayoutTests.cpp
    Source/Tests/SessionStateTests.cpp
    Source/Tests/MidiRenderTests.cpp
    Source/Persistence/SessionState.cpp
    # The audio path under test, as the plugin compiles it.
    Source/Sequencer/MidiRhythmCore.cpp
    "${MU_CLID_SOURCE}/Sequencer/Rhythm.cpp"
    "${MU_CLID_SOURCE}/Sequencer/SequencerEngine.cpp"
    "${CMAKE_SOURCE_DIR}/mu-core/Audio/MidiOutputEngine.cpp"
    "${CMAKE_SOURCE_DIR}/mu-core/Sequencer/HitGenerator.cpp"
    "${CMAKE_SOURCE_DIR}/mu-core/Sequencer/EuclideanGenerator.cpp"
    "${CMAKE_SOURCE_DIR}/mu-core/Modulation/ModulationMatrix.cpp"   # pulled in by Layer.h
    "${CMAKE_SOURCE_DIR}/mu-core/Sequencer/ControlSequence.cpp"     # pulled in by Layer.h
)

target_compile_features(mu-clid-midi-tests PRIVATE cxx_std_17)

target_include_directories(mu-clid-midi-tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Source"
    "${MU_CLID_SOURCE}"
    "${CMAKE_SOURCE_DIR}/mu-core"
)

target_link_libraries(mu-clid-midi-tests PRIVATE
    juce::juce_core
    juce::juce_data_structures
    juce::juce_events
    juce::juce_audio_basics
    juce::juce_audio_processors
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

target_compile_definitions(mu-clid-midi-tests PRIVATE
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
)

set_target_properties(mu-clid-midi-tests PROPERTIES EXCLUDE_FROM_ALL TRUE)
