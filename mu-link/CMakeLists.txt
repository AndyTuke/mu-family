# mu-link — local audio receiver + master clock for the mu family.
#
# Status: foundation phase. Today this builds only the IPC unit-test runner
# (mu-link-tests) covering the lock-free SPSC AudioRing and the sample-accurate
# TransportClock. The standalone GUI app target (juce_add_gui_app: AudioServer +
# device picker + connected-client view + MIDI-clock-out) and the shared-memory
# mapping land in later increments — see docs/mu-link/design-mulink.md.

# ── mu-link-tests — console UnitTests over the IPC foundation ────────────────
juce_add_console_app(mu-link-tests PRODUCT_NAME "mu-link-tests")

target_sources(mu-link-tests PRIVATE
    Source/Tests/TestMain.cpp
    Source/Tests/IpcTests.cpp
)

target_compile_features(mu-link-tests PRIVATE cxx_std_17)

target_include_directories(mu-link-tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Source"
)

target_link_libraries(mu-link-tests PRIVATE
    juce::juce_core
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)

target_compile_definitions(mu-link-tests PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
