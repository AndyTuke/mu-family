# mu-core-bench-<shape>: mixer timing for the runtime-counts work (docs/design-runtime-counts.md section 5). Not in ALL: build on demand,
# in Release for the numbers (tests/scripts/run-bench.ps1). Each shape links the same mu-core library variant its product does, so no
# extra library is compiled.
function(mu_core_add_bench name)
    juce_add_console_app(${name} PRODUCT_NAME "${name}")
    set_target_properties(${name} PROPERTIES EXCLUDE_FROM_ALL ON)
    target_sources(${name} PRIVATE MixerBlockBench.cpp)
    target_compile_features(${name} PRIVATE cxx_std_17)
    target_include_directories(${name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../Tests" "${CMAKE_CURRENT_SOURCE_DIR}/..")
    mu_core_variant(_lib ${ARGN})
    target_link_libraries(${name} PRIVATE ${_lib}
        juce::juce_core juce::juce_audio_basics juce::juce_audio_processors juce::juce_data_structures juce::juce_events
        juce::juce_graphics juce::juce_gui_basics juce::juce_dsp
        juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)
    target_compile_definitions(${name} PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
endfunction()

mu_core_add_bench(mu-core-bench-clid)                                                                       # 8 layers, no buses, ext sidechain
mu_core_add_bench(mu-core-bench-tronic LAYERS 16 BUSES 3 INSERTS 2 EXT_SIDECHAIN 0 VERSION_BASE "${MUFAMILY_PRE_VERSION_BASE}")
mu_core_add_bench(mu-core-bench-host   LAYERS 1 BUSES 0 EXT_SIDECHAIN 0 VERSION_BASE "${MUFAMILY_PRE_VERSION_BASE}")
