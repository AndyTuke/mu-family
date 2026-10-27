# mu-tronic — μ-family performance unit: 16 layer slots, each holding a layer taken from the other products
# (Standalone only, pay what you want). docs/mu-tronic/design.md.
#
# The target combines the product's shell (the 16-slot processor, the empty-slot chooser) with the shared
# platform (ProcessorBase + MixerEngine + FXChain + the shared editor shell / sidebar / mixer). Shared
# audio/UI/ProcessorBase come from mu-core via the link; the layer types come from the layer libraries
# (mu-<product>-layer) as each is registered in Source/Persistence/LayerTypes.cpp. Registered: Arp (mu-toni-layer), Kick, Bass, Hat, Snare, Rumble and FM Perc (mu-on-layer), Pattern (mu-tant-layer), Rhythm (mu-clid-layer).

# ── mu-Tronic — Standalone only (no VST3, no CLAP, no AU) ────────────────────
juce_add_plugin(mu-tronic
    COMPANY_NAME "Transwarp Development Project"
    BUNDLE_ID "com.transwarp.mu-tronic"
    PLUGIN_MANUFACTURER_CODE TDP1
    PLUGIN_CODE MTrn
    FORMATS Standalone
    PRODUCT_NAME "mu-Tronic"
    PLUGIN_NAME  "μ-Tronic"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE    # the Rhythm type's MIDI output mode sends notes to the Standalone's MIDI output device
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VERSION         "${MUFAMILY_PRE_PLUGIN_VERSION}"   # pre-release 0.1 line until it launches
    COMPANY_COPYRIGHT "${MUFAMILY_COPYRIGHT}"
    ICON_BIG   "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon.png"
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon.png"
)

target_sources(mu-tronic
    PRIVATE
        Source/Plugin/PluginProcessor.cpp
        Source/Plugin/PluginProcessor_Preset.cpp
        Source/Plugin/PluginEditor.cpp
        Source/Persistence/LayerTypes.cpp
        Source/Persistence/ArpLayerType.cpp
        Source/Persistence/TrackLayerTypes.cpp
        Source/Persistence/PatternLayerType.cpp
        Source/Persistence/RhythmLayerType.cpp
        Source/Persistence/ProductImport.cpp
        Source/UI/EmptySlotChooser.cpp
        Source/UI/TronicSettingsOverlay.cpp
        # Offline-license verifier + online activation live in mu-core (shared); compiled here + monocypher.c.
        "${CMAKE_SOURCE_DIR}/mu-core/License/LicenseManager.cpp"
        "${CMAKE_SOURCE_DIR}/mu-core/License/LemonSqueezyClient.cpp"
        "${CMAKE_SOURCE_DIR}/mu-core/License/OnlineActivation.cpp"
        "${CMAKE_SOURCE_DIR}/ThirdParty/monocypher/monocypher.c"
        ${MU_TONI_LAYER_UI_SOURCES}   # the Arp type's UI content (mu-toni-layer)
        ${MU_ON_LAYER_UI_SOURCES}     # the Track types' UI content: TrackContent, StepRows, TrackStepRow (mu-on-layer)
        ${MU_TANT_LAYER_UI_SOURCES}   # the Pattern type's UI content: PatternContent, GatingDesigner (mu-tant-layer)
        ${MU_CLID_LAYER_UI_SOURCES}   # the Rhythm type's UI content: RhythmContent, VoiceSection, the sample browser (mu-clid-layer)
)

target_include_directories(mu-tronic
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/Source"
    SYSTEM PRIVATE
        "${CMAKE_SOURCE_DIR}/ThirdParty/signalsmith-basics/include"
        "${CMAKE_SOURCE_DIR}/ThirdParty/signalsmith-dsp/include"
        "${CMAKE_SOURCE_DIR}/ThirdParty/monocypher"   # LicenseManager
)

# monocypher is vendored third-party crypto; silence its benign MSVC C4244 at the build level.
if(MSVC)
    set_source_files_properties(
        "${CMAKE_SOURCE_DIR}/ThirdParty/monocypher/monocypher.c"
        PROPERTIES COMPILE_OPTIONS "/wd4244")
endif()

# 16 layer slots and 3 group buses with 2 inserts each: MU_MAX_LAYERS / MU_MAX_BUSES / MU_BUS_INSERTS are
# per-target capacities (mu-core is an INTERFACE library, so each binary compiles its own copy); the four
# other products keep 8 layers and no buses.
target_compile_definitions(mu-tronic
    PUBLIC
        MU_MAX_LAYERS=16
        MU_MAX_BUSES=3
        MU_BUS_INSERTS=2
        MU_EXT_SIDECHAIN=0
        MU_TRONIC_WITH_UI=1   # the layer types build their content and sidebar pictures (Persistence/ArpLayerType.cpp, TrackLayerTypes.cpp, PatternLayerType.cpp)
        JUCE_WEB_BROWSER=0
        JUCE_USE_MP3AUDIOFORMAT=1   # the Rhythm type loads the same sample formats as mu-Clid
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        "MU_VERSION_BASE=\"${MUFAMILY_PRE_VERSION_BASE}\""   # pre-release 0.1 line shown in About
)

target_link_libraries(mu-tronic
    PRIVATE
        mu-toni-layer   # the Arp type: its engine sources and the Source/ include path
        mu-on-layer     # the Track types (Kick, Bass, Hat, Snare, Rumble, FM Perc): the same
        mu-tant-layer   # the Pattern type: the same
        mu-clid-layer   # the Rhythm type: the same
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

# Custom standalone app: the shared "Close μ-Tronic?" confirmation on quit (mirrors the other products).
target_compile_definitions(mu-tronic_Standalone PRIVATE JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1)
target_sources(mu-tronic_Standalone PRIVATE
    Source/Plugin/StandaloneApp.cpp)

# Hardware controller support (mu-control) in this Standalone only; a no-op unless MUFAMILY_WITH_CONTROL is ON.
mu_control_attach(mu-tronic_Standalone)
mu_asio_attach(mu-tronic_Standalone)   # ASIO devices when -DMUFAMILY_WITH_ASIO=ON

# Family build-counter (shared increment target defined in mu-clid/CMakeLists.txt).
add_dependencies(mu-tronic increment_build_number)

# Custom Windows VERSIONINFO with the full four-part family version (JUCE's generated .rc can't take x.y.0.N).
if(WIN32)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/versioninfo.rc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/versioninfo.rc"
        @ONLY
        NEWLINE_STYLE CRLF   # RC compiler expects CRLF on Windows
    )
    target_compile_definitions(mu-tronic        PRIVATE "JUCE_USER_DEFINED_RC_FILE=\"${CMAKE_CURRENT_BINARY_DIR}/versioninfo.rc\"")
    target_compile_definitions(mu-tronic_rc_lib PRIVATE "JUCE_USER_DEFINED_RC_FILE=\"${CMAKE_CURRENT_BINARY_DIR}/versioninfo.rc\"")
    add_dependencies(mu-tronic_rc_lib increment_build_number)
endif()

# ── mu-tronic-tests — console UnitTests ─────────────────────────────────────
# Built as part of ALL_BUILD (small and local, like mu-toni-tests).
include("${CMAKE_SOURCE_DIR}/mu-core/cmake/ProcessorTestSources.cmake")
juce_add_console_app(mu-tronic-tests PRODUCT_NAME "mu-tronic-tests")

target_sources(mu-tronic-tests PRIVATE
    Source/Tests/TestMain.cpp
    Source/Tests/TronicProcessorTests.cpp   # the real PluginProcessor, headless: 16 empty slots, types, state round trip
    Source/Tests/TronicLayoutTests.cpp      # the 16-slot parameter layout: size, build time, unique ids
    Source/Tests/TronicArpTests.cpp         # the Arp type in a slot: layout, saved rows, same stream as mu-Toni, follower, presets, stress
    Source/Tests/TronicTrackTests.cpp       # the Kick, Bass, Hat and Snare types in a slot: saved rows, same stream as mu-On, tails, presets
    Source/Tests/TronicLayerPresetTests.cpp # layer presets in a slot: save/load per type, Empty-slot adopt, cross-type refusal, staged load
    Source/Tests/TronicPatternTests.cpp     # the Pattern type in a slot: saved rows, same stream as mu-Tant, Note mode per layer, services, wavetables, presets
    Source/Tests/TronicSwapTests.cpp        # swapLayers in a slot host: Kick, Arp, Rumble and Rhythm reorder with rows, strips, Rumble source and tails
    Source/Tests/TronicRhythmTests.cpp      # the Rhythm type in a slot: saved rows, same stream as mu-Clid, parameter sync, samples, MIDI out, tails, .muRhythm presets
    Source/Tests/TronicMidiInTests.cpp      # per-layer MIDI In: the strip row, channel routing, carried keys, save / swap, the load meter
    Source/Tests/TronicSceneTests.cpp       # type-carrying clips, scenes (one boundary), clip banks in the saved state, performance files of full presets
    Source/Tests/TronicImportTests.cpp      # Import product preset: a real fixture per product, a Rumble source, sidechain remap, refusals, the saved state after
    Source/Tests/TronicSceneUiTests.cpp     # what the editor reads: Notes In and clip-bank types, the scene picker offer, scene launch reports
    Source/Tests/TronicClipAutoSaveTests.cpp # clip auto-save: every type's capture is idempotent, an edit is stored, a slot swap re-adopts
    # The real PluginProcessor, built headless (createEditor lives in PluginEditor.cpp, not linked).
    Source/Plugin/PluginProcessor.cpp
    Source/Plugin/PluginProcessor_Preset.cpp
    Source/Persistence/LayerTypes.cpp
    Source/Persistence/ArpLayerType.cpp
    Source/Persistence/TrackLayerTypes.cpp
    Source/Persistence/PatternLayerType.cpp
    Source/Persistence/RhythmLayerType.cpp
    Source/Persistence/ProductImport.cpp
    "${CMAKE_SOURCE_DIR}/mu-core/License/LicenseManager.cpp"
    "${CMAKE_SOURCE_DIR}/mu-core/License/LemonSqueezyClient.cpp"
    "${CMAKE_SOURCE_DIR}/mu-core/License/OnlineActivation.cpp"
    "${CMAKE_SOURCE_DIR}/ThirdParty/monocypher/monocypher.c"
    # The mu-core .cpps behind ProcessorBase (the console target can't link mu-core directly).
    ${MU_CORE_PROCESSOR_TEST_SOURCES}
    "${CMAKE_SOURCE_DIR}/mu-core/Audio/Wavetable/WavetableBank.cpp"   # the Arp type's oscillator tables
    # The Rhythm type's sequencer (the Euclid generators and the layer they make); its voice engine is in the list above.
    "${CMAKE_SOURCE_DIR}/mu-core/Sequencer/Rhythm.cpp"
    "${CMAKE_SOURCE_DIR}/mu-core/Sequencer/HitGenerator.cpp"
    "${CMAKE_SOURCE_DIR}/mu-core/Sequencer/EuclideanGenerator.cpp"
)

target_compile_features(mu-tronic-tests PRIVATE cxx_std_17)

target_include_directories(mu-tronic-tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Source"
    "${CMAKE_SOURCE_DIR}/mu-core"
    SYSTEM
        "${CMAKE_SOURCE_DIR}/ThirdParty/signalsmith-basics/include"   # ReverbSlot
        "${CMAKE_SOURCE_DIR}/ThirdParty/signalsmith-dsp/include"
        "${CMAKE_SOURCE_DIR}/ThirdParty/monocypher"   # LicenseManager
)

target_link_libraries(mu-tronic-tests PRIVATE
    mu-toni-layer
    mu-on-layer
    mu-tant-layer
    mu-clid-layer
    juce::juce_core
    juce::juce_audio_basics
    juce::juce_audio_processors
    juce::juce_data_structures
    juce::juce_events
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_dsp
    juce::juce_audio_formats
    juce::juce_audio_utils   # the Rhythm type's sample library (SampleSlotHost) includes it
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)

# Preset files written by the real mu-Toni and mu-On, round-tripped through a slot (Tests/TronicLayerPresetTests.cpp).
target_compile_definitions(mu-tronic-tests PRIVATE "MU_TRONIC_FIXTURE_DIR=\"${CMAKE_SOURCE_DIR}/tests/presets/mu-tronic-slot\"")
# Full presets written by the real mu-Clid, mu-Tant, mu-Toni and mu-On, imported into slots (Tests/TronicImportTests.cpp).
target_compile_definitions(mu-tronic-tests PRIVATE "MU_TRONIC_IMPORT_FIXTURE_DIR=\"${CMAKE_SOURCE_DIR}/tests/presets/mu-tronic-import\"")
target_compile_definitions(mu-tronic-tests PRIVATE MU_MAX_LAYERS=16 MU_MAX_BUSES=3 MU_BUS_INSERTS=2 JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)

add_dependencies(mu-tronic mu-tronic-tests)
