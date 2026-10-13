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
                           RhythmSequencer/EuclideanGenerator)
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
- `UI/ChannelSidebar` + `UI/SidebarItem` — the shared left "layers" sidebar (select / add / delete / drag-reorder). Reads channel metadata from `ProcessorBase::getNumLayers/getLayerName/getLayerColourIndex`. The per-layer mini-graphic (and its animation) is the only product-specific part, injected via `createMiniVisual` (mu-clid → `RhythmMiniVisual` wrapping a `RhythmCircle`; mu-tant → a voice glyph). Reorder + hot-swap semantics are product hooks (`onSwapChannels`, `isPendingSwap`, `onCancelPendingSwap`) wired by each product to its own stager (every product implements hot-swap on the shared `mu_hotswap::Stager` — see [Hot-swap](#hot-swap-staged-preset--layer-swaps-family-pattern) below). Add/delete is driven by the product (`onAddChannel` + a panel delete button → the central `ProcessorBase::addLayer`/`removeLayer` in mu-tant, `rhythms.add`/`rhythms.remove` (`RhythmManager`) in mu-clid until its layer-plan stage).
- `UI/LayerHeaderBar` — the shared per-layer header (colour dot · editable name · reset · delete · per-layer preset dropdown · Save). `UI/LayerPresetHeader.h` (`mu_ui::wireSlotPresetHeader` + `refreshSlotPresetList`) wires its reset (confirmed) / preset load / save (name prompt) to the ProcessorBase slot-preset API, so panels only add delete / rename where they have them Products with fixed layers (mu-Toni, mu-On) hide delete and rename (`setShowDelete(false)`, `setNameEditable(false)`); `setPresetFiles` + `onPresetFileChosen` fill the list from files and hand back the chosen one.
- `Persistence/ScopedApvtsLoading.h` (`mu_core::ScopedApvtsLoading`) — RAII guard that holds a processor's `std::atomic<bool> apvtsLoading` flag true during a bulk APVTS push so UI listeners early-out; the host class exposes `isApvtsLoading()`.
- `Persistence/PresetFiles.h` (`mu_pp`) — shared preset-file handling: safe file names, atomic writes, listing preset files, the category list, and write / read a full preset (the state tree wrapped in `<Mu…Preset name description category>`). The state inside — layer nodes, globals, the composed state — is `Persistence/LayerState.h`.
- `Persistence/LayerState.h` (`mu_pp`) — the composed slot state every product saves and loads (see [Slot state](#slot-state-presets--sessions--family-standard)): `LayerLayout`, `LayerExtras`, `captureSlot` / `applySlot`, `captureState` / `applyState`, `composeLegacyState`; wrapped by `ProcessorBase::initLayerState` and its capture / apply helpers.
- `Persistence/PresetMeta.h` (`mu_pp::readPresetMeta`) — a preset's root tag + root attributes (category, description, embed flag, e.g. mu-On's `lane`) read from the opening tag only, never the body. It understands both file shapes (the wrapper's `category` / `description` attributes and mu-Clid's `presetCategory` / `presetDescription`). Every preset list reads files through it — never `parseXML` a whole folder.
- `UI/StandardSettingsOverlay::addProgramChangeSection(layerTable, fullTable)` — the shared MIDI Program Change section (buttons opening the shared program-change tables). Every product with presets calls it; MIDI program changes are queued on the audio thread (`scanMidiProgramChanges` → `requestDeferredWork()`) and loaded in `runDeferredWork` on the message thread (`drainPendingMidiProgramChanges`).
- `UI/Voice/VoiceBand` — the shared voice band (mu-Clid's layout): Pitch | Filter | Amp | Effects in raised boxes, the FX sends right-aligned in the Effects box beside a narrowed insert dropdown. Products supply the four section components (binding stays product-side); `VoiceBandSection` builds a section from controls a product already owns (two Size-2 rows, `place(control, row, col, span, dropdown)`). Used by mu-Clid (`VoiceSection` subclasses it) and mu-Toni (`EnginePanel`); mu-Tant's voice panel is a different structure and stays product-side.
- `UI/Voice/InsertSubsection` — the shared insert-effect voice subsection (algorithm dropdown + 4 generic slot knobs), bound to a channel via a constructor prefix (`"r"` / `"v"` / `"a"` / `"t"` for Rhythm / Pattern / Arp / Track layers); optional product hooks for mod-arc indicators, the Comp/Limiter GR meter, and the algo-switch bulk-write wrapper. Part of the voice section (engine → insert → mixer).

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
| `ProcessorCore` | `mu-core/Plugin/ProcessorCore.h` (header-only; ruled 2026-10-11) | The slim parent of `ProcessorBase` and the **test seam**: `juce::AudioProcessor` + the channel / layer accessors (`getNumLayers`, `getLayer` + const wrapper, `getMaxLayers`, `getLayerName`, `getLayerColourIndex`) + the published host transport (`isHostPlaying`, `getHostTimeSignature`, `getHostBarStartPpq`, `getHostPpqPosition`, `pollHostTransport`, the `processBlockBypassed` override). It depends only on JUCE, `Plugin/HostTransport.h` and `Sequencer/Layer.h`: no mixer, FX, voice, licensing, persistence or MIDI-control code, so `mu-core-tests` can construct a derived fake. `ProcessorBase : ProcessorCore` keeps everything else; no product or UI code changes (they keep taking `ProcessorBase*`). Rule: new state or behaviour that a unit test must reach without the engine goes in `ProcessorCore`; anything that needs `fxChain`, `mixerEngine`, licensing or presets stays in `ProcessorBase`. When layer ownership moves into the base (Layer ownership stage 4), the layer array, count and lock land in `ProcessorCore`. |
| `ShellBackend`, `ShellChrome` | `mu-core/Plugin/ShellBackend.h`, `mu-core/UI/ShellChrome.{h,cpp}` (ruled 2026-10-12, not yet built; stages S1 to S4) | The processor-free seam under the shared chrome: `ShellBackend` is what `TransportBar`, `StandardSettingsOverlay` and the preset flow read from the app (`ProcessorBase` implements it; so does mu-link's `LinkShellBackend`); `ShellChrome` is the helper that owns the bar, overlays and layout for a host component. See [Shell chrome and the app seam](#shell-chrome-and-the-app-seam--family-standard-ruled-2026-10-12) |
| `MixerFxParams` | `mu-core/Plugin/MixerFxParams.h` | `mu_mixfx::addGlobalFxParams(layout)` declares the shared global-FX / return / master param set (`eff_`/`dly_`/`rev_`/`eff2*`/`echo_`/`ret_*`/`mstr_lvl`/`mstr_pan`/`mst_ins*`) the MixerOverlay/FXRow/DelayRow bind to. **Strips:** `mu_mixfx::addChannelStripParams(layout, "ch{N}_", name, options)` declares each channel strip's 12 params (options: version hint / continuous ranges for mu-clid's legacy ids, per-product sidechain defaults); the product adds only its own globals (e.g. mu-clid `mstrLoop`). **Sync contract:** each product calls `ProcessorBase::startFxParamSync()` once at the end of its constructor — the base listens to every mixer / FX id, routes changes to `syncGlobalFxParam` and seeds the engines; a product's own listener (mu-clid's rhythm params) skips those ids |
| `VoiceEngine` | `mu-core/Audio/VoiceEngine.{h,cpp}` | Per-slot voice chain; no PluginProcessor dependency |
| `MixerEngine` | `mu-core/Audio/MixerEngine.{h,cpp}` | Channel strips / sends / sidechain / returns / master; optional per-channel render hook (`RenderChannelFn`) |
| `InsertProcessor` | `mu-core/Audio/InsertProcessor.{h,cpp}` | Parameterised entirely via `VoiceParams` |
| `MultiModeFilter`, `SamplePlayer`, `MidiOutputEngine` | `mu-core/Audio/` | Generic audio units |
| `FXChain` + slots | `mu-core/Audio/FX/` | Generic send / return / insert chain |
| `ModulationMatrix`, `ControlSequence`, `ModulatorSerialise` | `mu-core/Modulation/`, `mu-core/Sequencer/` | Modulation system + shared (de)serialise; product's slot inherits `Layer` |
| UI widgets | `mu-core/UI/Components/` | Knob, dropdown, segment, step / LFO editor, VU, status bar, `MuLookAndFeel` |
| `MixerChannel`, `MixerOverlay`, `FXRow`, `DelayRow` | `mu-core/UI/` | Shared mixer + FX panels. `MixerChannel` is callback-driven: `isMaster()` replaces the old `hasInsert()` / `insertShown()`, and `setShowInsertPanel` is gone (the master strip never hosts an insert; the panel is a separate component). `MixerOverlay` owns the panel and the binder |
| `MasterInsertPanel`, `InsertSlotConfig.h` | `mu-core/UI/` | The one master insert layout in the family (callback-only; used by the plugins and mu-link). `SlotConfig::disabledFromP1` plus `insertSlotEnabled()` give the Vocoder / VocoderSt grey-out (P2 to P4 off when P1, the Wave, is 2 or more) inside the panel; `setGainReductionSource(slot, const std::atomic<float>*)` feeds the Compressor / Limiter GR meter |
| `MasterInsertBinding` | `mu-core/UI/MasterInsertBinding.{h,cpp}` | **Plugin-only** (takes `ProcessorBase&` and `MasterInsertPanel&`; not on the chrome list). Writes the `mst_ins*` / `mst_ins2*` APVTS parameters from the panel's callbacks and refreshes the panel from `mixerEngine` on `MixerOverlay`'s deferred apvts-dirty flush |
| `HoverStatus` | `mu-core/UI/Components/HoverStatus.h` | Header-only `mu_ui` mouse listener that reports a name and value to the status bar; not on the chrome `.cpp` list |
| `LayerSidebar` + `SidebarItem` | `mu-core/UI/` | Shared layers sidebar (product injects the mini-graphic; was `ChannelSidebar`, renamed per the 2026-10-12 ruling below) |
| `LayerPanel` | `mu-core/UI/` | Built (#1506): the per-layer stack (preset strip, product content band, `ModulatorPanel`); see "Shared layer sidebar and layer panel" |
| `LayerHeaderBar` | `mu-core/UI/` | Shared per-layer header (name / reset / delete / preset / save) |
| `VoiceBand` / `VoiceBandSection` | `mu-core/UI/Voice/` | Shared voice band layout + drawing (Pitch / Filter / Amp / Effects) |
| `InsertSubsection` | `mu-core/UI/Voice/` | Shared insert-effect voice subsection (channel-prefix bound) |
| `mu_hotswap::Stager` / `LayerSwapper` | `mu-core/Plugin/HotSwap.h` | Shared hot-swap staging state + loop-wrap predicates; `LayerSwapper<Payload, N>` is the whole loop-boundary driver (stage-or-apply, commit, audio-thread flagging with a product-supplied boundary rule), held by `ProcessorBase` as `layerSwapper` (flagged from `processBlock` through `flagSwapBoundaries`) and used by mu-Tant, mu-Toni and mu-On; mu-Clid deliberately keeps its own `HotSwapStager` on `Stager` (decided 2026-10-13, see stage 7 step 5) |
| Per-slot preset API | `ProcessorBase` (`slotPresetFiles` / `saveLayerPreset` / `loadLayerPreset` / `resetLayer` / `hasPendingSwap` / `cancelPendingSwap` + `onLayerPresetCommitted`) | One name for a layer's preset save / load / reset in every product; MIDI program-change slots load through it (`applyMidiPresetLayer` default) |
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

### Headless product processors (the test seam) — ruled 2026-10-11

`ProcessorCore` lets `mu-core-tests` drive a fake. A *real* `PluginProcessor` (mu-Tant, mu-Toni, mu-On,
mu-Clid) must also be constructible in a console unit target, so layer ownership after construction, the
structural-edit locks and `processBlock` under a second thread are tested on the real class, not a mirror
of its constructor. Today they cannot be, for one reason only: `PluginProcessor::createEditor()` is
defined in `PluginProcessor.cpp` and names `PluginEditor`, which drags the whole UI tree into the link.
The processor headers and the other processor `.cpp`s already include no UI (mu-Tant's
`PluginProcessor_APVTS.cpp` is already linked into its tests for this reason). The seam is therefore
three small rules, no new class and no `ProcessorBase` split:

1. **`createEditor()` is defined in `PluginEditor.cpp`, never in a `PluginProcessor*.cpp`.** Every product
   (mu-Clid, mu-Tant, mu-Toni, mu-On, mu-Clid MIDI) does the same: `PluginEditor.cpp` ends with
   `juce::AudioProcessorEditor* PluginProcessor::createEditor() { return new PluginEditor(*this); }`.
   No new file and no new `PluginProcessor_<Aspect>` (the aspect set in
   [design-naming.md §3](design-naming.md#files-and-folders) stays `APVTS, Preset, Modulation, Content`).
   Rule for new code: a processor `.cpp` includes no header from `UI/` and no `PluginEditor.h`.
2. **`ProcessorBase::ScopedStorageRoot`** (a small RAII type in `ProcessorBase.h`, message thread, tests
   only). While one is alive, `initAppSettings(name)` puts the settings file and the `*_midi*.json` maps
   under `<root>/settings/` and `getContentDir()` defaults to `<root>/content/<name>`, so constructing a
   processor never reads or writes the user's real `TDP` settings, presets or licence folder. The override
   is a process-wide function-local `juce::File`; production code never sets it.
   Implementation detail: because `createEditor()` now lives in `PluginEditor.cpp`, each product's test target
   (which does not link the editor) defines a one-line `createEditor` stand-in in its `ProcessorSeamTests.cpp`
   (returns `nullptr`).
3. **Product test targets build the real processor from a shared source list.** A console target cannot
   link the INTERFACE `mu-core`, so the list of `mu-core` `.cpp`s behind `ProcessorBase` (today the block
   under "Sources behind ProcessorBase" in `mu-core/Tests/CMakeLists.txt`) moves to one file,
   `mu-core/cmake/ProcessorTestSources.cmake`, defining `MU_CORE_PROCESSOR_TEST_SOURCES`, included by
   `mu-core-tests` and by each product's tests. A product adds its own `PluginProcessor*.cpp`, engine and
   sequencer `.cpp`s and the three licence `.cpp`s (licensed products), and its test `TestMain` creates a
   `juce::ScopedJuceInitialiser_GUI` (the base starts a message-thread poll timer).

What tests do with it: construct `ExposedXxx : mu_xxx::PluginProcessor` (a test subclass that `using`-exposes
the protected `layers` / `numLayers` / `layersLock`), call `prepareToPlay(48000, n)` and then
`processBlock` from a worker thread while the test thread calls `addLayer` / `removeLayer` / `swapLayers` /
`resetLayer` / `loadUserWavetable` / the swap commit. No audio device, host or editor is involved.
`ProductLayerSetupTests.cpp` (the constructor mirrors on `TestProcessorCore`) is deleted once the real
processors are covered (mu-Tant first, then mu-Toni and mu-On, mu-Clid last). Not chosen: a
`ProcessorBase` split into engine and UI halves (nothing else needs it; `createEditor` is the only UI edge)
and a test-only constructor on each product (it would be a second construction path to keep in step).

### Product-specific components (under `<product>/Source/`)

| Component | Why specific |
|---|---|
| mu-clid `RhythmSequencer` | Euclidean pattern generation |
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
Source/Sequencer/RhythmSequencer.{h,cpp}
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

`RhythmSequencer` stores `std::vector<Rhythm>`. mu-tant's engine will store `std::vector<Layer>` or a derived type.

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
- **GPU-backed image caches are heap-allocated and never destroyed** (ruled 2026-10-12) — a function-static or global cache of `juce::Image` under the Direct2D renderer is held by a leaked `new` pointer, not a static object. A static destructor runs at plugin-DLL unload under the loader lock, where releasing the Direct3D device hangs the graphics driver (found with Debug pluginval on mu-Clid; fixed in the `MuLookAndFeel` brushed-grain cache). Clearing such a cache at a size cap is safe only on the message thread, and only if every caller uses the returned reference immediately and never keeps it.

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
3. **`ProcessorBase` owns the layers** (the storage lives in its slim parent `ProcessorCore`, so it is unit-testable; see the platform table): `std::array<std::unique_ptr<Layer>, kMaxLayers> layers`,
   `std::atomic<int> numLayers`, `juce::CriticalSection layersLock`. A product supplies
   `createLayer(typeId)` and typed accessors (`getPattern(i)` = `layerAs<Pattern>(i)`, which asserts
   `typeId`). `getNumLayers()` is `numLayers`. Slot `i` of `layers`, of the mixer, and of the
   parameter prefix list are the same index.
4. **`Layer` stays data-light.** Its members stay `voiceParams`, `controlSequences`, `modulationMatrix`,
   `modLock`, `voiceParamsLock`, `name`, `colourIndex`; it gains the virtuals above and a message-thread-only
   clip bank (**amended 2026-10-13:** `SlotBound<mu_core::ClipBank> clips`, never read by the audio thread;
   the design is "Clip bank" below). It does **not** hold `InsertProcessor`: `Rhythm` is copied by
   value today and mu-Clid's insert lives in its `VoiceEngine` so a retired engine keeps its tail. mu-Tant
   and mu-Toni hold the insert as a plain member of `Pattern` / `Arp` (`InsertProcessor insert`, as `Arp`
   already does; replacing the `inserts` array). **Amended 2026-10-12:** the planned `mu_core::InsertStage`
   wrapper is dropped: mu-Toni's `insCfg` is already gone, mu-Tant builds its insert parameters per block, so
   the wrapper would carry nothing the two users share. What is shared is the virtual
   `Layer::insertGainReduction()` (default `nullptr`; `Pattern` / `Arp` return `&insert.grReduction`; `Rhythm`
   keeps the default: amended 2026-10-12, the engine stays processor-side at stage 7, so mu-Clid's `getInsertGRReductionPtr` stays and the
   meter reads it until stage 8 decides where engines live), which replaces `getInsertGRPtr` for mu-Tant and mu-Toni. A member that
   belongs to the *slot* and not to the layer's data (the insert's delay lines) is wrapped in `SlotBound<T>`
   (next to `OwnerLink` in `Layer.h`: copy and assignment do nothing), so a layer copy or `adoptDefaults` never
   moves or clobbers it. It still travels with the object on a pointer move; state that must stay at the slot
   index or outlive the layer (a voice engine, the retire tail, the UI snapshot) lives processor-side, see
   "Slot-keyed audio state stays processor-side". Anything a layer owns *as data* moves with it when the pointer moves.
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
      products do), the follower state moved with the strip (`scEnv`, see below; `resetSidechainEnv` only for new or cleared strips), `numLayers`, then `Layer::onMoved` for each moved layer.
   **Ruled 2026-10-13 (#1318):** the sidechain follower (`MixerEngine::scEnv`, the ducking-gain state) belongs to the
   *strip*, not the slot index: it travels with the strip on swap (exchanged) and remove (copied in the shift), so a
   ducked strip never un-ducks for ~90 ms when it lands on another ducked strip's slot. It is zeroed only where the
   strip itself is new or cleared: `addLayer`, `resetLayer`, and the vacated top slot after a remove.
   **Reset layer** (`resetLayer`) means "back to a freshly added layer in this slot": layer data, parameters **and the
   mixer strip** (level, pan, sends, mute/solo, sidechain, output bus) all return to defaults, family-wide, as
   `addLayer` already does. The "mixer state stays with the slot" wording applies only to loading or saving a *layer
   preset* (`.muRhythm` etc.), which never touches the strip; it does not describe reset.
   `layersMoved(first, last)` fires here, under the lock.
   5. Unlock, then move the **parameter values** by slot, inside the `ScopedApvtsLoading` guard and still
      suspended, so no block ever pairs a moved layer with unmoved parameters. For products where the APVTS
      is the truth (Tant, Toni, On): `LayerLayout::writeParams(node, from)` then `applyParams(node, to)`
      (the rows carry no prefix, so a slot loads into any slot; a missing row = default, which is how a new or
      vacated slot is cleared). mu-Clid, whose `Rhythm` is the truth for the Euclidean params, keeps pushing
      from the layer (`pushRhythmToApvts`) through a `syncSlotParams(slot)` hook. This is the one allowed difference.
   Between 5 and 6, `addLayer` alone calls `layerAdded(index)` (added 2026-10-12 with mu-On: message thread,
   still suspended, parameters at defaults, not under `layersLock`) so a product can give the new layer its own
   starting point; mu-On picks the first engine not in use and seeds that engine's groove there. Only `addLayer`
   calls it; `resetLayer` does not (a reset keeps the track's identity).
   6. `suspendProcessing(false)`; destroy any removed layer; set `colourIndex` (first unused palette entry,
      the mu-Clid / mu-Tant rule, hoisted into the base); fire `onLayersChanged` for the shell.
   The last layer cannot be removed; there is no demo cap any more (retired 2026-10-11; see "Licensing and the support prompt").
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
| 1 | M | Derived types as value members, no behaviour change | **Tant**: `Pattern : Layer` holds `gate`, `filter`, `pitch` (`GatePattern`), `ring` (`VoiceRingBuffer`), `snap[]`, `InsertStage`, the retiring state, the user-wavetable path / index per oscillator, the voice engine pointer; `std::array<Pattern, 8> patterns` replaces `voiceSlots`, `gatePatterns`, `filterPatterns`, `pitchPatterns`, `voiceRingBuffers`, `voiceSnap`, `inserts`, `retiring`, `voiceColourIndex`, `osc*User*`. `Pattern::copyFrom(const Pattern&)` replaces the `copyDataFrom` calls. **Toni**: `Arp : Layer` holds `ArpVoiceRunner`, `InsertStage`, `vp` pointers, `modDestAtoms`; replaces `runners`, `inserts`, `insCfg`, `vp`, `modDestAtoms`, `voiceSlots`. **On** (built 2026-10-12): `Track : Layer` holds the trigger counter, its `StepRow` and its Rumble envelope (data the layer owns, so it moves with the pointer; `StepPattern` only binds each slot to the track's row, rebound in `layersMoved`); `GrooveVoices` (engines per slot, one object, heap-allocated) stays in the processor. **Clid**: nothing; `Rhythm` already derives. | Each product's unit tests; its `*_roundtrip` (sample-identical); `check-core-boundary` |
| 2 | M | `ProcessorBase::getLayer(int)` accessor (virtual, returns the product's value-array slot) | Default `getLayerName` / `getLayerColourIndex` come from `Layer::name` / `colourIndex`, built with the names the products show today ("Voice N" for mu-Tant, "Layer N" for mu-Toni, lane names for mu-On, the rhythm name for mu-Clid), so nothing visible changes; the editors' `voiceSlots[sel]` / `voiceSlot(lane)` become `getLayer(sel)`; the GR-meter pointer helpers (`getInsertGRPtr`, `getInsertGRReductionPtr`) become `getLayer(i)->insertGainReduction()` | unit tests; boundary check; build all four |
| 3 | M | Persistence virtuals | `Layer::writeExtras` / `applyExtras` (base: modulators); each product's `LayerExtras` lambdas become the virtual overrides on `Pattern` / `Arp` / `Track`; `initLayerState(prefixes)` builds the `LayerExtras` that calls `getLayer(slot)`. mu-Clid keeps `PresetIO` shapes and gains nothing here. | `MuTantPersistTests`, `ModulatorSerialiseTests`, `PresetRoundTripTests`, `PresetXMLRoundTripTests`, `TONI_roundtrip`, `ON_roundtrip`, `TANT_roundtrip` |
| 4 | S | Ownership moves to `ProcessorBase` | The `layers` array, `numLayers`, `layersLock`, `createLayer`, `layerAs<T>`; Tant first (its `numVoices` / `voicesLock` become these, `bankLock` split out), then Toni and On at a constant count (4 / 5) with the try-lock added so a later edit is safe; the product's `addVoice` etc. still exist and now move pointers instead of copying data. | as stage 3, plus a new `LayerOwnershipTests` (construct, publish, destroy off-lock) and a listening render with structural edits scripted mid-play |
| 5 | S | Central edits and the generic stager. **Part 1 done (commit a41c5fd):** `addLayer` / `removeLayer` / `swapLayers` / `resetLayer` in `ProcessorBase` with the hooks `createLayer`, `canEditLayers`, `cancelPendingSwap`, `Layer::onMoved`; mu-Tant adopted (its `addVoice` / `removeVoice` / `swapVoices` / `resetVoice` are gone). Deviations: mu-Tant keeps all 8 `Pattern`s allocated (add / remove reset and re-use a slot rather than creating / destroying one); no `typeId` yet (stage 8); no `onLayersChanged` hook; mu-Clid adopts with stage 7 (`RhythmManager` stays until then). **Part 2 done (commit 8a8be28, ruled 2026-10-11):** the stager storage `mu_hotswap::Stager` stays a template in `mu-core/Plugin/HotSwap.h`; the generic piece is `mu_hotswap::LayerSwapper<Payload, N>` (the old `BarLineSwapper` generalised: the boundary rule is a callable giving the loop length in beats per slot), held by `ProcessorBase` as `layerSwapper` (a `ValueTree` instance, `N = kMaxLayers = 8`) with default `hasPendingSwap` / `hasPendingFullPreset` / `cancelPendingSwap` over it and `flagSwapBoundaries` for the audio thread. mu-Tant's `VoiceHotSwapStager` is deleted and the Toni / On `BarLineSwapper` members are dropped (they now call `flagSwapBoundaries` with the default bar rule); mu-Tant's boundary rule is `mu_tant::swapLoopBeats`; mu-Clid keeps its `PendingRhythm` stager until stage 7. Deviations: (a) N is 8 in Toni and On too (was 4 / 5), so the stager is one size family-wide; (b) `onPresetSwapCommitted` now fires once, after all applies in a commit, rather than between them; (c) mu-Tant decided stage-or-apply itself on `isInternalPlaying()` at first; it now follows the family rule through `useFull` / `useSlot` (see the Hot-swap section). || new `LayerStructureTests`: fill to 8, remove the middle, swap ends, reset; assert parameter values, colour, mixer channel, sidechain source and pending-swap cancel all follow; a two-thread stress test (message thread edits while a loop calls `processBlock`, no allocation, no crash); every `*_roundtrip` and the hot-swap listening tests (`swap`, `slot swap`) |
| 6 | O | Variable layers in mu-Toni (#1240) and mu-On (#1241); the clip bank (#1277) | See owner decisions below. The clip bank is `Layer::clips`: a clip is a stored layer-preset tree, launching it is a staged layer swap (Stage 5), and a pad never writes a preset. Ruled and staged in "Clip bank" below. | tests per feature, written with the decisions |
| 7 | S | mu-Clid into the container | **Amended 2026-10-12 (Architecture Steward): the ordered plan is "Stage 7 implementation plan (#1318)" below; read that, not this cell.** In short: `mu_core::Rhythm` is used directly (no `mu_clid::Rhythm`, no engine members in the layer, `Rhythm` stays a copyable value); the voice / MIDI engines, retired tails, play state, sample paths and modulation previous-state stay processor-side keyed by slot and move in a `ProcessorBase` hook; `RhythmSequencer` stops owning the rhythms and binds `Rhythm*` per slot; `rhythmsLock` becomes `layersLock`; `RhythmManager` is deleted. Highest risk of the plan (about 1580 uses, the retire-tail), so four steps, each its own commit. | `mu-clid-tests` (build the target explicitly, it is not in ALL), `mu-clid-midi-tests`, `CLID_roundtrip`, the retire-tail and hot-swap listening tests, a scripted structural-edit render |
| 8 | O | Registry and `Layer::render` | With the combined instance. The saved-data renames it waited for (#1263) are done. Until the owner schedules the combined instance the mixer's `RenderChannelFn` stays product-side, and nothing is built for this stage; see "Stage 8: what the owner decides (#1319)". | with that work |

Stages 1-3 are order-independent between products (any product can go first). From stage 4 on, do Tant first
(it already has the dynamic count and the lock), then Toni, On, and Clid last.

### Owner decisions for Stage 6

- **mu-On (#1241):** its parameters were prefixed by *lane type* (`k_ b_ h_ s_ r_`); since 2026-10-12 they are
  index-based (`t0_` ... `t4_`, #1263 step 2, with legacy-state and MIDI-map migration), which a variable
  count with a per-layer engine selector needs. **Settled by Andy 2026-10-11: (b), a variable lane count on the
  index prefixes** (the alternative, five fixed lanes, is dropped). Open sub-points from #1241 that need a
  design before the work: which kick a Rumble layer processes (Andy 2026-10-08: a Source selector, default the first
  Kick), per-layer params (each layer keeps every engine's settings), step rows per layer, and how 5-lane
  presets map to layers (they load as their 5 layers).
- **mu-Toni (#1240):** `kNumChannels` is 4 today. Going to 8 adds layers 5-8 parameters (additive, old sessions
  load with the new layers at defaults) and changes the host's automation list. Default count and demo cap
  **confirmed by Andy 2026-10-12:** fresh instance 1 layer, no cap.
- **Both:** the add / delete / reorder UI (it is the shared `ChannelSidebar`, so it is a visible change for the
  freeware and licensed products alike) and whether delete fades or cuts (item 7 says cut).

### Stage 6 build rulings (2026-10-12, UX Controller)

- **The structural edit is not re-implemented.** `addLayer` / `removeLayer` / `swapLayers` / `resetLayer` (stage 5) are the one
  central implementation (the processor-side logic was lifted from mu-Clid / mu-Tant when mu-Tant adopted it; it
  lives in `ProcessorBase` with the pointer moves in `ProcessorCore`, and a separate "composed slot-state class" is
  **not** needed: the per-layer parameter and extras state is already `LayerLayout` + `Layer::writeExtras`).
  Two small additions, both hoisting what mu-Tant does alone today (ruled 2026-10-12, Architecture Steward; see
  "Count and colour persistence" and "Layer references" below). mu-Toni and mu-On adopt the edits as mu-Tant did: all 8 layers' parameters and `Layer` objects
  allocated at construction (spare slots re-used, `keepSpareLayers`), `canEditLayers()` true, the product's
  per-slot caches (`vp`, `modDestAtoms`, runners) indexed by slot. What is still duplicated is the **editor glue**
  (mu-Tant `PluginEditor::onAddLayer`, `onDelete` + confirm, `deleteShownVoice`, `onPresetLoaded` / `onPresetNew`
  refresh). It lifts to `mu-core/UI/LayerEditController.{h,cpp}`, `mu_ui::LayerEditController`, constructed with
  `(ProcessorBase&, LayerSidebar&, LayerPanel&, Hooks{ unbindAll(), showLayer(int) })`; the product supplies only the
  two hooks (mu-Tant's `clearAllModBindings` / `clearPatternBindings` and its `setLayer`). Behaviour: [design-ui-family.md](design-ui-family.md#layer-add--delete--reorder-family-standard-ruled-2026-10-12).
  mu-Tant moves onto it in the same step; mu-Clid when it adopts stage 7.
- **mu-Toni:** the layer count is persisted with the state; absent (old file) = 4. Fresh = 1. No cap. No
  parameter migration (additive).
- **mu-On as built, decisions confirmed 2026-10-12 (Architecture Steward):** a track's name derives from its engine and is
  neither saved nor editable (`setNameEditable(false)`). mu-Toni and mu-Tant are also non-editable now (a layer is named after its index); only mu-Clid saves
  user layer names. A product with fixed or derived names calls `setNameEditable(false)`; `LayerPanel` does the same for fixed layers; `.muTrack` presets carry their engine and every track lists all of them; Reset is a sound reset and keeps the
  engine, Rumble source, step row and envelope; a saved MIDI map is migrated only for the five original `k_ b_ h_ s_ r_` ids
  (the only ones that ever existed); an engine change never prunes modulation assignments, on any route (UI, automation, preset): the old engine's assignments stay in place, inert while another engine runs (the destination tables are per engine and the audio side reads only the running engine's) and live again on switching back; selecting Rumble on a track re-points every Rumble that processed it to the first Kick (else the first non-Rumble track).
- **mu-On saved data (extends #1263):** each layer's block is `t{N}_eng` (choice: Kick, Bass, Hat, Snare, Rumble;
  the existing five engines, Hat and Snare being the sample engine with different defaults), `t{N}_src` (Rumble
  source, a layer index), then every engine's parameters **engine-qualified**: `t{N}_kick_*`, `t{N}_bass_*`,
  `t{N}_hat_*`, `t{N}_snare_*`, `t{N}_rumble_*` (the unqualified names collide: both Kick and Bass have `tune` and
  `drive`, Bass and the samplers `dec`). Legacy migration, through the existing mechanism: old `t{i}_x` becomes
  `t{i}_<engine of lane i>_x`, `t{i}_eng` = i, `numLayers` = 5, `t{i}_src` = 0 for the Rumble layer, layer names
  Kick / Bass / Hat / Snare / Rumble. The mixer sidechain values are carried as saved (they are mixer params); the Bass
  Duck / D.Atk / D.Rel knobs and the pre-wired default go (owner 2026-10-08). Layer-order dependent state remaps on every
  structural edit like the mixer sidechain source: `t{N}_src` follows its layer.
- **mu-On step data (as built, 2026-10-12):** `kMaxLayers` rows x 16 (on / accent), one row per layer, kept when the
  engine changes (a Rumble layer ignores its row). Ruling amended: the row (`StepRow`) and the Rumble envelope are members
  of `Track`, not processor arrays, because they are data the layer owns and must move with it ("Slot-keyed audio state
  stays processor-side" is only for state that must outlive the layer or keep a stable address); `StepPattern` holds
  per-slot pointers to the rows, rebound under `layersLock` in `layersMoved`, and the envelope persists only when non-default.
  `GrooveVoices` owns one engine of each type per slot (or lazily, DSP's call) and renders the selected `eng`.
  **Render order: all non-Rumble layers first, then Rumble layers**, so a Rumble source is always rendered this
  block regardless of layer order (to `dsp-controller` for review); a Rumble whose source is itself a Rumble or is out of range
  processes silence.

#### Count and colour persistence (ruled 2026-10-12, Architecture Steward)

Today only mu-Tant saves the live layer count and the palette colours, as two root properties of its composed state
(`numVoices`, an int, and `voiceColours`, 8 comma-separated palette indices), written in its `captureFullPreset` and
applied in `applyFullPresetTree`. They move into `ProcessorBase` so a second and third product do not copy them:

- **Saved names do not change:** `numVoices` and `voiceColours` stay as written, in every product (the "voice" word is
  legacy saved data; see design-naming §3). A layer preset (`.muPattern`, `.muArp`, `.muTrack`) never carries them.
  Layer names are not saved (the product's `getLayerName` derives "Voice N" / "Layer N" / "Track N" from the index).
- **`captureComposedState()`** adds both properties when `canEditLayers()`; **`applyComposedState()`** applies the
  parameters and extras as now and then stores the count and restores the colours (the order mu-Tant has: count last).
  A new `int savedLayerCount(const juce::ValueTree&) const` returns the clamped count a state carries, so a product that
  must act on the old and new count before the apply (mu-Tant's retire-tail) reads it from there, not from the property.
- **A state without `numVoices` loads with the product's legacy count**, a protected `int unsavedLayerCount` set in the
  constructor: mu-Tant 1 (as today), mu-Toni 4, mu-On 5. A state without `voiceColours` leaves the constructor colours
  (slot `i` = palette entry `i`, as mu-Tant) unless the product's `to…State` step writes the property: mu-On's legacy
  migration writes `0,2,1,3,4,5,6,7` so Kick / Bass / Hat / Snare / Rumble keep their red / orange / cyan / magenta / lime.
- **Fresh instance = 1 layer** in all three (`numLayers.store(1)` after construction), per the UI family standard.
- The audio thread still never reads these properties; the count is the `numLayers` atomic, stored on the message
  thread (host restore, preset load, the staged commit), exactly as mu-Tant does.

#### Layer references (ruled 2026-10-12, Architecture Steward)

A parameter whose value names another layer (mu-On's Rumble `t{N}_src`) must follow its layer through a structural
edit, as the mixer's sidechain source does. `ProcessorBase` gets `virtual void remapLayerReferences(const
std::function<int(int)>& oldToNew)` (default empty), called from phase 3 of `removeLayer` and `swapLayers` immediately
beside `remapSidechainSources`, before the parameter values shift. `oldToNew` is built from
`mu_mix::sidechainAfterRemove` / `sidechainAfterSwap` (`mu-core/Audio/SidechainRemap.h`, already tested) and returns
-1 when the referenced layer is gone; the product rewrites its own parameters through it (a gone source becomes its
default, the first Kick). `resetLayer` and `addLayer` do not call it. Only mu-On overrides it today; it is in the base
because the remap has to sit inside the same suspended, ordered edit as the sidechain one, which a product cannot do
from outside.

#### Order of work for #1240 and #1241 (ruled 2026-10-12)

Each step ends with a Debug build, that product's unit suites, `mu-core` suites, and the `*_roundtrip` listening
tests; mu-Clid is not edited (its `canEditLayers()` stays false and it does not call `captureComposedState`), but
`mu-clid-tests` is built and run after steps 1 and 2 because they touch `ProcessorBase`.

1. **`mu-core` + mu-Tant (no behaviour change):** the count / colour persistence above, mu-Tant switched over (its
   `numVoices` / `voiceColours` lines go; `NoDemoCapsTests`, `PresetRoundTripTests`, `TANT_roundtrip` unchanged and
   green is the proof); `remapLayerReferences` with a `LayerOwnershipTests` case (fake product, three layers, remove the
   middle, assert the references follow and the gone one maps to -1); `unsavedLayerCount` default 1.
2. **`LayerEditController`** (the editor glue, above), mu-Tant moved onto it. Owned by the UX ruling; mu-Toni and mu-On
   cannot ship the add / delete UI without it.
3. **mu-Toni (#1240)**, then **4.** **mu-On (#1241)**. mu-Toni first: it has no engine change, so it proves the adoption
   path (8 layers of parameters, try-lock in `processBlock`, `layersMoved`, count persistence) alone. mu-On then adds
   only what is its own (engine selector, engine-qualified parameters, per-slot step rows, Rumble source).

**mu-Toni recipe (the same eight points mu-Tant satisfies; nothing else is new):**
(a) `kNumChannels` is deleted; parameters (`a{N}_*`, `ch{N}_*`), `vp` / `modDestAtoms` pointers and mixer strips are
declared and cached for all `mu_limits::kMaxLayers` slots; the `aud = (i == 0)` default (layers 2 and up silent) goes,
because a layer added by the user must be audible at its defaults (to `ux-controller` if it objects);
(b) `keepSpareLayers = true`, all 8 `Arp`s built by `createLayer(slot)` (which sets the shared `bank` on the runner,
`insert->setSlotIndex(slot)` and prepares it), `canEditLayers()` true, `numLayers` 1, `unsavedLayerCount` 4, colour
`i` for slot `i`; `getLayerColourIndex` is the base default over `Layer::colourIndex`; `getMaxLayers` override goes;
(c) `processBlock` takes `ScopedTryLock(layersLock)` once, skips the render while `layerEditInFlight`, loops
`readVoice` / `render` over `numLayers` only, and still runs `flagSwapBoundaries` when the lock misses (mu-Tant's shape);
(d) `layersMoved(first, last)` resets the runner, insert and any held-note or accent state of each moved slot, no
allocation; (e) `prepareToPlay` prepares all 8 runners and inserts under `layersLock`; (f) `setupLayerState` uses
`kMaxLayers` prefixes and `legacyLayerParamPrefixes(kMaxLayers)`; (g) the editor wires `onAddLayer` / delete through
`LayerEditController` and calls `layerPanel.setFixedLayers(false)`; (h) tests: Toni `ProcessorSeamTests` (count now 1 fresh),
a `LayerStructureTests`-style case (fill to 8, remove middle, swap, parameters / colour / mixer strip follow), the
two-thread stress test copied from mu-Tant's seam tests into the shared `ProcessorSeamHelpers.h` rather than a second
copy, an old 4-layer composed state and an old APVTS dump both loading as 4 layers, a state with `numVoices` 6 round trip.

### What is left under #1265 and #1264 (ruled 2026-10-12)

State of the tree: stages 1 to 3 and 5 are done for mu-Tant, mu-Toni and mu-On, stage 4 for those three; the
`Slot` to `Layer` renames are done (`voiceSlots`, `kMaxChannels`, `kMaxVoices` no longer exist). Open:

1. **#1265 closes after three small steps** (it is the hoisting umbrella): (a) `SlotBound<T>` and
   `Layer::insertGainReduction()` in `Layer.h`; (b) mu-Tant's `inserts` array becomes `Pattern::insert`
   (`SlotBound<InsertProcessor>`), mu-Toni's `Arp::insert` is wrapped the same way and both override
   `insertGainReduction()`, the editors reach the meter through one `ProcessorBase::getInsertGR(i)`; (c) `slotLayer`
   is replaced by the base default (`keepSpareLayers ? layers[i].get() : getLayer(i)`) and mu-Tant's override goes.
   Not in scope, by ruling: `typeId` and `Layer::render` (stage 8, #1319), `resetToDefaults` (the shipped
   `adoptDefaults` does this job), the clip bank (#1277), hot-swap staging state (done: `LayerSwapper`), the mixer
   binding (slot index is the binding; nothing to hoist).
2. **#1264 closes after:** mu-Tant's remaining per-slot arrays (`voices`, `voiceSnap`, `retiring`) are **not**
   moved into `Pattern` (ruled 2026-10-12; "part A as written" is dropped, see "Slot-keyed audio state stays
   processor-side" below). They are gathered into one processor-side `std::array<SlotState, kMaxLayers>`
   (`engine`, `retiring`, `snap`), a tidy-up that is optional and needs no review beyond the DSP check
   (`blkPatternBeats` is a per-block audio-thread cache and stays in the
   processor); the sequencer role names `Arpeggiator` to `ArpSequencer` (done), and `SequencerEngine` to
   `RhythmSequencer` (done 2026-10-12, ahead of stage 7, as a pure rename);
   `GrooveSequencer` already conforms and mu-Tant has no sequencer class to rename (`GatePattern` is data and stays);
   the Tant `voice` vocabulary: non-UI identifiers (`currentVoice`, `numVoices`, locals) now, `VoicePanel` /
   `VoiceSidebar` with the shared `LayerSidebar` / `LayerPanel` work (#1505 / #1506; the per-layer sound panel is
   now `<Type>Content` inside `LayerPanel`, superseding the earlier `EnginePanel` choice; see the ruling below);
   mu-On's lane / channel wording to `Track` is **folded into #1241**, which rewrites the same code (233 uses),
   not done on its own. The step grid and `GrooveVoices` stay in the processor until #1241 decides the per-layer engine selector.
3. Stage 6 no longer waits on Andy: confirmed 2026-10-12, a fresh mu-Toni instance has 1 layer, old 4-layer states load
   as 4, no demo cap (mu-Toni is freeware and the demo cap is retired). Order of work: "Order of work for #1240 and #1241" above.

#### Slot-keyed audio state stays processor-side (ruled 2026-10-12)

`SlotBound<T>` only means "a layer copy, assignment or `adoptDefaults` does not carry it". It does **not** pin the
member to a slot index: `removeLayer` / `swapLayers` move the `unique_ptr<Layer>`, so a `SlotBound` member travels
with its `Pattern`. That is right for the insert's delay lines (they follow the layer's sound) and wrong for state
that must stay at the slot index or outlive the layer. mu-Tant's three arrays are of the second kind:

- `retiring[v]` must outlive the layer: a count-reducing full-preset swap fades the old tail of slots past the new
  live count, and the removed layer's `unique_ptr` is released after unlocking. `voices[v]` (one `VoiceEngine` per
  slot, built once in the constructor, rendered and reset past the live count) is what the tail is rendered through.
- `voiceSnap[v]` is read by the UI at 30 Hz by index (`getModSnapshot`) for slots that may be gone; the atomics must
  keep a stable address.

So they stay keyed by slot index in the processor, as one `SlotState { unique_ptr<VoiceEngine> engine;
RetiringVoice retiring; std::array<std::atomic<float>, kTantSnapCount> snap; }` array if the owner of the code wants
the three arrays tidied. Constraints for it, all existing behaviour that must not change: allocate every `engine`
in the constructor and never resize or reassign the array (the audio thread never allocates, the UI never sees a
dangling atomic); the retire tail is armed on the message thread under `suspendProcessing` before the new count is
published, with `config` written before the `samplesLeft` release store and read after its acquire load; the tail
is found by slot index, never through a `Pattern`. mu-Clid (stage 7) keeps its retired engines in
the same way (a per-slot record that survives `Rhythm`); mu-Toni's `ArpVoiceRunner` stays in `Arp` because it has no tail.

For the combined instance (design-future): none of the above blocks it, and nothing here closes it off. The
registry adds `typeId` -> `LayerTypeInfo`; the saved `Slot` node gains a `type` attribute (absent = the
product's own type); the prefix lists become per-type; `layerAs<T>` already asserts the type.

### Stage 7 implementation plan (#1318; ruled 2026-10-12, Architecture Steward)

Builds on stage 6 as committed (29bb55d): mu-Tant, mu-Toni and mu-On run on `ProcessorBase` `addLayer` / `removeLayer` /
`swapLayers` / `resetLayer`, `LayerEditController`, `numVoices` / `voiceColours`. mu-Clid is the last product with its own
edit routine (`RhythmManager`, `proc.rhythms.*`), its own storage (`RhythmSequencer` holds `std::vector<Rhythm>`),
its own count (`numActiveRhythms`) and its own lock (`rhythmsLock`).

**Status (built, build 1407).** Steps 0 to 4 are done; step 5 is decided against (2026-10-13, see the step 5 row). As built: `RhythmManager` is gone; mu-Clid's
rhythms are `mu_core::Rhythm` layers on the shared container (`keepSpareLayers`, `numLayers`, `layersLock`); `RhythmSequencer` binds `Rhythm*` per slot and
keeps only per-slot audio state; the processor-side per-slot state is moved by the hooks in `mu-clid/Source/Plugin/PluginProcessor_Layers.cpp`
(`createLayer`, `layersReordered`, `layersMoved`, `syncSlotParams`); `numActiveRhythms`, `rhythmStore` and the `swapLayers` / `resetLayer` /
`canReorderLayers` overrides are deleted. `kAutomatedRhythms` stays: it is the count of rhythms with full "Rhythm N" automation names, not a live count.
The MIDI product keeps its own `rhythmsLock` for its one fixed rhythm (ruling 4). The text below is the plan as ruled and is kept for the reasoning.

**Rulings that change the earlier stage 7 text.**

1. **No `mu_clid::Rhythm`.** mu-Clid uses `mu_core::Rhythm` (the `GeneratorAliases.h` alias stays), as the MIDI product does.
   The earlier plan put the voice engine, MIDI engine, retired tails, play state, sample path and previous-state flags into a
   derived layer. They stay processor-side, keyed by slot, as "Slot-keyed audio state stays processor-side" already rules for
   retired tails and UI-read atomics, because: `processCoreBlock` takes the engines as one contiguous
   `unique_ptr<VoiceEngine>` array; `installRhythm` retires the outgoing engine into a per-slot tail that must outlive the
   layer; `rhythmPlayState` and `modSnapshot` are read by the UI at 30 Hz by index and need a stable address (a mu-On
   `GrooveVoices` precedent). Putting an engine in the layer is exactly the `Layer::render` question of stage 8, which is not
   decided; stage 7 must not decide it by accident.
2. **`Rhythm` stays a copyable value** (the earlier plan made it non-copyable). `PendingRhythm`, `PreparedFullPreset`,
   `installRhythm`'s swap-in and `PresetIO`'s prepared rhythms all rely on that; it is only non-copyable if it holds an engine.
3. **`RhythmSequencer` stops owning rhythms.** It binds `Rhythm*` per slot (`bindRhythm(slot, Rhythm*)`, null = unbound), keeps
   `getRhythm(i)` / `getNumRhythms()` (so about 100 call sites do not change), keeps all its audio-private per-slot state
   (phase, step history, pending hand-over, pattern caches), and loses `addRhythm(const Rhythm&)`. `setNumRhythms(n)` sets the
   live count and clears the state of grown slots; it no longer resizes anything. `removeRhythm(i)` / `swapRhythmSlots(i, j)` become
   `removeSlotState(i)` / `swapSlotState(i, j)`: they move the per-slot state only (the pointer moves belong to
   `ProcessorCore`), and keep the lock contract (caller holds the layer lock with processing suspended). The slot state
   follows the rhythm on a reorder and on a remove, exactly as today; this is why `layersMoved` alone is not enough (below).
4. **The MIDI product owns its one `Rhythm` as a member of `MidiRhythmCore` and binds it to slot 0.** It does not adopt the layer container
   (one fixed layer, no edits, no `createLayer`); its `getLayer(0)` / `getRhythm()` return the same object as now. Its whole change is that
   constructor line and the CMake line for the new test, so "keeping the MIDI product working" is a step 1 property, proven before mu-Clid is touched.
5. **Two additions to `ProcessorBase`**, both additive with defaults that leave mu-Tant, mu-Toni and mu-On untouched:
   - `virtual void layersReordered(LayerEdit kind, int a, int b)` with `LayerEdit { Remove, Swap }`, called under `layersLock`
     immediately before `layersMoved` (remove: `a` = index removed; swap: `a`, `b`). It carries what `layersMoved(first, last)`
     cannot: whether the data at a slot moved from elsewhere. mu-Clid moves its per-slot audio state in it (the locked section
     of today's `RhythmManager::remove` / `swap`).
   - The parameter phase of the edits (phase 3: `copyLayerParams`, `swapLayerParams`, `resetLayerParams`) becomes a
     `virtual bool layerHoldsParamTruth() const` (default false) plus `virtual void syncSlotParams(int slot)`. When true the base
     skips the APVTS copy and calls `syncSlotParams(s)` for every slot the edit touched (the shifted ones, the vacated top slot,
     both exchanged slots, the reset slot), message thread, still suspended, not under `layersLock`. mu-Clid's override runs in its
     `apvtsLoading` guard: `pushRhythmToApvts(s)` and `pushMixerChannelToApvts(s)`, `sequencer.updatePattern(s)`, `voiceEngines[s]->setParams`,
     and frees engines parked under the lock. This is the "one allowed difference" of contract item 6.5, now a real hook.
6. **`Rhythm` gains `adoptDefaults(Layer&)`** (in `mu-core/Sequencer/Rhythm.cpp`, shared with the MIDI product, which does not call it):
   exchange `genA / genB / genC`, `logic`, `resetSteps`, `patternLegato` and the `RelaxedInt` output settings, then the base. Without it
   `resetLayer` would reset only the voice parameters and modulators. `createLayer(slot)` returns a default `Rhythm` named
   "Rhythm N+1" with the default genA; `layerAdded(index)` applies `_default.muRhythm` (what `applyDefaultRhythm` does today).
7. **`keepSpareLayers = true`**: all 8 `Rhythm`s are built in the constructor (the restore path writes data into slots above the live
   count before it sets the count, `PresetIO_HostState` does that today with `setNumRhythms(MaxRhythms)`), `canEditLayers()` true,
   `numLayers` replaces `numActiveRhythms`, `getNumActiveLayers` and `kAutomatedRhythms` read it, `rhythmsLock` is `layersLock`.
   mu-Clid does **not** call `captureComposedState` / `applyComposedState`, so it does not start saving `numVoices` / `voiceColours`:
   its saved data does not change (`r0_` prefixes, `PresetIO` shapes, `MuClidState`, `.muClid`, `.muRhythm` all stay).
8. **`HotSwapStager` stays as it is** (it keeps its own `mu_hotswap::Stager<PendingRhythm, ...>`). Moving it onto
   `layerSwapper` (step 5) was **decided against, 2026-10-13**: see the step 5 row. `installRhythm` changes only in that the swap-in lands in `*layers[r]`
   (an address that no longer moves) instead of the vector element.

**Steps.** Each is one commit, ends with a Debug build, the suites named, and a byte comparison of the listening renders against
a baseline taken before step 1. A step that fails its proof is reverted, not carried into the next.

| Step | Kind | What | Files | Proof |
|---|---|---|---|---|
| 0 | gate | Start from a clean tree: the in-flight `SequencerEngine` to `RhythmSequencer` rename (and `ChannelHeaderBar` to `LayerHeaderBar`) must be committed first, because step 1 edits the same files. Build `mu-clid-tests` and `mu-clid-midi-tests` explicitly. Render the baselines: `CLID_roundtrip`, the `swap` and `slot swap` hot-swap listening tests, and the retire-tail test. | none | all green; baseline WAVs kept for byte comparison |
| 1 | M | Sequencer storage decoupled, no behaviour change. `RhythmSequencer` binds `Rhythm*` per slot (ruling 3). mu-Clid temporarily owns `std::array<Rhythm, kMaxLayers> rhythmStore` and `RhythmManager` moves data between stored elements exactly as the vector did; the MIDI product owns and binds its one `Rhythm` (ruling 4). | `mu-core/Sequencer/RhythmSequencer.{h,cpp}`, `mu-clid/Source/Plugin/PluginProcessor.{h,cpp}`, `mu-clid/Source/Sequencer/RhythmManager.cpp`, `mu-clid/Source/Persistence/PresetIO.cpp`, `PresetIO_HostState.cpp` (the `setNumRhythms` sites), `mu-clid-midi/Source/Sequencer/MidiRhythmCore.{h,cpp}`, `mu-clid-midi/Source/Tests/MidiRenderTests.cpp` (construction sites), `mu-clid/Source/Tests/RhythmSequencerWrapTests.cpp`, `SwapCommitTimingTests.cpp`, `MidiOutputModeTests.cpp`, `NoDemoCapsTests.cpp` (they construct sequencers), a new `RhythmSequencerSlotTests.cpp` | `RhythmSequencerSlotTests` (bind; grown slot clean; `removeSlotState` and `swapSlotState` carry phase, step history and pending start with the rhythm); every existing sequencer test unchanged; `mu-clid-midi-tests` including `SessionStateTests` and `MidiRenderTests` (the MIDI product's whole proof); `CLID_roundtrip` byte-identical |
| 2 | M | `mu-core` additions, no product uses them yet: `layersReordered`, `layerHoldsParamTruth` / `syncSlotParams` (ruling 5), `Rhythm::adoptDefaults` (6). `LayerEdit` lives in `ProcessorBase.h`. | `mu-core/Plugin/ProcessorBase.{h,cpp}`, `mu-core/Sequencer/Rhythm.{h,cpp}` | `LayerOwnershipTests` / `LayerStructureTests`: a fake product that sets `layerHoldsParamTruth`, asserts `layersReordered` fires once per edit under the lock with the right arguments and before `layersMoved`, and that `syncSlotParams` visits exactly the touched slots; a `Rhythm::adoptDefaults` case (all euclid data exchanged, name and colour kept, no `ControlSequence` address changes); full Debug build of all five products (`mu-core` touched); `mu-core`, mu-Tant, mu-Toni and mu-On suites and their `*_roundtrip` unchanged |
| 3 | S | mu-Clid adopts the container. Constructor builds 8 `Rhythm`s through `createLayer`, `keepSpareLayers`, `canEditLayers`, `numLayers`; `rhythmsLock` to `layersLock` with the same try-lock plus the `layerEditInFlight` skip; the locked sections of `RhythmManager::add / remove / swap / reset` move into `layersReordered`, `layersMoved`, `layerAdded` and `syncSlotParams` with identical order (engines, MIDI engines, `parkPendingMidiOff`, retired tails and their drained flags, `setSlotIndex` on moved engines, `samples.swapPaths` / `clearPath`, `resetPlayState`, previous-state flags and euclid overrides reset); `swapLayers` / `resetLayer` / `canReorderLayers` overrides go; `PluginEditor` `onAddLayer` / `onRhythmDeleted` and the `RhythmContent` rename / reset calls move onto `LayerEditController` (hooks: unbind the modulator panel and `RhythmContent` pointers, then show the layer). `RhythmManager` is deleted; `rename` becomes a short processor method taking `layersLock`. | `mu-clid/Source/Plugin/PluginProcessor.{h,cpp}`, `PluginProcessor_APVTS.cpp`, `PluginProcessor_Modulation.cpp`, `PluginEditor.cpp`, `mu-clid/Source/Sequencer/RhythmManager.{h,cpp}` (deleted), `mu-clid/Source/Persistence/PresetIO.cpp`, `PresetIO_HostState.cpp`, `mu-clid/Source/Plugin/HotSwapStager.cpp` (`installRhythm` target, `checkBoundaries` count), `mu-clid/Source/Audio/SampleLibrary.{h,cpp}`, `mu-clid/Source/UI/RhythmContent.cpp`, `RhythmMiniVisual.cpp`, `mu-clid/CMakeLists.txt`, tests `ProcessorSeamTests.cpp`, `SwapCommitTimingTests.cpp`, a new `ClidLayerStructureTests.cpp` | `ClidLayerStructureTests` (fill to 8; remove the middle; swap ends; reset; assert for each: the rhythm's data, parameters, colour, mixer strip, sidechain source, sample path, engine pointer identity, retired tail and pending-swap cancel all follow the right slot; a removed slot's owed MIDI note-off still sounds; the last layer cannot be removed); the two-thread stress test from the shared `ProcessorSeamHelpers.h` (message thread edits while a loop calls `processBlock`: no crash, no allocation); every `mu-clid-tests` suite; `CLID_roundtrip` byte-identical; `swap`, `slot swap`, retire-tail renders byte-identical; a new scripted render with add, reorder and delete mid-play compared against the same script at the step 1 commit (audio equal except the intended difference, nothing at all if none); `mu-clid-midi-tests` |
| 4 | M | Tidy and docs. Delete `numActiveRhythms`, `rhythmStore`, `kAutomatedRhythms` if unused, the dead `RhythmManager` references in comments; `GeneratorAliases.h` keeps its aliases; update this doc, design-naming 4 / 5, `mu-clid/CLAUDE.md`. | the same files | `check-core-boundary`, `check-comment-refs`, full Debug build of all products |
| 5 | **Decided against, 2026-10-13** | Was: `HotSwapStager` onto `layerSwapper` (`PendingRhythm` as the payload, mu-Clid's swap-mode boundary rule as the callable). Not done and not planned. Reasoning: the shared behaviour is already shared, because `HotSwapStager` is built on `mu_hotswap::Stager` (the handshake and the loop-wrap predicates), so a move removes no second copy of anything. mu-Clid's payload is a prebuilt `Rhythm` + `VoiceEngine` (sample I/O done at stage time) plus a full-preset slot and engine retire tails, while `layerSwapper` is the `ValueTree` / APVTS-apply driver the other three products use; forcing the engine install through it would only move code and risk the swap timing that `SwapCommitTimingTests` and the hot-swap listening renders pin. Revisit only if a second product needs engine-carrying payloads (then generalise `LayerSwapper` first). | none | none |

**What stays per product (mu-Clid):** `VoiceEngine` / `MidiOutputEngine` arrays, retired tails and drain flags, `rhythmPlayState`,
`modSnapshot`, `lastEuclidOverrides` / `prevEuclidOverrides` / `prevMatrixHadAssignments`, the pending-parameter drain
(`pendingDirty`, `rawParams`, `suffixIndex`: keyed by APVTS slot, not by layer), `orphanOffs`, `SampleLibrary` paths, `HotSwapStager`,
`PresetIO`, the MIDI output modes, the euclid mod-target table. What goes: `RhythmManager`, `numActiveRhythms`, `rhythmsLock`, the `swapLayers` /
`resetLayer` / `canReorderLayers` overrides, `rhythmStore` (step 1 scaffold).

**Risks, and the answer to each.**
- *Address stability changes.* A `Rhythm` used to sit at a fixed vector index with data copied through it; now the object moves with the pointer, and a removed
  layer is destroyed after the unlock. Anything holding a `Rhythm*` / `ControlSequence*` across an edit dangles. Audit: `ModulatorPanel` and its
  bound `ControlSequence` pointers (the `LayerEditController` unbind hook), `RhythmContent`, `MidiOutSection`, the voice subsections, `EuclideanPanel::currentRhythm`
  (fetches per call, safe), `RhythmMiniVisual` (reads by index per tick, safe). `adoptDefaults` exchanges elements one by one for the same reason.
- *Allocation under the lock.* The locked section may move pointers and engines but never construct or free one: parked engines (removed slot, dropped tails) are released in
  `syncSlotParams` after the unlock; `updatePattern` also runs there, not in `layersMoved`.
- *Lock miss.* On a failed try-lock the block renders silence and must still advance the transport and boundary detection (hot-swap principle 4). Today mu-Clid's
  transport derivation sits before the lock; keep that order and confirm the sequencer's wrap detector does not need the block.
- *CLAP does not honour `suspendProcessing`.* `layersLock` plus `layerEditInFlight` is the barrier; the audio path must test both.
- *Colour choice differs slightly.* The base picks the first unused palette entry; mu-Clid's `onAddLayer` probed from `count % palette`. The family rule wins (contract item 6.6),
  a visible difference in some add / delete sequences: `ux-controller` to confirm.
- *The MIDI product breaks if step 1 or 2 changes `Rhythm` semantics.* It shares `mu_core::Rhythm` and `RhythmSequencer`; `mu-clid-midi-tests` runs after every step, and the product has no layer edits to exercise.
- *Not in the default build.* `mu-clid-tests` and `mu-clid-midi-tests` are built on demand: build them by target name, or a green full build proves nothing about them.

**Unattended or supervised.** Not one pass. Steps 0 to 2 are safe unattended: they are mechanical, additive and proven by existing byte-identical renders and
unit tests, and nothing in mu-Clid's audio thread changes behaviour. Step 3 changes the audio thread's lock, the order of an edit that moves audio state and the
retire-tail; it can be attempted unattended only as a local, unpushed commit with `Refs` (never `Closes`), and only if every proof listed passes, otherwise reverted to
the step 2 commit. Steps 4 and 5 follow a supervised look. **`dsp-controller` reviews after step 3 and before anything is pushed or closed**, and must be given: the
`layersLock` try-lock and `layerEditInFlight` skip in `processBlock`; `RhythmSequencer` slot-state moves in `layersReordered` (phase, `pendingKind`, `patternLock`
order against `layersLock`); engine, MIDI engine, retired-tail and drain-flag shifts, and the owed note-off path (`parkPendingMidiOff`, `orphanOffs`); that nothing is
constructed or destroyed under the lock; the `Rhythm*` rebinding in `bindRhythm` is done under the lock and the audio thread reads the slot table only inside it; the
transport and wrap detection on a lock miss; `installRhythm` landing in `*layers[r]` (the swap must not allocate).

**Design-future check.** Easier: mu-Clid on the same container makes every product's edits one routine, which the combined instance needs; the sequencer no
longer owning its layers is the shape a registry wants (a layer type supplies data, the host owns it). Not closed off: the engine stays where stage 8 can still decide
whether it moves into the layer. Harder: nothing. The new hooks are generic (`layersReordered`, `syncSlotParams`) and have no mu-Clid names.

### Stage 8: what the owner decides (#1319; 2026-10-12, Architecture Steward)

The row waited for the saved-data renames (#1263); those are done (closed at build 1322), so that dependency is gone. What remains is not a technical question. The
registry (`LayerTypeInfo`: id, name, prefix, preset extension, mod-target table, `create`) and `Layer::render` (the layer draws itself into the mixer, so a session can mix
layer types) have exactly one customer: the combined instance of [design-future.md](design-future.md) ("One framework, one instance"), which is still unscheduled and
whose own open questions are the owner's. The docs already settle the technical order (registry only when two layer types can share an instance; the registry
constructs and describes and never dispatches; dispatch stays virtual). They do not settle whether to build it.

Options:
- **A. Park until the combined instance is scheduled (recommended).** Move the row to On Hold with a one-line note. Costs nothing, loses nothing: stage 7 deliberately keeps
  engines processor-side so `Layer::render` can still take them either way. A `typeId` and `LayerTypeInfo` built now would have one type per product and no way to be tested
  against a second, so the shape would be guessed.
- **B. Build only the describing half now:** `Layer::typeId()` virtual plus a registry that constructs and lists types, with the saved `Slot` node gaining a `type` attribute
  (absent = the product's own). Small and additive, no audio-thread change. Trade-off: dead code until a second type exists, and the risk of choosing the wrong fields.
- **C. Full stage 8:** B plus `Layer::render`, moving each product's engine into its layers and replacing the mixer's `RenderChannelFn`. Large, touches the audio thread of every
  product, reverses the processor-side engine ruling above, and cannot be finished honestly without the owner's answers on licensing and product shape.

Question for Andy (one paragraph, for the morning): do you want the combined instance scheduled, and if so is it a fifth product or a mode of an existing one, and how do
freeware layers (mu-Toni, mu-On while pre-release) sit inside a licensed instance? Until you answer, #1319 should stay parked (option A); the answer decides where the registry
lives (which product's processor hosts it), whether the plugin ID and licence of the combined instance are new, and so whether option B or C is next. No work on it is useful
before that, and nothing that is done on stages 6 and 7 makes the answer harder.

### Shared layer sidebar and layer panel (ruled 2026-10-12, #1505 / #1506)

**Files and names.** `mu-core/UI/LayerSidebar.{h,cpp}` is `ChannelSidebar` renamed (git mv; no alias, no
second name; the four editors change in the same step). `SidebarItem` keeps its name and file (neutral, role
suffix correct). `mu-core/UI/LayerPanel.{h,cpp}` is new. `VoiceBand` / `VoiceBandSection` stay in
`mu-core/UI/Voice/`: they are a content section *inside* the panel's content band (used by mu-Clid's
`VoiceSection` and mu-Toni), not something `LayerPanel` replaces. The header bar is `LayerHeaderBar` (renamed from `ChannelHeaderBar` 2026-10-12).
No product subclass of either class. Product pieces keep the layer-type noun, in the product's `UI/`:
`RhythmMiniVisual` (mu-Clid, exists), `PatternMiniVisual` (mu-Tant; the spectrum glyph moves out of
`VoiceSidebar.cpp`), `ArpMiniVisual` (mu-Toni, replaces the placeholder `LayerGlyph`), `TrackMiniVisual`
(mu-On). The content band's component is named `<Type>Content` (`RhythmContent`, `PatternContent`,
`ArpContent`, `TrackContent`) in the product's `UI/`.

**`LayerSidebar` contract.** Constructor `(ProcessorBase&, addLabel)`. Everything it can read comes from
`ProcessorBase`: count, name, colour index (`getNumLayers` / `getLayerName` / `getLayerColourIndex`;
renamed from `getNumChannels` / `getChannelName` / `getChannelColourIndex` in the accessor sweep), pending badge
`proc.hasPendingSwap(i)`, cancel `proc.cancelPendingSwap(i)`, reorder `proc.swapLayers(a, b)`. The only
product hook is `createMiniVisual(int index, std::function<void()> pulse)` (the visual calls `pulse` on its
hit edge, so a lambda never captures the sidebar). User-action callbacks stay the editor's:
`onLayerSelected`, `onAddLayer`, `onLayersReordered`. Drag-reorder is enabled iff `proc.canReorderLayers()`.
`onSwapChannels`, `isPendingSwap` and `onCancelPendingSwap` went. `onAnimationTick` (optional, 5 Hz) is
**kept**: mu-On pulses its sidebar items from it (corrected 2026-10-12; the earlier "unused" was wrong).
Two small `ProcessorBase` changes make this dependency-free: (1) `virtual bool canReorderLayers() const
{ return canEditLayers(); }` and `swapLayers` becomes virtual (same pattern, and same reason, as the
virtual `resetLayer`: mu-Clid has not moved its rhythms into the container); mu-Clid overrides both
(`rhythms.swap`) until its layers move in (layer stage 7), then deletes the overrides. (2) mu-Clid overrides
`cancelPendingSwap(int)` to forward to `hotSwapStager.cancelStagedSwap`, and its public `cancelStagedSwap`
wrapper goes. mu-Toni and mu-On (fixed count) inherit `false` and get no drag, as today. Visible behaviour of
the sidebar does not change in any product; the Add button's visibility rule is not touched here.

**`LayerPanel` contract** (the per-layer stack of design-ui-family §5: header strip 32 / content 464 /
modulator 318). It owns the preset strip (`LayerHeaderBar` in its metal strip with end screws), the shared
`ModulatorPanel`, the band layout, the 30 Hz playhead and pending-swap badge refresh, and the header wiring
that already exists generically on the processor: name and colour from `Layer` (`name`, `colourIndex`),
preset list/load/save through `slotPresetFiles` / `loadLayerPreset` / `saveLayerPreset` / `getLayerPresetDir`
/ `getLayerPresetExtension`, reset through `resetLayer`. **As built (2026-10-12):** delete (`removeLayer`) and
rename are wired by the product on `getHeader().onDelete` / `onNameChanged`, not by the panel, and there is no
`onLayerRemoved` / `onStatusUpdate` on the panel. Extras that exist: `setBeatSource`, `setDefaultPresetName`,
`setOwnsPresetList` (mu-Clid keeps its categorised list and save card), `addOverlay`, `setFixedLayers`,
`refreshPresetList`, `refreshHeader`. Product input is: `setContent(juce::Component*)` (the
464 band, as one component, may be called again so a combined instance can swap content by layer type),
`setDestProvider(const ModDestProvider*)` (the product's modulation destinations), and callbacks
`onLayerChanged(int)` (rebind the content's APVTS attachments), `onLayerRemoved(int)`,
`onStatusUpdate(name, value, colour)`. `setLayer(int)` commits any pending rename, refreshes the header,
points the modulator panel at `proc.getLayer(i)` and then fires `onLayerChanged`. Because it only calls the
`ProcessorBase` API, it needs no product header; `mu-core/UI` stays inside the boundary check. Heights come
from three new `MuLookAndFeel` tokens (`kLayerHeaderStripH` 32, `kLayerModulatorH` 318,
`kLayerContentH = kChannelPanelH - kLayerHeaderStripH - kLayerModulatorH`, with a `static_assert` that the
sum is `kChannelPanelH`); `kSidebarW` is the width (fix its stale "mirrored from RhythmSidebar" comment).
mu-Clid and mu-Tant carry product-only chrome in the old header area (mu-Clid's sample bar and output
selector, mu-Tant's Root / Scale row): both go *inside* the content component, per the content-band rule.

**Migration order (smallest blast radius first; each numbered step builds on its own).**
1. Tokens in `MuLookAndFeel` plus the two `ProcessorBase` changes above, with mu-Clid's overrides. No UI
   change. Unit test: `swapLayers` virtual default still gated by `canEditLayers`.
2. Sidebar: `git mv` `ChannelSidebar` to `LayerSidebar`, new hook signature, fix the four `PluginEditor`s and
   `mu-core`, `mu-clid`, `mu-tant` CMake lists (full Debug build, `mu-core` touched). Then drop the subclasses:
   mu-Toni (placeholder visual; `ArpMiniVisual`), mu-On, mu-Tant (delete `VoiceSidebar.*`, new
   `PatternMiniVisual`), mu-Clid last (delete `RhythmSidebar.*`; the editor uses `onLayerSelected` etc. in
   place of the `onRhythm*` aliases). The 30 Hz glyph timers must keep their `stopTimer()` in the destructor.
   Verify drag-reorder and the staged-swap badge in mu-Clid and mu-Tant by hand.
3. `LayerPanel` built and adopted in mu-Toni first (smallest `EnginePanel`: it already has header, modulator
   and `VoiceBand`), then mu-Tant (`VoicePanel` becomes `PatternContent` inside `LayerPanel`, which also
   delivers the `EnginePanel` family name through `LayerPanel` plus a content class, so the separate rename
   is not done), then mu-Clid (`RhythmPanel` keeps its drag-and-drop target and circle refresh as the content
   component; `VoiceSection` stays as the content's voice band). UI only: no `dsp-controller` pass or listening
   tests; run the unit tests and get the `ux-controller` sign-off on the three bands.
4. mu-On: sidebar only now. Its per-lane `GroovePanel` shows all lanes at once and is rewritten by the
   per-layer engine selector work (#1241); it adopts `LayerPanel` there, not before.

**Design-future check.** Easier: this is the one-framework direction (per-product sidebar and panel copies
become one class; product input is a mini-visual factory, a content component and a destination provider,
all keyed by layer index, so a combined instance can answer them per layer type without a change to either
class). Nothing here closes off Link, the controller work (`mu-control` reads the same `ProcessorBase`
layer API) or the standard UI sizing (the three tokens are that standard). Risk to name: step 2 touches
`mu-core` and all four products at once, so it is one mechanical commit with no behaviour change; do not mix
the accessor rename (`getNumChannels` family to `getNumLayers`, done in its own commit 0338a05) into it.

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

## Shell chrome and the app seam — family standard (ruled 2026-10-12)

**Problem.** The shared chrome (`TransportBar`, `StandardSettingsOverlay`, the overlay and layout logic in `EditorShellBase`, the
preset flow) took `ProcessorBase&`, so mu-link, which is an app with no `AudioProcessor`, could not use it. mu-link's old
"documented deviation" (a bespoke window) is **withdrawn**: mu-link uses the same chrome as the products. The rule is the one
`ControlSink` already follows: the shared UI depends on a small interface, and the app implements it.

**Three pieces, smallest first.**

| Piece | Where | What it is |
|---|---|---|
| `mu_core::ShellBackend` | `mu-core/Plugin/ShellBackend.h` (header-only; namespace `mu_core`) | An abstract class: **everything the chrome reads from or tells the app**, taken verbatim from the "Shell-facing API" block of `ProcessorBase`. Groups: **transport** (`isStandaloneApp`, `isInternalPlaying`, `toggleInternalPlay`, `getInternalBpm` / `setInternalBpm`, `getInternalBeatPos`, `getTimeSignature` / `setTimeSignature`, `setNudgeDirection`); **clock sync** (`getMidiSyncEnabled` / `setMidiSyncEnabled`, `getMidiSyncMessages` / `setMidiSyncMessages`, `getMidiClockBpm`, `isMidiClockPlaying`, `getMidiClockState`, `getSyncOffsetMs` / `setSyncOffsetMs`, `kSyncOffsetMinMs` / `MaxMs`); **host reads** (`isHostPlaying`, `getHostPpqPosition`, `getHostTimeSignature`, `getHostBarStartPpq`), defaulted to "no host"; **display** (`getUiScale` / `setUiScale`, `kUiScaleMedium` / `Large`); **presets** (`getPresetsDir`, `getFullPresetExtension`, `hasPendingFullPreset` defaulted false, `loadPreset`, `savePreset`, `loadCategoryList`, `ensureCategoryInList`, `getDefaultPresetFile`); **master level** (`hasMasterLevel` defaulted false, `getMasterLevel` / `setMasterLevel`); `getContentDir`; and the four callbacks the shell sets (`onLoadError`, `onUiScaleChanged`, `onSaveAndQuit`, `onPresetSwapCommitted`) as public `std::function` members. |
| `mu_ui::ShellChrome` | `mu-core/UI/ShellChrome.{h,cpp}` | The processor-free half of `EditorShellBase`, as a **helper object, not a base class**: constructed with the host `juce::Component&` and a `ShellBackend&`, it owns and parents to the host the `MuLookAndFeel`, `TransportBar`, `StatusBar`, `AboutOverlay`, `SaveDialog`, `PresetBrowser`, the mixer / settings overlay slots and the upgrade banner; it holds the overlay state machine (`hideAllOverlays`, `fadeSwitch`, `show*`), the layout (transport / optional sidebar / main / status), `setMetalStyle`, `setScrews`, `forwardKnobStatus`, the version check, the play / stop key, and the preset flow (load, save with overwrite confirm, new with discard confirm, dirty flag). |
| `EditorShellBase` | unchanged name and public / protected API | `juce::AudioProcessorEditor` + a `ShellChrome` (passing itself and the `ProcessorBase`) + what genuinely needs a processor: the APVTS dirty listener (calls the chrome's `markDirty`), `ActivationOverlay`, the MIDI program-change overlays, the support prompt, licensing affordances and the `processorRef` cleanup in the destructor. |

**Why a helper and not a sibling shell class.** `AudioProcessorEditor` is itself a `Component`, so a processor-free
`ShellBase : Component` cannot be inserted under it, and a template over the base class would turn the whole shell header-only
for no gain. A helper parented to the host keeps the component tree exactly as today (children directly under the editor, the
bar at x = 0, `OverlayHost` still finds the editor), so the four plugins stay visually and behaviourally unchanged. mu-link's
content component holds the same helper over its own `ShellBackend`.

**`ProcessorBase` implements `ShellBackend`** (`class ProcessorBase : public ProcessorCore, public mu_core::ShellBackend`), like
`ControlSink`. The interface uses `ProcessorBase`'s existing signatures, so the existing members become overrides (add
`override`), the four host reads forward to `ProcessorCore`, `isStandaloneApp()` is `wrapperType == wrapperType_Standalone`, and
the master level reads and writes `mstr_lvl` through the APVTS parameter (not a `SliderAttachment`, as today). `kUiScale*` and
the four callbacks move to `ShellBackend`; the names stay, so `ProcessorBase::kUiScaleLarge` and `proc.onLoadError` still
compile. Every current call site `TransportBar(proc)`, `StandardSettingsOverlay(proc)`, `EditorShellBase(proc)` keeps compiling
(`ProcessorBase&` converts to `ShellBackend&`), as do the `TestProcessorBase` tests. **Do not** add a convenience constructor that
takes `ProcessorBase&` to a shared-chrome class: it brings the `ProcessorBase.h` include back and the seam is lost.

**What stays plugin-only** (takes `ProcessorBase&` on purpose): `ActivationOverlay`, `MidiPresetsOverlay`,
`MidiFullPresetsOverlay`, `MixerOverlay`, `ChannelSidebar`, `LayerPresetHeader`, `ParamKnobGrid`, the APVTS binding half of `MixerChannel`
(`MixerChannel_Bindings.cpp`), `MasterInsertBinding` (the master-insert binder; `MixerChannel_Insert.cpp` was deleted
2026-10-12), `InsertSubsection`. **Rule:** a file in the
shared chrome list below never includes `Plugin/ProcessorBase.h`, `Plugin/ProcessorCore.h` or `UI/EditorShellBase.h`
(checked by `check-core-boundary.ps1`).

**The chrome source list: one definition.** `mu-core/UIChromeSources.cmake` sets `MU_CORE_CHROME_SOURCES` (absolute paths to
the `.cpp` files that need no processor: `MuLookAndFeel`, `MuTheme`, the components, `TransportBar`, `StandardSettingsOverlay`,
`SettingsOverlayBase`, `ShellChrome`, `AboutOverlay`, `SaveDialog`, `PresetBrowser`, `ModalDialog`, `MixerChannel.cpp`,
`InsertProcessor`, and so on). `mu-core/CMakeLists.txt` includes it into the INTERFACE source list (the plugins build exactly as
today) and `mu-link` and `mu-core-tests` add the same variable to their own targets. This keeps "mu-link does not link the
INTERFACE library" true without a hand-kept second list that drifts. mu-link adds only what is genuinely its own.

**mu-link's adapter and window.** `mu_link::LinkShellBackend` (`mu-link/Source/UI/LinkShellBackend.h`) implements `ShellBackend`
over the `AudioServer`: `isStandaloneApp()` is **true** (so the bar uses the internal transport and shows the BPM field and
Play), play and BPM drive the master `TransportClock`, the beat is the clock's beat, the clock source is Internal / MIDI In,
`getMidiSyncMessages()` is "Clock + Transport" while MIDI In is chosen, the sync offset goes to the server, the meter is a
constant 4/4, presets are `Documents/TDP/muLink/Presets` with extension `muLink`, the display scale is kept in mu-link's own
settings file. **Host transport is simply not there:** the host reads keep their "no host" defaults and the bar never calls
them because `isStandaloneApp()` is true. mu-link's standalone window is therefore never "idle"; it is the master. The
`MuLinkComponent` holds a `ShellChrome` over the adapter, the Mixer and Scenes panels, and nothing else of its own chrome; the
`DocumentWindow` stays a native-title-bar, non-resizable window at the family size.

**`MidiClockSync::ClockState` gains `Drifting`** (the shared lamp draws it as in [mu-link design-ui §4](mu-link/design-ui.md));
`MidiClockSync` itself never reports it, only mu-link's adapter does.

**Staged migration (each stage ends with a Debug build of all products and the unit tests; the four plugins look and behave
identically after every stage):**

| Stage | Change | Plugins visually unchanged because |
|---|---|---|
| S1 | `Plugin/ShellBackend.h` (transport, clock sync, host reads, display, `Drifting`); `ProcessorBase` implements it; `TransportBar` takes `ShellBackend&`; `UIChromeSources.cmake` created with the files already free of `ProcessorBase` | same values flow through the same methods |
| S2 | `ShellBackend` gains master level; `StandardSettingsOverlay` takes `ShellBackend&`; its `Options` gain row switches (below) with today's values as defaults | defaults reproduce today's page |
| S3 | `ShellChrome` part A: look and feel, bar, status bar, About, Save, browser, slots, overlay state machine, layout, banner, version check; `EditorShellBase` delegates | same children, same parent, same order |
| S4 | `ShellChrome` part B: preset flow (load / save / new, dirty) over `ShellBackend`, the play key; `ShellBackend` gains the preset operations | `EditorShellBase` keeps its `onPreset*` hooks and calls them at the same moments |
| S5 | `MixerChannel.cpp` free of `ProcessorBase` (client and master strips callback-driven; binding stays in the two `.cpp` files); chrome list completed; boundary check extended | binding code moves nowhere |
| S6 | mu-link: `LinkShellBackend`, its CMake switched to `MU_CORE_CHROME_SOURCES`, tests for the adapter | mu-link window unchanged (no UI change yet) |
| S7 | mu-link window rewritten on `ShellChrome` (the "mu-link UI 4" row) | n/a, this is the visible change, to [mu-link design-ui](mu-link/design-ui.md) |
| S8 | mu-link Settings on `StandardSettingsOverlay` (the "mu-link UI 5" row) | n/a |

`StandardSettingsOverlay::Options` (S2): `showTransport` (exists), `showMeter` (time-signature row; default true, standalone
only as today), `showClockSource` (the Source and Messages rows; default true), `syncOffsetAlways` (show Sync offset even when
Source is Internal; default false). The master-volume row appears when `hasMasterLevel()`. mu-link sets `showTransport = false`,
`showMeter = false`, `showClockSource = false`, `syncOffsetAlways = true`.

### Rulings on the S7 and S8 additions (2026-10-12)

- **`TransportBar::refreshTempo()`**: accepted as is. It is generic (standalone only, reads `ShellBackend::getInternalBpm`), and sits beside `refreshPresets()`.
- **Engraved groove tokens**: `MuTheme::Lighting::grooveShade` (0.45) and `grooveLight` (0.09) are the one definition of a groove. `MixerOverlay::paintMetal` still has its own two lambdas with `ringTrack` and `lampHot * 0.3`, which are numerically the same (0.45 and 0.09), and `mu-link`'s `MixerPanel` has a third copy. **Adopt now, not later**: the values are identical so nothing changes on screen and it removes a drifting hard-coded factor (`* 0.3`). Do it as one helper, not by pasting the tokens into three places: `MuLookAndFeel::drawGroove(Graphics&, Point<int> start, int length, bool vertical)` in `mu-core/UI/Components/MuLookAndFeel.{h,cpp}` (dark line on the start edge, light line one pixel right of or below it, both via `L.shadow(L.grooveShade)` and `L.highlight(L.grooveLight)`). `MixerOverlay` and `MixerPanel` call it; the `lampHot * 0.3` expression disappears. The main conversation (coder) does this; no `ux-controller` ruling is needed because the look is unchanged (tell it afterwards so the groove is listed in `design-ui-family` section 11).
- **`HoverStatus`**: generic (a mouse listener that reports a name and value to the status bar; nothing about mu-link), so it moves to `mu-core/UI/Components/HoverStatus.h`, namespace `mu_ui`, header-only; `mu-link` includes it from there. Decide the side before the second copy: any plugin panel with a segment control or number field and no status line will want it. Not on the chrome `.cpp` list (header-only).
- **`ClientColumns`**: stays in `mu-link` (`mu_link::ClientColumns`). It places eight *client* columns, a divider and the master column for two mu-link panels; it is mu-link's concept (the plugins' `MixerOverlay` lays out channels, returns and a master by its own constants). Generalise it to a neutral `ColumnLayout` in `mu-core` only when a second place needs the same calculation.
- **`mu-link-tests` build**: correct as done. `UIChromeSources.cmake` is the one source list (include moved above the tests so both targets use it); `JUCE_MODAL_LOOPS_PERMITTED=1` and `ScopedJuceInitialiser_GUI` match `mu-core-tests` and the other products' `TestMain`; the extra `juce_audio_utils`, `juce_gui_extra` and `juce_data_structures` links are what the window needs. Recompiling `MuLinkComponent.cpp` and the panels into the test target is the accepted cost of testing the window without the app target.
- **Peak accessors** (`ServerEngine` / `AudioServer` `clientPeakMeter`, `masterPeakMeter`): accepted. They are `noexcept`, allocate nothing, and exist because `MixerChannel::bindClientMeter` takes `std::atomic<float>&`. Follow-up done (2026-10-12): `clientPeakMeter` / `masterPeakMeter` return `const std::atomic<float>&` so the GUI cannot write the engine's meter, and an invalid slot now asserts instead of falling back to slot 0.
- **`HoverStatus`** moved to `mu-core/UI/Components/HoverStatus.h` (done 2026-10-12); the groove helper `MuLookAndFeel::drawGroove` is in and used by the mixers (done).

### One master insert panel for the family (ruled 2026-10-12; done 2026-10-12)

**Status: steps 1 to 4 are done** (commits c1f428e and 1a1126e). What exists now: `MasterInsertPanel` carries the Vocoder grey-out (`SlotConfig::disabledFromP1`, `insertSlotEnabled()` in `InsertSlotConfig.h`) and a GR-meter hook (`setGainReductionSource(slot, const std::atomic<float>*)` for Compressor / Limiter; the old panel did show one); `MasterInsertBinding.{h,cpp}` (plugin-only) is the binder; `MixerOverlay` owns panel and binder; `MixerChannel_Insert.cpp` and the built-in insert controls are deleted; `MixerChannel::hasInsert()` / `insertShown()` became `isMaster()` and `setShowInsertPanel` was removed. Step 5 (Andy's on-screen check of one plugin's master column and a Vocoder insert) remains. The text below is the original ruling, kept for the reasoning.

**Direction confirmed: one copy.** `mu_ui::MasterInsertPanel` is the only master insert layout in the family. The plugins' built-in panel in `MixerChannel` (the `ins*` knobs, `addInsertControls`, `layoutInsertPanel`, `configureInsertAlgorithm`, the `insertSnapshots*` arrays, and `MixerChannel_Insert.cpp`) is deleted once the plugins use the shared one. The panel stays callback-only; the plugin half becomes a small binder in the plugin-only files (it takes `ProcessorBase&`, so it never goes on the chrome list).

**Order (each step a Debug build of all products plus the mu-core tests; the plugins' master insert looks the same after each):**

1. Give `MasterInsertPanel` what the old panel has and it lacks: the Vocoder / VocoderSt grey-out (P2 to P4 disabled when P1, the Wave, is 2 or more) done inside the panel from the algorithm table so every user of it gets it, and a GR-meter hook only if the old panel actually shows one (check `MixerChannel_Insert.cpp` and `MixerChannel_Bindings.cpp` first; if it does not, say so and add none). Extend `MasterColumnFitTests` for the grey-out.
2. Add the binder in the plugin-only half (suggested `MasterInsertBinding`, `mu-core/UI/MasterInsertBinding.{h,cpp}`, takes `ProcessorBase&` and a `MasterInsertPanel&`; not on the chrome list): `onAlgorithm` / `onParam` write the `mst_ins*` and `mst_ins2*` APVTS parameters with `setValueNotifyingHost` (the algorithm change writes the four parameters the panel hands back, as today); a `refresh()` reads `mixerEngine.getMasterInsertParams(slot)` and calls `setAlgorithm` then `setParam` for each, called from `MixerOverlay`'s deferred apvts-dirty flush (30 Hz timer), never from the audio thread.
3. `MixerChannel` (Master, plugin mode) hosts the panel via `setShowInsertPanel(false)` plus the panel placed by `MixerOverlay` as `mu-link` does (strip then panel, column 80 + 136); `MixerOverlay` owns the panel and binder.
4. Delete the built-in copy: `MixerChannel_Insert.cpp`, `addInsertControls`, `layoutInsertPanel`, `configureInsertAlgorithm`, `insParam*`, `insCharBox*`, `insertSnapshots*`, `masterInsertProc`, and `kInsertPanelW` in `MixerChannel.h` (use `MuLookAndFeel::kMixerInsertPanelW`). Remove the file from the CMake lists.
5. Not done until Andy has looked at one plugin's mixer on screen (the master column and a Vocoder insert), since the old child bounds were never captured.

The same turn that does step 1 to 4 also takes the groove helper above.

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
`juce::AbstractFifo` (exactly `pcFifo`'s pattern), then `requestDeferredWork()`. The message-thread drain
(`runDeferredWork`, beside `drainPendingMidiProgramChanges`) resolves `apvts.getParameter(paramId)` and performs.
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

## Performance mode and live state — family standard (ruled 2026-10-12, revised 2026-10-13 for the owner's decisions; backlog #1279)

Performance mode is the gig view: the normal main preset screen for the preset now playing, **locked (read-only)**, with an
indication of the mode, driven only by a control surface. The screen is a display derived from the live state; it never
acts. This section is structure only; the look is the `ux-controller`'s. The file is in
[design-launchpad.md §3.2c](design-launchpad.md#32c-the-performance-file-architecture-ruling-2026-10-12-backlog-1279).

**0. Scope (owner, 2026-10-13): standalone only for now, plugins not closed off.** `ShellBackend` gains a defaulted
`supportsPerformanceMode()` (false); `ProcessorBase` returns `isStandaloneApp()`. That one app-level switch hides the mode toggle and
makes every performance action return false in a plugin, so VST3 and CLAP behave exactly as today. The state and actions still live in
the processor and in `mu-core`, so turning plugins on later is changing that return value and nothing else. Hardware pads in a plugin
with no editor open: **no** (confirmed). The `mu-control` controller manager is created by the standalone shell and takes only a
`ControlSink&` and a `LiveView&` (never `ProcessorBase`, an editor or a window), so it can move to a hub later (item 7).

**1. Where the state lives: `mu_core::LiveState`, owned by the instance, not by the editor or the shell.**
`mu-core/Control/LiveState.{h,cpp}`, namespace `mu_core`; plain class, no `ProcessorBase` dependency, **message-thread only**
(no atomics, no locks; the audio thread never touches it). It holds: `performanceMode`, the loaded performance (`mu_pp::PerformanceFile`
and its path), `currentItem`, `queuedItem` (or none), `selectedLayer`, per layer the `playingClip` and `queuedClip` (#1277), and a `revision` counter
bumped on every change. It is wired by callbacks the owner supplies: `canLoad(item)` (item's `product` is this product's state
identifier and the file exists), `stageItem(item)` (the existing `stageFullPreset`), `cancelItem()` (cancels the staged full preset), and
for clips `stageClip(layer, index)` (the per-layer path of the Stager). `ProcessorBase` owns one and implements `ControlSink::perform`
over it; a future hub or one-instance host owns its own. Why not the others: the editor is wrong because a pad press needs the state with
no window and a plugin editor closes; `ShellBackend` is an interface (what the chrome reads), not storage; a `PerformanceController` would be a
second owner of "what is selected / queued" beside `LiveState`, the screen-vs-surface disagreement to avoid. **Persistence:** the performance
path, `currentItem` and `performanceMode` are root properties of the product state in the host session (§3.2c), mode off on a fresh instance,
restored on reload so a standalone restart returns to the gig. `selectedLayer`, queued and playing clips are not saved. Nothing goes in the
preset or the APVTS (not a musical parameter, must not be automatable). **Wiring (ruled 2026-10-13):** `ProcessorBase` gains two non-virtual
helpers, `saveLiveState(ValueTree& root) const` and `restoreLiveState(const ValueTree& root)`, thin calls to `LiveState::saveTo / restoreFrom`;
each product calls them on the root it already assembles in `getStateInformation` / `setStateInformation` (mu-Clid: the session `MuClidPreset`
root built in `PresetIO::getStateInformation`; mu-Tant, mu-Toni, mu-On: the `captureFullPreset()` / state tree root, same properties). Restore
runs after the preset restore, because `restoreFrom` sets `currentItem` without staging anything. mu-Clid standalone returns early to the
`_default.muClid` auto-load before it reads the session, so the live state must be restored from the parsed root on that path too (parse first,
restore live state, then branch); with the mode restored on, the session wins over `_default` (otherwise `currentItem` names a preset that is
not playing). mu-Clid MIDI has no standalone and no audio: it does nothing.

**2. How the shell reads it without a product dependency.** `mu-core/Control/LiveView.h`: read-only getters of the above plus
`isItemPlayable(item)` (this product's item and its file exists, so the indicator tells an available lamp from a fault lamp without a
product symbol) and `addListener / removeListener` with `Listener::liveStateChanged()` (message thread). The listener calls are `const`: listening is
not a change of the state, so a holder of the read-only view may register. `ShellBackend` gains three defaulted accessors, as built:
`const LiveView* getLiveView() const` and `ControlSink* getControlSink()` (both nullptr by default; pointers, not references, so "none" is
expressible and mu-link needs no stub) and `bool supportsPerformanceMode() const` (false); `ProcessorBase` overrides them to return `&liveState`,
`this` and `isStandaloneApp()`. `ShellChrome` and `TransportBar` show the mode only when `supportsPerformanceMode()` and both pointers are set
(mu-link: nothing changes). No product symbol is named; `PerformanceFile.cpp` (juce_core only, which `LiveView::getPerformance` needs) is compiled
with the chrome list in `UIChromeSources.cmake`, so mu-link links the chrome without `LiveState`.

**3. The lock lives in `ShellChrome`, once.** While `performanceMode` is true the chrome puts an input shield over the content area
(sidebar, main panel, overlays, transport bar): it swallows mouse and key events, so **no product panel needs any code** to be read-only.
The mode toggle (the way out) sits above the shield, and the host window's own close button is unaffected. The shield is a shared chrome
component; its look and the toggle's place are for the `ux-controller`. Consequence: **there are no on-screen or keyboard step actions**.
Surface and MIDI map are the only inputs while locked; the keyboard table keeps only the mode toggle.

**4. The screen follows the surface.** In mode, the editor sets its layer selection from `LiveView.selectedLayer` (one way; outside the
mode the sidebar selection stays local editor state, as today). Entering the mode is `perform({PerformanceMode, layer = the editor's
current selection})`, which seeds `selectedLayer`. A pad press selects that layer **at once** (before the commit), so the screen shows
the playing layer preset plus the queued one; the content changes at the commit. A full-preset switch shows the queued item on
the lamps and the strip, and changes the display at the commit (the editor refresh is the existing `onPresetSwapCommitted`).

**5. Actions (stable string names; `ControlAction` gains `int index`; `layer` keeps meaning a layer).** `PerformanceMode` (**`index`**
-1 toggles (the default, so an unset action toggles), 0 off, 1 on; ruled 2026-10-13 after the step 2 review: the MIDI router overwrites `value`
with the incoming 0..1 on every dispatch, so a `value` encoding can never toggle from a mapped button, while `index` is left alone;
refuses when no performance is loaded or `supportsPerformanceMode()` is false), `PerformanceItem` (`index`
0..7), `PerformancePrev`, `PerformanceNext`, `SelectLayer` (absolute), `LaunchClip` (`layer`, `index`), and for the second page
`ClipNext`, `ClipPrev` (selected layer). `LayerNext / LayerPrev` are not added (the right column is absolute); they can be if the second page needs
them. Every action except `PerformanceMode` returns false when the mode is off. Rules inside `perform`:
- `PerformanceItem(n)`: item `n` not loadable returns false. `n` equals `currentItem` and an item is queued: **cancel the queued switch**
  (`cancelItem()`, clear `queuedItem`). `n` equals `currentItem` and nothing queued: true, no change. Otherwise `stageItem`, `queuedItem = n`;
  `currentItem` moves only at the commit (or at once when stopped).
- `PerformancePrev / Next` **wrap** (revised), skipping empty, missing and other-product items, stepping from the target (queued item if any, else
  current), so two presses before the commit land two items on. With no other loadable item they return false.
- `LaunchClip(layer, c)`: `selectedLayer = layer` now, `stageClip`, `queuedClip[layer] = c`; `playingClip` moves at `onLayerPresetCommitted`. A full-preset
  commit clears all clip state and takes the new preset's. Pressing the playing clip is mute / unmute, as in design-launchpad §3.2.
- **Timing.** `perform` does not quantise item switches or clip launches (MIDI control mapping §4 stands). The boundary is the Stager's,
  and it is set by the **preset's swap timing** (below), so a pad launch lands at the loop or bar the preset says, never twice-quantised.
- `PresetNext / PresetPrev` stay "next file in the browser list" and are not used in performance mode. Loading a performance file is a
  screen-only call on `LiveState` made before the mode is entered; switching between performances from a pad is out of scope.

**6. Per-preset swap timing, one definition in `mu-core` (owner, 2026-10-13).** Before this, mu-Clid (`SwapMode`: OnMasterLoop, OnRhythmLoop,
a processor atomic read by `HotSwapStager`) and mu-Tant (`SwapMode`: OnMasterLoop, OnVoiceLoop) each had a runtime-only "Timing" setting, with the
predicates in `mu_clid::hotswap` and mu-Tant's `swapLoopBeats`; mu-Toni and mu-On have the fixed bar rule. It is now a
family setting saved **with the preset**:
- **Type:** `mu_hotswap::SwapTiming { OnMasterLoop = 0, OnLayerLoop = 1, OnBar = 2 }` in `mu-core/Plugin/HotSwap.h`, beside `loopWrapped` / `boundaryReached`.
  "Layer" is the neutral word for rhythm / voice / pattern. The integers are saved data: append only.
- **Parameter:** one APVTS choice parameter in the shared master section of the layout, id `mstr_swap` (checked 2026-10-13: fits the
  design-naming "Mixer / FX ids `mstr_`" scheme, beside `mstr_lvl` / `mstr_pan`), so it is in the preset, the full-preset file and the session with no new persistence code. Defaults supplied
  by the product: mu-Clid and mu-Tant `OnMasterLoop` (today's value, so old presets and sessions that lack the parameter behave as now),
  mu-Toni and mu-On `OnBar`. The audio thread reads its raw value; nothing else changes on the audio path.
- **Predicates:** `usesMasterLoop(timing, hasMasterLoop)`, `layerBoundaryReached(...)` and `fullPresetBoundaryReached(...)` move to `mu_hotswap`
  with their behaviour unchanged, including the fallback when `OnMasterLoop` is chosen but no master loop is set (use the layer's own loop; the full
  preset uses layer 0's). `OnBar` uses `kBarBeats`. A layer's own loop length in beats stays supplied by the product (shared principle 2): for
  mu-Toni and mu-On "own loop" is the bar, so `OnLayerLoop` and `OnBar` agree there.
- **Migration:** (a) **built 2026-10-13, architecture-steward Pass:** `SwapTiming`, `swapTimingFromIndex`, the predicates and the parameter
  (`mstr_swap`, "Swap Timing", choices "On master loop" / "On layer loop" / "On bar"; `addGlobalFxParams(layout, swapTimingDefault)`, mu-Toni and
  mu-On pass 2) are in `mu-core`, covered by `mu-core/Tests/SwapTimingTests.cpp` (including `OnBar`). The product tests `HotSwapBoundaryTests` and
  `SwapCommitTimingTests` stay in mu-Clid until (b): they test the product predicates and `HotSwapStager`, which (b) rewrites, so moving them now
  would be done twice. In (b) the cases are ported, not dropped: boundary cases not already in `SwapTimingTests` go to `mu-core-tests`; the stager
  commit-timing cases stay with `HotSwapStager` in mu-Clid, retargeted to the shared type. `mstr_swap` is deliberately **not** in
  `isGlobalFxParamId` (the engine does not sync it; the audio thread reads the raw value). It must be added to mu-Clid's `kGlobalParamDefs`
  (mu-Tant has no such table: its full preset writes every non-layer APVTS parameter through `LayerLayout`, so `mstr_swap` is carried already) in (b),
  because that table decides what a full-preset file carries (`mstrLoop` is listed there the same way);
  (b) **built 2026-10-13 (commit 7727cdb), architecture-steward Pass:** mu-Clid and mu-Tant read the shared type and parameter; both product `SwapMode`
  enums, `swapModeAtomic`, `mu_clid::hotswap` and `HotSwapBoundary.h` are deleted (no forwarders); `mstr_swap` is in mu-Clid's `kGlobalParamDefs`; both
  Settings "Timing" dropdowns write the parameter; the old value was never persisted (runtime-only atomic), so there is nothing to seed from. mu-Clid has no
  bar-line rule, so `OnBar` reads as `OnLayerLoop` there (accepted; (c) and (d) must not offer or display `OnBar` for a product without the rule, see (d));
  (c) mu-Toni and mu-On pass `OnBar` default and gain the control;
  (d) the "Timing" dropdown leaves the two products' `SettingsOverlay` for a preset-level control in the shared `MasterLoopSection` (the `ux-controller`
  places it, as it is preset data now, not an app setting; the control offers only the values the product supports, so mu-Clid shows two and a stored `OnBar` is shown as the layer loop). Each stage is a Debug build of all products and the unit tests.
- **Standing check:** easier. One definition and one parameter replace two enums and two predicate sets, the other products gain the choice
  without a copy, and the pad launch needs nothing new.

**7. Future (recorded only, not scheduled).** A multi-app rig through mu-link, with one app "in focus" on the pad. A MIDI device is
typically opened by one program, so a hub (mu-link) likely owns the pad and forwards the same device-independent `ControlAction`s to the focused
instance, with new `FocusApp(index)`, `FocusNext`, `FocusPrev` actions. **Verify the Windows MIDI exclusivity claim before relying on it**
(classic drivers are generally single-client per port; Windows MIDI Services is said to allow several; unchecked). A mixed-engine rig needs a shared master
loop and clock (mu-link). Nothing here needs to change for it, provided the manager keeps depending only on `ControlSink` and `LiveView` and the actions stay serialisable by name.

**8. Order (each step buildable alone; Debug builds, tests in `mu-core-tests`).**

| Step | Work | Needs |
|---|---|---|
| 1 | `PerformanceFile` + tests (#1279) | **Done 2026-10-13** (`mu-core/Persistence/PerformanceFile`, saves via `replaceFileAtomically`, tests in `mu-core-tests`) |
| 1b | Swap timing in `mu-core` (item 6, stages a to d) | nothing from this feature; best before step 4 so launches use the final setting |
| 2 | `LiveState`, `LiveView`, the actions, `perform`, `supportsPerformanceMode`, persistence, tests with fake callbacks | step 1; `stageItem` is the existing `stageFullPreset`. This is the #1328 work for `SelectLayer`, the performance actions and `PresetNext/Prev`. **Not** #1276. **2a built 2026-10-13 (1b4bf08); 2b and the review fixes built 2026-10-13 (a0fd6ec):** `PerformanceMode` takes `index` (-1 toggle, 0 off, 1 on), `MidiControlMap` saves and loads `index` with a round-trip test, `ProcessorBase::saveLiveState / restoreLiveState` are called by all four session products (mu-Clid parses first and skips the `_default.muClid` auto-load when `LiveState::modeSaved`), and the queue identity is the stage serial plus `reconcile`. architecture-steward re-check 2026-10-13: **Pass** (placement, boundary script, persistence wiring as ruled). Step 3 may start |
| 3 | `ShellBackend` accessors, the input shield, the indication and mode toggle, editor following `selectedLayer` | step 2 and the `ux-controller` ruling. **Built 2026-10-13 (570ee8e); architecture-steward review 2026-10-13: Pass** on structure (accessors, `ShellChrome::InputShield` and entry order, `StepIndicator` in `UI/Components`, pure `PerformanceIndication` in `UI/` under `mu_ui::perf`, `LayerSidebar` wired once by `EditorShellBase::setMainArea`, no product symbol, no state in the editor). Toggle order right to left: Mixer, Perf, gear. The interim header-only `UI/PerformanceChooser.h` and the `FileChooser` in `EditorShellBase` are gone: the chooser is a `ShellChrome` page (`UI/PerformanceChooser` component over `UI/PerformanceChooserModel`, logic in `mu_ui::perf::ChooserModel`, no display), loading through the neutral `ShellBackend::getProductStateId()` / `loadPerformance()` hooks, picking presets through `PresetBrowser::beginPick` and naming through `SaveDialog::setNameAndDescriptionOnly` (architecture-steward review 2026-10-13: Pass, boundary checker clean). The "Queued" status says "(at the next swap point)" until `LiveView` exposes the preset's `SwapTiming`; the ux wording of "when" is a follow-up, not a structural one |
| 4 | `mu-control` surface model reads `LiveView`: top row = `PerformanceItem(n)`, right column = `SelectLayer`, grid = `LaunchClip` | #1276, step 2; the grid needs 5 |
| 5 | `LaunchClip`, `ClipNext/Prev`, the clip bank and per-layer clip fields in `LiveState` | #1277 (this part of #1328); the bank and launch path are steps C1 to C4 of [Clip bank](#clip-bank--family-standard-ruled-2026-10-13-backlog-1277), the `LiveState` part is its C5 |

With steps 1 to 3 the mode can be driven by the MIDI map before `mu-control` exists. #1278 (live preset switch) is the same staging path, so step 2
reuses it; its hardware checks (FX tails, largest-preset load time, neighbour pre-load, loading state) do not block steps 1 to 3 but must pass
before a gig-ready claim. Panic stays with #1276.

Do not: keep a performance flag, selected layer or queued item in the editor; give the surface its own copy of the state; add on-screen or
keyboard step actions while locked; make product panels lock themselves (the chrome shield does it); quantise item switches or clip launches
in `perform`; keep a second copy of the swap timing or its predicates in a product; let the audio thread read or write `LiveState`; put
`LiveState` in `mu-control`; let the controller manager name `ProcessorBase` or an editor; save the mode in the preset or APVTS.

## Clip bank — family standard (ruled 2026-10-13; backlog #1277)

Eight stored states per layer that a controller pad launches, as in Ableton Live and Bitwig. Owner decisions already made are in
[design-launchpad.md §3.2](design-launchpad.md#32-four-levels-of-what-is-playing-owner-2026-10-09) (what a pad does and how it lights) and are not
repeated. This section is the structure. Items marked **Andy** are file-format or behaviour commitments the architecture does not decide; each
carries a recommendation and the build assumes it until he answers.

**1. Names.** `Layer` settled in #1265 as the parent class; `Clip` is the launchable thing ([design-naming.md §1](design-naming.md#1-the-vocabulary), owner).
They do not clash: no type called `Clip` exists, `Layer` is the container, `Clip` is one stored state of it. "Slot" stays for FX / insert slots and
the stager's index. New names: `mu_core::ClipBank` (the store), `mu_limits::kClipsPerLayer = 8` (beside `kMaxLayers` in `MuLimits.h`), XML tags
`Clips` / `Clip`, actions `LaunchClip` / `ClipNext` / `ClipPrev` (already declared, `ControlAction.h`), and in the UI a `ClipRow` (for the
`ux-controller`). A product never adds `<Type>Clip`: a clip is the product's *layer preset* and has no type of its own.

**2. What a clip is.** One clip = **exactly what that product's layer preset carries**, no more: the layer's parameters, sequencer data, engine
selection and modulators; **not** the mixer strip, not the name or colour (the layer keeps its own on launch). It is stored as the tree the product's
`loadLayerPreset` consumes after parsing (the root tag is the product's own: `MuTantVoice`, `MuClidRhythm`, ...), so a clip and a `.muPattern` /
`.muRhythm` are the same shape and one code path reads both. mu-core treats the payload as opaque. An empty clip is an invalid `ValueTree`.

**3. Where the store lives: on `Layer`, as `SlotBound<mu_core::ClipBank> clips`.** `mu-core/Sequencer/ClipBank.h`, header-only, namespace
`mu_core`: `std::array<juce::ValueTree, kClipsPerLayer>` plus a per-clip name, with `set(i, tree, name)`, `get(i)`, `clear(i)`, `clearAll()`,
`occupancy()` (an 8-bit mask) and the persistence pair below. Why this and not the alternatives:
- **On the layer, not a processor-side array by slot.** Clips are layer data: when `removeLayer` / `swapLayers` move the `unique_ptr<Layer>` the bank
  must go with its layer, exactly as the layer's sequencer data does. A slot-keyed array would need its own move logic in every structural edit.
- **`SlotBound`, not a plain member.** mu-Clid's `Rhythm` is a copyable value and a staged layer preset reaches the slot by assigning a freshly prepared
  `Rhythm` (`installRhythm`); `adoptDefaults` also copies. A plain member would be wiped by every layer-preset load or reset. `SlotBound` makes copy and
  assignment do nothing, so loading a layer preset, launching a clip and `adoptDefaults` never touch the bank, and the bank still travels with the pointer
  move. (Same reason the insert uses it; the word "slot" in the name is the old layer-position meaning and is not renamed.)
- **`ClipBank` is a class, not a bare `std::array<ValueTree, 8>`:** `ValueTree` copies share their data, so an unguarded copy would alias two layers'
  clips. The class owns `createCopy()` on every store and read-out, so no caller can alias a stored tree.
- **Not on `ProcessorBase`/`LiveState`:** `LiveState` holds *what is playing and queued* (volatile, unsaved); the bank is *saved data*.
- **Message thread only.** The audio thread never reads it (no allocation, no lock, no atomics needed). Every access is on the message thread, which
  is also where layers are created and destroyed, so a `Layer*` taken inside one message-thread call cannot dangle.
- **Structural edits.** The bank follows its layer on swap and remove (pointer move). Because `adoptDefaults` does not touch a `SlotBound`, the base
  `addLayer`, `resetLayer` (see Andy 3) and the vacated top slot after `removeLayer` call `clearAll()` **explicitly, after `layersLock` is released**
  (freeing eight trees is not a microsecond job). Under `keepSpareLayers` the spare layer is re-used, so this is not optional.

**4. Actions: data on `Layer`, behaviour on `ProcessorBase`.** The backlog row asked for generic launch / mute / store actions on the parent `Layer`.
Ruled otherwise: `Layer` gains **no new virtuals** (a launch needs the swapper, the parameter layout and the control sink, none of which a `Layer`
can see). `ProcessorBase` gets, all message-thread, all non-virtual:
- `bool storeClip(int layer, int clip, const juce::String& name = {})`: capture the live layer into the bank (the same capture `saveLayerPreset` uses, below).
- `bool clearClip(int layer, int clip)`, `bool loadClipFromFile(int layer, int clip, const juce::File& layerPreset)` (a layer-preset file read into a clip, validated by the same tag check as a load).
- `bool launchClip(int layer, int clip)`: false when the layer or clip is out of range, the clip is empty, or **a full preset is staged** (`layerSwapper.hasFullPending()`; Andy 4).
  Otherwise it deep-copies the stored tree and hands it to the product's stage hook with `keepIdentity = true`.
  This is what `LiveState`'s `stageClip(layer, index)` callback calls.
- `uint8_t clipMask(int layer) const`.
Pad *mute on press of the playing clip* is not a bank concern: `perform` sets the layer's mixer mute parameter through the control router, like any mapped mute.

**5. Capture and apply through the Stager: one new pair of hooks, no second path.** Today each product's `saveLayerPreset` builds a tree and writes a
file; `loadLayerPreset` parses a file, migrates it, preloads assets and calls `layerSwapper.useSlot` (mu-Clid: `stageRhythmPreset` builds a prepared
`PendingRhythm` and stages it in its own stager until a later step unifies it). Split each at the tree:

| Hook (virtual on `ProcessorBase`, default empty/false) | Meaning | Today's equivalent per product |
|---|---|---|
| `juce::ValueTree captureLayerPresetTree(int layer)` | the tree `saveLayerPreset` would write, without the file | mu-Tant/Toni/On: `captureLayerNode(layer, tag)`; mu-Clid: the body of `PresetIO::saveRhythmPresetToFile` |
| `void stageLayerPresetTree(int layer, juce::ValueTree tree, bool keepIdentity)` | everything `loadLayerPreset` does after the parse: migrate, preload, then `useSlot` (or mu-Clid's prepare + stage) | the tail of each `loadLayerPreset`; mu-Clid: `prepareRhythm` + `hotSwapStager.stage` or `applyRhythmPreset` |

`saveLayerPreset` becomes `write(captureLayerPresetTree)`, `loadLayerPreset` becomes `parse` then `stageLayerPresetTree`, `storeClip` is
`bank.set(captureLayerPresetTree)`, `launchClip` is `stageLayerPresetTree(bank.get().createCopy())`. The copy matters: the migration step mutates the tree it is
given. Everything the Hot-swap section requires then holds with no new rule: stopped means apply at once, playing means stage and commit at the preset's
`SwapTiming` boundary, heavy work (wavetable and sample preloads) at stage time, readiness gate, `onLayerPresetCommitted(layer)` after the commit, a structural
edit cancels the pending swap, and `layerSwapper.useSlot` lands a pending full preset first. The commit never touches the bank (the apply is `applyLayerNode`,
which has no bank code, and `SlotBound` protects the mu-Clid assignment).

**6. Clip state in `LiveState`** (the Performance mode section's `playingClip` / `queuedClip`, made precise):
- Per layer: `playingClip` (-1 = none), `queuedClip` (-1 = none), `clipMask`. Message thread, unsaved, as the rest of `LiveState`. After any preset load, reset
  or full-preset commit `playingClip` is **none** (no pad is bright: the layer plays its saved live state, which is not necessarily one of its clips). A launch
  sets it at the commit, never before.
- **Queue identity.** A clip launch must not be mistaken for a different swap on the same layer (a layer-preset file loaded after the launch replaces the
  pending payload; the commit then fires `onLayerPresetCommitted` for a payload that is not the clip). Mirror the full-preset rule: `LayerSwapper` gains
  `slotStageSerial(i)` (bumped by `stage` and `cancel` per slot, as `fullStageSerial` is), `LiveState` records it at the launch, and the commit callback
  and `reconcile()` treat a mismatch as "the queued clip was superseded or cancelled" (clear `queuedClip`, do not move `playingClip`). Any non-clip
  `stageLayerPresetTree` clears that layer's `queuedClip` and `playingClip` through `ProcessorBase`, the one choke point.
- **Structural edits.** `LiveState` gains `remapLayers(oldToNew)`, called from the same place as `remapLayerReferences` (phase 3 of `removeLayer` / `swapLayers`, same
  functor): `playingClip` follows its layer, `queuedClip` of an affected layer is cleared (the swap was cancelled), a gone layer is dropped.
- **Mask.** `ProcessorBase` pushes `clipMask` into `LiveState` from one helper (`publishClipMasks()`) at every point the bank changes: store, clear, file load,
  full-preset commit, structural edit. `LiveView` gains `getPlayingClip(layer)`, `getQueuedClip(layer)`, `getClipMask(layer)` and `isLayerMuted(layer)`
  (the last read from the mixer mute parameter by `ProcessorBase`, so `mu-control` can flash a muted playing clip without naming a product).
- **`ClipNext` / `ClipPrev`** act on `selectedLayer`: from the queued clip (else the playing one, else none) step to the next **non-empty** clip, wrapping;
  false when no other non-empty clip exists. They do not mute.

**7. Preset-file impact** (all additive; **Andy 1** to confirm). Clips are saved in **full presets and host sessions only**, never in a layer preset (a
layer preset is one clip's worth of state; carrying a bank would make loading it overwrite the bank, and nest banks).
- *Composed products (mu-Tant, mu-Toni, mu-On):* each `<Slot idx>` node of the composed state gains one optional child
  `<Clips><Clip idx="0..7" name=""> <MuTantVoice ...>...</MuTantVoice> </Clip> ...</Clips>`: `idx` is the position, `name` is optional and may be empty,
  the single child is the stored layer-preset tree, and an empty clip has no `<Clip>` element. Written and read **once, centrally**, by
  `captureComposedState` / `applyComposedState` through `ClipBank::writeTo(node)` / `readFrom(node)`; no product code. `readFrom` of an absent or invalid
  node clears the bank, which is how a slot the state lacks is emptied. The `format` property stays 2.
- *mu-Clid:* each `<Rhythm>` child of `MuClidPreset` (and of a host session) gains the same `<Clips>` child, with `MuClidRhythm`-shaped payloads, written and
  read in `PresetIO::buildFullPresetTree` and the full-preset prepare/commit using the same `ClipBank` functions; the prepared full-preset payload carries
  each rhythm's bank and the commit sets it (the `SlotBound` assignment would not). Clip payloads reference a sample by path only (**Andy 2**).
- *Layer presets (`.muRhythm .muPattern .muArp .muTrack`):* unchanged. *Performance files:* unchanged (pointers to presets). *Extensions:* no new one. *MIDI map:* `LaunchClip`
  already serialises by name with `index`; nothing new.
- **Backwards compatibility.** A file without `<Clips>` loads with eight empty clips, no migration code. An older build opening a file with clips ignores the unknown
  child and, if it re-saves, drops the clips (accepted: no loss of anything the older build understood; stated in the release notes of the release that adds the feature).
  The only fixtures needed are a pre-feature state per product, which already exist as the round-trip test files.
- **Size.** A full preset grows by up to eight layer-preset trees per layer. No cap beyond the 8 x 8.

**8. Staged build order.** Each step: one product at a time after C1, a Debug build, that product's suite plus `mu-core-tests`, and `check-core-boundary`
(mu-core names no product). `dsp-controller` reviews C3 and C4 only (the stage and commit paths, reused unchanged); C1, C2 and C5 never touch the audio thread.

| Step | Kind | Work | Tests (written with the step) |
|---|---|---|---|
| C1 | M | `MuLimits.h` `kClipsPerLayer`; `ClipBank` (+ `SlotBound` member on `Layer`, `clearAll()` calls in `addLayer` / `resetLayer` / `removeLayer`); `writeTo` / `readFrom`; no product code | `mu-core-tests` `ClipBankTests`: set / get / clear / mask; deep-copy (editing a returned tree never changes the stored one); XML round trip incl. names and an empty middle clip; absent node clears; `idx` out of range, duplicate and 9th clip ignored; `SlotBound` survives `Layer` copy, assignment and `adoptDefaults`; travels with a `unique_ptr` move |
| C2 | M | `ProcessorBase::captureComposedState` / `applyComposedState` write and read the banks (Tant, Toni, On). Structural-edit rules from item 3 | Per product, an old composed fixture loads with empty banks; a state with banks round-trips (full preset and host session); extend `LayerStructureTests`: add gives an empty bank, remove-middle shifts banks with their layers and empties the vacated top, swap exchanges banks, reset empties it (Andy 3); a count-reducing full preset empties banks past the new count; the `*_roundtrip` listening tests unchanged and sample-identical |
| C3 | M/S | `captureLayerPresetTree` / `stageLayerPresetTree` split for Tant first, then Toni, On; `storeClip` / `clearClip` / `loadClipFromFile` / `launchClip`; no `LiveState` yet | Per product: store then change parameters then launch (stopped) restores them, mixer strip, name and colour untouched; launch while playing is pending (`hasPendingSwap`) and applies only at the boundary, firing `onLayerPresetCommitted`; empty clip, out-of-range index and launch while a full preset is staged return false and change nothing; launching leaves the bank unchanged; a structural edit cancels a pending launch; a layer-preset file load after a launch supersedes it; the `swap` / `slot swap` hot-swap listening tests |
| C4 | S | mu-Clid: the same two hooks over `PresetIO`; `<Clips>` in `buildFullPresetTree`, the prepare and commit of a full preset, host session; sample-path-only rule; mu-Clid MIDI compiles and ignores the bank (Open question 5) | `mu-clid-tests` (build the target explicitly) and `mu-clid-midi-tests`: full-preset and session round trip with clips; an old `.muClid` loads empty; a staged full preset carries its banks and sets them at the commit; launch (stopped and staged) equals loading the same `.muRhythm`; the existing `HotSwapCancelTests` shape for a cancelled launch; `CLID_roundtrip` and the retire-tail listening test |
| C5 | M | `LiveState` / `LiveView` clip fields, `slotStageSerial`, `remapLayers`, `publishClipMasks`, `LaunchClip` / `ClipNext` / `ClipPrev` in `perform` | `mu-core-tests` with fake callbacks, as the performance step 2: launch queues and commit moves `playingClip`; superseded and cancelled launches clear `queuedClip`; full-preset commit clears all clip state; remap on remove and swap; `ClipNext` / `ClipPrev` wrap and skip empties; mode off returns false; `perform` mute on the playing clip |
| C6 | UX | `ClipRow` in the layer header, store / clear commands, wording; then the `mu-control` grid mapping (performance step 4) | `ux-controller` ruling first; the grid is the controller tests of #1276 |

C1 and C2 can ship with no visible change. C3 and C4 give the feature a screen-driven path (a menu or button calling `launchClip`) before any hardware exists,
so each is demonstrable on its own.

**9. Standing check (design-future).** Easier. The bank is generic `mu-core` data with an opaque per-product payload, so the combined instance (a clip is
a layer preset of that layer's type; the node already carries the product's root tag) and the layer-type registry need nothing new; the Performance
level, the launch surface and the screen share one path; no product gets a copy of the store.

**Open questions.** Andy:
1. **File format (Andy).** The `<Clips>`/`<Clip idx name>` shape, as an additive child of each layer node, `format` unchanged, clips in full presets and sessions
   only. Recommend as written. It becomes a commitment the day a build with it ships (live products are 1.1). Whether the per-clip `name` attribute is kept
   now is part of the same decision (recommend yes: adding it later is a second format change).
2. **Samples in mu-Clid clips (Andy).** Path only (relative to the Samples folder when inside it), a missing sample reported as for a layer preset; embedded sample
   data is never stored in a clip, even when the full preset embeds its rhythms' samples. Recommend path only (an embed would multiply file size by up to nine and
   duplicate the same bytes per clip). Consequence: a preset moved to another machine without its sample folder has silent clips there.
3. **Does Reset layer empty the bank (Andy).** The standing definition of reset is "back to a freshly added layer", which empties it; but it destroys user data
   that a sound reset in mu-On currently does not. Recommend empty it, guarded by the existing reset confirmation; the alternative is a bank that survives a
   reset and only `Clear` removes. The build follows the answer; C2's test pins it.
4. **Launch while a full preset is staged.** Recommend `launchClip` returns false: the clips on screen are about to be replaced by the new preset's, and
   applying an old clip on top of the new layer would be wrong. For `ux-controller`: show the pads as not launchable.
5. **mu-Clid MIDI.** It shares `Rhythm` and so gets an (empty, unused) bank for free. Does it need to keep a `<Clips>` child it did not write when it loads and re-saves a
   mu-Clid file? Recommend it preserves nothing and says so; check at C4 which full-preset code path it uses.
6. **Pressing the playing clip while another is queued on that layer** (`ux-controller` / Andy): mute (the design-launchpad rule as written) or cancel the queued
   launch (what `PerformanceItem` does for the current item). Recommend cancel, then mute on the next press; the surface semantics stay one rule.
7. **A bank as a shareable file** (`.muClips`, eight clips for one layer): not scheduled; nothing here closes it off (the container is already self-contained).

Do not: put the bank on `ProcessorBase`, `LiveState` or in the editor; store it as a plain `std::array<ValueTree, 8>` or a plain member of a copyable `Rhythm`;
give clips their own file type or a per-product clip class; make `launchClip` a second load path beside `loadLayerPreset`; write a clip from the audio thread or
read the bank there; let a pad, a launch or a layer-preset load write the bank (only the screen's store / clear / load-into-clip do); let a clip carry the mixer
strip, name or colour; clear banks under `layersLock`; store embedded sample bytes in a clip; add a `Clip` suffix type per product; edit the Layer virtuals
for this.

## Controller library (`mu-control`) — family standard (ruled 2026-10-13; backlog #1276)

Structure of the library that holds controller drivers (the Launchpad X first). What the device does, the lights, the staged build order and the
tests per stage are in [design-launchpad.md §3.2d](design-launchpad.md#32d-building-mu-control-ruled-2026-10-13-backlog-1276). This section is the shape
every future driver, and the one-instance rig, relies on.

**1. Kind, name, namespace.** `mu-control/` is a sibling library of `mu-core`, an **INTERFACE library** exactly like it (sources are compiled in the
consuming target, where the JUCE modules are already set up). Namespace **`mu_control`** (one namespace for the whole library; sub-areas nest only
when a device needs private helpers, `mu_control::launchpadx`). Include root is `mu-control/`; because the Standalone target has both include roots, **no
folder or file path under `mu-control/` may equal a path under `mu-core/`** (the folder names below are chosen for that). It uses `mu-core` headers
and nothing else of the family; the only `mu-core` types it names are `ControlAction`, `ControlSink`, `LiveView` and the attachment seam of item 4.

**2. Folders (by role, like `mu-core`; the eight-folder product layout is for instrument products and does not apply).**

| Folder | Holds |
|---|---|
| `Surface/` | The device-independent half: `SurfaceDescription.h`, `SurfaceEvent.h` (control id, press/release/value event, LED state), `SurfaceMapping.{h,cpp}`, `SurfaceModel.{h,cpp}` (including the blink animation) |
| `Driver/` | The port-facing half: `SurfaceDriver.h` (the interface), `DevicePorts.h` (the byte pipe the driver talks to), `JuceDevicePorts.{h,cpp}` (the real ports over `juce::MidiInput` / `MidiOutput`; the only place a JUCE MIDI port type appears) |
| `Devices/<Device>/` | One folder per device family. `Devices/LaunchpadX/`: `LaunchpadXProtocol.{h,cpp}` (pure byte encode and decode, no ports), `LaunchpadXPalette.h` (hue name to bright and dim colour), `LaunchpadXDriver.{h,cpp}` |
| `Manager/` | `ControllerManager.{h,cpp}` (discovery, hot-plug, lifetime, the refresh timer; implements `mu_core::ControlAttachment`) and `ControlRegistration.cpp` (item 4) |
| `Tests/` | `mu-control-tests` and its test-only helpers (`MockGridDriver.h`, `FakeDevicePorts.h`, `FakeLiveView.h`, `FakeControlSink.h`) |

File = class; the four parts of design-launchpad §3.1 are named **`SurfaceDriver`, `SurfaceDescription`, `SurfaceMapping`, `SurfaceModel`** (`Surface` is
reserved for this library, [design-naming.md](design-naming.md)); a device driver is `<Device>Driver` (`LaunchpadXDriver`, later `LaunchpadMiniDriver`,
`PushDriver`). Everything under `Surface/` and `Driver/SurfaceDriver.h` knows no device bytes and no product; everything under `Devices/` knows no
`ControlAction`, `LiveView` or mapping (a driver turns bytes into `SurfaceEvent`s and `LedState`s, nothing else). **Driver contract (settled 2026-10-13, M2):** `SurfaceDriver::open(DevicePorts&)` — the
driver owns open and close of the given ports (by its own `PortSpec` of case-insensitive wildcard names), enters the device's host mode on open and restores the device's own mode on
close **and in its destructor**; it sends only the lights that changed (delta lighting) while remembering the whole last frame; it watches `DevicePorts::isConnected()` /
`onConnectionChanged` and, on a replug, repeats its setup and the whole frame; and it holds no port type, only `DevicePorts`. Callbacks (`onBytes`, `onConnectionChanged`)
arrive on a MIDI or hot-plug thread; a driver is not internally synchronised, so the manager serialises everything onto one thread (the threads rule). The boundary check
(`Surface/` and `Manager/` never include `Devices/`; `Devices/` never includes `SurfaceMapping` / `SurfaceModel`) is in `check-core-boundary.ps1`.

**3. Build.**
- Root `CMakeLists.txt` always `add_subdirectory(mu-control)` (before the products), so the library is compiled and its tests built in every
  configuration; **what the option decides is whether the apps link it.**
- Option **`MUFAMILY_WITH_CONTROL`** (root, beside `MUFAMILY_FOCUSED_BUILD`; **OFF** until M4 is accepted on the hardware, then Andy decides the default). When ON,
  a product's CMake calls `mu_control_attach(<product>_Standalone)` (a function `mu-control/CMakeLists.txt` defines; a no-op when the option is OFF),
  which links `mu-control` to the **Standalone target only**, never the plugin's shared-code target, so VST3 and CLAP carry none of it (the Performance
  mode scope rule, above). mu-Clid, mu-Tant, mu-Toni and mu-On call it; mu-Clid MIDI has no standalone and does not; mu-link does not.
- `mu-control-tests` is a `juce_add_console_app` in `mu-control/Tests/CMakeLists.txt`, built in `ALL_BUILD`, mirroring `mu-core-tests` (compiles the library
  sources directly, includes `mu-core` for the headers, links `juce_core`, `juce_audio_devices`, `juce_data_structures`, `juce_events`; it also compiles
  `mu-core/Persistence/PerformanceFile.cpp`, which `LiveView.h` needs). Its sources link no product and no `ProcessorBase`.

**4. The seam: how the standalone shell gets a manager without knowing `mu-control`.** `mu-core` cannot include `mu-control` and `ProcessorBase` must not own
a controller. So `mu-core` owns the **slot** and `mu-control` fills it:
- `mu-core/Control/ControlAttachment.h` (new): the **device-neutral handle** the shell and Settings page see.
  `enum class ControlStatus { NotFound, Found, InUse, Released, Lost }` (the lamp states of [design-ui-family.md §14](design-ui-family.md#14-controller-status-and-release-ruled-2026-10-13-backlog-1276));
  `struct ControlAttachment { virtual ControlStatus getStatus() const = 0; virtual juce::String getDeviceLabel() const = 0; virtual void release() = 0; virtual void connect() = 0; addListener / removeListener (Listener::controlStatusChanged(), message thread); virtual ~ControlAttachment(); }`
  (device name and status text only; no driver type is visible), and
  `using ControlAttachmentFactory = std::function<std::unique_ptr<ControlAttachment>(ControlSink&, const LiveView&, const SurfaceLook&, const juce::File& settingsFolder)>;`
  with `ControlAttachmentFactory& controlAttachmentFactory()` (a function-local static, empty by default, so there is no initialisation-order hazard).
- `mu-core/Control/SurfaceLook.h` (new, plain data, no UI include): the colours and timings a surface needs, filled by the shell from the same tokens the screen uses
  (`MuLookAndFeel::layerPalette`, the app accent, `indicatorFault`, `Lighting::padDimScale`, `queuedPulseHz`, `clipMutedFlashHz` (added to `MuTheme` with the muted light in M5; `SurfaceLook::flashHz` is plain data until then), `stepFlashMs`): `{ std::array<uint32_t, 8> layerRgb; uint32_t accentRgb, faultRgb; float dimScale, pulseHz, flashHz; int stepFlashMs; }`.
  This is how `mu-control` gets screen colours with **no `mu-core/UI` include** and the pad stays the same colour as its layer on screen (design-launchpad §3.2b).
- The processor holds a **non-owning** `ControlAttachment*` (`ProcessorBase::setControlAttachment`, cleared before the attachment is destroyed) and `ShellBackend`
  gains a defaulted `ControlAttachment* getControlAttachment() const` (nullptr), so `StandardSettingsOverlay` shows the "Controller" group (§14) when it is non-null.
  `ProcessorBase` does not own, create or know the manager: it holds an observer of a `mu-core` interface, like `getControlSink()`.
- `mu-control/Manager/ControlRegistration.cpp` sets that factory from a namespace-scope initialiser. It is compiled **into the Standalone executable** (INTERFACE
  sources, not a static library, so the linker cannot drop it). Linking `mu-control` is therefore the whole switch: no `#if`, no product line, no `mu-core`
  include of `mu-control`.
- `mu-core/Plugin/StandaloneShell.h` `App::initialise`, in the **windowed branch only** (never the `--render` branch, which must not touch a MIDI port), after the
  window is up: if the factory is set and the processor's `ShellBackend` returns a non-null `getControlSink()` and `getLiveView()` and
  `supportsPerformanceMode()` is true, it stores `controlAttachment = factory(*sink, *view, SurfaceLook::fromTheme(), settingsFolder)` and calls
  `processor.setControlAttachment(controlAttachment.get())`. `App::shutdown` clears the processor's pointer and resets `controlAttachment`
  **first**, before the preset-name hook, the bridge and the plugin holder, because the manager holds references into the processor and must restore the
  device before the window goes.
- The manager's constructor takes **only `ControlSink&`, `const LiveView&` and the settings folder.** It names no `ProcessorBase`, `ShellBackend`, editor, window
  or product. This is what lets a mu-link hub or a one-instance host create one over its own sink and view later (Performance mode item 7); a hub supplies a
  sink that forwards to the focused instance and changes nothing in `mu-control`.
- Rejected: a product `StandaloneApp.cpp` line under `#if` (four copies of the same wiring, and the option would have to be repeated in each product); an
  `#include` of a `mu-control` header from `StandaloneShell.h` (a `mu-core` to `mu-control` dependency, even if guarded); `ProcessorBase` owning the manager (it
  is built in plugins and headless renders).

**5. Threads.** Everything in `mu-control` is message-thread or MIDI-input-thread; **it never touches the audio thread** and needs no `dsp-controller` review
unless that changes. A driver's input callback runs on the MIDI thread: it decodes into `SurfaceEvent`s, pushes them onto a fixed-size lock-free FIFO and
triggers an `AsyncUpdater`; the manager drains on the message thread, runs the mapping and calls `ControlSink::perform` there (the `ControlSink` contract). LED
output is built and sent on the message thread (the model diffs against the last frame and sends only the changes, batched into one lighting message).
`LiveView` listeners are told on the message thread; the model re-renders from the listener. Blinking is the driver side's own animation (design-launchpad §3.6): one
`juce::Timer` (about 30 Hz) that runs **only while some pad is pulsing or flashing**, recomputing those pads' colours from the clock and sending the changed ones; a
slow timer (about 1 Hz) rescans for a missing device and nothing else runs while idle.

**6. Device ownership (consistent with [Device MIDI output](#device-midi-output--family-standard)).** A driver owns its ports and opens them **only while
Performance mode is on**, and puts the device into its host-controlled mode (Programmer mode) at the same moment; leaving the mode, quitting, a window close, an
OS-requested quit, a disabled controller, a device change and manager destruction all close the session by restoring the device's own mode first. Outside Performance
mode the app holds no controller port and the device behaves as bought, so a DAW, another app or the on-device Setup menu is never blocked by a mu app that is idle
(design-launchpad R1, R2). **Detection is by name only:** the manager matches `MidiInput::getAvailableDevices()` / `MidiOutput::getAvailableDevices()` against each registered
driver's name patterns without opening anything, which is what makes the Settings lamp read `Found`; the device inquiry happens when the ports are opened at entry.
Hot-plug uses `juce::MidiDeviceListConnection`. `release()` closes the session as above and parks the manager (`Released`) until `connect()`; `connect()` rescans and, if
Performance mode is already on, reopens. A crash leaves the device in Programmer mode until it is unplugged (accepted, R1).

**7. Boundary, enforced.** `check-core-boundary.ps1` already scans `mu-control/` for plugin includes and plugin namespaces, and `mu-core` for `mu-control`
includes. Done in `check-core-boundary.ps1` 2026-10-13 except the `Surface/`/`Manager/` to `Devices/` rule, which lands with the first `Devices/` file (M2). The rules: a `mu-control` file must not include `ProcessorBase.h`, `ProcessorCore.h`, `EditorShellBase.h`, `ShellBackend.h` or any
`mu-core/UI/` header, so "depends only on `ControlSink` and `LiveView`" is checked, not remembered; and a file under `Surface/` or `Manager/` must not include
anything under `Devices/`, while `Devices/` must not include `Surface/SurfaceMapping.h` or `SurfaceModel.h`.

**8. Standing check (design-future).** Easier. A new device is one folder under `Devices/` plus a registration line in its driver list; the manager, mapping,
model, seam and tests are shared; the same four parts serve any grid surface; and the hub or the combined instance reuses the manager unchanged (item 4).

Do not: give `ProcessorBase` or the editor a manager; add `#if` wiring or a `mu-control` include to `mu-core`; link `mu-control` to a plugin's shared-code target; open a
port in `--render` or outside Performance mode; let a driver call `ControlSink`, read `LiveView` or write a parameter; let the model or mapping hold device bytes; copy
`MidiControlMap` into `mu-control`; name a product in `mu-control`.

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

**Product-side:** the payload (what is pre-built at stage time), the layer's own loop
length, and the apply. **Which boundary applies** (master loop, layer loop or bar) is the per-preset
`SwapTiming` setting (`mstr_swap`) defined once in `mu-core` (see Performance mode, item 6).

**Deferred-work mailbox (family rule, 2026-10-11): the audio thread never posts messages.** `ProcessorBase` is no longer a
`juce::AsyncUpdater` (no `triggerAsyncUpdate` / `handleAsyncUpdate`). The audio thread calls `requestDeferredWork()`, which only
sets an atomic mailbox flag. A 100 Hz `SwapPoll` timer on the message thread sees the flag and runs `runDeferredWork()`: the
layer-swap commit (`commitDeferredWork`), MIDI program changes and the control router. `flushPendingAsyncUpdates()` (offline
render) drains the mailbox synchronously. Any new audio-to-message hand-off uses this mailbox; never `AsyncUpdater`,
`MessageManager::callAsync` or a `Timer` start from the audio thread. Each destructor stops the poll first.

**Host requirement: a running JUCE message loop (decided 2026-10-12, documented, no fallback drain added).** The `SwapPoll` timer
(`MidiControlRouter::kDeferredWorkPollHz` = 100 Hz) is a `juce::Timer`, so it only fires while the host runs the JUCE message loop.
Everything that goes through the mailbox depends on it: queued parameter changes from the control router, MIDI program changes,
and the commit of staged hot-swaps (`commitDeferredWork`). A host that never runs a message loop (some headless or offline
renderers and command-line batch hosts) never drains the mailbox, so until a loop runs:

- a control-surface or MIDI-mapped parameter change is queued and not applied;
- a MIDI program change does not load its preset;
- a per-slot or full-preset load made while playing is staged but never committed at its loop boundary, so the old pattern
  keeps playing.

A load while stopped, and anything applied directly on the message thread, is unaffected. Workaround: render with a message loop
(pump the JUCE `MessageManager`, or use a host that does), or load through the full-preset path while stopped, which applies at
once. A fallback drain from `processBlock` was rejected: it would put message-thread work (ValueTree edits, allocation, locks)
on the audio thread, against the mailbox rule above. Offline render inside the products is covered by
`flushPendingAsyncUpdates()`.

User-facing wording (for the Linux release notes and the manuals, via `/notes` and the `user-documentation-author`):

> mu plugins need the host to run its normal event loop, as every ordinary DAW does. A few headless or offline-only renderers
> do not. In those, changes made by MIDI or a controller, MIDI program changes, and preset loads made while playing may not take
> effect until the host runs its event loop. If you hit this, load presets while the transport is stopped, or render with a host
> that runs its event loop.

**Open design issue (2026-10-11, being fixed):** the commit is polled, so it lands up to about 10 ms after the boundary the audio
thread flagged. The new pattern then starts from its own step 0 at the wrong phase relative to the loop that just wrapped.
The fix must make the commit phase-correct (carry the elapsed offset into the new state, or commit on the audio thread's
boundary); until then a swap can start late by up to one poll interval.

| | mu-clid | mu-tant | mu-toni | mu-on |
|---|---|---|---|---|
| Deep-dive doc | [mu-clid/design-hotswap.md](mu-clid/design-hotswap.md) | — (this section) | — | — |
| Stager | `HotSwapStager` → `Stager<PendingRhythm, PreparedFullPreset, 8>` (`Rhythm` + `VoiceEngine` + sample path) | `ProcessorBase::layerSwapper` (`LayerSwapper<ValueTree, 8>`) | same | same |
| Boundary predicates | shared `mu_hotswap::layerBoundaryReached` / `fullPresetBoundaryReached` (`mstr_swap`) | shared `loopWrapped` / `boundaryReached` through `mu_tant::swapLoopBeats` | shared (default bar rule, `kBarBeats`) | shared (default bar rule, `kBarBeats`) |
| Reference boundary | master loop / per-rhythm wrap (`mstr_swap`; no bar rule, `OnBar` reads as the rhythm loop) | full preset → voice 0's gate-pattern wrap (master loop when set); per-voice → that voice's own wrap (master loop when set) | every bar (the arp has no loop of its own) | the 16-step pattern wrap = every bar |
| Staged when | sequencer playing | the audio thread's resolved play state (host, MIDI clock or internal), via `layerSwapper.useFull` / `useSlot` | the audio thread's resolved play state (host, MIDI clock or internal) | the audio thread's resolved play state (host, MIDI clock or internal) |
| Commit isolation | `suspendProcessing` + `rhythmsLock`, microseconds (heavy work pre-built at stage) | **no blanket lock** — relies on the audio render's existing fine-grained locks | same as mu-tant | same as mu-tant |

**Stage-or-apply is one rule for every swapper product.** mu-Tant follows the resolved-transport rule like mu-Toni and mu-On: a load goes through `layerSwapper.useFull` / `useSlot`, which stage while the audio thread's resolved play state (`layerSwapper.isRunning()`) is true and otherwise apply at once, cancelling anything already staged for that scope so a stale payload can never land on top. After staging the swapper re-reads the play state; if the transport stopped in between (the stop edge may already have passed), the payload is taken back and applied, so nothing stays staged with no future boundary. Products read `isRunning()` only to pre-load assets for a load that will be staged. Audible consequence for mu-Tant: with the host (or MIDI clock / mu-link) playing and mu-Tant's own Play off, a load now waits for the voice's loop wrap instead of cutting in mid-loop.

**Shared principles (hold for any future product):**

1. **All heavy work at stage time, off the audio thread** — parse, build engines,
   load samples/wavetables. The commit is only fast in-memory moves / atomic stores.
2. **Stopped = commit immediately; playing = stage + commit at the boundary** — same
   build + same commit code, only the trigger differs (no divergent second path).
   **"Staged when" (family rule, 2026-10-11):** "playing" is the play state the audio thread resolved in
   its last block (`mu_core::resolveTransport`, see the Transport rule), published as one atomic; the
   message thread never asks a second source (a product's own Play button, a host query). Where the
   commit lands is the product's **boundary rule**, supplied to the shared swapper as a loop length in
   beats per layer and for a full preset: mu-Tant = the layer's own gate-pattern wrap (or the master
   loop per its Hot-swap timing; voice 0 / master loop for a full preset), mu-Toni and mu-On = the
   4/4 bar (`kBarBeats`), mu-Clid = its rhythm's loop. All share `boundaryReached` (wrap, or the
   playing-to-stopped edge commits at once). A product changes the rule only by changing those lengths,
   never by owning a second stager.
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
   and `ProcessorBase::onLayerPresetCommitted(slot)` (per layer, every product; the full callback fires once, after all applies of a commit) — the shell calls `onPresetLoaded` *synchronously*
   after `loadPreset`, which for a staged swap runs against pre-swap state, so the
   commit must re-trigger the refresh. **Clear the callbacks in the editor dtor**
   (processor outlives editor → UAF).

**Structural layer edits cancel pending per-layer swaps.** Add / remove / reorder /
reset of a layer renumbers or clears slots by index, so a staged per-layer swap
(keyed by index) would commit to the wrong slot — the structural-edit functions
cancel the affected pending swaps (a remove/down-shift cancels all). A staged **full
preset** is index-independent (it replaces every slot at commit) and is left to win.
Every product implements this (mu-clid `cancelPendingIfAny`; the others through the default
`ProcessorBase::cancelPendingSwap` over `layerSwapper`).

**Staging badges (shared, automatic):** `TransportBar` shows a "SWP" pill from
`ProcessorBase::hasPendingFullPreset()`; `ChannelSidebar` shows a per-layer badge
from the product's `isPendingSwap(i)` / cancels via `onCancelPendingSwap(i)`. A
product only has to override/​wire those.

---

## Licensing and the support prompt — family standard (ruled 2026-10-11)

**Owner decision.** The source is GPLv3. The apps are free, pay-what-you-want on Lemon Squeezy (minimum £0, suggested £20). An unlicensed copy is **fully functional**: no track, step, save or preset limits, no demo banner, and no "Lite" free tier (the former mu-Clid Lite is now a separate MIDI-effect product, see "The MIDI-effect product" below). Instead it shows one polite support dialog per session. A licence key still exists and still activates (a key marks a supporter, and unlocks nothing). mu-Toni (freeware) and mu-On (no licensing) never call `initLicensing`, so they never show the dialog.

### What is deleted, what is kept

| Delete | Keep |
|---|---|
| `ProcessorBase::demoMaxChannels`, `demoMaxSteps`, `canAddChannel`, `canSaveLayerPreset`, `maxSteps` (and their callers: `ChannelSidebar`, `MasterLoopSection`, `LayerPresetHeader`, both `PluginEditor` add-channel checks, `EuclideanPanel`, `RhythmPanel`, `VoicePanel`, `GatingDesigner::setMaxCells` use, the tant `limitToCells` demo calls, the preset-load clamps in `PresetIO*` and `PluginProcessor_Preset`, the sequencer `setStepCap`) | `LicenseManager`, `OnlineActivation`, `LemonSqueezyClient`, `ActivationStore`, `MachineFingerprint`, `initLicensing`, `ActivationOverlay`, `activateOnlineFn`, the `.lic` offline path, the license tool |
| `EditorShellBase::demoBanner`, `kDemoBannerH`, the banner click route, the banner layout rows | `upgradeBanner` and the version check |
| The save gate in `transportBar.onSavePreset`, `setSaveEnabled(licensed)` calls | The `StatusBar` is no longer given "Demo limited ..." messages (delete those two) |
| `MUFAMILY_REQUIRE_LICENSE` as a feature gate (see below) | |
| The mu-Clid *demo-limit* paths. mu-Clid Lite is **not** deleted: as of 2026-10-11 it is a product in its own right and leaves mu-Clid by the staged plan in "The MIDI-effect product" below, not by deletion. | |

`isLicensed()` goes too. It is replaced by `bool isSupporter() const` (offline `.lic` valid OR online-activated), which is true or false identically in Debug and Release. Nothing in the audio or editor path may branch on it except the support prompt and the About/Activation text.

### Where it lives

- **`mu-core/UI/SupportOverlay.{h,cpp}`**, a sibling of `ActivationOverlay` (same non-blocking card, click outside dismisses, same `MuLookAndFeel` tokens; the `ux-controller` rules the look and wording). The shell owns it as a member, wired in `wireOverlays()`, shown through a `showSupport(bool)` that joins the overlay state machine. Card content: a short thank-you and what the money does, the pay-what-you-want line, a **Support (opens the checkout page)** button, an **Enter a key** button that calls `showActivation(true)`, a **Not now** button, and a **Don't show this again** tick box.
- **`mu-core/License/SupportPromptPolicy.h`**, juce_core-only and header-only, the testable decision: `shouldShow(hasLicensing, isSupporter, hiddenForever, shownThisProcess)`. It is true only for a licensing product, not a supporter, not hidden for ever, and not yet shown in this process.
- **Once per session** means once per process: a function-local `inline std::atomic<bool>` in the policy header, set when the card is shown. A DAW project with twenty instances shows it once. The shell shows it from `parentHierarchyChanged()` the first time the editor is on screen (so a headless instance never prompts), deferred by a short `callAfterDelay` so it never competes with plugin load.
- **Persistence of "don't show again":** the product's own settings file (`ProcessorBase::appSettings`, key `supportPromptHidden`, bool), through two small `ProcessorBase` accessors in the style of `setUiScale`: `bool isSupportPromptHidden() const` and `void setSupportPromptHidden(bool)` (writes and `saveIfNeeded()`). Per user and per product, not in APVTS, because it is a user preference and must not travel inside a preset or a project. A supporter never sees the prompt, so the key is only written by the tick box.
- **Checkout URL plumbing:** a field on `ProcessorBase::LicensingConfig`, `const char* checkoutUrl`, supplied by each licensed product from its `Source/License/LicenseKey.h` (`kCheckoutUrl`), exposed as `ProcessorBase::getCheckoutUrl()`. This sits with the rest of the product's licensing identity and spares every editor an extra setter call; `ActivationOverlay` gets a "Get a key" link from the same accessor. Values today: mu-Clid `https://transwarp.lemonsqueezy.com/checkout/buy/ba9e6fba-f65d-4b09-87d8-5a6b295c84c2`, mu-Tant `https://transwarp.lemonsqueezy.com/checkout/buy/b2dd23a7-cd2c-48b2-baa5-8fdda3aa7576` (the same links the website uses). mu-core never names either.
- **Link, not a payment form.** The button opens the checkout in the browser (`juce::URL::launchInDefaultBrowser`). The plugin handles no money and no card data.
- A permanent entry point stays after the banner goes: the About panel shows "Support mu-X" and "Enter a key" buttons (supporters see "Thank you"), so the dialog is never the only route.

### `MUFAMILY_REQUIRE_LICENSE` after this change

It no longer gates features, so its Debug-bypass meaning disappears. Remove it from the root `CMakeLists.txt`. Debug and Release behave the same, which is what makes the dialog testable in a Debug build (below). The `LicenseManager.h` header comment is rewritten with the macro.

### Testing

- **Unit (mu-core-tests):** `SupportPromptPolicyTests.cpp` covers every input combination of `shouldShow` and the once-per-process flag (the flag is reset through a test-only `resetForTests()`). A second test builds `TestProcessorCore` over a temp `PropertiesFile` and checks `setSupportPromptHidden` round-trips through the settings file, and that `isSupporter()` is true after `onlineActivated` is set and false otherwise.
- **Manual / Debug:** with no key a Debug build now shows the prompt on first open (the Debug bypass is gone), so it is exercised on every test run. To see it again after "don't show again", delete the `supportPromptHidden` line from `%APPDATA%\TDP\<app>.xml`, or run with the env var `MU_SUPPORT_PROMPT=reset` (read once in `initLicensing`, Debug-only, clears the key). To see the supporter path, activate with the verified £0 key.
- **Regression:** the existing unit and round-trip tests must show no behavioural change in a licensed state; the full-preset save and an 8-track, 64-step preset must work with no key present (add one such case to each of mu-clid-tests and mu-tant-tests, replacing any test that asserts the demo clamp, for example `limitToCells`, which can stay as a pure `GatePattern` function test or go with the feature).

### Staged order (each stage buildable and independently shippable)

1. **Policy and persistence (additive).** `SupportPromptPolicy.h`, `isSupporter()`, `isSupportPromptHidden/Set`, `checkoutUrl` in `LicensingConfig` and both `LicenseKey.h` files, `mu-core-tests` for the above. Nothing visible changes.
2. **SupportOverlay and shell wiring.** The overlay, `showSupport`, first-show trigger, About buttons, "Get a key" link. The old demo banner and caps still exist, so this stage is visible but harmless. `ux-controller` rules the card first.
3. **Remove caps.** Delete the `ProcessorBase` cap API and every caller listed above, the demo status messages, the save gate, the preset-load clamps, the tant demo `limitToCells`; replace `isLicensed()` calls with nothing (or `isSupporter()` in About). Update tests. This is the one large edit, still product by product if wanted: mu-Tant, then mu-Clid.
4. **Remove the banner and the macro.** `demoBanner`, its layout, click route; `MUFAMILY_REQUIRE_LICENSE`; header comments.
5. **(Superseded 2026-10-11: Lite is not retired.)** The owner made mu-Clid Lite a separate MIDI-effect product. Its split from mu-Clid follows "The MIDI-effect product" below and is independent of stages 1 to 4 (it needs none of them).
6. **Docs and site.** Rewrite the licensing lines of both manuals (`user-documentation-author`), `/notes`, the site buy buttons and pricing (separate site task), and the CLAUDE.md wording below.

Each stage ends in a Debug build and the unit tests; stages 3 and 4 also run the round-trip listening tests and pluginval on the build PC.

---

## The MIDI-effect product (the former mu-Clid Lite) — ruled 2026-10-11

**Owner decision (2026-10-11).** mu-Clid Lite is a separate product: a Euclidean MIDI effect (no audio, no samples, no Standalone), not a variant or free tier of mu-Clid. The product is named **mu-Clid MIDI** (owner, 2026-10-11; decisions under Owner decisions below), slug `mu-clid-midi`, namespace `mu_clid_midi`; "the MIDI product" below means this product, and `mu-<slug>` / `<slug>` read as `mu-clid-midi`.

### Ruling: a product, reached in stages (not a target forever)

Today it is a second `juce_add_plugin` target inside `mu-clid/`, compiling about seventeen of mu-Clid's files a second time under `MUCLID_LITE_BUILD=1`, with roughly sixty `#if` sites in `PluginProcessor.{h,cpp}`, `_APVTS`, `_Modulation`, `PresetIO` and `StandaloneApp`. That breaks the family shape (one product, one folder, one namespace, eight `Source/` folders) and the "no per-product copy of shared behaviour" rule in both directions: mu-Clid carries MIDI-only branches, and a standalone MIDI product would otherwise need its own copy of the Euclidean sequencer. It therefore gets its own folder, namespace, docs and CLAUDE.md. It must **not** be done as a fork: the Euclidean sequencing both products share moves to `mu-core` first (the "second product needing a behaviour" rule).

Stages (each ends in a Debug build and the unit tests; stages 2 and 3 also run the round-trip listening tests and pluginval on the build PC):

1. **Identity, no code move (cheap, do first).** Rename every user-visible and release-facing name to the final name while keeping the CMake target, `MUCLID_LITE_BUILD` and every plugin ID untouched: `PRODUCT_NAME` / `PLUGIN_NAME`, `getName()`, the `.iss` and installer filename, `PackageRelease.cmake` slug, `DeployReleaseLite.cmake` filenames, `release.yml` staging and zip names, `mu_build_artifacts.ps1`, `check-build-artifacts.ps1`, `run-pluginval.ps1`, the site. Add the Windows `[InstallDelete]` and the uninstall of the old `mu-Clid Lite` bundle (below). Docs: a product CLAUDE.md stub and `docs/<slug>/`.
2. **Extract the shared sequencing into `mu-core/Sequencer/`.** The files that Lite and mu-Clid both compile (`EuclideanGenerator`, `HitGenerator`, and whatever of `Rhythm` / `RhythmSequencer` is free of sample, voice and sidechain concepts) move to `mu-core` under neutral names and the `mu_core` namespace, mu-Clid being the second consumer that makes it shared. The `Rhythm`-into-`Layer` container work (design-naming §5 step 5.7) rewrites the same classes; **sequence this stage after that one**, or hand the extraction to it, so the code moves once. **Owner chose the earlier split (2026-10-11):** extract only the pure generators (`EuclideanGenerator`, `HitGenerator`) now and leave `Rhythm` / `RhythmSequencer` for step 5.7; the MIDI product carries a thin `Rhythm` of its own until then, recorded as a known exception. **Superseded 2026-10-11 (architecture-steward, on the as-built tree):** the MIDI product does not carry a `Rhythm` of its own; it compiles mu-Clid's `Rhythm.cpp`, `RhythmSequencer.cpp`, `EuclideanPanel.cpp` and `RhythmCircle.cpp` from `mu-clid/Source` with that folder on its include path, which is the per-product copy and the product-to-product dependency this section forbids. The `Rhythm` that exists today is already pure (generators, logic, reset steps, legato, a `Layer`; no sample, voice or sidechain member), so it moves **now**, before stage 7, in the stages under "Stage 2b" below; stage 7 then no longer adds engine members to the shared class.

### Stage 2b: the shared Euclidean sequencer moves to mu-core (ruled 2026-10-11; done 2026-10-12, stages 1 to 5)

The shared pieces and their destination (the moves are `git mv`; a header keeps its `Folder/Name.h` include path, so `#include "Sequencer/Rhythm.h"` in mu-Clid and the MIDI product resolves unchanged through the `mu-core` include root):

| From `mu-clid/Source/` | To `mu-core/` | Namespace |
|---|---|---|
| `Sequencer/Rhythm.{h,cpp}` | `Sequencer/Rhythm.{h,cpp}` | `mu_core` (`Rhythm`, `Logic`, `EuclidOverrides`) |
| `Sequencer/SequencerEngine.{h,cpp}` (now `RhythmSequencer`) | `Sequencer/RhythmSequencer.{h,cpp}` | `mu_core` |
| `Sequencer/RhythmPlayState.h` | `Sequencer/RhythmPlayState.h` | `mu_core` |
| `Persistence/RhythmParamTable.h` | `Persistence/RhythmParamTable.h` | `mu_core` (`ParamKind`, `RhythmParamDef`, `kRhythmParamDefs`, `kRhythmParamCount`) |
| `Persistence/PresetHelpers.h` | `Persistence/RhythmPresetHelpers.h` (renamed: it is the kinded-property reader/writer for that table, not a product's preset I/O) | `mu_core` |
| `UI/EuclideanPanel.{h,cpp}` | `UI/EuclideanPanel.{h,cpp}` | `mu_ui` |
| `UI/RhythmCircle.{h,cpp}` | `UI/RhythmCircle.{h,cpp}` | `mu_ui` |

`Sequencer/GeneratorAliases.h` stays in mu-Clid and becomes the alias header for the sequencer and persistence set
(`using mu_core::Rhythm;` and so on inside `mu_clid`; the two panels get their own UI alias header, see the stage rules below), the pattern it already uses for the generators, so
mu-Clid's roughly 1580 uses need no edit. Rules for the moved code: **no sample, voice-engine, sidechain,
licence or APVTS-id knowledge in `mu_core::Rhythm`**; the euclid modulation-snapshot indices that
`EuclideanPanel` reads become a panel-local enum in `EuclideanPanel.h` (`EuclidSnap`; mu-Clid's `kSnapEuc*`
values are pinned to it by a `static_assert`, see the stage rules below), so the panel no longer includes
`Modulation/ModulationSnapshot.h`. **Stage 7 of the layer plan** (mu-Clid into the container) does **not**
derive a `mu_clid::Rhythm` (amended 2026-10-12; see "Stage 7 implementation plan"): mu-Clid and the MIDI
product both use `mu_core::Rhythm`, and the engine, retired engines, MIDI-out engine, sample path and
mod-previous-state flags stay processor-side keyed by slot. After stage 2b (done) the MIDI product's CMake has no
`${MU_CLID_SOURCE}` include path and compiles nothing from `mu-clid/`.

**The five stages (ruled 2026-10-11; supersedes an earlier split that created the `EuclidSnap` mapping first and moved Rhythm, RhythmPlayState, RhythmSequencer, RhythmParamTable and PresetHelpers in one step).** The backlog split is the correct one, backlog rows 1489 to 1493 (umbrella 1411), because it follows the include graph so each stage leaves both products building:

- `Rhythm.h` includes `Layer.h` (already mu-core) and `GeneratorAliases.h` (a `mu_clid` alias header, so it must be replaced by direct `Sequencer/EuclideanGenerator.h` / `HitGenerator.h` includes and `mu_core::` names when the file moves). `RhythmPlayState.h` includes only `<atomic>`. Both are leaves: **stage 1**.
- `RhythmSequencer.h` includes `Rhythm.h` (relative; must become `Sequencer/Rhythm.h`), `MuLimits.h` and `Audio/SpinLock.h` (both mu-core). It needs stage 1 only: **stage 2**.
- `RhythmParamTable.h` includes `Sequencer/Rhythm.h` and mu-core `Audio/AlgorithmNames.h`; `PresetHelpers.h` includes only `RhythmParamTable.h` and `AlgorithmNames.h`. Neither touches the engine, so they need stage 1 only, and they move together because the helper cannot compile without the table: **stage 3**.
- `RhythmCircle.h` includes `RhythmPlayState.h` (stage 1) and `GeneratorAliases.h`; `EuclideanPanel.h` includes `Rhythm.h` (stage 1), `GeneratorAliases.h`, mu-core components and `Plugin/ProcessorBase.h`, and its `.cpp` includes `Modulation/ModulationSnapshot.h` (mu-Clid) for the `kSnapEuc*` indices. Only that one include blocks the move, so the panel-local enum is part of the panel's own stage, not a stage of its own: **stage 4**.
- The MIDI product's include path and compile-ins can only go once all of the above has moved: **stage 5**.

Stages 2, 3 and 4 are independent of each other once stage 1 is in; run them in the backlog order. Rules that apply to every stage:

- **Each stage moves its own users.** Moving a file out of `mu-clid/Source` breaks the relative includes of what is left behind and the `mu_clid::` qualified uses in the MIDI product. Stage 1 therefore also: makes `GeneratorAliases.h` include `Sequencer/Rhythm.h` and `Sequencer/RhythmPlayState.h` and add `using mu_core::Rhythm; using mu_core::Logic; using mu_core::EuclidOverrides; using mu_core::RhythmPlayState;`; adds `#include "GeneratorAliases.h"` to the not-yet-moved `RhythmSequencer.h`, `RhythmParamTable.h`, `EuclideanPanel.h` and `RhythmCircle.h` (and changes `RhythmSequencer.h`'s `"Rhythm.h"` to `"Sequencer/Rhythm.h"`) so their unqualified `Rhythm` still resolves inside `mu_clid`; and in the MIDI product changes `using mu_clid::Rhythm; using mu_clid::RhythmPlayState;` in `MidiRhythmCore.h` and `mu_clid::Rhythm` in `PluginEditor.cpp` to `mu_core::`. Each later stage does the same for its own files (stage 2: `MidiRhythmCore.h` `RhythmSequencer`; stage 3: `mu_clid::kRhythmParamCount` / `kRhythmParamDefs` / `readKindedPropertyAsActualV2` in `MidiRhythmCore.{h,cpp}`, `SessionState.cpp`, `SessionStateTests.cpp`; stage 4: `mu_clid::EuclideanPanel` / `RhythmCircle` in the MIDI `PluginEditor.{h,cpp}`).
- **Alias headers.** `GeneratorAliases.h` carries the sequencer and persistence aliases. The two panels are GUI classes and must not be pulled into audio translation units through it, so stage 4 adds a small `mu-clid/Source/UI/EuclidPanelAliases.h` (`using mu_ui::EuclideanPanel; using mu_ui::RhythmCircle;`) included by the mu-Clid editor files. Stage 3 adds `using mu_core::ParamKind; ...` for the table to `GeneratorAliases.h` (or the files that use it name `mu_core::` directly where there are few).
- **CMake.** The moved `.cpp` files go into `mu-core/CMakeLists.txt` (the library source list; the headless test targets that list mu-core sources individually, `mu-clid/CMakeLists.txt` near `Source/Sequencer/Rhythm.cpp` and `mu-clid-midi/CMakeLists.txt` near its `HitGenerator.cpp` block, get the same line and lose the `${MU_CLID_SOURCE}` or `Source/` one). A header-only move changes no CMake list.
- **Panel mod snapshots (stage 4).** The panel-local enum is `EuclidSnap` in `EuclideanPanel.h` (`kEucAHits ... kEucCInsLen`, 18 entries, same order as the `kSnapEuc*` run in `ModulationSnapshot.h`); mu-Clid's `ModulationSnapshot.h` keeps its names and adds `static_assert(kSnapEucAHits + n == EuclidSnap::...)` for the first and last entry so the order is pinned. The MIDI product passes no `modulationSnapshot` callback (it has no modulation), so it needs nothing.
- Each stage ends with the checks named in its backlog row, and `check-core-boundary.ps1` clean.
3. **Create `mu-<slug>/`.** Own `CMakeLists.txt`, own thin `PluginProcessor` (a `ProcessorBase` subclass, MIDI-effect bus layout, `lite_midiNote`-style parameters renamed in the product only through a migration), own editor (`LiteEditor` becomes `<Product>Editor`, in `UI/`), the eight `Source/` folders with `.gitkeep` in the empty ones (`License`, `Modulation`, `Audio` are expected empty). `mu-clid` loses all `MUCLID_LITE_BUILD` code, the `mu-clid-lite` target, `LiteEditor.*` and the Lite members of `PluginProcessor`. Add the new directory to the root list in `check-core-boundary.ps1` and `check-mod-targets.ps1`.
4. **Retire the old paths.** Remove `BuildInstallerLite.cmake` / `DeployReleaseLite.cmake` in favour of the shared product-parameterised scripts (or renamed copies), the Lite lines in `PackageRelease.cmake`, and the `mu-clid/installer/mu-Clid-Lite.iss` (moves to `mu-<slug>/installer/`).

### Rules for the MIDI product

- **Plugin IDs never change** (see the warning below): `PLUGIN_CODE MCLt`, `PLUGIN_MANUFACTURER_CODE TDP1`, `BUNDLE_ID com.transwarp.mu-clid-lite`, `CLAP_ID com.transwarp.mu-clid-lite`, and the processor state id `MuClidState`. They are saved data (design-naming §3); the product name is display only. The bundle's visible name changes, the identity does not.
- **Layer type stays `Rhythm`** and the layer-preset extension stays `.muRhythm` (shared with mu-Clid so a rhythm made in either loads in the other; mu-Clid's sample binding is ignored by the MIDI product). This is the one named exception to "each product's layer type name is unique" (design-naming §1), because the type is one shared class in `mu-core`, not two.
- **No licensing glue**, like mu-On: it never calls `initLicensing`, never shows the support prompt, and `License/` holds a `.gitkeep`. (Owner decided 2026-10-11: no prompt.)
- **No modulation targets**: it has no `ModulationMatrix` use today, so there is no `mu_mod::ModTarget` table and `Modulation/` is a `.gitkeep`; if it later gains modulation it adds one table like every product.
- **Formats:** VST3 + CLAP only (owner, 2026-10-11; no AU, no Standalone), `IS_MIDI_EFFECT TRUE`. Free. Linux is built by `release.yml` as for mu-Clid today.

### WARNING: changing plugin IDs breaks existing DAW projects

DAWs find a plugin in a saved project by its identity (VST3 class ID, CLAP id, AU type / subtype / manufacturer), and hand the saved state blob back to it. Change `PLUGIN_CODE`, `PLUGIN_MANUFACTURER_CODE`, `BUNDLE_ID`, `CLAP_ID`, the company name, or `MuClidState` and every existing project shows the Lite instance as missing, with its settings lost. Changing the display name alone is safe. The two traps in the rename:

- **Stale duplicate.** The old `mu-Clid Lite.vst3` (and `.clap`) stays on disk after an upgrade, so a DAW sees two plugins with the same ID and picks one unpredictably. The new installer must delete the old bundles: Inno `[InstallDelete]` for `{commoncf64}\VST3\mu-Clid Lite.vst3` and `{commoncf64}\CLAP\mu-Clid Lite.clap`, and the old Start-menu group. Keep the `AppId` GUID of the Lite `.iss` so the upgrade is recognised as an upgrade, not a second install.
- **Name-keyed hosts.** Some hosts (preset databases, Reaper FX names, Bitwig favourites) remember the *display name*. A project still loads (the ID matches), but favourites and saved FX chain presets named "mu-Clid Lite" may need re-picking. Note it in the release notes.

If the owner wants brand-new IDs anyway (a clean break), do it as its own decision with a version note, and expect all existing Lite sessions to need the plugin re-inserted.

### What the migration touches

| Area | Files | Stage |
|---|---|---|
| Installer | `mu-clid/installer/mu-Clid-Lite.iss` (rename, new output filename, `[InstallDelete]`, keep `AppId`), `cmake/BuildInstallerLite.cmake` | 1, 4 |
| Release workflow | `.github/workflows/release.yml` staging dirs, copy paths and the three zip names (Windows, macOS on hold, Linux): `mu-Clid-Lite-*.zip` | 1, 3 (paths) |
| Packaging and deploy | `cmake/PackageRelease.cmake` (`-DPRODUCT=`), `cmake/DeployReleaseLite.cmake`, the deploy blocks in `mu-clid/CMakeLists.txt` | 1, 3, 4 |
| Build tooling and hooks | `scripts/mu_build_artifacts.ps1` (artefact list that the post-build hook prints), `tests/scripts/check-build-artifacts.ps1`, `tests/scripts/run-pluginval.ps1`, `tests/scripts/check-core-boundary.ps1`, `check-mod-targets.ps1` (product root list), `/build` and `/release` skills | 1, 3 |
| CMake | root `CMakeLists.txt` `add_subdirectory`, `mu-clid/CMakeLists.txt` Lite block, new `mu-<slug>/CMakeLists.txt`, `increment_build_number` dependency, `versioninfo-lite.rc.in`, `resources/` icon and the shared `mu-clid-resources` target | 3 |
| Source | `MUCLID_LITE_BUILD` sites, `LiteEditor`, `computeLiteTransport` and the Lite processBlock helpers, `lite_midiNote` parameters | 3 |
| Website | `site/download.html` (card, file names, the analytics label `μ-clid LITE download`), a product page, a release-notes page, the `/notes` command's product list, `web-designer` brief | 1 |
| Manuals | a short `docs/<slug>/create_manual.ps1` and `.docx` (`user-documentation-author`); the mu-Clid manual drops any Lite mention and copy to the OneDrive root | 3 |
| Tests | a `<slug>-tests` target (the generator and processor tests move with the code); `tests.md` rows; pluginval path | 3 |
| Presets and extension | none: `.muRhythm` and `MuClidState` are unchanged; a new full-preset extension only if the product ever gains full presets | none |
| Docs | this section, design-naming §3 / §4 / §5, `CLAUDE.md` monorepo layout and product list, a `mu-<slug>/CLAUDE.md` | 1, 3 |

### Owner decisions (settled 2026-10-11)

1. **Name:** display name **mu-Clid MIDI** (the descriptive-suffix exception for a deliberate sibling, design-naming §3). Slug **`mu-clid-midi`**, namespace **`mu_clid_midi`**, source folder `mu-clid-midi/`, docs folder `docs/mu-clid-midi/`, test target `mu-clid-midi-tests`, installer/zip names `mu-Clid-MIDI-*`. The names are display and layout only; the next item does not change.
2. **Plugin IDs kept:** `MCLt`, `TDP1`, `com.transwarp.mu-clid-lite`, `MuClidState` all stay (the CMake target may be renamed in stage 3; the IDs inside it may not).
3. **`.muRhythm` / `Rhythm` shared with mu-Clid:** yes (rule above).
4. **Licensing:** no support prompt and no licensing glue. It is free.
5. **Formats:** VST3 + CLAP only (no AU, no Standalone). Free, no price on the site.
6. **Identity:** it keeps its own icon and its own site product page and release-notes page.
7. **Extraction timing (stage 2):** extract the pure generators now (`EuclideanGenerator`, `HitGenerator` into `mu-core/Sequencer/`, namespace `mu_core`); `Rhythm` and `RhythmSequencer` follow with the layer-container stage (design-naming §5 step 5.7). Until then mu-Clid MIDI carries a thin `Rhythm` of its own, a recorded known exception, not to be extended. **Resequenced 2026-10-11:** they move now, as Stage 2b above, because the class is already pure and stage 7 is the last and riskiest of the plan; the owner is asked to confirm the new order.

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

1. A build is a **Debug** build unless the owner asks for Release (when to build: CLAUDE.md, "Keep working until a build is worth it"). Every full Debug build **increments the number by
   exactly 1** — no time/session throttle. Each Debug build is a distinct testable artefact.
2. **Release** builds happen only when the owner explicitly says so. A Release rebuilds
   only the Release artefacts (Debug untouched) and is stamped with the **same number as
   the last Debug build** — Release **never** increments.
3. **Release ≤ last Debug, always.** A Release coming out *higher* than the last Debug
   means the counter advanced without a Debug build → the build **stops with a
   FATAL_ERROR**; surface it to the owner, don't work around it.

### JUCE version policy

Owner rule, 2026-10-09. The family tracks the latest stable JUCE release rather than staying on an old one. The version is pinned in **one place**, [juce-version.txt](juce-version.txt): every CI workflow checks out that tag and configure fails if the local `JUCE_PATH` checkout differs, so a bump is a one-line edit and local builds cannot drift from CI. To bump: read JUCE's `BREAKING_CHANGES.md` between the old and new version, grep the tree for each affected API, bump the pin, then run the unit tests, the round-trip listening tests and pluginval on the build PC. When writing code, use current JUCE APIs and do not build on deprecated ones. Version in use: JUCE 9.0.3 today. `pwsh tests/scripts/check-juce-version.ps1` reports how far the pin is behind the latest release.

### Version numbering

The live products (mu-Clid, mu-Tant, mu-link, mu-Clid MIDI) use [version.txt](version.txt), currently 1.1 (the UI rework, build 972). The pre-release products (mu-On, mu-Toni) use [version-pre.txt](version-pre.txt), currently 0.1 (the .1 marks the metal-panel design upgrade, Andy 2026-10-11). Plugins show `v1.1.0.NNN` / `v0.1.0.NNN`, commits `v1.1.NNN`. The build number never resets on a major/minor change (the Release ≤ Debug guard and the in-plugin update check rely on it). Change a base **only** by editing its file, and only when Andy says so.

### Shipping a Release (two ways, every time)

Release builds stay local (nothing is deployed). (a) a zip built and uploaded to the GitHub "latest release" so website download
links resolve; (b) **release notes promoted** — move accumulated "Next release · In
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

Time stretching sits behind `TimeStretcherBase` (currently a stub; SoundTouch in v1 or RubberBand in v2 slots in without refactoring). The time-stretch library ships as a separate DLL, required for LGPL/GPL compliance, when it is implemented.

## Related design documents

- [mu-clid/design-voice.md](mu-clid/design-voice.md) — μ-Clid voice chain, ADSR, filter, InsertProcessor details (mu-tant's voice doc lives at [mu-tant/design-voice.md](mu-tant/design-voice.md))
- [design-fx.md](design-fx.md) — FX chain, slot interface, intra-FX routing (family-shared)
- [mu-clid/design-hotswap.md](mu-clid/design-hotswap.md) — **seamless preset/rhythm hot-swap** deep-dive (stager, boundary detection, retire-then-swap voice tail, threading). The family-level pattern + the product-side decision + the mu-tant variant are summarised in [Hot-swap](#hot-swap-staged-preset--layer-swaps-family-pattern) above.
- [mu-clid/design-presets.md](mu-clid/design-presets.md) — μ-Clid preset storage. Each product defines its own preset format under its own `design-presets.md`; the family-level conventions are: per-layer preset in camelCase noun (`.muRhythm` / `.muPattern` / `.muArp` / `.muTrack`), full preset in plugin-name camelCase (`.muClid` / `.muTant` / `.muToni` / `.muOn`). A mu-On `.muTrack` belongs to the instrument it was saved from (it records `lane`), so each lane lists only its own.
- [design-future.md](design-future.md) — inter-plugin sync (μ family), MIDI CC control
