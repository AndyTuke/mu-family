# mu-core — shared audio/FX/modulation/UI source set across the μ-family.
#
# INTERFACE library: sources are compiled in each consuming plugin's target
# context, where JUCE modules are fully initialised by juce_add_plugin. A
# STATIC library would need to duplicate JUCE's module setup macros — INTERFACE
# avoids that entirely.
#
# Consume from a sibling plugin with:
#     target_link_libraries(mu-tant PRIVATE mu-core)
#
# Anything in this directory must be plugin-agnostic. If you find yourself
# reaching for `Rhythm`, `HitGenerator`, `PluginProcessor` etc. from a mu-core
# source file, you've crossed the boundary — that file belongs in the consuming
# plugin's source tree instead.

add_library(mu-core INTERFACE)

# Pin C++ standard explicitly so a future JUCE bump cannot silently change it.
target_compile_features(mu-core INTERFACE cxx_std_17)

target_sources(mu-core INTERFACE
    # Modulation
    "${CMAKE_CURRENT_SOURCE_DIR}/Modulation/ModulationMatrix.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Sequencer/ControlSequence.cpp"
    # Audio
    "${CMAKE_CURRENT_SOURCE_DIR}/Audio/VoiceEngine.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Audio/InsertProcessor.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Audio/MultiModeFilter.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Audio/SamplePlayer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Audio/MixerEngine.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Audio/MidiOutputEngine.cpp"
    # FX slots
    "${CMAKE_CURRENT_SOURCE_DIR}/Audio/FX/Slots/EffectSlot.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Audio/FX/Slots/DelaySlot.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Audio/FX/Slots/ReverbSlot.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Audio/FX/Slots/FXChain.cpp"
    # Shared UI components
    "${CMAKE_CURRENT_SOURCE_DIR}/UI/Components/MuLookAndFeel.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/UI/Components/MuTheme.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/UI/Components/BipolarSliderRow.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/UI/Components/KnobWithLabel.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/UI/Components/SegmentControl.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/UI/Components/NudgeInput.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/UI/Components/DropdownSelect.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/UI/Components/StepEditor.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/UI/Components/LFOEditor.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/UI/Components/AddButton.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/UI/Components/StatusBar.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/UI/Components/VUMeter.cpp"
    # Shared mixer/FX panels
    "${CMAKE_CURRENT_SOURCE_DIR}/UI/FXRow.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/UI/DelayRow.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/UI/MixerChannel.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/UI/MixerChannel_Bindings.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/UI/MixerChannel_Insert.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/UI/MixerOverlay.cpp"
    # Shared plugin scaffolding
    "${CMAKE_CURRENT_SOURCE_DIR}/Plugin/ProcessorBase.cpp"
    # RenderMode.cpp is consumed by each plugin's Standalone target rather than
    # the shared INTERFACE — needs JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP context.
    # Persistence — MIDI PC preset maps (per-slot ch 1-8 + full-preset ch 9).
    "${CMAKE_CURRENT_SOURCE_DIR}/Persistence/MidiPresetMap.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Persistence/MidiFullPresetMap.cpp"
    # Shared preset browser + MIDI PC editor panels.
    "${CMAKE_CURRENT_SOURCE_DIR}/UI/PresetBrowser.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/UI/MidiPresetsPanel.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/UI/MidiFullPresetsPanel.cpp"
    # Shared modal overlays — product chrome supplied via setters.
    "${CMAKE_CURRENT_SOURCE_DIR}/UI/AboutPanel.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/UI/SaveDialog.cpp"
    # Shared transport bar — product slots in its own loop section + logo text.
    "${CMAKE_CURRENT_SOURCE_DIR}/UI/TransportBar.cpp"
    # Shared editor shell — chrome / overlay state machine / main-area layout.
    "${CMAKE_CURRENT_SOURCE_DIR}/UI/EditorShellBase.cpp"
    # Shared modulator panels — product supplies its destination provider.
    "${CMAKE_CURRENT_SOURCE_DIR}/UI/ModulatorPanel.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/UI/ModulatorEditor.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/UI/ModMatrixPanel.cpp"
)

# Consumers can `#include "Audio/VoiceEngine.h"` etc. — same path namespace as
# the old single-plugin layout, so source files in mu-core or in any consuming
# plugin compile without changing existing include directives.
target_include_directories(mu-core INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
