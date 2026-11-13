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

# The family typefaces and vector icons (docs/design-ui-family.md, Visual polish P1 and P6): Inter for UI text, JetBrains Mono for LCD and
# monospaced readouts, both SIL OFL 1.1, the unmodified upstream static files. Embedded once here; every consumer of
# mu-core links it, and MuLookAndFeel::uiFont / lcdFont are the only places a typeface is chosen.
juce_add_binary_data(mu-core-assets HEADER_NAME "MuAssetData.h" NAMESPACE MuAssetData
    SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/UI/Fonts/Inter-Regular.ttf"
        "${CMAKE_CURRENT_SOURCE_DIR}/UI/Fonts/Inter-Medium.ttf"
        "${CMAKE_CURRENT_SOURCE_DIR}/UI/Fonts/Inter-Bold.ttf"
        "${CMAKE_CURRENT_SOURCE_DIR}/UI/Fonts/JetBrainsMono-Regular.ttf"
        "${CMAKE_CURRENT_SOURCE_DIR}/UI/Fonts/JetBrainsMono-Bold.ttf"
        # Vector icons (Visual polish P6): one white-filled SVG each, tinted from a token at draw time (UI/Components/SvgIcon.h).
        "${CMAKE_CURRENT_SOURCE_DIR}/UI/Assets/play.svg"
        "${CMAKE_CURRENT_SOURCE_DIR}/UI/Assets/stop.svg"
        "${CMAKE_CURRENT_SOURCE_DIR}/UI/Assets/walk.svg"
        "${CMAKE_CURRENT_SOURCE_DIR}/UI/Assets/loop.svg"
        "${CMAKE_CURRENT_SOURCE_DIR}/UI/Assets/metronome.svg"
        "${CMAKE_CURRENT_SOURCE_DIR}/UI/Assets/wave.svg")
set_target_properties(mu-core-assets PROPERTIES POSITION_INDEPENDENT_CODE TRUE)
# juce_animation rides with the asset target because every target that compiles the shell chrome already links it.
target_link_libraries(mu-core-assets INTERFACE juce::juce_animation)
target_link_libraries(mu-core INTERFACE mu-core-assets)

# The processor-free UI sources (components, TransportBar, dialogs): one list, shared with mu-link
# and mu-core-tests. Defines MU_CORE_CHROME_SOURCES.
include("${CMAKE_CURRENT_SOURCE_DIR}/UIChromeSources.cmake")

# Pin C++ standard explicitly so a future JUCE bump cannot silently change it.
target_compile_features(mu-core INTERFACE cxx_std_17)

target_sources(mu-core INTERFACE
    ${MU_CORE_CHROME_SOURCES}
    # Modulation
    "${CMAKE_CURRENT_SOURCE_DIR}/Modulation/ModulationMatrix.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Sequencer/ControlSequence.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Sequencer/EuclideanGenerator.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Sequencer/HitGenerator.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Sequencer/Rhythm.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Sequencer/RhythmSequencer.cpp"
    # Analysis
    "${CMAKE_CURRENT_SOURCE_DIR}/Analysis/OnsetDetector.cpp"   # onset detection for loop slicing
    "${CMAKE_CURRENT_SOURCE_DIR}/Audio/LoopLoader.cpp"   # decodes a whole-loop file for a layer (cap, no resample)
    # Audio
    "${CMAKE_CURRENT_SOURCE_DIR}/Audio/VoiceEngine.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Audio/InsertProcessor.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Audio/MultiModeFilter.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Audio/SamplePlayer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Audio/SamplePreview.cpp"   # the file browser's preview player (lifted from mu-Clid)
    "${CMAKE_CURRENT_SOURCE_DIR}/Audio/SampleLibrary.cpp"   # per-slot sample paths and the primary sample folder
    "${CMAKE_CURRENT_SOURCE_DIR}/Audio/MixerEngine.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Audio/MidiOutputEngine.cpp"
    # FX slots
    "${CMAKE_CURRENT_SOURCE_DIR}/Audio/FX/Slots/EffectSlot.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Audio/FX/Slots/DelaySlot.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Audio/FX/Slots/ReverbSlot.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Audio/FX/Slots/FXChain.cpp"
    # Shared UI components
    "${CMAKE_CURRENT_SOURCE_DIR}/UI/EuclideanPanel.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/UI/RhythmCircle.cpp"
    # Shared mixer/FX panels
    "${CMAKE_CURRENT_SOURCE_DIR}/UI/FXRow.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/UI/DelayRow.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/UI/MixerChannel_Bindings.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/UI/MasterInsertBinding.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/UI/MixerOverlay.cpp"
    # Shared plugin scaffolding
    "${CMAKE_CURRENT_SOURCE_DIR}/Plugin/ProcessorBase.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Plugin/ProcessorBase_Settings.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Plugin/FxParamSync.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Plugin/ProcessorBase_Presets.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Plugin/ProcessorBase_LayerEdits.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Plugin/ProcessorBase_LayerTypes.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Plugin/ProcessorBase_Clips.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Plugin/ProcessorBase_Render.cpp"   # the layer render contract's block driver
    "${CMAKE_CURRENT_SOURCE_DIR}/Plugin/ProcessorBase_ClipAutoSave.cpp"   # clip auto-save: poll and compare
    # The headless --render mode is header-only (Plugin/ProductRender.h), compiled
    # into each Standalone target through Plugin/StandaloneShell.h.
    # Persistence — MIDI PC preset maps (per-slot ch 1-8 + full-preset ch 9).
    "${CMAKE_CURRENT_SOURCE_DIR}/Persistence/MidiPresetMap.cpp"
    # MIDI control mapping (CC / note -> family actions): the saved map + the audio-thread router.
    "${CMAKE_CURRENT_SOURCE_DIR}/Control/MidiControlMap.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Control/MidiControlRouter.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Control/LiveState.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Persistence/MidiFullPresetMap.cpp"
    # Shared MIDI PC editor panels (the preset browser is in the chrome list).
    "${CMAKE_CURRENT_SOURCE_DIR}/UI/MidiPresetListPanel.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/UI/SampleBrowser.cpp"   # the sample file browser with preview (lifted from mu-Clid)
    "${CMAKE_CURRENT_SOURCE_DIR}/UI/MidiPresetsOverlay.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/UI/MidiFullPresetsOverlay.cpp"
    # Shared settings page (the framework base is in the chrome list).
    # Shared transport "Master Loop" sub-pane — driven by ProcessorBase virtuals + mstrLoop param.
    "${CMAKE_CURRENT_SOURCE_DIR}/UI/MasterLoopSection.cpp"
    # Shared modal overlays that need the processor.
    "${CMAKE_CURRENT_SOURCE_DIR}/UI/ActivationOverlay.cpp"
    # Shared spec-driven wrapping grid of APVTS-bound knobs/combos (engine surface).
    "${CMAKE_CURRENT_SOURCE_DIR}/UI/ParamKnobGrid.cpp"
    # Shared editor shell — chrome / overlay state machine / main-area layout.
    "${CMAKE_CURRENT_SOURCE_DIR}/UI/EditorShellBase.cpp"
    # Shared modulator panels — product supplies its destination provider.
    "${CMAKE_CURRENT_SOURCE_DIR}/UI/ModulatorPanel.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/UI/ModulatorEditor.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/UI/ModMatrixPanel.cpp"
    # Shared left sidebar — product injects the per-layer mini-graphic.
    "${CMAKE_CURRENT_SOURCE_DIR}/UI/SidebarItem.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/UI/LayerSidebar.cpp"
    # Shared scene pads (eight lamp pads under a caption) at the sidebar's foot, opt-in.
    "${CMAKE_CURRENT_SOURCE_DIR}/UI/SceneGrid.cpp"
    # Shared per-layer header bar (name / reset / delete / preset / save).
    "${CMAKE_CURRENT_SOURCE_DIR}/UI/LayerHeaderBar.cpp"
    # Shared per-layer clip strip (eight lamp pads), a child of the header bar.
    "${CMAKE_CURRENT_SOURCE_DIR}/UI/ClipStrip.cpp"
    # Shared per-layer panel shell (header strip + product content + modulators).
    "${CMAKE_CURRENT_SOURCE_DIR}/UI/LayerPanel.cpp"
    # Shared add / delete / reorder editor glue for products with variable layers.
    "${CMAKE_CURRENT_SOURCE_DIR}/UI/LayerEditController.cpp"
    # Shared insert-effect voice subsection (algo dropdown + 4 slot knobs).
    "${CMAKE_CURRENT_SOURCE_DIR}/UI/Voice/InsertSubsection.cpp"
    # Shared voice band (Pitch / Filter / Amp / Effects layout + drawing) and its cell-grid section.
    "${CMAKE_CURRENT_SOURCE_DIR}/UI/Voice/VoiceBand.cpp"
    # Shared wavetable bank (mip-mapped tables + procedural factory set) — mu-Tant, mu-Toni.
    "${CMAKE_CURRENT_SOURCE_DIR}/Audio/Wavetable/WavetableBank.cpp"
)

# Consumers can `#include "Audio/VoiceEngine.h"` etc. — same path namespace as
# the old single-plugin layout, so source files in mu-core or in any consuming
# plugin compile without changing existing include directives.
target_include_directories(mu-core INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

# Monocypher header path for License/LicenseManager.cpp (the offline-license verifier).
# The verifier source LIVES in mu-core (License/LicenseManager.cpp) but is compiled per
# product — each licensed product adds it + monocypher.c to its own target_sources (not
# the INTERFACE source list, so non-licensed siblings like mu-on/mu-toni/mu-link don't
# pull in monocypher). This SYSTEM include just lets the verifier find <monocypher.h>.
target_include_directories(mu-core SYSTEM INTERFACE
    "${CMAKE_SOURCE_DIR}/ThirdParty/monocypher"
)

# mu-core as compiled static libraries, built once per configuration instead of being recompiled inside every consumer (an
# INTERFACE library adds its sources to each target that links it, ~95 files and most of a target's compile time).
# The sources read per-binary settings (MU_MAX_LAYERS, MU_MAX_BUSES, MU_BUS_INSERTS, MU_EXT_SIDECHAIN, MU_VERSION_BASE), and those
# change the layout of shared types, so there is one library per distinct set of settings ("variant"). A product asks for its
# variant with mu_core_variant() and gets the settings from it, so the library and the product can never disagree.
# The INTERFACE mu-core above stays for the test apps that compile the sources they need themselves.


# JUCE's include paths, definitions and options WITHOUT its module sources. A JUCE module target adds its source wrapper to whatever
# target links it, so linking one here would compile JUCE inside the library as well as in each consumer; this target carries only
# what the compiler needs to read JUCE headers, and the consumers (which link the modules themselves) provide the compiled code.
set(MU_JUCE_MODULES juce::juce_audio_utils juce::juce_dsp juce::juce_gui_basics juce::juce_gui_extra juce::juce_audio_formats juce::juce_animation)
set(_mu_todo ${MU_JUCE_MODULES})
set(_mu_seen "")
while(_mu_todo)
    list(POP_FRONT _mu_todo _mu_t)
    if(_mu_t IN_LIST _mu_seen)
        continue()
    endif()
    list(APPEND _mu_seen ${_mu_t})
    get_target_property(_mu_deps ${_mu_t} INTERFACE_LINK_LIBRARIES)
    foreach(_mu_d IN LISTS _mu_deps)
        if(_mu_d MATCHES "^juce::juce_[a-z_0-9]+$")
            list(APPEND _mu_todo ${_mu_d})
        endif()
    endforeach()
endwhile()
add_library(mu-juce-usage INTERFACE)
foreach(_mu_t IN LISTS _mu_seen)
    target_include_directories(mu-juce-usage INTERFACE "$<TARGET_PROPERTY:${_mu_t},INTERFACE_INCLUDE_DIRECTORIES>")
    target_include_directories(mu-juce-usage SYSTEM INTERFACE "$<TARGET_PROPERTY:${_mu_t},INTERFACE_SYSTEM_INCLUDE_DIRECTORIES>")
    target_compile_definitions(mu-juce-usage INTERFACE "$<TARGET_PROPERTY:${_mu_t},INTERFACE_COMPILE_DEFINITIONS>")
    target_compile_options(mu-juce-usage INTERFACE "$<TARGET_PROPERTY:${_mu_t},INTERFACE_COMPILE_OPTIONS>")
endforeach()
# The library itself links no JUCE target (so it holds no JUCE code); it reads the embedded-asset header through the usage target.
target_include_directories(mu-juce-usage INTERFACE "$<TARGET_PROPERTY:mu-core-assets,INTERFACE_INCLUDE_DIRECTORIES>")



# mu_core_variant(<out-var> [LAYERS n] [BUSES n] [INSERTS n] [EXT_SIDECHAIN 0|1] [VERSION_BASE x.y])
# Creates (once) the compiled library for these settings and sets <out-var> to the target a product links. Omitted settings keep the
# MuLimits.h / BuildNumber.h defaults. The settings are public on the library, so every product that links it is compiled with the
# same values the library was; a product must not define them itself.
function(mu_core_variant out_var)
    cmake_parse_arguments(V "" "LAYERS;BUSES;INSERTS;EXT_SIDECHAIN;VERSION_BASE" "" ${ARGN})
    set(key "")
    set(defs "")
    foreach(pair LAYERS:MU_MAX_LAYERS BUSES:MU_MAX_BUSES INSERTS:MU_BUS_INSERTS EXT_SIDECHAIN:MU_EXT_SIDECHAIN)
        string(REPLACE ":" ";" pair "${pair}")
        list(GET pair 0 arg)
        list(GET pair 1 macro)
        if(NOT "${V_${arg}}" STREQUAL "")
            string(APPEND key "-${arg}${V_${arg}}")
            list(APPEND defs "${macro}=${V_${arg}}")
        endif()
    endforeach()
    # A base equal to the live one is the default, so products on the same version share a library; it only splits when they differ.
    if("${V_VERSION_BASE}" STREQUAL "${MUFAMILY_VERSION_BASE}")
        set(V_VERSION_BASE "")
    endif()
    if(NOT "${V_VERSION_BASE}" STREQUAL "")
        string(APPEND key "-v${V_VERSION_BASE}")
        list(APPEND defs "MU_VERSION_BASE=\"${V_VERSION_BASE}\"")
    endif()
    string(REPLACE "." "_" key "${key}")
    set(lib "mu-core-lib${key}")
    if(NOT TARGET ${lib})
        get_target_property(mu_srcs mu-core INTERFACE_SOURCES)
        set(stat "mu-core-static${key}")
        add_library(${stat} STATIC)
        target_sources(${stat} PRIVATE ${mu_srcs})
        target_include_directories(${stat} PUBLIC "${CMAKE_CURRENT_FUNCTION_LIST_DIR}")
        target_include_directories(${stat} SYSTEM PUBLIC
            "${CMAKE_SOURCE_DIR}/ThirdParty/monocypher"
            "${CMAKE_SOURCE_DIR}/ThirdParty/signalsmith-basics/include"
            "${CMAKE_SOURCE_DIR}/ThirdParty/signalsmith-dsp/include"
            "${CMAKE_SOURCE_DIR}/ThirdParty/signalsmith-linear/include"
            "${CMAKE_SOURCE_DIR}/ThirdParty/signalsmith-stretch/include")
        target_compile_features(${stat} PUBLIC cxx_std_17)
        # The JUCE settings every consumer uses (they are the same for all targets), then this variant's own.
        target_compile_definitions(${stat} PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_USE_MP3AUDIOFORMAT=1 MU_CORE_STATIC=1 ${defs})
        add_dependencies(${stat} mu-core-assets)
        # ProcessorBase gains a CLAP base class when the CLAP header is visible (MU_CORE_HAS_CLAP), which changes its layout; every product
        # target already sees that header, so the library does too, and its code and the products agree on one layout.
        target_link_libraries(${stat} PUBLIC clap_juce_extensions)
        target_link_libraries(${stat}
            PRIVATE mu-juce-usage
                $<COMPILE_ONLY:juce::juce_recommended_config_flags> $<COMPILE_ONLY:juce::juce_recommended_lto_flags>
                $<COMPILE_ONLY:juce::juce_recommended_warning_flags>)
        # What a product links: the compiled library plus the embedded assets (which bring juce_animation and, through it, the
        # JUCE modules the product then compiles itself, as before).
        add_library(${lib} INTERFACE)
        target_link_libraries(${lib} INTERFACE ${stat} mu-core-assets)
    endif()
    set(${out_var} ${lib} PARENT_SCOPE)
endfunction()

# Unit tests for shared mu-core code (no product linked).
add_subdirectory(Tests)
