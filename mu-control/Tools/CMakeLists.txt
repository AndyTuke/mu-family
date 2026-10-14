# mu-control-probe - hardware probe for the Launchpad X: lists the MIDI ports and runs numbered lighting and input
# steps against the real pad. A developer tool, run by hand with the pad connected. EXCLUDE_FROM_ALL like the
# other on-demand tools, so a normal build and release packaging never build or ship it:
#   cmake --build build --config Debug --target mu-control-probe
juce_add_console_app(mu-control-probe PRODUCT_NAME "mu-control-probe")

target_sources(mu-control-probe PRIVATE
    ProbeMain.cpp
    ../Surface/SurfaceMapping.cpp
    ../Surface/SurfaceModel.cpp
    ../Devices/LaunchpadX/LaunchpadXProtocol.cpp
    ../Devices/LaunchpadX/LaunchpadXDriver.cpp
    ../Devices/LaunchpadX/LaunchpadXBackend.cpp
    ../Driver/JuceDevicePorts.cpp
    ../Manager/ControllerManager.cpp
    # LiveView.h needs the performance list.
    ../../mu-core/Persistence/PerformanceFile.cpp
)

target_compile_features(mu-control-probe PRIVATE cxx_std_17)

target_include_directories(mu-control-probe PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
    "${CMAKE_CURRENT_SOURCE_DIR}/../../mu-core"
)

target_link_libraries(mu-control-probe PRIVATE
    juce::juce_core
    juce::juce_audio_devices
    juce::juce_data_structures
    juce::juce_events
    # PerformanceFile.cpp includes PresetFiles.h, which pulls in the audio-processor module and its graphics / GUI dependencies.
    juce::juce_audio_basics
    juce::juce_audio_processors
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)

target_compile_definitions(mu-control-probe PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0 JUCE_MODAL_LOOPS_PERMITTED=1)

set_target_properties(mu-control-probe PROPERTIES EXCLUDE_FROM_ALL TRUE)
