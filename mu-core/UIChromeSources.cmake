# The mu-core .cpp files that need no processor: the shared components and the chrome built on
# the mu_core::ShellBackend interface (the TransportBar and the dialogs, overlays and settings base).
# One definition, used three ways: mu-core/CMakeLists.txt adds it to the INTERFACE source list (the
# plugins build exactly as before), mu-core-tests compiles it directly, and mu-link adds it to its
# own target, which does not link the INTERFACE library. A file belongs here only if neither it nor
# its header includes Plugin/ProcessorBase.h, Plugin/ProcessorCore.h or UI/EditorShellBase.h.
set(_mu_core_dir "${CMAKE_CURRENT_LIST_DIR}")
set(MU_CORE_CHROME_SOURCES
    # Look and feel, theme, standard controls
    "${_mu_core_dir}/UI/Components/MuLookAndFeel.cpp"
    "${_mu_core_dir}/UI/Components/MuTheme.cpp"
    "${_mu_core_dir}/UI/Components/BipolarSliderRow.cpp"
    "${_mu_core_dir}/UI/Components/KnobWithLabel.cpp"
    "${_mu_core_dir}/UI/Components/SegmentControl.cpp"
    "${_mu_core_dir}/UI/Components/SlideSwitch.cpp"
    "${_mu_core_dir}/UI/Components/NudgeInput.cpp"
    "${_mu_core_dir}/UI/Components/StatusLamp.cpp"
    "${_mu_core_dir}/UI/Components/StepIndicator.cpp"
    "${_mu_core_dir}/UI/Components/NoteLengthControl.cpp"
    "${_mu_core_dir}/UI/Components/DropdownSelect.cpp"
    "${_mu_core_dir}/UI/Components/StepEditor.cpp"
    "${_mu_core_dir}/UI/Components/LFOEditor.cpp"
    "${_mu_core_dir}/UI/Components/AddButton.cpp"
    "${_mu_core_dir}/UI/Components/StatusBar.cpp"
    "${_mu_core_dir}/UI/Components/VUMeter.cpp"
    # Mixer strips: the callback-driven half (MixerChannel_Bindings.cpp and MasterInsertBinding.cpp bind to ProcessorBase and stay out)
    "${_mu_core_dir}/UI/MixerChannel.cpp"
    "${_mu_core_dir}/UI/MasterInsertPanel.cpp"
    # Shared chrome over ShellBackend
    "${_mu_core_dir}/UI/TransportBar.cpp"
    "${_mu_core_dir}/UI/SettingsOverlayBase.cpp"
    "${_mu_core_dir}/UI/StandardSettingsOverlay.cpp"
    "${_mu_core_dir}/UI/AboutOverlay.cpp"
    "${_mu_core_dir}/UI/SaveDialog.cpp"
    "${_mu_core_dir}/UI/ModalDialog.cpp"
    "${_mu_core_dir}/UI/PresetBrowser.cpp"
    "${_mu_core_dir}/UI/ShellChrome.cpp"
    "${_mu_core_dir}/UI/PerformanceIndication.cpp"
    "${_mu_core_dir}/UI/PerformanceChooserModel.cpp"
    "${_mu_core_dir}/UI/PerformanceChooser.cpp"
    # The performance list the chrome reads (juce_core only), compiled with the chrome so mu-link links it too
    "${_mu_core_dir}/Persistence/PerformanceFile.cpp"
)
unset(_mu_core_dir)
