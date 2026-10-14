# mu-core sources behind ProcessorBase (the settings page included), for console test targets.
#
# A console target cannot link the INTERFACE mu-core library, so each test target that builds a
# processor adds these .cpp files itself: mu-core-tests and every product's tests include this file
# and add ${MU_CORE_PROCESSOR_TEST_SOURCES} to target_sources. A product adds its own
# PluginProcessor*.cpp, engine and sequencer .cpps (and the licence .cpps when licensed) on top.
set(_mu_core_dir "${CMAKE_CURRENT_LIST_DIR}/..")
set(MU_CORE_PROCESSOR_TEST_SOURCES
    "${_mu_core_dir}/Plugin/ProcessorBase.cpp"
    "${_mu_core_dir}/Audio/VoiceEngine.cpp"
    "${_mu_core_dir}/Audio/InsertProcessor.cpp"
    "${_mu_core_dir}/Audio/MultiModeFilter.cpp"
    "${_mu_core_dir}/Audio/SamplePlayer.cpp"
    "${_mu_core_dir}/Audio/MixerEngine.cpp"
    "${_mu_core_dir}/Audio/MidiOutputEngine.cpp"
    "${_mu_core_dir}/Audio/FX/Slots/EffectSlot.cpp"
    "${_mu_core_dir}/Audio/FX/Slots/DelaySlot.cpp"
    "${_mu_core_dir}/Audio/FX/Slots/ReverbSlot.cpp"
    "${_mu_core_dir}/Audio/FX/Slots/FXChain.cpp"
    "${_mu_core_dir}/Persistence/MidiPresetMap.cpp"
    "${_mu_core_dir}/Persistence/MidiFullPresetMap.cpp"
    "${_mu_core_dir}/Control/MidiControlMap.cpp"
    "${_mu_core_dir}/Control/MidiControlRouter.cpp"
    "${_mu_core_dir}/Control/LiveState.cpp"             # Performance mode state (ProcessorBase owns one)
    "${_mu_core_dir}/Persistence/PerformanceFile.cpp"   # the list LiveState holds
    "${_mu_core_dir}/UI/SettingsOverlayBase.cpp"
    "${_mu_core_dir}/UI/StandardSettingsOverlay.cpp"
    "${_mu_core_dir}/UI/Components/MuLookAndFeel.cpp"
    "${_mu_core_dir}/UI/Components/MuTheme.cpp"
    "${_mu_core_dir}/UI/Components/KnobWithLabel.cpp"
    "${_mu_core_dir}/UI/Components/SegmentControl.cpp"
    "${_mu_core_dir}/UI/Components/NudgeInput.cpp"
    "${_mu_core_dir}/UI/Components/DropdownSelect.cpp"
    "${_mu_core_dir}/UI/Components/StatusLamp.cpp"      # the settings page's Controller lamp
    "${_mu_core_dir}/Modulation/ModulationMatrix.cpp"   # pulled in by Layer.h
    "${_mu_core_dir}/Sequencer/ControlSequence.cpp"     # pulled in by Layer.h
)
unset(_mu_core_dir)
