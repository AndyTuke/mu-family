# ControlDesign — a tiny GUI sandbox for iterating on the mu knob style.
#
# Not a plugin: it does NOT link the mu-core INTERFACE library (that pulls the whole
# plugin stack). Like mu-link, it compiles just the shared design-system sources it
# needs (MuLookAndFeel + MuTheme) so a style change in MuLookAndFeel shows here
# immediately, at family parity.
set(MU_CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../mu-core")

juce_add_gui_app(ControlDesign PRODUCT_NAME "ControlDesign")

target_sources(ControlDesign PRIVATE
    Source/Main.cpp
    "${MU_CORE_DIR}/UI/Components/MuLookAndFeel.cpp"
    "${MU_CORE_DIR}/UI/Components/MuTheme.cpp"
    "${MU_CORE_DIR}/UI/Components/KnobWithLabel.cpp"
    "${MU_CORE_DIR}/UI/Components/SegmentControl.cpp"
)

target_compile_features(ControlDesign PRIVATE cxx_std_17)

target_include_directories(ControlDesign PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Source"
    "${MU_CORE_DIR}"
)

target_link_libraries(ControlDesign PRIVATE
    juce::juce_gui_extra
    juce::juce_gui_basics
    juce::juce_audio_basics
    juce::juce_data_structures
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)

target_compile_definitions(ControlDesign PRIVATE
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_STANDALONE_APPLICATION=1
)
