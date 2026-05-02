cmake_minimum_required(VERSION 3.22)
project(mu-clid VERSION 1.0.0)

add_subdirectory($ENV{JUCE_PATH} JUCE)

juce_add_plugin(mu-clid
    COMPANY_NAME "Transwarp Development Project"
    BUNDLE_ID "com.transwarp.mu-clid"
    PLUGIN_MANUFACTURER_CODE TDP1
    PLUGIN_CODE MCld
    FORMATS VST3 Standalone
    PRODUCT_NAME "mu-Clid"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
)

target_sources(mu-clid
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/Sequencer/EuclideanGenerator.cpp
        Source/Sequencer/HitGenerator.cpp
        Source/Sequencer/Rhythm.cpp
        Source/Sequencer/SequencerEngine.cpp
        Source/Sequencer/ControlSequence.cpp
        Source/Audio/SamplePlayer.cpp
        Source/Audio/VoiceEngine.cpp
        Source/Audio/MidiOutputEngine.cpp
        Source/Modulation/ModulationMatrix.cpp
        Source/UI/Components/MuClidLookAndFeel.cpp
        Source/UI/Components/KnobWithLabel.cpp
        Source/UI/Components/SegmentControl.cpp
        Source/UI/Components/NudgeInput.cpp
        Source/UI/Components/TimeSelector.cpp
        Source/UI/Components/DropdownSelect.cpp
        Source/UI/Components/StepEditor.cpp
        Source/UI/Components/LFOEditor.cpp
        Source/UI/Components/AddButton.cpp
        Source/UI/Components/StatusBar.cpp
        Source/UI/TransportBar.cpp
        Source/UI/RhythmCircle.cpp
        Source/UI/SidebarItem.cpp
        Source/UI/RhythmSidebar.cpp
        Source/UI/EuclideanPanel.cpp
        Source/UI/VoiceSection.cpp
        Source/UI/ModulatorEditor.cpp
        Source/UI/ModMatrixPanel.cpp
        Source/UI/ModulatorPanel.cpp
        Source/UI/RhythmPanel.cpp
        Source/FX/EffectSlot.cpp
        Source/FX/DelaySlot.cpp
        Source/FX/ReverbSlot.cpp
        Source/FX/FXChain.cpp
        Source/UI/FXRow.cpp
        Source/UI/DelayRow.cpp
        Source/Audio/MixerEngine.cpp
        Source/UI/Components/VUMeter.cpp
        Source/UI/MixerChannel.cpp
        Source/UI/MixerOverlay.cpp
)

target_compile_definitions(mu-clid
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_USE_MP3AUDIOFORMAT=1
)

target_link_libraries(mu-clid
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_gui_basics
        juce::juce_gui_extra
        juce::juce_audio_formats
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# Increment build_number.txt and regenerate Source/BuildNumber.h before every build.
add_custom_target(increment_build_number ALL
    COMMAND ${CMAKE_COMMAND} -P "${CMAKE_SOURCE_DIR}/cmake/IncrementBuildNumber.cmake"
    COMMENT "Incrementing build number"
    VERBATIM
)
add_dependencies(mu-clid increment_build_number)