# mu-core-tests — console UnitTests for shared mu-core behaviour that needs no product
# (ProcessorCore host transport + layer access). Built as part of ALL_BUILD like the
# per-product test targets. Tests of shared mu-core code live here, not in a product.
include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/ProcessorTestSources.cmake")
juce_add_console_app(mu-core-tests PRODUCT_NAME "mu-core-tests")

target_sources(mu-core-tests PRIVATE
    TestMain.cpp
    HostTransportPublishTests.cpp
    LayerAccessTests.cpp
    ClipBankTests.cpp
    LayerOwnershipTests.cpp
    RhythmAdoptDefaultsTests.cpp
    ../Sequencer/Rhythm.cpp
    ../Sequencer/HitGenerator.cpp
    ../Sequencer/EuclideanGenerator.cpp
    ScopedStorageRootTests.cpp
    ChannelStateResetTests.cpp
    MixerStereoPeakTests.cpp
    SupportPromptPolicyTests.cpp
    SettingsOverlayFitTests.cpp
    SettingsOverlayBackendTests.cpp
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
    PerformanceFileTests.cpp
    LiveStateTests.cpp
    ControlAttachmentTests.cpp
    SwapTimingTests.cpp
    LegacyPrefixTests.cpp
    SegmentControlTests.cpp
    FaderThumbGeometryTests.cpp
    BipolarSliderRowTests.cpp
    FaderThumbRenderTests.cpp
    ../UI/Components/BipolarSliderRow.cpp
    TransportBarTests.cpp
    ShellChromeTests.cpp
    PerformanceModeShellTests.cpp
    PerformanceChooserTests.cpp
    VoiceEngineParamsTests.cpp
    TestShellBackend.h
    ClientStripFitTests.cpp
    MasterColumnFitTests.cpp
    MasterInsertBindingTests.cpp
    ../UI/MixerChannel_Bindings.cpp
    ../UI/MasterInsertBinding.cpp
    # Sources behind ProcessorBase + the settings page (shared list, see ProcessorTestSources.cmake).
    ${MU_CORE_PROCESSOR_TEST_SOURCES}
)

# The processor-free UI sources (UIChromeSources.cmake, defined by the parent). The settings-page
# set above already compiles some of them, so only the remainder is added here.
set(_chrome_extra "")
set(_have "")
foreach(_f ${MU_CORE_PROCESSOR_TEST_SOURCES})
    get_filename_component(_n "${_f}" REALPATH)
    list(APPEND _have "${_n}")
endforeach()
foreach(_f ${MU_CORE_CHROME_SOURCES})
    get_filename_component(_n "${_f}" REALPATH)
    if(NOT _n IN_LIST _have)
        list(APPEND _chrome_extra "${_n}")
    endif()
endforeach()
target_sources(mu-core-tests PRIVATE ${_chrome_extra})

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

target_compile_definitions(mu-core-tests PRIVATE JUCE_USE_MP3AUDIOFORMAT=1 JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0
    JUCE_MODAL_LOOPS_PERMITTED=1   # the teardown tests pump the message loop to let timers run
    MU_VOICE_ENGINE_TEST_SEAM=1)   # exposes VoiceEngine::setParamsHoldHook to VoiceEngineParamsTests only
