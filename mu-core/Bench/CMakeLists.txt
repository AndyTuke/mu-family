# mu-core-bench: mixer timing for the runtime-counts work (docs/design-runtime-counts.md section 5). Not in ALL: build on demand,
# in Release for the numbers (tests/scripts/run-bench.ps1). One executable; the mixer shape (clid, tronic or host) is a command-line argument.
juce_add_console_app(mu-core-bench PRODUCT_NAME "mu-core-bench")
set_target_properties(mu-core-bench PROPERTIES EXCLUDE_FROM_ALL ON)
target_sources(mu-core-bench PRIVATE MixerBlockBench.cpp)
target_compile_features(mu-core-bench PRIVATE cxx_std_17)
target_include_directories(mu-core-bench PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../Tests" "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(mu-core-bench PRIVATE mu-core-lib
    juce::juce_core juce::juce_audio_basics juce::juce_audio_processors juce::juce_data_structures juce::juce_events
    juce::juce_graphics juce::juce_gui_basics juce::juce_dsp
    juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)
target_compile_definitions(mu-core-bench PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
