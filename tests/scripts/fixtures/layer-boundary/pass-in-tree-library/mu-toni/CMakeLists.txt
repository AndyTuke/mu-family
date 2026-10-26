# A product that declares its layer library over its own Source/ folders.
add_library(mu-toni-layer INTERFACE)
target_sources(mu-toni-layer INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/Sequencer/Arp.cpp"
)
set(MU_TONI_LAYER_UI_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/UI/ArpContent.cpp")
