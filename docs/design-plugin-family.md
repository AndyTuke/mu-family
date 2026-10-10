# μ-family Plugin Architecture

This document covers the shared-code strategy for building additional plugins in the μ-family (mu-tant, mu-toni, mu-on, and future siblings) that reuse mu-clid's voice chain, modulation system, and mixer.

**Siblings already built on this platform:** **mu-tant** (wavetable drone synth — per-voice oscillators + gater), **mu-on** (909 groove sequencer — five fixed engine lanes + a per-lane `ModulationMatrix` resolved through the shared `mu_mod::resolveLane` helper), and **mu-toni** (generative arpeggiator mono-synth — a scale/chord note pool scanned by a deterministic skewed-triangle walk into a note-triggered voice, with a per-voice `ModulationMatrix`). Each supplies only its engine(s) + sequencer/trigger model and inherits `ProcessorBase`, `EditorShellBase`, the mixer/FX rack, and the modulation system unchanged.

---

## Goal

A **mu-tant** / **mu-toni** plugin will use the same:
- Voice engine (sample playback, ADSR, filter, insert processing)
- Modulation system (ControlSequence LFO/step modulators, ModulationMatrix assignments)
- Mixer (per-channel sends, sidechain, returns, master insert, FX chain)

…but swap in a **different sequencer / trigger engine** in place of mu-clid's Euclidean generators.

The code must be structured so these shared components live in a single `mu-core` library that both mu-clid and its siblings (and any future μ plugin) link against, with zero duplication.

---

## UX parity principle (firm)

Every mu-family product has an **identical UX and GUI**. The **only** part that
differs per product is the **synth / voice engine** (the region the user marks
with a red outline on a screenshot). The audio stream a product's engine
produces is fed **into the insert effect and then onto the mixer** — that signal
flow (`engine → insert → mixer`) is the same for every product.

Consequences — treat these as firm rules, not preferences:

- **Identical, shared, single-source.** The shell, sidebar + 8-layer management
  (select / **add** / **delete** / reorder), preset load/save (full **and**
  per-layer), panel **styling + colouring**, the **filter / insert effect /
  mixer** experience, and the modulators are **the same across all products** and
  therefore live in `mu-core`. A product never re-implements them.
- **No duplicated code.** Any implementation common to more than one product is
  centralised in `mu-core`. If a feature is built for one product and another
  needs it, **lift it to `mu-core`** rather than copy it. Plan + do this whenever
  it comes up — it does not need re-confirming.
- **Mirror mu-clid; document the choice.** mu-clid is the reference. For any
  design question, consult the mu-clid **design docs**; if it isn't documented,
  derive it from the mu-clid **implementation**, **record the decision in the
  mu-clid design docs**, then apply it to the sibling. Every design choice gets
  written down.
- **Product-specific = the voice ENGINE only.** A product supplies its engine
  UI + DSP and its trigger model. The engine region **includes mu-clid's pitch /
  filter / amp ENVELOPE sections** — those are mu-clid's engine, **not** shared.
  - mu-clid engine: sample playback + Euclidean trigger + pitch/filter/amp ADSR.
  - mu-tant engine: oscillators + the **Gater** (gate pattern) + **modulation**
    controlling parameters. The Gater replaces amp ADSR; **drawable PITCH and FILT
    envelope layers** (added in #760 / #735) shape pitch and filter cutoff
    per-voice via the GatingDesigner. No sample-playback / Euclidean trigger.
    (Its filter cutoff/res/type are mu-tant engine controls, not the shared
    mu-clid filter-with-ADSR.)
  The engine's output is fed `engine → insert → mixer`. Everything else (shell,
  sidebar/layers, presets, insert effect, mixer, modulators, panel styling) is
  inherited from `mu-core`.

---

## Platform contract

The standard mu platform is everything in `mu-core/`. New products link `mu-core` and supply their own sequencer / engine glue / UI under `<product>/Source/`.

**Repo layout for a new product:**

```
<product>/
  CMakeLists.txt           juce_add_plugin + target_sources + target_link_libraries(... mu-core)
  resources/               icon set, logo
  installer/               Inno Setup .iss
  Source/
    Plugin/                PluginProcessor (inherits ProcessorBase), PluginEditor, StandaloneApp
                           (a thin mu_standalone::App — the window, mu-link bridge and headless
                           --render are shared), product hot-swap / preset glue
    Sequencer/             Product-specific sequencer (replaces mu-clid's Rhythm/HitGenerator/
                           SequencerEngine/EuclideanGenerator)
    UI/                    Product-specific panels — sidebars, main panels, voice subsections.
                           Always built from mu-core/UI/Components/ widgets, never one-offs.
    Persistence/           Product-specific param tables, preset I/O
    License/               LicenseChecker (Monocypher-based; same pattern as mu-clid)
    Tests/                 JUCE UnitTest subclasses for data-layer regressions
```

**What `mu-core` provides** (do not duplicate in a product):

- `Plugin/ProcessorBase` — abstract base that owns `fxChain` + `mixerEngine` + `processCoreBlock()`. Every product's `PluginProcessor` inherits this.
- `Audio/VoiceEngine` — per-slot voice chain: sample player → filter → amp env → insert FX. Engine-level ADSR, not per-voice.
- `Audio/MixerEngine` — channel strips, sends, sidechain, FX returns, master inserts.
- `Audio/FX/Slots/{Effect,Delay,Reverb,FXChain}` — global FX chain.
- `Audio/MultiModeFilter`, `Audio/InsertProcessor`, `Audio/SamplePlayer`, `Audio/MidiOutputEngine`.
- `Modulation/ModulationMatrix` + `Sequencer/ControlSequence` — modulation system. Product's slot type inherits `Sequencer/Layer`. `Modulation/ModulatorSerialise.h` is the shared (de)serialise for a `Layer`'s ControlSequences + matrix assignments (products inject their own source/dest ID validators).
- `UI/Components/` — every standard widget (knob, dropdown, segment, step editor, LFO editor, VU meter, status bar).
- `UI/MixerChannel`, `UI/MixerOverlay`, `UI/FXRow`, `UI/DelayRow` — shared mixer + FX panels.
- `UI/ChannelSidebar` + `UI/SidebarItem` — the shared left "layers" sidebar (select / add / delete / drag-reorder). Reads channel metadata from `ProcessorBase::getNumChannels/getChannelName/getChannelColourIndex`. The per-layer mini-graphic (and its animation) is the only product-specific part, injected via `createMiniVisual` (mu-clid → `RhythmMiniVisual` wrapping a `RhythmCircle`; mu-tant → a voice glyph). Reorder + hot-swap semantics are product hooks (`onSwapChannels`, `isPendingSwap`, `onCancelPendingSwap`) wired by each product to its own stager (every product implements hot-swap on the shared `mu_hotswap::Stager` — see [Hot-swap](#hot-swap-staged-preset--layer-swaps-family-pattern) below). Add/delete is driven by the product (`onAddChannel` + a panel delete button → `addVoice`/`removeVoice` in mu-tant, `rhythms.add`/`rhythms.remove` (`RhythmManager`) in mu-clid).
- `UI/ChannelHeaderBar` — the shared per-layer header (colour dot · editable name · reset · delete · per-layer preset dropdown · Save). `UI/LayerPresetHeader.h` (`mu_ui::wireSlotPresetHeader` + `refreshSlotPresetList`) wires its reset (confirmed) / preset load / save (name prompt) to the ProcessorBase slot-preset API, so panels only add delete / rename where they have them Products with fixed layers (mu-Toni, mu-On) hide delete and rename (`setShowDelete(false)`, `setNameEditable(false)`); `setPresetFiles` + `onPresetFileChosen` fill the list from files and hand back the chosen one.
- `Persistence/ScopedApvtsLoading.h` (`mu_core::ScopedApvtsLoading`) — RAII guard that holds a processor's `std::atomic<bool> apvtsLoading` flag true during a bulk APVTS push so UI listeners early-out; the host class exposes `isApvtsLoading()`.
- `Persistence/PresetFiles.h` (`mu_pp`) — shared preset-file handling: safe file names, atomic writes, listing preset files, the category list, and write / read a full preset (the state tree wrapped in `<Mu…Preset name description category>`). The state inside — layer nodes, globals, the composed state — is `Persistence/LayerState.h`.
- `Persistence/LayerState.h` (`mu_pp`) — the composed slot state every product saves and loads (see [Slot state](#slot-state-presets--sessions--family-standard)): `LayerLayout`, `LayerExtras`, `captureSlot` / `applySlot`, `captureState` / `applyState`, `composeLegacyState`; wrapped by `ProcessorBase::initLayerState` and its capture / apply helpers.
- `Persistence/PresetMeta.h` (`mu_pp::readPresetMeta`) — a preset's root tag + root attributes (category, description, embed flag, e.g. mu-On's `lane`) read from the opening tag only, never the body. It understands both file shapes (the wrapper's `category` / `description` attributes and mu-Clid's `presetCategory` / `presetDescription`). Every preset list reads files through it — never `parseXML` a whole folder.
- `UI/StandardSettingsOverlay::addProgramChangeSection(layerTable, fullTable)` — the shared MIDI Program Change section (buttons opening the shared program-change tables). Every product with presets calls it; MIDI program changes are queued on the audio thread (`scanMidiProgramChanges` → `triggerAsyncUpdate`) and loaded in `handleAsyncUpdate` (`drainPendingMidiProgramChanges`).
- `UI/Voice/VoiceBand` — the shared voice band (mu-Clid's layout): Pitch | Filter | Amp | Effects in raised boxes, the FX sends right-aligned in the Effects box beside a narrowed insert dropdown. Products supply the four section components (binding stays product-side); `VoiceBandSection` builds a section from controls a product already owns (two Size-2 rows, `place(control, row, col, span, dropdown)`). Used by mu-Clid (`VoiceSection` subclasses it) and mu-Toni (`EnginePanel`); mu-Tant's voice panel is a different structure and stays product-side.
- `UI/Voice/InsertSubsection` — the shared insert-effect voice subsection (algorithm dropdown + 4 generic slot knobs), bound to a channel via a constructor prefix (`"r"` / `"v"`); optional product hooks for mod-arc indicators, the Comp/Limiter GR meter, and the algo-switch bulk-write wrapper. Part of the voice section (engine → insert → mixer).

**The swap-out point:**

`mu-clid/Source/Sequencer/` defines what a "rhythm" is in μ-Clid: Euclidean generators, hit table, step-relative modulation timing. A new product replaces that file set with whatever drives it — a step matrix, a probability engine, a phrase grid, a MIDI re-trigger. The product still produces hits/voices that feed `VoiceEngine::trigger()` from `mu-core`, so everything downstream (voice chain, modulation, FX, mixer, UI controls) just works.

**Boundary rule:** anything in `mu-core/` must be plugin-agnostic. If a `mu-core` source file reaches for `Rhythm`, `HitGenerator`, or a `PluginProcessor` subclass, it has crossed the line — lift the dependency back out into the consuming plugin's tree.

**Wiring a new product into the build:**

1. Implement the source files under `<product>/Source/`.
2. Replace the placeholder `<product>/CMakeLists.txt` with a real `juce_add_plugin` (copy mu-clid/CMakeLists.txt and adjust).
3. Add `add_subdirectory(<product>)` to the family root `CMakeLists.txt`.

---

## Current shared / product-specific split

The `mu-core` INTERFACE library (introduced in Stage 33) holds everything shared; each product supplies only its engine + engine UI. Paths are relative to the repo root.

### Shared (mu-core) components

| Component | File(s) | Notes |
|---|---|---|
| `ProcessorBase` | `mu-core/Plugin/ProcessorBase.{h,cpp}` | Abstract base; owns apvts + fxChain + mixerEngine + `processCoreBlock()` + MIDI-PC preset plumbing + `syncGlobalFxParam()` (maps a mixer/FX/return/master/`ch{N}_` APVTS id → fxChain/mixerEngine state; prefix-routes to per-family helpers); also the internal transport (`internalPlaying` / `internalBpm` / `internalBeatPos` + the play / BPM overrides) and the family bus layout (`isBusesLayoutSupported`: one optional stereo sidechain in, stereo out) |
| `MixerFxParams` | `mu-core/Plugin/MixerFxParams.h` | `mu_mixfx::addGlobalFxParams(layout)` declares the shared global-FX / return / master param set (`eff_`/`dly_`/`rev_`/`eff2*`/`echo_`/`ret_*`/`mstr_lvl`/`mstr_pan`/`mst_ins*`) the MixerOverlay/FXRow/DelayRow bind to. **Strips:** `mu_mixfx::addChannelStripParams(layout, "ch{N}_", name, options)` declares each channel strip's 12 params (options: version hint / continuous ranges for mu-clid's legacy ids, per-product sidechain defaults); the product adds only its own globals (e.g. mu-clid `mstrLoop`). **Sync contract:** each product calls `ProcessorBase::startFxParamSync()` once at the end of its constructor — the base listens to every mixer / FX id, routes changes to `syncGlobalFxParam` and seeds the engines; a product's own listener (mu-clid's rhythm params) skips those ids |
| `VoiceEngine` | `mu-core/Audio/VoiceEngine.{h,cpp}` | Per-slot voice chain; no PluginProcessor dependency |
| `MixerEngine` | `mu-core/Audio/MixerEngine.{h,cpp}` | Channel strips / sends / sidechain / returns / master; optional per-channel render hook (`RenderChannelFn`) |
| `InsertProcessor` | `mu-core/Audio/InsertProcessor.{h,cpp}` | Parameterised entirely via `VoiceParams` |
| `MultiModeFilter`, `SamplePlayer`, `MidiOutputEngine` | `mu-core/Audio/` | Generic audio units |
| `FXChain` + slots | `mu-core/Audio/FX/` | Generic send / return / insert chain |
| `ModulationMatrix`, `ControlSequence`, `ModulatorSerialise` | `mu-core/Modulation/`, `mu-core/Sequencer/` | Modulation system + shared (de)serialise; product's slot inherits `Layer` |
| UI widgets | `mu-core/UI/Components/` | Knob, dropdown, segment, step / LFO editor, VU, status bar, `MuLookAndFeel` |
| `MixerChannel`, `MixerOverlay`, `FXRow`, `DelayRow` | `mu-core/UI/` | Shared mixer + FX panels |
| `ChannelSidebar` + `SidebarItem` | `mu-core/UI/` | Shared layers sidebar (product injects the mini-graphic) |
| `ChannelHeaderBar` | `mu-core/UI/` | Shared per-layer header (name / reset / delete / preset / save) |
| `VoiceBand` / `VoiceBandSection` | `mu-core/UI/Voice/` | Shared voice band layout + drawing (Pitch / Filter / Amp / Effects) |
| `InsertSubsection` | `mu-core/UI/Voice/` | Shared insert-effect voice subsection (channel-prefix bound) |
| `mu_hotswap::Stager` / `BarLineSwapper` | `mu-core/Plugin/HotSwap.h` | Shared hot-swap staging state + loop-wrap predicates; `BarLineSwapper<Payload, N>` is the whole bar-line driver (stage-or-apply, commit, audio-thread flagging) for a product with one payload type and a fixed loop (mu-Toni, mu-On) |
| Per-slot preset API | `ProcessorBase` (`slotPresetFiles` / `saveLayerPreset` / `loadLayerPreset` / `resetSlot` / `hasPendingSwap` + `onLayerPresetCommitted`) | One name for a layer's preset save / load / reset in every product; MIDI program-change slots load through it (`applyMidiPresetLayer` default) |
| Default preset at launch | `ProcessorBase` (`getDefaultPresetFile` / `loadDefaultPreset` / `loadStartupDefault` / `skipAutoLoadDefault`) | Every product restores `<presets>/_default.<ext>` at the end of its constructor; render mode skips it |
| Transport rule | `mu-core/Plugin/TransportResolver.h` | `mu_core::resolveTransport` — where every product's block play state, tempo and beat come from (see [Transport rule](#transport-rule--family-standard)) |
| Headless render | `mu-core/Plugin/ProductRender.h` + `StandaloneShell.h` | `--render` for every product (preset / swap / slot swap / MIDI PC / play flags) on the ProcessorBase preset API; products override only `prepareRender` / `renderPlaysByDefault` |
| MIDI-clock tempo | `mu-core/Plugin/MidiClockTempo.h` | The one tempo PLL (jitter-rejecting) used by `MidiClockSync` and mu-link's `MidiClockEstimator` |
| Timed MIDI out | `mu-core/Plugin/TimedMidiOut.h` | `mu_core::TimedMidiOut` — lock-free queue of time-stamped short MIDI messages plus a sender thread that sends each when due, to a port it never owns. Used by mu-link's `MidiClockOut` and `MuLinkBridge` (see [Device MIDI output](#device-midi-output--family-standard)) |
| Atomic file writes | `mu-core/Persistence/PresetFiles.h` | `mu_pp::replaceFileAtomically` / `writeXmlAtomically` (temp + rename, failure reported) — every preset / map save goes through them |
| `ExpDecay` | `mu-core/Audio/ExpDecay.h` | One-multiply exponential decay envelope — use it instead of `std::exp` per sample |
| `ModulatorPanel`, `ModMatrixPanel`, `ModulatorEditor` | `mu-core/UI/` | Shared modulator UI (take `Layer&` + a product `ModDestProvider`) |
| `EditorShellBase`, `TransportBar`, `AboutOverlay`, `SaveDialog`, `PresetBrowser`, MIDI-preset panels | `mu-core/UI/` | Shared editor shell + chrome |
| `mu_fmt` value text | `mu-core/ValueFormat.h` | One formatter set (`time` / `parseTime` / `freq` / `lowCut` / `percent`) for parameter text, knob text and status-bar text — never re-write a formatter in a product |
| `mu_ui::choiceNames` / `addChoiceItems` | `mu-core/UI/ParamChoices.h` | Fill a selector from its `AudioParameterChoice`, so the shown list can't drift from the stored one |
| Spin lock helpers | `mu-core/Audio/SpinLock.h` | `mu_core::trySpinLock` (audio thread), `spinLock` / `spinLockFor(n)` (message thread), `spinUnlock`, `ScopedSpinLock` — on any `std::atomic<bool>` / `CopyableSpinLock` flag; never hand-roll a `compare_exchange` loop |
| Filter families | `mu-core/Audio/Filters/FilterFamilies.h` | `SvfFilter12<type>`, `LadderFilter24<mode>`, `StereoBiquadFilter<setCoefficients>`, `CombFeedbackFilter<sign>` — a new filter of an existing family is a one-line class |
| Selector lists | `mu-core/Audio/AlgorithmNames.h` | `populateFilterTypeDropdown` / `populateInsertAlgoDropdown` — every filter / insert selector uses them (display order + ids in one place) |
| Preset lists | `mu-core/Persistence/PresetFiles.h`, `mu-core/Persistence/PresetMeta.h`, `mu-core/UI/PresetDropdown.h` | `mu_pp::listPresetsByCategory` (metadata via `mu_pp::readPresetMeta`) + `mu_ui::fillPresetDropdown` — the category-sorted preset selector (transport bar, layer header) |
| `SaveDialog` | `mu-core/UI/SaveDialog.h` | The one save card (`setTitle` / `setEmbedLabel` / `setDefaultDescription`; compact without a logo) — layer presets use it too |
| `MidiPresetListPanel` | `mu-core/UI/MidiPresetListPanel.{h,cpp}` | Shared body of the MIDI program-change panels (128-row list, Browse / Clear); subclasses supply map access + top-row controls |
| Standard full presets | `ProcessorBase` (`getFullPresetTag` / `captureFullPreset` / `useLoadedFullPreset`) | Save / load / category list for any product naming its preset tag; mu-Clid keeps PresetIO |
| Controller-input engine | `mu-core/Control/` (`ControlAction.h`, `ControlSink.h`, `MidiControlMap.{h,cpp}`, `MidiControlRouter.{h,cpp}`, `ParamGestureHold.h`; built, build 1228) | The device-independent half of controller support, namespace `mu_core`. `ControlAction` is the family's control vocabulary (set a parameter, transport, mute / solo a layer, …); `ControlSink` is what performs one (`ProcessorBase` implements it, message thread). `MidiControlMap` is the saved CC / note to action table with MIDI learn (one per processor, stored per user, not per preset). `MidiControlRouter` is the audio-thread to message-thread hand-off: claims mapped messages out of the MIDI buffer, coalesces knob sweeps, quantises mute / solo / stop to the next beat or bar of the resolved transport. `ParamGestureHold` writes a controller sweep as one host gesture (closes after 300 ms quiet). No device bytes, no product names; device drivers, surface layouts and Launchpad mapping stay in `mu-control` (see [design-launchpad.md](design-launchpad.md)) and produce `ControlAction`s the same way |
| `ModalDialog` + `ConfirmDialog` | `mu-core/UI/ModalDialog.{h,cpp}` + `ConfirmDialog.h` | Themed in-editor modal dialog (replaces `juce::AlertWindow`) + the `mu_ui::messageAsync`/`confirmAsync`/`promptTextAsync`/`confirmQuitAsync` builders (each takes an editor-anchor `Component*`). One shared implementation so every prompt matches; extensible via an injected content component. |

### Product-specific components (under `<product>/Source/`)

| Component | Why specific |
|---|---|
| mu-clid `SequencerEngine` | Euclidean pattern generation |
| mu-clid `RhythmManager` (`proc.rhythms`), `SampleLibrary` (`proc.samples`) | Rhythm-slot add / remove / swap / reset / rename; per-rhythm sample paths, preview and the primary sample folder — collaborators the processor owns |
| mu-clid `Rhythm : Layer` | Adds `HitGenerator genA/B/C` (Euclidean) on top of the shared `Layer` |
| mu-clid `HitGenerator`, `EuclideanGenerator` | Euclidean algorithm |
| mu-clid `EuclideanPanel`, `RhythmPanel`, `RhythmCircle`, `RhythmMiniVisual`, `RhythmSidebar`, `VoiceSection` | Euclidean / sample-trigger engine UI (`RhythmSidebar` is a thin `ChannelSidebar` subclass) |
| mu-tant `SynthVoice`, `WavetableBank` / `WavetableOscillator`, `GatePattern`, `VoicePanel`, `VoiceSidebar`, `GatingDesigner` | Wavetable-drone engine + gate UI |
| each product's `PluginProcessor : ProcessorBase`, `PluginEditor : EditorShellBase` | Product glue |

---

## Stage 33 (complete — build 368)

> **Historical snapshot.** The paths below are the original single-tree (`Source/…`) layout as Stage 33 left it. They have since been relocated to the `mu-core/` + `<product>/Source/` monorepo layout (see "Current shared / product-specific split" above for the authoritative current locations) and the `MuClidLookAndFeel` shim was removed. This section is kept for the decision history.

### #258 — `mu-core` CMake INTERFACE library

`add_library(mu-core INTERFACE)` in `CMakeLists.txt` propagates shared source files via `target_sources(mu-core INTERFACE ...)`. Both `mu-clid` and `mu-clid-lite` call `target_link_libraries(... mu-core)`. INTERFACE chosen over STATIC because JUCE's CMake module-initialisation macros are propagated only via `juce_add_plugin`'s INTERFACE targets — a STATIC library that doesn't link them would fail with "No global header file was included!".

**Shared (mu-core):**
```
Source/Audio/VoiceEngine.{h,cpp}
Source/Audio/VoiceParams.{h,cpp}
Source/Audio/InsertProcessor.{h,cpp}
Source/Audio/MixerEngine.{h,cpp}
Source/Audio/MultiModeFilter.{h,cpp}
Source/Audio/SamplePlayer.{h,cpp}
Source/FX/**
Source/Modulation/**
Source/UI/Components/**   (includes MuLookAndFeel.h/.cpp)
Source/UI/FXRow.{h,cpp}
Source/UI/MixerChannel.{h,cpp}
Source/UI/MixerOverlay.{h,cpp}
```

**mu-clid only:**
```
Source/Sequencer/SequencerEngine.{h,cpp}
Source/Sequencer/Layer.h              ← base struct for shared voice data
Source/Sequencer/Rhythm.{h,cpp}
Source/Sequencer/HitGenerator.{h,cpp}
Source/UI/EuclideanPanel.{h,cpp}
Source/UI/RhythmPanel.{h,cpp}
Source/UI/RhythmCircle.{h,cpp}
Source/UI/RhythmSidebar.{h,cpp}
Source/UI/ModMatrixPanel.{h,cpp}
Source/UI/ModulatorEditor.{h,cpp}
Source/PluginProcessor.{h,cpp}
Source/PluginEditor.{h,cpp}
```

### #259 — `Layer` base struct (done)

Non-Euclidean members extracted from `Rhythm` into `Source/Sequencer/Layer.h`. Explicit copy constructor and assignment operator provided because `std::atomic<bool>` is non-copyable:

```cpp
// Sequencer/Layer.h
struct Layer {
    static constexpr int MaxControlSequences = 8;
    VoiceParams                   voiceParams;
    std::vector<ControlSequence>  controlSequences;
    ModulationMatrix              modulationMatrix;
    mutable std::atomic<bool>     modLock { false };  // audio thread try-locks; message thread spins
    juce::String                  name;
    int                           colourIndex = 0;
    // explicit copy ctor/assignment defined (atomic members are non-copyable)
};

// Sequencer/Rhythm.h
struct Rhythm : public Layer {
    HitGenerator genA, genB, genC;
    // ...
};
```

`SequencerEngine` stores `std::vector<Rhythm>`. mu-tant's engine will store `std::vector<Layer>` or a derived type.

### #260 — `ProcessorBase` shared skeleton (done)

`Source/ProcessorBase.{h,cpp}` — `PluginProcessor` now inherits `ProcessorBase` instead of `AudioProcessor` directly. `ProcessorBase` owns `fxChain` and `mixerEngine` (both `public`) and provides `processCoreBlock()` which calls `fxChain.setHostBpm()` then `mixerEngine.processBlock()`. mu-tant's future processor will extend the same base.

### #261 — `MuClidLookAndFeel` → `MuLookAndFeel` (done)

`Source/UI/Components/MuLookAndFeel.{h,cpp}` holds the renamed class. `MuClidLookAndFeel.h` is a backward-compat shim:

```cpp
#include "MuLookAndFeel.h"
using MuClidLookAndFeel = MuLookAndFeel;
```

All 58 existing `#include "MuClidLookAndFeel.h"` sites compile unchanged via the shim. `CMakeLists.txt` compiles `MuLookAndFeel.cpp` via `mu-core`.

---

## mu-tant plugin structure (Stage 34+)

A new `mu-tant/` plugin directory can be created. It will:

1. Have its own `CMakeLists.txt` that adds a JUCE plugin target and links `mu-core`
2. Provide its own `PluginProcessor` (extending `ProcessorBase`)
3. Provide its own `PluginEditor` and voice-engine-specific UI panels
4. Reuse `MixerOverlay`, `FXRow`, `ModulatorEditor`, `ModMatrixPanel` directly from mu-core

The shared visual identity (`MuLookAndFeel`) ensures a consistent look across all μ plugins without duplicating any styling code.

---

## Architectural rules preserved

- **Audio thread never allocates** — unchanged; `processCoreBlock` allocates nothing.
- **ModulationMatrix is the single reader** — unchanged; still the only path from ControlSequence → VoiceParams.
- **Everything in APVTS** — each plugin defines its own APVTS layout; `ProcessorBase` provides helpers for the common subset (voice params, mixer params, FX params).
- **VoiceSlots are fully self-contained** — no cross-slot modulation. Same rule as current per-rhythm constraint.

---

## Modulation targets — family standard

**Rule: modulation depth is a percentage of the target knob's range.** 100% depth from a full-scale modulator moves the target across its whole knob range; 3.2% moves it 3.2%. Same in every product, for every target — no per-target units and no scale factors (owner decision, backlog #1122–#1124).

**One table per product, the only place targets are defined.** Each row is a [`mu_mod::ModTarget`](../mu-core/Modulation/ModTarget.h): `id` (the name saved in presets — never change it once shipped), `label` (dropdown text), `section` (dropdown heading), `param` (the parameter it drives, without the per-channel prefix; `nullptr` = a reserved / retired slot kept so indices don't shift). New targets are **appended as one row**. The knob's own `NormalisableRange` supplies min / max / curve / step — nothing is restated elsewhere.

| Product | Table | Resolve path |
|---|---|---|
| mu-Tant | `kModDestTable` — [MuTantModDest.h](../mu-tant/Source/Modulation/MuTantModDest.h) | `mu_mod::resolveLane` |
| mu-Toni | `kModDestTable` — [MuToniModDest.h](../mu-toni/Source/Modulation/MuToniModDest.h) | `mu_mod::resolveLane` |
| mu-On | one table per lane — [MuOnModDest.h](../mu-on/Source/Modulation/MuOnModDest.h) | `mu_mod::resolveLane` |
| mu-Clid | `ModDest::kTable` — [ModulationDestinations.h](../mu-clid/Source/Modulation/ModulationDestinations.h) | hand-written seed / write-back in `PluginProcessor.cpp` (values come from per-rhythm state and step-count-dependent ranges), using the knob ranges in [ModulationSkew.h](../mu-clid/Source/Modulation/ModulationSkew.h) — the APVTS layout is built from the same constants |

**How it works.** Each target is seeded into the `ModulationMatrix` as its knob's 0..1 proportion; the matrix adds `source% × depth% / 10000`; the product converts back through the knob's range (clamped at the ends; stepped targets rounded to whole steps by the product). Offset-style targets (mu-Clid's pitch octave / semitones) are seeded 0 and read back as an offset of the same proportion of the knob's range.

**Preset data.** `serialiseModulators` writes `depthUnits="range"` on every `<Modulators>` tree. Data without it predates the standard; mu-Clid upgrades it on load (Pre / Post Pad depth was % of 12 steps → rescaled to % of 0..63, so old presets move the same number of steps). No other target's old units differed.

**Enforced by:**
- [`mu_mod::checks`](../mu-core/Modulation/ModTargetChecks.h) in each product's unit tests — every row names a real parameter; 10% depth moves each target exactly 10% of its range.
- [tests/scripts/check-mod-targets.ps1](../tests/scripts/check-mod-targets.ps1) — fails if any code reintroduces per-target depth scales, defines its own target-row struct, or a product has no `ModTarget` table.

## Slot state (presets & sessions) — family standard

**A slot — one rhythm / voice / layer / lane — is one unit**, written and applied by the same
code wherever it appears: as a layer preset, inside a full preset, and inside the host session.

```
layer preset   <ProductLayerTag> rows + extras </ProductLayerTag>
full preset    <MuXxxPreset name description category>
                 <MuXxxState format="2" …product root properties…>
                   <Globals> rows for every parameter outside the slots </Globals>
                   <Slots> <Slot idx="0"> rows + extras </Slot> … </Slots>
                 </MuXxxState>
host session   the same <MuXxxState format="2"> tree
```

- **Rows** are `<p id="…" x="…"/>`: the id without the slot's prefix (so a slot loads into any
  slot) and the **actual** value, plus `c="…"` — the choice name — for a choice parameter. A range
  or choice-list change can't silently move a saved value. Rows written before format 2
  (normalised `v="…"`) still read.
- **Missing means default.** A parameter a node doesn't carry goes back to its default, and
  absent extras clear (no modulators, an empty gate), so a sparse or older file never leaves the
  previous slot's data behind. Each parameter is written once, and not at all when unchanged.
- **Extras** — a product's non-parameter slot data (modulators, gates, step rows, wavetable
  paths) — are one `LayerExtras` write / apply pair, declared once in the product constructor via
  `ProcessorBase::initLayerState(prefixes, extras)`.
- **Older states** (the APVTS dump of `<PARAM>`s + a `<VoiceData>` of `<Voice idx>` nodes) are
  rebuilt by `composeLegacyState` before anything applies them, so stopped loads, hot-swap commits
  and host restores share one apply path. Product-specific old shapes (mu-On's root `<Pattern>`,
  mu-Toni's `<MuToniMods>`, mu-Tant's pre-2-lane X-Mod rows) move into their slots in the product's
  `to…State` step. A `<PARAM>` without a value means its default, as `replaceState` read it.
- **mu-Clid** keeps its own (already actual-value, kinded) file shapes — `.muRhythm` and the
  `<Rhythm>` children of `.muClid` — but follows the same rule: one `PresetIO::prepareRhythm`
  builds a rhythm from either shape (defaults for what it lacks), one `HotSwapStager::installRhythm`
  installs it (stopped load, loop-boundary swap, full preset), and the host session is the `.muClid`
  tree plus `<SessionParams>` rows for every parameter that tree doesn't carry.
- **Verification:** the listening tests' `roundtrip` / `session_roundtrip` options re-render from a
  saved preset / session and require sample-identical audio (`*_roundtrip` tests, one per product,
  each starting from an older-format file where one exists).

Files saved by this format don't load correctly in builds before it (the older readers expect the
APVTS dump).

## Layer ownership and structural edits — family standard

Ruled 2026-10-10 (Architecture Steward) for backlog #1264 / #1265. Naming and the derived types are in
[design-naming.md](design-naming.md); this is the contract and the staged plan. The design is decided
throughout; the stages marked **O** also wait for owner decisions on behaviour or UX.

### The contract

1. **Capacity is fixed, the count is runtime.** Parameters, mixer channels and the `LayerLayout` are
   declared for `mu_limits::kMaxLayers` (8) once, at construction. Adding a layer *activates* slot `n`;
   it never allocates parameters. (mu-Toni and mu-On must therefore declare all 8 layers' parameters
   before they can go variable; that is additive and needs no migration for mu-Toni, see Stage 6.)
2. **Dispatch is virtual on `Layer`.** Hooks, all called once per layer per block or per edit, never
   per sample: `typeId()` (an int from the product's enum), `writeExtras(ValueTree&) const` /
   `applyExtras(const ValueTree&)` (the product's `LayerExtras`; absent children clear), `resetToDefaults()`
   (keeps `name` and `colourIndex`), `onMoved(newIndex)` (reset play state, envelopes, sidechain
   followers), `insertGainReduction()` (the GR meter pointer, `nullptr` when none) and, last, `render(...)`.
   The base implementations handle the modulators through the shared `ModulatorSerialise` with
   virtual `isValidSource` / `isValidDest` validators, which also removes the derived-to-base overload
   trap in [design-naming.md §3](design-naming.md#namespaces). A **registry** (`LayerTypeInfo`: id, name,
   param prefix, preset extension, mod-target table, `create`) is added only when two layer types can
   share one instance; it constructs and describes layers and never dispatches.
3. **`ProcessorBase` owns the layers**: `std::array<std::unique_ptr<Layer>, kMaxLayers> layers`,
   `std::atomic<int> numLayers`, `juce::CriticalSection layersLock`. A product supplies
   `createLayer(typeId)` and typed accessors (`getPattern(i)` = `layerAs<Pattern>(i)`, which asserts
   `typeId`). `getNumChannels()` is `numLayers`. Slot `i` of `layers`, of the mixer, and of the
   parameter prefix list are the same index.
4. **`Layer` stays data-light.** Its members stay `voiceParams`, `controlSequences`, `modulationMatrix`,
   `modLock`, `voiceParamsLock`, `name`, `colourIndex`; it gains the virtuals above and a message-thread-only
   clip bank (`std::array<juce::ValueTree, 8>`, one composed slot node each, never read by the audio
   thread; the clip-launch design is #1277). It does **not** hold `InsertProcessor`: `Rhythm` is copied by
   value today and mu-Clid's insert lives in its `VoiceEngine` so a retired engine keeps its tail. mu-Tant
   and mu-Toni compose a `mu_core::InsertStage { InsertProcessor proc; VoiceParams cfg; }` member
   into `Pattern` / `Arp` (replacing the `inserts` and `insCfg` arrays); `Rhythm` overrides
   `insertGainReduction()` to return its engine's. Anything a layer owns moves with it when the pointer moves.
5. **Audio-thread rules.**
   - The audio thread takes `ScopedTryLock(layersLock)` once per block. On failure it renders silence for
     that block but **still advances the transport and boundary detection** (hot-swap principle 4). It reads
     `numLayers` once (acquire) and uses `layers[i].get()` only inside the lock; it never keeps a `Layer*`
     across blocks, never allocates, never destroys a layer.
   - The message thread holds `layersLock` only for pointer moves, count changes and mixer-channel
     state moves (microseconds). Constructing, `prepareToPlay`-ing and **destroying** a layer happen outside
     the lock, on the message thread: a removed layer's `unique_ptr` is moved to a local and released after
     unlocking. A layer is published by `layers[n] = std::move(fresh); numLayers.store(n + 1, release)`
     inside the lock.
   - The in-layer spin-locks (`modLock`, `voiceParamsLock`, a product's `GatePattern.editLock`) are unchanged
     and still nest inside `layersLock`, never the other way round.
   - `layersLock` guards the *set of layers* only. mu-Tant's `voicesLock` also guards the wavetable bank
     append; when it becomes `layersLock` the bank gets its own `bankLock`, so a wavetable import never
     blocks a layer edit and the reverse.
   - Hot-swap commits still go through each layer's own fine-grained locks (`applyExtras` is the apply);
     they do not take `layersLock`, so principle 3 of [Hot-swap](#hot-swap-staged-preset--layer-swaps-family-pattern) holds.
6. **Structural edits are one routine in `ProcessorBase`, message thread only**
   (`addLayer`, `removeLayer`, `swapLayers`, `resetLayer`), in this order:
   1. Cancel the affected pending per-layer swaps (add: the new slot; swap: both; remove: all; reset: that one),
      through `Stager::cancel`. A staged full preset is left to win.
   2. `suspendProcessing(true)`; it is not a barrier, `layersLock` is.
   3. Build and prepare the new layer (add) before locking.
   4. Under `layersLock`: pointer moves (`std::swap` / shift down by `std::move`), the mixer-channel state
      move (`swapChannelState` / `copyFrom` / `reset`), the **sidechain-source re-translation on every
      structural edit** (today only `RhythmManager::swap` does it; check that `remove` and the other
      products do), `resetSidechainEnv` for moved slots, `numLayers`, then `Layer::onMoved` for each moved layer.
   5. Unlock, then move the **parameter values** by slot, inside the `ScopedApvtsLoading` guard and still
      suspended, so no block ever pairs a moved layer with unmoved parameters. For products where the APVTS
      is the truth (Tant, Toni, On): `LayerLayout::writeParams(node, from)` then `applyParams(node, to)`
      (the rows carry no prefix, so a slot loads into any slot; a missing row = default, which is how a new or
      vacated slot is cleared). mu-Clid, whose `Rhythm` is the truth for the Euclidean params, keeps pushing
      from the layer (`pushRhythmToApvts`) through a `syncSlotParams(slot)` hook. This is the one allowed difference.
   6. `suspendProcessing(false)`; destroy any removed layer; set `colourIndex` (first unused palette entry,
      the mu-Clid / mu-Tant rule, hoisted into the base); fire `onLayersChanged` for the shell.
   The last layer cannot be removed; the demo cap uses `canAddChannel()` as today.
7. **Retired layers do not fade yet.** A removed layer is cut at the next block (as today in mu-Clid and mu-Tant).
   mu-Clid's per-engine retire-tail and mu-Tant's count-reducing preset fade stay inside the product
   layer; a generic `Layer` tail is not in scope.

### Why virtual and not a table

A table of function pointers re-implements the vtable and still needs a base type to hold the shared
data. The hooks are all per-layer, so the call cost is nothing. What a vtable cannot do is create a
layer from a name read out of a file and list the types in a menu, and that is all the registry is for.
It stays out until the combined instance needs it, so there is exactly one place (the base class) where a layer's
behaviour is described. Typed access by `static_cast` after a `typeId` assert keeps product code as
readable as `getRhythm(i)` is today.

### Stages (each: one product at a time, Debug build, unit tests, round-trip listening tests; macOS untouched)

M = mechanical (unattended is safe), S = supervised (audio thread / lock change: run the listening tests
on the build PC), O = needs the owner.

| # | Kind | What | Exact content | Guard |
|---|---|---|---|---|
| 1 | M | Derived types as value members, no behaviour change | **Tant**: `Pattern : Layer` holds `gate`, `filter`, `pitch` (`GatePattern`), `ring` (`VoiceRingBuffer`), `snap[]`, `InsertStage`, the retiring state, the user-wavetable path / index per oscillator, the voice engine pointer; `std::array<Pattern, 8> patterns` replaces `voiceSlots`, `gatePatterns`, `filterPatterns`, `pitchPatterns`, `voiceRingBuffers`, `voiceSnap`, `inserts`, `retiring`, `voiceColourIndex`, `osc*User*`. `Pattern::copyFrom(const Pattern&)` replaces the `copyDataFrom` calls. **Toni**: `Arp : Layer` holds `ArpVoiceRunner`, `InsertStage`, `vp` pointers, `modDestAtoms`; replaces `runners`, `inserts`, `insCfg`, `vp`, `modDestAtoms`, `voiceSlots`. **On**: `Track : Layer` holds the trigger counter and the lane id; the shared `StepPattern` grid, `GrooveVoices` and the Rumble envelope stay in the processor for now (they are one object across lanes, not four). **Clid**: nothing; `Rhythm` already derives. | Each product's unit tests; its `*_roundtrip` (sample-identical); `check-core-boundary` |
| 2 | M | `ProcessorBase::getLayer(int)` accessor (virtual, returns the product's value-array slot) | Default `getChannelName` / `getChannelColourIndex` come from `Layer::name` / `colourIndex`, built with the names the products show today ("Voice N" for mu-Tant, "Layer N" for mu-Toni, lane names for mu-On, the rhythm name for mu-Clid), so nothing visible changes; the editors' `voiceSlots[sel]` / `voiceSlot(lane)` become `getLayer(sel)`; the GR-meter pointer helpers (`getInsertGRPtr`, `getInsertGRReductionPtr`) become `getLayer(i)->insertGainReduction()` | unit tests; boundary check; build all four |
| 3 | M | Persistence virtuals | `Layer::writeExtras` / `applyExtras` (base: modulators); each product's `LayerExtras` lambdas become the virtual overrides on `Pattern` / `Arp` / `Track`; `initLayerState(prefixes)` builds the `LayerExtras` that calls `getLayer(slot)`. mu-Clid keeps `PresetIO` shapes and gains nothing here. | `MuTantPersistTests`, `ModulatorSerialiseTests`, `PresetRoundTripTests`, `PresetXMLRoundTripTests`, `TONI_roundtrip`, `ON_roundtrip`, `TANT_roundtrip` |
| 4 | S | Ownership moves to `ProcessorBase` | The `layers` array, `numLayers`, `layersLock`, `createLayer`, `layerAs<T>`; Tant first (its `numVoices` / `voicesLock` become these, `bankLock` split out), then Toni and On at a constant count (4 / 5) with the try-lock added so a later edit is safe; the product's `addVoice` etc. still exist and now move pointers instead of copying data. | as stage 3, plus a new `LayerOwnershipTests` (construct, publish, destroy off-lock) and a listening render with structural edits scripted mid-play |
| 5 | S | Central edits and the generic stager | `addLayer` / `removeLayer` / `swapLayers` / `resetLayer` per the contract; `RhythmManager` and Tant's three functions become thin wrappers or disappear; a `Stager<ValueTree, ValueTree, 8>` in the base for the three products whose payload is a tree (`Layer::applyExtras` is the apply); mu-Clid keeps its `PendingRhythm` stager. | new `LayerStructureTests`: fill to 8, remove the middle, swap ends, reset; assert parameter values, colour, mixer channel, sidechain source and pending-swap cancel all follow; a two-thread stress test (message thread edits while a loop calls `processBlock`, no allocation, no crash); every `*_roundtrip` and the hot-swap listening tests (`swap`, `slot swap`) |
| 6 | O | Variable layers in mu-Toni (#1240) and mu-On (#1241); the clip bank (#1277) | See owner decisions below. The clip bank is `Layer::clips`: a clip is a composed slot node (Stage 3 makes a layer loadable from one), launching it is a staged layer swap (Stage 5), and a pad never writes a preset. | tests per feature, written with the decisions |
| 7 | S | mu-Clid into the container | `Rhythm` gains `VoiceEngine`, `MidiOutputEngine`, play state, retired engines, sample path, modulated-euclid overrides and the modulation previous-state flags (replacing the `std::array`s indexed by rhythm in `PluginProcessor` and `SequencerEngine`); `SequencerEngine` takes `Rhythm&` from the base instead of a `std::vector<Rhythm>`; `Rhythm` becomes non-copyable, with `resetToDefaults()` replacing `r = Rhythm{}` and `add(const Rhythm&)` taking a built `unique_ptr<Rhythm>`; `rhythmsLock` becomes `layersLock`. Highest risk of the plan (about 1580 uses, the Lite build, the retire-tail). | `mu-clid-tests` (build the target explicitly, it is not in ALL), `CLID_roundtrip`, the retire-tail and hot-swap listening tests, Lite build |
| 8 | O | Registry and `Layer::render` | With the combined instance and the saved-data renames (#1263). Until then the mixer's `RenderChannelFn` stays product-side. | with that work |

Stages 1-3 are order-independent between products (any product can go first). From stage 4 on, do Tant first
(it already has the dynamic count and the lock), then Toni, On, and Clid last.

### Owner decisions for Stage 6

- **mu-On (#1241):** its parameters are prefixed by *lane type* (`k_ b_ h_ s_ r_`), not by index. A variable
  count with a per-layer engine selector needs index-based prefixes, which is a saved-data rename (#1263, on
  hold). Options: (a) mu-On stays five fixed lanes and only gains `Track` and the clip bank (no migration;
  recommended until #1263); (b) index prefixes with a migration, done with #1263. The stage 5 edit code works
  for both.
- **mu-Toni (#1240):** `kNumChannels` is 4 today. Going to 8 adds layers 5-8 parameters (additive, old sessions
  load with the new layers at defaults) and changes the host's automation list; confirm the default count for a
  fresh instance and the demo cap (mu-Toni is freeware, so likely none).
- **Both:** the add / delete / reorder UI (it is the shared `ChannelSidebar`, so it is a visible change for the
  freeware and licensed products alike) and whether delete fades or cuts (item 7 says cut).

For the combined instance (design-future): none of the above blocks it, and nothing here closes it off. The
registry adds `typeId` -> `LayerTypeInfo`; the saved `Slot` node gains a `type` attribute (absent = the
product's own type); the prefix lists become per-type; `layerAs<T>` already asserts the type.

## Transport rule — family standard

Every product's `processBlock` takes its play state, tempo and beat from one call,
`mu_core::resolveTransport` ([mu-core/Plugin/TransportResolver.h](../mu-core/Plugin/TransportResolver.h)):

1. **A playhead with a position** — a DAW host, or mu-link through its injected playhead: play,
   tempo **and beat** all follow it. Patterns, gates and arp steps lock to the host's bars, and a
   DAW loop or locate lands on the matching step.
2. **A host that gives no position:** play and tempo follow the host; the beat runs on.
3. **Standalone with MIDI clock sync on:** the clock owns play / stop, tempo and beat.
4. **Otherwise** the product's own transport (its Play button, BPM field and beat counter).

Whenever the source is outside (1–3) the Play button mirrors it, and the internal beat counter
carries the beat on (the UI's position, and where the own transport resumes). Inside a DAW the
own Play button does nothing — the host is in charge. A product may bound its beat space
(mu-Tant wraps at its longest gate pattern, 64 beats). Product-specific play modes that aren't a
transport — mu-Clid's and mu-Tant's MIDI Note mode, mu-Toni's MIDI-trigger arp — sit on top. The
headless render's `--host-start-beat` simulates rule 1 (`TONI_host_lock`).

## Device MIDI output — family standard

Decided 2026-10-10. This covers any MIDI that the family sends to an OS MIDI port itself, outside a
host's `processBlock` MIDI buffer. A plugin (VST3 / CLAP) never does this: its MIDI out goes into the
`MidiBuffer` for the host to route.

**One timed sender: `mu_core::TimedMidiOut`** ([mu-core/Plugin/TimedMidiOut.h](../mu-core/Plugin/TimedMidiOut.h)).
It lives next to `MidiClockTempo.h` and, like that file, it is **header-only and not in mu-core's INTERFACE
source list**. That is required, not just tidy: mu-link includes mu-core headers without linking the
INTERFACE library, and plugin builds that don't include the header don't get its thread or
`juce_audio_devices` code.

- **Producer (one thread per instance, real-time safe):** `push(dueMs, bytes, size)` and
  `pushBuffer(midi, blockStartMs, sampleRate)` stamp short messages (1–3 bytes) with a
  `Time::getMillisecondCounterHiRes` due time and push them to a fixed-size SPSC FIFO. No locks, no
  allocation, nothing queued while no port is set; SysEx and anything longer than 3 bytes is dropped, and
  so is a message when the FIFO is full.
- **Sender:** its own `juce::Thread` (real-time priority, falling back to highest) wakes about every
  millisecond and sends what is due with `sendMessageNow`. `sendDue(nowMs, send)` is the step itself, so
  tests drive it without a thread (`startSender = false`, `armForTest()`).
- **Port:** `setOutput(juce::MidiOutput*)` takes a **non-owning** pointer (message thread), swapped under
  a lock that the sender holds while it sends. `setOutput(nullptr)` returns only after any send in progress
  ends; after that the caller may delete the port.
- What it does **not** do: choose devices, open ports, filter or encode messages. Callers own the meaning
  of the bytes (clock encoding, echo filtering, real-time byte stripping).

**Port ownership rule.** A MIDI port has **exactly one owner at a time**, and `TimedMidiOut` is never it.
- The owner is the `AudioDeviceManager` (the user's chosen default output) or **standalone-only code**:
  the header-only standalone files in mu-core (`Link/MuLinkBridge.h`, `Plugin/StandaloneShell.h`), mu-link's
  app, and `mu-control`'s drivers. Nothing in mu-core's INTERFACE source list, and never `ProcessorBase`,
  opens an OS MIDI port.
- Before deleting the port it lent out, an owner calls `setOutput(nullptr)` on every sender using it.
  mu-link does this by borrowing the manager's port across `audioDeviceStopped` and
  `audioDeviceAboutToStart`, which is safe because it always has an audio device open.
- **Who owns the port while an app is attached to mu-link:** `MuLinkBridge`. The app may have no audio
  device open, so it can't rely on those callbacks, and the manager can delete or replace its default port
  at any time (for example when the user changes it in Settings). So the bridge opens its own
  `std::unique_ptr<juce::MidiOutput>` with `MidiOutput::openDevice(deviceManager.getDefaultMidiOutputIdentifier())`
  on the message thread when it attaches. It frees the manager's handle first, because Windows MIDI ports
  are often single-client (design-launchpad R2). It follows Settings changes as a `ChangeListener` on the
  manager. On detach it stops the bus render (`client.detach()`), calls `setOutput(nullptr)`, closes its
  port and hands the identifier back to the manager. The user's saved MIDI-out choice must survive quitting
  while attached.
- Lead time: the bus renders ahead of what is heard, so each event is stamped
  `nowMs + (leadFrames + samplePosition) * 1000 / sampleRate`. `leadFrames` is the render lead that
  `MuLinkClient` reports.

**Controllers (`mu-control`).** Pad LEDs are sent from the message thread on a timer
([design-launchpad.md §3.2a](design-launchpad.md#32a-where-the-code-lives-a-separate-mu-control-library-owner-deferred-to-this-recommendation-2026-10-09)).
They have no due time, and much of the traffic is SysEx, so they **do not go through `TimedMidiOut`**. A
driver owns its controller's port and sends directly. If MIDI clock is sent to a controller (so Launchpad
flashing and pulsing follow the beat), the driver lends its port to a `TimedMidiOut` like any other owner.
This doesn't change where anything lives: `mu-control` uses mu-core, never the reverse.

## MIDI control mapping — family standard (ruled 2026-10-10)

The generic "a MIDI CC / note drives something" layer (backlog #1275). It lives in `mu-core/Control/` (new folder; it holds
only what [design-launchpad.md §3.2a](design-launchpad.md#32a-where-the-code-lives-a-separate-mu-control-library-owner-deferred-to-this-recommendation-2026-10-09)
keeps in `mu-core`), namespace `mu_core`, one main class per file:

| File | Role |
|---|---|
| `Control/ControlAction.h` | The device-independent action vocabulary: `enum class ControlActionType` + `struct ControlAction { type; int layer; juce::String paramId; float value; Quantise quantise; }`. Persisted by **stable string name**, never by the enum integer. |
| `Control/ControlSink.h` | `struct ControlSink { virtual bool perform(const ControlAction&) = 0; }` — message thread only. `ProcessorBase` implements it; `mu-control` surface models call it too, so a MIDI message and a Launchpad pad end in the same place. |
| `Control/MidiControlMap.{h,cpp}` | The saved table: `MidiMapping { source (CC / Note, channel 0 = omni, number), action, min, max, invert, quantise override }`, edit / MIDI-learn / load / save, and the audio-thread lookup table. |
| `Control/MidiControlRouter.{h,cpp}` | The audio-to-message hand-off (below). A member of `ProcessorBase`, called once per block like `queueMidiProgramChanges`. |

**Vocabulary now:** `Parameter` (knob-to-parameter), `TransportToggle`, `TransportPlay`, `TransportStop`, `MuteLayer`,
`SoloLayer`. **Declared, unimplemented (`perform` returns false):** `SelectLayer`, `LaunchClip`, `PresetNext`,
`PresetPrev`, `Panic`. Mute and solo are not new engine paths: they resolve to the layer's strip `mute` / `solo`
parameter and take the `Parameter` route.

**1. Parameter writes go to the APVTS parameter, not the ModulationMatrix.** The CC sets the knob's *base value* through
`RangedAudioParameter::beginChangeGesture / setValueNotifyingHost / endChangeGesture` (a gesture is held open until
about 300 ms after the last CC for that mapping, so a host records one automation stroke). The matrix keeps reading
the base value and adds modulation on top, so "ModulationMatrix is the single reader" holds and a CC never competes
with a `ControlSequence`. Feeding the matrix would leave the knob, the saved state and host automation unmoved.

**2. The audio thread never writes a parameter.** `setValueNotifyingHost` runs every APVTS listener synchronously
(`startFxParamSync` → `syncGlobalFxParam`, attachments, the host wrapper), none of which is real-time safe. The audio
thread only (a) looks the message up in a fixed `std::array<std::atomic<int16_t>, 2 * 16 * 128>` (kind, channel,
number → mapping index, or -1; the message thread rewrites it on every edit), (b) for a **knob** mapping stores the
latest value in `std::atomic<float> latest[kMaxMappings]` and sets a dirty bit (a 127-step sweep coalesces to one write
and cannot overflow a queue), (c) for an **action** mapping pushes `{mapping, value, dueSample}` into a
`juce::AbstractFifo` (exactly `pcFifo`'s pattern), then `triggerAsyncUpdate()`. The message-thread drain
(`handleAsyncUpdate`, beside `drainPendingMidiProgramChanges`) resolves `apvts.getParameter(paramId)` and performs.
Mapped messages are **removed from the block's `MidiBuffer`** (the scan takes it non-const and runs before the engine
reads notes), so a mapped pad does not also play a note. `kMaxMappings = 512`; MIDI learn is one packed
`std::atomic<uint32_t>` the audio thread fills with the first message seen while learn is armed.
Sample offsets are carried in the event but **not used for v1 parameter or action application**: both land
on the message thread, so exactness below one block is unreachable by design. Escape hatch if a measured need
appears: the audio thread applies mute / transport atomics itself at the offset. Do not do it speculatively.

**3. Persistence: one global per-user file per product, not part of the preset.** `<settingsDir>/<appName>_midiControl.json`,
beside `…_midiPresets.json`, with the same `setStorageFile` / `load` / auto-save-on-edit shape as `MidiPresetMap`.
Reasons: the map describes the player's hardware, not the song; a full-preset hot-swap must never remap the pads under
the player's hands; a shared preset must not carry someone else's CC numbers; it is the model the family already has for
program changes. Parameter targets are stored by **parameter id string** and resolved at drain time, so a layer that was
added or removed leaves an *unresolved* row (shown greyed in the editor, never an error, never a dangling pointer) that
works again when the id reappears. Layer actions store the layer **index** (the 0-based position), resolved against the
live layer count. The file carries a `version`; parameter ids are saved data (design-naming) and a prefix change needs a migration of this
file too. A later "export / import map" is a file copy and needs no format change.

**4. Quantise is a global setting with a per-mapping override.** `enum class Quantise { Default, Off, Beat, Bar }` on
the mapping; a global `Off / Beat / Bar` in the app settings (default **Bar**) fills `Default`. It applies to
`MuteLayer`, `SoloLayer` and `TransportStop`. It never applies to `Parameter` (knobs are continuous),
to `TransportPlay` / `TransportToggle` starting from stop (nothing to align to), nor to preset or clip changes (the hot-swap
`Stager` already waits for its own loop / bar boundary; quantising twice is wrong). The audio thread decides: while
the resolved transport is not playing, or `Off`, the action is due at once; otherwise the next boundary beat is
`ceil(startBeat / q) * q` (q = 1 beat, or the bar length from `getHostTimeSignature`, 4/4 when none), computed from the
`BlockTransport` that `mu_core::resolveTransport` returned for this block (so it follows host, mu-link, MIDI clock or
internal, whichever won the rule). Due within this block → it is queued with `dueSample`; later → it waits in a
fixed 32-entry audio-thread-owned pending list and is released in the block that contains the boundary. A transport
press never bypasses the resolver: `TransportPlay` / `TransportStop` / `TransportToggle` are no-ops when `BlockTransport::playOutside`
is true.

Do not: add a MIDI-learn table per product; write parameters from `processBlock`; store the map in the preset; give an action
its own persistence integer; put the map in `mu-control` (it needs no device).

## Hot-swap (staged preset / layer swaps) — family pattern

Every product loads presets *while playing* without an audible glitch by **staging**
the incoming state and **committing it at a musical loop boundary**. The staging state
is shared; the boundary rule and the apply are **product-side**.

**Shared: `mu-core/Plugin/HotSwap.h`.** `mu_hotswap::Stager<SlotPayload, FullPayload, N>`
owns N per-slot pending payloads plus one full-preset payload and the
store-release/load-acquire handshake: `stage` / `stageFull` (a full preset drops any
pending per-slot swaps) / `cancel` on the message thread, `flagIfReady(slot, atBoundary)` /
`flagFullIfReady` on the audio thread, then `consume(slot, apply)` / `consumeFull(apply)`
on the message thread (from `commitDeferredWork`). Payloads are only touched on the
message thread; the audio thread touches only the flags. `mu_hotswap::loopWrapped` /
`boundaryReached` are the shared loop-wrap predicates (commit on the wrap, or at once on
the playing→stopped edge); `kBarBeats` is one 4/4 bar. A load while stopped applies at once.

**Product-side:** the payload (what is pre-built at stage time), which loop is the
boundary, and the apply.

| | mu-clid | mu-tant | mu-toni | mu-on |
|---|---|---|---|---|
| Deep-dive doc | [mu-clid/design-hotswap.md](mu-clid/design-hotswap.md) | — (this section) | — | — |
| Stager | `HotSwapStager` → `Stager<PendingRhythm, PreparedFullPreset, 8>` (`Rhythm` + `VoiceEngine` + sample path) | `VoiceHotSwapStager` → `Stager<ValueTree, ValueTree, 8>` | `BarLineSwapper<ValueTree, 4>` | `BarLineSwapper<ValueTree, 5>` |
| Boundary predicates | `HotSwapBoundary.h` (`mu_clid::hotswap`) | shared `loopWrapped` / `boundaryReached` | shared (inside `BarLineSwapper`) | shared (inside `BarLineSwapper`) |
| Reference boundary | master loop / per-rhythm wrap (`swapMode`) | full preset → voice 0's gate-pattern wrap; per-voice → that voice's own wrap (master loop when set) | every bar (the arp has no loop of its own) | the 16-step pattern wrap = every bar |
| Staged when | sequencer playing | internal transport playing | the audio thread's play state (host, MIDI clock or internal) | the audio thread's play state (host, MIDI clock or internal) |
| Commit isolation | `suspendProcessing` + `rhythmsLock`, microseconds (heavy work pre-built at stage) | **no blanket lock** — relies on the audio render's existing fine-grained locks | same as mu-tant | same as mu-tant |

**Shared principles (hold for any future product):**

1. **All heavy work at stage time, off the audio thread** — parse, build engines,
   load samples/wavetables. The commit is only fast in-memory moves / atomic stores.
2. **Stopped = commit immediately; playing = stage + commit at the boundary** — same
   build + same commit code, only the trigger differs (no divergent second path).
3. **The commit must not block the audio thread for more than ~1 block.** Two
   product-specific ways to achieve it:
   - mu-clid pre-builds the `VoiceEngine` and the commit is a pointer/`std::move`
     swap under a microsecond `suspendProcessing`; the **old engine is retired and
     plays out its tail** (see the mu-clid doc §8) rather than being hard-cut.
   - mu-tant takes **no blanket lock** at commit at all — each structure it mutates
     is already guarded by its own fine-grained lock the audio render respects
     (APVTS params atomic; `GatePattern.editLock`; `Layer.modLock`; bank append).
4. **Advance the transport / boundary detection OUTSIDE the render lock.** If the
   render bails on a lock, the transport must still advance — otherwise a commit
   freezes the playhead.
5. **Resolve shared resources lock-free at commit, and decode/build them off the
   lock at stage.** mu-tant's hard-won lesson (#886/#888): (a) resolving a wavetable
   via `bank.addOrLoadFile()` (which takes the bank lock) *at commit* makes the render
   bail to silence for the coincident block; pre-load at **stage** and resolve at
   commit with a lock-free lookup (`findByPath`). (b) Even at *stage* time, don't do
   the heavy decode/build **under** the lock — that silences the render for the whole
   decode (an intermittent pause when staging a new resource while playing). Split the
   resource load into a lock-free **decode** (`WavetableBank::decodeFile`) and a
   microsecond locked **append** (`appendTable`). **Any** lock held around slow work,
   stage or commit, can silence the render for as long as it's held.
6. **Readiness gate — never commit a half-built payload.** Set the staging `isReady`
   store-release flag **only after** the entire payload is prepared; the audio-thread
   boundary check must only flag swaps whose `isReady` is true. Then if a loop
   boundary arrives before the new state is ready, it is skipped and the swap commits
   at the next loop point — the commit never blocks the audio thread waiting on a
   build, and never installs a partial payload. (This also makes a future background
   decode safe with no extra machinery.)
7. **Editor refresh after commit** via `ProcessorBase::onPresetSwapCommitted` (full)
   and `ProcessorBase::onLayerPresetCommitted(slot)` (per layer, every product) — the shell calls `onPresetLoaded` *synchronously*
   after `loadPreset`, which for a staged swap runs against pre-swap state, so the
   commit must re-trigger the refresh. **Clear the callbacks in the editor dtor**
   (processor outlives editor → UAF).

**Structural layer edits cancel pending per-layer swaps.** Add / remove / reorder /
reset of a layer renumbers or clears slots by index, so a staged per-layer swap
(keyed by index) would commit to the wrong slot — the structural-edit functions
cancel the affected pending swaps (a remove/down-shift cancels all). A staged **full
preset** is index-independent (it replaces every slot at commit) and is left to win.
Both products implement this (mu-clid `cancelPendingIfAny`, mu-tant
`hotSwapStager.cancelVoice`).

**Staging badges (shared, automatic):** `TransportBar` shows a "SWP" pill from
`ProcessorBase::hasPendingFullPreset()`; `ChannelSidebar` shows a per-layer badge
from the product's `isPendingSwap(i)` / cancels via `onCancelPendingSwap(i)`. A
product only has to override/​wire those.

---

## Build & packaging reference

The day-to-day build/release **procedure** lives in the `/build` and `/release` skills
(`.claude/commands/`). This section is the *reference detail* behind those skills.

### Plugin formats (family rule)

Every product builds **VST3 + Standalone + CLAP** on all platforms, **plus AU (Audio
Unit v2) on macOS**. AU is macOS-only (needs Apple's AudioUnit SDK), so the root
[CMakeLists.txt](../CMakeLists.txt) defines `MUFAMILY_AU_FORMAT` (= `AU` on `APPLE`,
empty otherwise) and **every `juce_add_plugin` must append `${MUFAMILY_AU_FORMAT}` to
its `FORMATS`** — that's how a new sibling gets AU for free. (CLAP is added separately
via `clap_juce_extensions_plugin`.) AU requires the shared `PLUGIN_MANUFACTURER_CODE
TDP1` plus a **unique 4-char `PLUGIN_CODE`** per product. The `_AU` targets exist only
in an `APPLE` configure, so they're built/validated on the macOS CI runner (`auval`),
never locally on Windows. Shipping AU to Mac users later needs Apple notarization +
signing (the Apple side of #99).

> **macOS is on hold** (owner decision, 2026-10-02): no Apple code-signing certificate, so
> no Mac build is shipped and `release.yml` no longer runs a macOS runner. The `APPLE` / AU
> plumbing above is kept intact so the work can resume without re-plumbing.

### Build-number policy (owner rules, [cmake/IncrementBuildNumber.cmake](../cmake/IncrementBuildNumber.cmake))

1. A code change → a **Debug** build only. Every Debug build **increments the number by
   exactly 1** — no time/session throttle. Each Debug build is a distinct testable artefact.
2. **Release** builds happen only when the owner explicitly says so. A Release rebuilds
   only the Release artefacts (Debug untouched) and is stamped with the **same number as
   the last Debug build** — Release **never** increments.
3. **Release ≤ last Debug, always.** A Release coming out *higher* than the last Debug
   means the counter advanced without a Debug build → the build **stops with a
   FATAL_ERROR**; surface it to the owner, don't work around it.

### Shipping a Release (three ways, every time)

(a) artefacts copied to the OneDrive tester share (Release configured with
`-DMUFAMILY_DEPLOY_TESTERS=ON`, OFF by default — deploys mu-clid + Lite, mu-tant, mu-on,
mu-toni, mu-link's exe via guarded POST_BUILDs; a plain Release stays local; Debug never
deploys); (b) a zip built and uploaded to the GitHub "latest release" so website download
links resolve; (c) **release notes promoted** — move accumulated "Next release · In
testing" items into a new dated `v1.1.NNN` section in each affected product's notes
(`site/mu-clid-releases.html`, `site/mu-tant-releases.html`), clear the In-Testing section,
and bump the hardcoded version default in `site/download.html`.

Artefacts land at `build/<product>/<target>_artefacts/<Config>/<Format>/`. mu-clid and
mu-clid-lite both use `build/mu-clid/...` because they share a CMakeLists.

### Third-party libraries

| Library | Purpose | Notes |
|---|---|---|
| JUCE | Core framework | Via `JUCE_PATH` env var |
| Signalsmith Reverb | Room/hall/plate reverb | MIT, header-only |
| Monocypher | License key crypto | BSD-2-Clause, compiled in |
| clap-juce-extensions | CLAP format support | MIT, compiled in |
| SoundTouch | Time stretching (v1, planned) | LGPL — will ship as DLL when implemented |
| RubberBand | Time stretching (v2, planned) | Wrapped behind `TimeStretcherBase` — no refactor needed when upgrading |

---

## Related design documents

- [mu-clid/design-voice.md](mu-clid/design-voice.md) — μ-Clid voice chain, ADSR, filter, InsertProcessor details (mu-tant's voice doc lives at [mu-tant/design-voice.md](mu-tant/design-voice.md))
- [design-fx.md](design-fx.md) — FX chain, slot interface, intra-FX routing (family-shared)
- [mu-clid/design-hotswap.md](mu-clid/design-hotswap.md) — **seamless preset/rhythm hot-swap** deep-dive (stager, boundary detection, retire-then-swap voice tail, threading). The family-level pattern + the product-side decision + the mu-tant variant are summarised in [Hot-swap](#hot-swap-staged-preset--layer-swaps-family-pattern) above.
- [mu-clid/design-presets.md](mu-clid/design-presets.md) — μ-Clid preset storage. Each product defines its own preset format under its own `design-presets.md`; the family-level conventions are: per-layer preset in camelCase noun (`.muRhythm` / `.muPattern` / `.muArp` / `.muTrack`), full preset in plugin-name camelCase (`.muClid` / `.muTant` / `.muToni` / `.muOn`). A mu-On `.muTrack` belongs to the instrument it was saved from (it records `lane`), so each lane lists only its own.
- [design-future.md](design-future.md) — inter-plugin sync (μ family), MIDI CC control
