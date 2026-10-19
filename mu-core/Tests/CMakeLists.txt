# mu-core-tests — console UnitTests for shared mu-core behaviour that needs no product
# (ProcessorCore host transport + layer access). Built as part of ALL_BUILD like the
# per-product test targets. Tests of shared mu-core code live here, not in a product.
#
# mu-core-tests-16 is the same executable built with MU_MAX_LAYERS=16 (the 16-layer host's capacity): one
# source list, one function, so the two can never drift apart (docs/mu-tronic/design.md 3.4).
include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/ProcessorTestSources.cmake")

set(MU_CORE_TEST_SOURCES
    TestMain.cpp
    HostTransportPublishTests.cpp
    GoldenRenderHarnessTests.cpp
    LayerRenderTests.cpp            # the layer render contract: driver, epoch, LayerBlock, time model
    RetireBinTests.cpp              # the shared retire bin and the retire isolation twin test
    LayerTypeSwapTests.cpp          # the layer type registry and setLayerType on a Published host
    LayerPagingTests.cpp            # the sidebar and the mixer page a host of more than one page of layers
    ../UI/LayerSidebar.cpp
    ../UI/SidebarItem.cpp
    ../UI/MixerOverlay.cpp
    ../UI/FXRow.cpp
    ../UI/DelayRow.cpp
    LayerEquivalenceTests.cpp       # the useLayerRender equivalence harness on the fake host
    LayerContractFixtures.h
    LayerEquivalence.h
    LayerAccessTests.cpp
    ClipBankTests.cpp
    HeldNoteStackTests.cpp          # the shared held-key stack and its two overflow policies
    ClipLaunchTests.cpp
    LayerOwnershipTests.cpp
    RhythmAdoptDefaultsTests.cpp
    ../Sequencer/Rhythm.cpp
    ../Sequencer/HitGenerator.cpp
    ../Sequencer/EuclideanGenerator.cpp
    ScopedStorageRootTests.cpp
    ChannelStateResetTests.cpp
    MixerStereoPeakTests.cpp
    MixerBlockValueTests.cpp
    SupportPromptPolicyTests.cpp
    UpdateCheckPolicyTests.cpp
    SettingsOverlayFitTests.cpp
    SettingsOverlayBackendTests.cpp
    AlgorithmNameTableTests.cpp
    BarPositionTests.cpp
    RhythmPlayStateTests.cpp
    RhythmCircleStepTests.cpp
    HotSwapTests.cpp
    DeferredWorkTests.cpp
    FilterDSPSmokeTests.cpp
    InsertDSPSmokeTests.cpp
    SpectralBehaviourTests.cpp      # Fold / SoftClip / HardClip level, Notch12 / Bp12 response, Vocoder, SSB image, Karplus pitch
    StereoParityTests.cpp           # identical L / R in gives identical L / R out while a parameter ramps
    EnvelopeReleaseTests.cpp        # mu_env::applyParameters and the VoiceEngine release tail
    FxAlgoSwitchTests.cpp           # eff_algo / rev_algo by automation pushes the knob values
    ReverbDecayTests.cpp            # reverb Decay presets, tail length, Size independence
    LegacyReverbDecayTests.cpp      # a state saved before the Decay knob loads its algorithm's old Decay
    ParamRefreshQueueTests.cpp      # the allocation-free listener hand-off
    MidiRawTests.cpp                # raw-byte note tests and the allocation-free MIDI clock scan
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
    ControllerShellTests.cpp
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
    ClipStripTests.cpp
    StepEditorTests.cpp
    ../UI/ClipStrip.cpp
    ../UI/RhythmCircle.cpp
    ../UI/LayerHeaderBar.cpp
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

# One mu-core test executable; `max_layers` is the MU_MAX_LAYERS it is built with (empty = the default).
function(mu_core_add_test_target name max_layers)
    juce_add_console_app(${name} PRODUCT_NAME "${name}")
    target_sources(${name} PRIVATE ${MU_CORE_TEST_SOURCES} ${_chrome_extra})
    target_compile_features(${name} PRIVATE cxx_std_17)
    target_include_directories(${name} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/.."
        "${CMAKE_SOURCE_DIR}/ThirdParty/signalsmith-basics/include"   # ReverbSlot
        "${CMAKE_SOURCE_DIR}/ThirdParty/signalsmith-dsp/include"
    )
    target_link_libraries(${name} PRIVATE
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
    target_compile_definitions(${name} PRIVATE JUCE_USE_MP3AUDIOFORMAT=1 JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0
        JUCE_MODAL_LOOPS_PERMITTED=1   # the teardown tests pump the message loop to let timers run
        MU_VOICE_ENGINE_TEST_SEAM=1)   # exposes VoiceEngine::setParamsHoldHook to VoiceEngineParamsTests only
    if(NOT "${max_layers}" STREQUAL "")
        target_compile_definitions(${name} PRIVATE MU_MAX_LAYERS=${max_layers})
    endif()
endfunction()

mu_core_add_test_target(mu-core-tests "")
mu_core_add_test_target(mu-core-tests-16 16)   # the 16-layer build of the same sources
