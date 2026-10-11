# mu-core-tests — console UnitTests for shared mu-core behaviour that needs no product
# (ProcessorCore host transport + layer access). Built as part of ALL_BUILD like the
# per-product test targets. Tests of shared mu-core code live here, not in a product.
include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/ProcessorTestSources.cmake")
juce_add_console_app(mu-core-tests PRODUCT_NAME "mu-core-tests")

target_sources(mu-core-tests PRIVATE
    TestMain.cpp
    HostTransportPublishTests.cpp
    LayerAccessTests.cpp
    LayerOwnershipTests.cpp
    ProductLayerSetupTests.cpp
    ScopedStorageRootTests.cpp
    ChannelStateResetTests.cpp
    SupportPromptPolicyTests.cpp
    SettingsOverlayFitTests.cpp
    AlgorithmNameTableTests.cpp
    BarPositionTests.cpp
    HotSwapTests.cpp
    DeferredWorkTests.cpp
    FilterDSPSmokeTests.cpp
    InsertDSPSmokeTests.cpp
    MidiClockSyncTests.cpp
    MidiControlTests.cpp
    ModulationMatrixTests.cpp
    Mp3FormatTests.cpp
    SendFXSmokeTests.cpp
    SidechainRemapTests.cpp
    TransportResolverTests.cpp
    UIScalingConsistencyTests.cpp
    MidiFullPresetMapTests.cpp
    # Sources behind ProcessorBase + the settings page (shared list, see ProcessorTestSources.cmake).
    ${MU_CORE_PROCESSOR_TEST_SOURCES}
)

target_compile_features(mu-core-tests PRIVATE cxx_std_17)

target_include_directories(mu-core-tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
    "${CMAKE_SOURCE_DIR}/ThirdParty/signalsmith-basics/include"   # ReverbSlot
    "${CMAKE_SOURCE_DIR}/ThirdParty/signalsmith-dsp/include"
)

target_link_libraries(mu-core-tests PRIVATE
    juce::juce_core
    juce::juce_audio_basics
    juce::juce_audio_processors
    juce::juce_audio_formats
    juce::juce_data_structures
    juce::juce_events
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)

target_compile_definitions(mu-core-tests PRIVATE JUCE_USE_MP3AUDIOFORMAT=1 JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
