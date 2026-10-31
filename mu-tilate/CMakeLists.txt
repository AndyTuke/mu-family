# mu-tilate — μ-family loop slicer (planned product, built in stages; docs/mu-tilate/design-buildplan.md).
#
# Stage 1: the engine pieces that need no processor — the layer library and its console tests. The plugin target
# (juce_add_plugin, VST3 + CLAP + Standalone) arrives with the Phrase layer in a later stage.

# ── mu-tilate-layer — the Phrase layer type as a library (docs/mu-tronic/design.md 2.5) ──
# The product's own Source/ folders: Audio/ Sequencer/ Modulation/ Persistence/ and UI/. INTERFACE, like mu-core,
# so its sources compile in each consumer's target; tests/scripts/check-layer-boundary.ps1 keeps it free of
# Plugin/ and License/.
add_library(mu-tilate-layer INTERFACE)
target_sources(mu-tilate-layer INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/Audio/SlicePlayer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/Persistence/PhraseParams.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/Persistence/PhraseState.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/Sequencer/SliceList.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/Sequencer/Timeline.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/Sequencer/PhrasePlayback.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/Sequencer/Phrase.cpp"
)
target_include_directories(mu-tilate-layer INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/Source")
# Signalsmith Stretch (MIT, header-only) and its FFT library; SYSTEM so their warnings stay out of ours.
target_include_directories(mu-tilate-layer SYSTEM INTERFACE
    "${CMAKE_SOURCE_DIR}/ThirdParty/signalsmith-stretch"
    "${CMAKE_SOURCE_DIR}/ThirdParty/signalsmith-linear/include")

# ── mu-tilate-tests — console UnitTests ─────────────────────────────────────
juce_add_console_app(mu-tilate-tests PRODUCT_NAME "mu-tilate-tests")

target_sources(mu-tilate-tests PRIVATE
    Source/Tests/TestMain.cpp
    Source/Tests/SlicePlayerTests.cpp   # the monophonic slice player: native / re-pitch / reverse, fades, the cut fade
    Source/Tests/SliceDetectionTests.cpp   # onset detection on synthetic loops, and the slice list
    Source/Tests/StretchSliceTests.cpp   # the stretched slice: length, dry head, pitch and level, no pre-echo, no allocation
    Source/Tests/PhraseTests.cpp   # the timeline model and the transport-anchored scheduler
    Source/Tests/PhraseParamsTests.cpp   # the parameter table and the modulation target table
    Source/Tests/PhraseLayerTests.cpp   # the Phrase layer through the shared render contract
    Source/Tests/PhraseStateTests.cpp   # saving and loading the slices, timeline and loop reference
    # mu-core's detector (the plugin targets get it through mu-core; a console target lists it).
    "${CMAKE_SOURCE_DIR}/mu-core/Analysis/OnsetDetector.cpp"
    # What the modulation-table checks need (as mu-toni-layer-tests lists them).
    "${CMAKE_SOURCE_DIR}/mu-core/Modulation/ModulationMatrix.cpp"
    "${CMAKE_SOURCE_DIR}/mu-core/Sequencer/ControlSequence.cpp"
    # The Layer base and the insert the Phrase layer renders through.
    "${CMAKE_SOURCE_DIR}/mu-core/Audio/InsertProcessor.cpp"
    "${CMAKE_SOURCE_DIR}/mu-core/Audio/MultiModeFilter.cpp"
)
target_include_directories(mu-tilate-tests PRIVATE "${CMAKE_SOURCE_DIR}/mu-core" "${CMAKE_SOURCE_DIR}/mu-core/Tests" "${CMAKE_CURRENT_SOURCE_DIR}/Source")
target_include_directories(mu-tilate-tests SYSTEM PRIVATE "${CMAKE_SOURCE_DIR}/ThirdParty/signalsmith-basics/include" "${CMAKE_SOURCE_DIR}/ThirdParty/signalsmith-dsp/include")   # Tests: AllocationProbe.h

target_compile_features(mu-tilate-tests PRIVATE cxx_std_17)

target_link_libraries(mu-tilate-tests PRIVATE
    mu-tilate-layer
    juce::juce_core
    juce::juce_audio_basics
    juce::juce_audio_processors   # the parameter table tests build an APVTS
    juce::juce_data_structures
    juce::juce_events
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_dsp   # FFT for the detector
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)

target_compile_definitions(mu-tilate-tests PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
