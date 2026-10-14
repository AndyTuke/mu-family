# mu-control-tests - console UnitTests for the device-independent surface layer (description, mapping, model),
# run against both a Launchpad-style description and the mock grid, plus the Launchpad X protocol and driver
# against fake ports. Mirrors mu-core-tests: compiles the library
# sources directly and links no product and no ProcessorBase.
juce_add_console_app(mu-control-tests PRODUCT_NAME "mu-control-tests")

target_sources(mu-control-tests PRIVATE
    TestMain.cpp
    SurfaceDescriptionTests.cpp
    SurfaceMappingTests.cpp
    SurfaceModelTests.cpp
    LaunchpadXProtocolTests.cpp
    LaunchpadXDriverTests.cpp
    ControllerManagerTests.cpp
    TestDescriptions.h
    MockGridDriver.h
    FakeLiveView.h
    FakeControlSink.h
    FakeDevicePorts.h
    ../Surface/SurfaceMapping.cpp
    ../Surface/SurfaceModel.cpp
    ../Devices/LaunchpadX/LaunchpadXProtocol.cpp
    ../Devices/LaunchpadX/LaunchpadXDriver.cpp
    ../Devices/LaunchpadX/LaunchpadXBackend.cpp
    ../Manager/ControllerManager.cpp
    ../Driver/JuceDevicePorts.cpp
    # LiveView.h needs the performance list.
    ../../mu-core/Persistence/PerformanceFile.cpp
)

target_compile_features(mu-control-tests PRIVATE cxx_std_17)

target_include_directories(mu-control-tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
    "${CMAKE_CURRENT_SOURCE_DIR}/../../mu-core"
)

target_link_libraries(mu-control-tests PRIVATE
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

target_compile_definitions(mu-control-tests PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0 JUCE_MODAL_LOOPS_PERMITTED=1)
