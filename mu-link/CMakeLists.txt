# mu-link — local audio receiver + master clock for the mu family.
#
# The IPC contract + client glue (the Link module) lives in mu-core so every product's
# standalone shares it; mu-link adds the SERVER half + the GUI. Every target therefore
# puts mu-core on its include path so `#include "Link/..."` resolves. mu-link still does
# NOT link the mu-core INTERFACE library (that pulls the whole plugin stack) — it includes
# only the headers it needs and, for the GUI, compiles the 3 shared UI sources directly.
#
# Targets (see docs/mu-link/design-mulink.md §6 build stages):
#   • mu-link-tests  — console UnitTests: IPC foundation + ServerEngine + MuLinkClient.
#                      juce_core only, headless, with a real child process for the
#                      cross-process shared-memory round-trip.
#   • mu-link-server — runnable headless audio server (owns the HW device, sums clients).
#   • mu-link-tone   — reference tone client, built on mu-core MuLinkClient.
#   • mu-link        — the GUI app (device picker, client meters, master transport).
set(MU_CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../mu-core")

# ── mu-link-tests — console UnitTests over the IPC foundation + engine + client ──
juce_add_console_app(mu-link-tests PRODUCT_NAME "mu-link-tests")

target_sources(mu-link-tests PRIVATE
    Source/Tests/TestMain.cpp
    Source/Tests/IpcTests.cpp
    Source/Tests/SharedMemoryTests.cpp
    Source/Tests/ServerTests.cpp
    Source/Tests/ClientTests.cpp
    "${MU_CORE_DIR}/Audio/InsertProcessor.cpp"   # ServerEngine embeds two master-bus inserts
)

target_compile_features(mu-link-tests PRIVATE cxx_std_17)

target_include_directories(mu-link-tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Source"
    "${MU_CORE_DIR}"
)

target_link_libraries(mu-link-tests PRIVATE
    juce::juce_core
    juce::juce_audio_basics   # ServerEngine now embeds the mu-core EqInsert (AudioBuffer/SmoothedValue)
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)

target_compile_definitions(mu-link-tests PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)

# ── mu-link-server — runnable headless audio server (owns the HW device) ──────
juce_add_console_app(mu-link-server PRODUCT_NAME "mu-link-server")

target_sources(mu-link-server PRIVATE
    Source/Server/ServerMain.cpp
    "${MU_CORE_DIR}/Audio/InsertProcessor.cpp"   # ServerEngine embeds two master-bus inserts
)

target_compile_features(mu-link-server PRIVATE cxx_std_17)

target_include_directories(mu-link-server PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Source"
    "${MU_CORE_DIR}"
)

target_link_libraries(mu-link-server PRIVATE
    juce::juce_audio_devices
    juce::juce_audio_basics
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)

target_compile_definitions(mu-link-server PRIVATE
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_STANDALONE_APPLICATION=1
)

# ── mu-link-tone — reference client: pushes a sine into the bus (L2 verification) ──
juce_add_console_app(mu-link-tone PRODUCT_NAME "mu-link-tone")

target_sources(mu-link-tone PRIVATE
    Source/Tools/ToneClientMain.cpp
)

target_compile_features(mu-link-tone PRIVATE cxx_std_17)

target_include_directories(mu-link-tone PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Source"
    "${MU_CORE_DIR}"
)

target_link_libraries(mu-link-tone PRIVATE
    juce::juce_core
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)

target_compile_definitions(mu-link-tone PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)

# ── mu-link-harness — headless "virtual mu-link": render the bus to WAV + self-assert ──
juce_add_console_app(mu-link-harness PRODUCT_NAME "mu-link-harness")

target_sources(mu-link-harness PRIVATE
    Source/Tools/HarnessMain.cpp
    "${MU_CORE_DIR}/Audio/InsertProcessor.cpp"   # ServerEngine embeds two master-bus inserts
)

target_compile_features(mu-link-harness PRIVATE cxx_std_17)

target_include_directories(mu-link-harness PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Source"
    "${MU_CORE_DIR}"
)

target_link_libraries(mu-link-harness PRIVATE
    juce::juce_audio_formats
    juce::juce_audio_basics
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)

target_compile_definitions(mu-link-harness PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)

# ── mu-link — the GUI app (device picker, client meters, master transport) ────
# Not a plugin, so it does NOT link the mu-core INTERFACE library (that pulls the whole
# plugin stack: ProcessorBase, EditorShellBase, …). Instead it compiles just the shared
# design-system sources it needs for family visual parity — MuLookAndFeel + MuTheme +
# VUMeter — alongside its own window. See docs/mu-link/design-mulink.md §3.3.
juce_add_gui_app(mu-link PRODUCT_NAME "mu-link"
    ICON_BIG   "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon.png"
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon.png")

target_sources(mu-link PRIVATE
    Source/Plugin/Main.cpp
    Source/UI/MuLinkComponent.cpp
    "${MU_CORE_DIR}/UI/Components/MuLookAndFeel.cpp"
    "${MU_CORE_DIR}/UI/Components/MuTheme.cpp"
    "${MU_CORE_DIR}/UI/Components/VUMeter.cpp"
    "${MU_CORE_DIR}/UI/Components/KnobWithLabel.cpp"   # master-insert knobs
    "${MU_CORE_DIR}/UI/Components/DropdownSelect.cpp"  # master-insert algo dropdowns
    "${MU_CORE_DIR}/UI/SaveDialog.cpp"                 # shared preset save dialog
    "${MU_CORE_DIR}/UI/PresetBrowser.cpp"              # shared preset browser
    "${MU_CORE_DIR}/Audio/InsertProcessor.cpp"         # ServerEngine master-bus inserts
)

target_compile_features(mu-link PRIVATE cxx_std_17)

target_include_directories(mu-link PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Source"
    "${MU_CORE_DIR}"
)

target_link_libraries(mu-link PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_devices
    juce::juce_audio_basics
    juce::juce_gui_extra
    juce::juce_data_structures   # juce::ValueTree for the preset save/load
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)

target_compile_definitions(mu-link PRIVATE
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_STANDALONE_APPLICATION=1
)

# ── Deploy the Release exe to the tester share (bare exe — no installer) ──────
# Guarded by MUFAMILY_DEPLOY_TESTERS (OFF by default). Testers need mu-link.exe to use the
# mu-Clid standalone↔mu-link bridge, so mu-link now ships alongside the plugins. Debug is
# skipped inside the script; run Debug builds from the build output folder.
if(MUFAMILY_DEPLOY_TESTERS)
add_custom_command(TARGET mu-link POST_BUILD
    COMMAND "${CMAKE_COMMAND}"
            "-DCONFIG=$<CONFIG>"
            "-DAPP_EXE=$<TARGET_FILE:mu-link>"
            "-DDIST_WIN=${MUFAMILY_WIN_DIST}"
            -P "${CMAKE_SOURCE_DIR}/cmake/DeployMuLink.cmake"
    COMMENT "Deploying mu-link Release exe to OneDrive (Debug skipped)"
    VERBATIM
)
endif()
