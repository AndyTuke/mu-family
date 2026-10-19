# mu-layer-link-test — mu-clid-layer, mu-tant-layer, mu-on-layer and mu-toni-layer linked into one console target,
# headers of each included, nothing run (extraction-plan 2.6). Exercises the include-path and symbol collisions
# of design 2.5 that no single-product build can see. Built on demand, not part of ALL_BUILD.
# The mu-core list is the union of the four *-layer-tests targets' lists.
juce_add_console_app(mu-layer-link-test PRODUCT_NAME "mu-layer-link-test")

target_sources(mu-layer-link-test PRIVATE
    LayerLinkTest.cpp
    "${CMAKE_SOURCE_DIR}/mu-core/Audio/Wavetable/WavetableBank.cpp"
    "${CMAKE_SOURCE_DIR}/mu-core/Audio/VoiceEngine.cpp"
    "${CMAKE_SOURCE_DIR}/mu-core/Audio/InsertProcessor.cpp"
    "${CMAKE_SOURCE_DIR}/mu-core/Audio/MultiModeFilter.cpp"
    "${CMAKE_SOURCE_DIR}/mu-core/Audio/SamplePlayer.cpp"
    "${CMAKE_SOURCE_DIR}/mu-core/Audio/MixerEngine.cpp"
    "${CMAKE_SOURCE_DIR}/mu-core/Audio/MidiOutputEngine.cpp"
    "${CMAKE_SOURCE_DIR}/mu-core/Audio/FX/Slots/EffectSlot.cpp"
    "${CMAKE_SOURCE_DIR}/mu-core/Audio/FX/Slots/DelaySlot.cpp"
    "${CMAKE_SOURCE_DIR}/mu-core/Audio/FX/Slots/ReverbSlot.cpp"
    "${CMAKE_SOURCE_DIR}/mu-core/Audio/FX/Slots/FXChain.cpp"
    "${CMAKE_SOURCE_DIR}/mu-core/Modulation/ModulationMatrix.cpp"
    "${CMAKE_SOURCE_DIR}/mu-core/Sequencer/ControlSequence.cpp"
    "${CMAKE_SOURCE_DIR}/mu-core/Sequencer/Rhythm.cpp"
    "${CMAKE_SOURCE_DIR}/mu-core/Sequencer/HitGenerator.cpp"
    "${CMAKE_SOURCE_DIR}/mu-core/Sequencer/EuclideanGenerator.cpp"
)

target_compile_features(mu-layer-link-test PRIVATE cxx_std_17)

target_include_directories(mu-layer-link-test PRIVATE
    "${CMAKE_SOURCE_DIR}/mu-core"
    SYSTEM
        "${CMAKE_SOURCE_DIR}/ThirdParty/signalsmith-basics/include"
        "${CMAKE_SOURCE_DIR}/ThirdParty/signalsmith-dsp/include"
)

target_link_libraries(mu-layer-link-test PRIVATE
    mu-clid-layer
    mu-tant-layer
    mu-on-layer
    mu-toni-layer
    juce::juce_core
    juce::juce_audio_basics
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_data_structures
    juce::juce_events
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_dsp
    juce::juce_audio_formats
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)

target_compile_definitions(mu-layer-link-test PRIVATE JUCE_USE_MP3AUDIOFORMAT=1 JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)

set_target_properties(mu-layer-link-test PROPERTIES EXCLUDE_FROM_ALL TRUE)
