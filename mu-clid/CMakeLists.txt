# mu-Clid — Euclidean rhythm sequencer + sample trigger plugin.
#
# Plugin-specific sources live under Source/. Shared audio/UI infrastructure
# is linked in via mu-core (defined in the family-root CMakeLists).

# Binary resources (logo PNG).
juce_add_binary_data(mu-clid-resources
    SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/mu-clid.png"
)

# ── mu-Clid — full plugin (VST3 + CLAP + Standalone) ────────────────────────
juce_add_plugin(mu-clid
    COMPANY_NAME "Transwarp Development Project"
    BUNDLE_ID "com.transwarp.mu-clid"
    PLUGIN_MANUFACTURER_CODE TDP1
    PLUGIN_CODE MCld
    FORMATS VST3 Standalone ${MUFAMILY_AU_FORMAT}   # AU on macOS only (family rule)
    PRODUCT_NAME "mu-Clid"
    PLUGIN_NAME  "μ-Clid"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VERSION         "${MUFAMILY_PLUGIN_VERSION}"
    COMPANY_COPYRIGHT "${MUFAMILY_COPYRIGHT}"
    ICON_BIG   "${CMAKE_CURRENT_SOURCE_DIR}/resources/Icon.png"
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/resources/Icon.png"
)

# Custom VERSIONINFO with Greek μ in description. Windows-only —
# the .rc + the JUCE `<target>_rc_lib` resource target only exist on Windows.
if(WIN32)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/versioninfo.rc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/versioninfo.rc"
        @ONLY
        NEWLINE_STYLE CRLF
    )
    target_compile_definitions(mu-clid        PRIVATE "JUCE_USER_DEFINED_RC_FILE=\"${CMAKE_CURRENT_BINARY_DIR}/versioninfo.rc\"")
    target_compile_definitions(mu-clid_rc_lib PRIVATE "JUCE_USER_DEFINED_RC_FILE=\"${CMAKE_CURRENT_BINARY_DIR}/versioninfo.rc\"")
endif()

target_sources(mu-clid
    PRIVATE
        Source/Plugin/PluginProcessor.cpp
        Source/Plugin/PluginProcessor_APVTS.cpp
        Source/Plugin/PluginProcessor_Modulation.cpp
        Source/Sequencer/RhythmManager.cpp
        Source/Plugin/PluginProcessor_Content.cpp
        Source/Persistence/PresetIO.cpp
        Source/Persistence/PresetIO_HostState.cpp
        Source/Persistence/PresetMigrations.cpp
        Source/Plugin/HotSwapStager.cpp
        Source/Audio/SamplePreview.cpp
        Source/Audio/SampleLibrary.cpp
        Source/Plugin/PluginEditor.cpp
        Source/UI/RhythmMiniVisual.cpp
        Source/UI/RhythmSidebar.cpp
        Source/UI/VoiceSection.cpp
        Source/UI/Voice/PitchSubsection.cpp
        Source/UI/Voice/FilterSubsection.cpp
        Source/UI/Voice/AmpSubsection.cpp
        Source/UI/RhythmPanel.cpp
        Source/UI/SampleBrowser.cpp
        Source/UI/SettingsOverlay.cpp
        # Offline-license verifier lives in mu-core (shared); compiled here + monocypher.c.
        "${CMAKE_SOURCE_DIR}/mu-core/License/LicenseManager.cpp"
        "${CMAKE_SOURCE_DIR}/mu-core/License/LemonSqueezyClient.cpp"
        "${CMAKE_SOURCE_DIR}/mu-core/License/OnlineActivation.cpp"
        "${CMAKE_SOURCE_DIR}/ThirdParty/monocypher/monocypher.c"
)

# monocypher is vendored third-party crypto. Silence MSVC C4244 (a benign u64→u32
# narrowing in its Argon2 block-index math — the value is bounded well within u32)
# at the build level rather than patching upstream source, so our /W4 build stays
# warning-clean without diverging from the vendor file.
if(MSVC)
    set_source_files_properties(
        "${CMAKE_SOURCE_DIR}/ThirdParty/monocypher/monocypher.c"
        PROPERTIES COMPILE_OPTIONS "/wd4244")
endif()

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

# Standalone-only: custom app class with close confirmation dialog plus headless
# --render CLI for the listening-test pipeline. The mu-link bridge is now
# the shared header-only mu-core/Link/MuLinkBridge.h — included ONLY by StandaloneApp.cpp, so
# VST3/CLAP never compile it and a plugin can never attach.
target_compile_definitions(mu-clid_Standalone PRIVATE JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1)
target_sources(mu-clid_Standalone PRIVATE
    Source/Plugin/StandaloneApp.cpp
)

# ── Build-number increment (runs every build; Debug bumps, Release reuses) ────
# Writes mu-core/BuildNumber.h (About panel) AND regenerates the Windows version
# resources from the same post-increment number, so file properties == About panel.
# Counter file + header live in family-shared space so every product shares one number.
add_custom_target(increment_build_number ALL
    COMMAND ${CMAKE_COMMAND} -DBUILD_CONFIG=$<CONFIG>
            "-DMUFAMILY_COPYRIGHT=${MUFAMILY_COPYRIGHT}"
            "-DRC1_IN=${CMAKE_SOURCE_DIR}/mu-clid/resources/versioninfo.rc.in"
            "-DRC1_OUT=${CMAKE_BINARY_DIR}/mu-clid/versioninfo.rc"
            "-DRC2_IN=${CMAKE_SOURCE_DIR}/mu-clid-midi/resources/versioninfo.rc.in"
            "-DRC2_OUT=${CMAKE_BINARY_DIR}/mu-clid-midi/versioninfo.rc"
            "-DRC3_IN=${CMAKE_SOURCE_DIR}/mu-tant/resources/versioninfo.rc.in"
            "-DRC3_OUT=${CMAKE_BINARY_DIR}/mu-tant/versioninfo.rc"
            "-DRC4_IN=${CMAKE_SOURCE_DIR}/mu-toni/resources/versioninfo.rc.in"
            "-DRC4_OUT=${CMAKE_BINARY_DIR}/mu-toni/versioninfo.rc"
            "-DRC5_IN=${CMAKE_SOURCE_DIR}/mu-on/resources/versioninfo.rc.in"
            "-DRC5_OUT=${CMAKE_BINARY_DIR}/mu-on/versioninfo.rc"
            "-DRC6_IN=${CMAKE_SOURCE_DIR}/mu-link/resources/versioninfo.rc.in"
            "-DRC6_OUT=${CMAKE_BINARY_DIR}/mu-link/versioninfo.rc"
            -P "${CMAKE_SOURCE_DIR}/cmake/IncrementBuildNumber.cmake"
    COMMENT "Incrementing build number + regenerating version resources"
    VERBATIM
)
add_dependencies(mu-clid increment_build_number)
# The resource lib must compile the FRESHLY regenerated .rc (post-increment number),
# so depend on the increment target — otherwise Windows file properties lag by one on Debug.
if(WIN32)
    add_dependencies(mu-clid_rc_lib increment_build_number)
endif()

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
# Guarded by MUFAMILY_DEPLOY_TESTERS (OFF by default) so no build pushes to the
# tester share unless explicitly opted in. See root CMakeLists.txt.
if(MUFAMILY_DEPLOY_TESTERS)
add_custom_command(TARGET mu-clid_Standalone POST_BUILD
    COMMAND "${CMAKE_COMMAND}"
            "-DCONFIG=$<CONFIG>"
            "-DSTANDALONE=$<TARGET_FILE:mu-clid_Standalone>"
            "-DVST3_BUNDLE=${CMAKE_BINARY_DIR}/mu-clid/mu-clid_artefacts/$<CONFIG>/VST3/mu-Clid.vst3"
            "-DCLAP_FILE=${CMAKE_BINARY_DIR}/mu-clid/mu-clid_artefacts/$<CONFIG>/CLAP/mu-Clid.clap"
            "-DDIST_WIN=${MUFAMILY_WIN_DIST}"
            "-DPRODUCT_FILENAME=mu-Clid"
            -P "${CMAKE_SOURCE_DIR}/cmake/DeployRelease.cmake"
    COMMENT "Deploying mu-Clid Release artifacts to OneDrive (Debug skipped)"
    VERBATIM
)
endif()

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

# Package mu-Clid Release zip in build/dist/ (Release only; script skips Debug silently).
add_custom_command(TARGET mu-clid_Standalone POST_BUILD
    COMMAND "${CMAKE_COMMAND}"
            "-DCONFIG=$<CONFIG>"
            "-DPRODUCT=mu-Clid"
            "-DVST3_BUNDLE=${CMAKE_BINARY_DIR}/mu-clid/mu-clid_artefacts/$<CONFIG>/VST3/mu-Clid.vst3"
            "-DCLAP_FILE=${CMAKE_BINARY_DIR}/mu-clid/mu-clid_artefacts/$<CONFIG>/CLAP/mu-Clid.clap"
            "-DSTANDALONE=$<TARGET_FILE:mu-clid_Standalone>"
            "-DDIST_DIR=${CMAKE_BINARY_DIR}/dist"
            "-DSOURCE_DIR=${CMAKE_SOURCE_DIR}"
            -P "${CMAKE_SOURCE_DIR}/cmake/PackageRelease.cmake"
    COMMENT "Packaging mu-Clid Release zip"
    VERBATIM
)


# ── mu-clid-tests — data-layer regression tests ────────────────────────────
# Console executable that runs every juce::UnitTest defined in Source/Tests/.
# Built on-demand only — not in the default ALL build.
include("${CMAKE_SOURCE_DIR}/mu-core/cmake/ProcessorTestSources.cmake")
juce_add_console_app(mu-clid-tests
    PRODUCT_NAME "mu-clid-tests"
)

target_sources(mu-clid-tests PRIVATE
    Source/Tests/TestMain.cpp
    Source/Tests/RhythmParamRoundTripTests.cpp
    Source/Tests/KindedPropertyRoundTripTests.cpp
    Source/Tests/GlobalParamDefsTests.cpp
    Source/Tests/ModulatorSerialiseTests.cpp
    Source/Tests/InsertAlgoTableTests.cpp
    Source/Tests/HitGeneratorTests.cpp
    Source/Tests/ApvtsLayoutTests.cpp
    Source/Tests/RhythmParamBoundaryTests.cpp
    Source/Tests/PresetXMLRoundTripTests.cpp
    Source/Tests/PresetMigrationTests.cpp      # C1 + C2 (format migrations)
    Source/Tests/HotSwapBoundaryTests.cpp      # C3 (swap loop-boundary predicates)
    Source/Tests/ModulationSkewTests.cpp       # C5 (proportion-space skew round-trips)
    Source/Tests/NoDemoCapsTests.cpp          # unlicensed is fully functional: 8 layers, 64 steps, saves
    Source/Tests/EmbeddedSampleTests.cpp       # preset samples: FLAC round trip is sample-identical
    Source/Tests/SequencerEngineWrapTests.cpp  # master-loop wrap detection
    Source/Tests/SwapCommitTimingTests.cpp     # a delayed swap commit still starts the new pattern on the next step
    "${CMAKE_SOURCE_DIR}/mu-core/Sequencer/SequencerEngine.cpp"        # under test by the wrap tests
    Source/Persistence/PresetMigrations.cpp    # under test by C1/C2
    "${CMAKE_SOURCE_DIR}/mu-core/Sequencer/Rhythm.cpp"                 # SequencerEngine builds patterns through it
    "${CMAKE_SOURCE_DIR}/mu-core/Sequencer/HitGenerator.cpp"
    "${CMAKE_SOURCE_DIR}/mu-core/Sequencer/EuclideanGenerator.cpp"
    # Pull in mu-core .cpp's the tests need (they can't link mu-core directly
    # because the INTERFACE library propagates sources via target_sources, and
    # the console-app target doesn't share JUCE-plugin context).
    # The real PluginProcessor, built headless (createEditor lives in PluginEditor.cpp, not linked), its
    # collaborators and the licence glue, so tests construct the product class itself.
    Source/Tests/ProcessorSeamTests.cpp
    Source/Plugin/PluginProcessor.cpp
    Source/Plugin/PluginProcessor_APVTS.cpp
    Source/Plugin/PluginProcessor_Modulation.cpp
    Source/Plugin/PluginProcessor_Content.cpp
    Source/Sequencer/RhythmManager.cpp
    Source/Persistence/PresetIO.cpp
    Source/Persistence/PresetIO_HostState.cpp
    Source/Plugin/HotSwapStager.cpp
    Source/Audio/SamplePreview.cpp
    Source/Audio/SampleLibrary.cpp
    "${CMAKE_SOURCE_DIR}/mu-core/License/LicenseManager.cpp"
    "${CMAKE_SOURCE_DIR}/mu-core/License/LemonSqueezyClient.cpp"
    "${CMAKE_SOURCE_DIR}/mu-core/License/OnlineActivation.cpp"
    "${CMAKE_SOURCE_DIR}/ThirdParty/monocypher/monocypher.c"
    # The mu-core .cpps behind ProcessorBase (the console target can't link mu-core directly).
    ${MU_CORE_PROCESSOR_TEST_SOURCES}
)

target_compile_features(mu-clid-tests PRIVATE cxx_std_17)

target_include_directories(mu-clid-tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Source"
    "${CMAKE_SOURCE_DIR}/mu-core"
    "${CMAKE_SOURCE_DIR}/ThirdParty/signalsmith-basics/include"
    "${CMAKE_SOURCE_DIR}/ThirdParty/signalsmith-dsp/include"
    "${CMAKE_SOURCE_DIR}/ThirdParty/monocypher"   # LicenseManager
)

target_link_libraries(mu-clid-tests PRIVATE
    juce::juce_core
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_audio_utils   # AudioTransportSource (SamplePreview)
    juce::juce_data_structures
    juce::juce_events
    juce::juce_audio_basics
    juce::juce_audio_processors
    juce::juce_audio_formats
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

target_compile_definitions(mu-clid-tests PRIVATE
    JUCE_USE_MP3AUDIOFORMAT=1   # as the plugin targets, so the MP3 test checks what ships
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
)

set_target_properties(mu-clid-tests PROPERTIES EXCLUDE_FROM_ALL TRUE)
