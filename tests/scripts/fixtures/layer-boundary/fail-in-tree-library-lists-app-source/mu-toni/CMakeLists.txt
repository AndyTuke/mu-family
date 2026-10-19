# A product whose layer library lists a Plugin/ source (exactly one violation: rule 9).
add_library(mu-toni-layer INTERFACE)
target_sources(mu-toni-layer INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/Sequencer/Arp.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/Plugin/PluginProcessor.cpp"
)
set(MU_TONI_LAYER_UI_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/UI/ArpContent.cpp")
