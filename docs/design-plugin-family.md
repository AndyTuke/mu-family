# μ-family Plugin Architecture

This document covers the shared-code strategy for building additional plugins in the μ-family (mu-tant, mu-toni, mu-on, and future siblings) that reuse mu-clid's voice chain, modulation system, and mixer.

**Siblings already built on this platform:** **mu-tant** (wavetable drone synth — per-voice oscillators + gater), **mu-on** (909 groove sequencer — five fixed engine lanes + a per-lane `ModulationMatrix` resolved through the shared `mu_mod::resolveLane` helper), and **mu-toni** (generative arpeggiator mono-synth — a scale/chord note pool scanned by a deterministic skewed-triangle walk into a note-triggered voice, with a per-voice `ModulationMatrix`). Each supplies only its engine(s) + sequencer/trigger model and inherits `ProcessorBase`, `EditorShellBase`, the mixer/FX rack, and the modulation system unchanged. A fifth sibling, **mu-tilate** (loop slicer: a Phrase layer is one loop, its slices, one sample player and a timeline row; designed in [mu-tilate/design.md](mu-tilate/design.md)), is built and in testing: plugin target (VST3, CLAP, Standalone), `mu-tilate-layer` library, `LayerTypeId::Phrase` in `mu-core`, 96 tests in `mu-tilate-tests`. Its shared parts live in `mu-core/Analysis/`, `mu-core/Audio/LoopLoader` and `mu-core/UI/Components/LoopWaveform`.

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

- **Identical, shared, single-source.** The shell, sidebar + layer management
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
    License/               LicenseKey.h only (product id, licence filename, embedded PUBLIC key, checkout
                           URL); the verifier, activation and prompt policy are shared in mu-core/License/.
                           Empty (.gitkeep) only for the freeware products (mu-Clid MIDI, mu-Link)
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
- `UI/LayerSidebar` + `UI/SidebarItem` — the shared left "layers" sidebar (select / add / delete / drag-reorder). Reads channel metadata from `ProcessorBase::getNumLayers/getLayerName/getLayerColourIndex`. The per-layer mini-graphic (and its animation) is the only product-specific part, injected via `createMiniVisual` (mu-clid → `RhythmMiniVisual` wrapping a `RhythmCircle`; mu-tant → a voice glyph). Reorder, pending-swap badges and cancel go through the `ProcessorBase` layer API (`swapLayers`, `hasPendingSwap`, `cancelPendingSwap`), so no product wires them (every product implements hot-swap on the shared `mu_hotswap::Stager` — see [Hot-swap](#hot-swap-staged-preset--layer-swaps-family-pattern) below). Add/delete is driven by the editor (`onAddLayer` + a panel delete button, through the shared `LayerEditController`) and lands in the central `ProcessorBase::addLayer`/`removeLayer` in every product.
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

`mu-core` (one compiled static library, `mu-core-static`; introduced as an INTERFACE library in Stage 33) holds everything shared; each product supplies only its engine + engine UI. Counts (layers, buses, inserts per bus, host sidechain) are not compile-time: each product passes a `mu_limits::ProcessorLimits` constant to `ProcessorBase` and to its parameter layout ([design-runtime-counts.md](design-runtime-counts.md), [mu-core/MuLimits.h](../mu-core/MuLimits.h)). Paths are relative to the repo root.

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
| `MasterInsertBinding` (+ `BusInsertBinding`, ruled 2026-10-20: the per-bus counterpart shares the file and panel; split it out only if a third binder appears) | `mu-core/UI/MasterInsertBinding.{h,cpp}` | **Plugin-only** (takes `ProcessorBase&` and `MasterInsertPanel&`; not on the chrome list). Writes the `mst_ins*` / `mst_ins2*` APVTS parameters from the panel's callbacks and refreshes the panel from `mixerEngine` on `MixerOverlay`'s deferred apvts-dirty flush |
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
| Analysis (onsets, tempo) | `mu-core/Analysis/OnsetDetector.{h,cpp}`, `LoopTempo.h` | `mu_core::computeCurve` / `pickOnsets` (spectral flux and SuperFlux onset detection) and `estimateTempoFromLength`. New folder `Analysis/` for offline signal analysis that is not a real-time unit; message thread only. Tests belong in `mu-core-tests` |
| Loop decode | `mu-core/Audio/LoopLoader.{h,cpp}` | `mu_core::loadLoop` / `resampleLoop`: a whole-loop file decoded at its own rate and channels, with a byte cap; blocking, never on the audio thread |
| Waveform view | `mu-core/UI/Components/LoopWaveform` | The zoomable loop waveform, built for mu-Tilate and shared |
| Voice DSP helpers (ruled 2026-11-01) | `mu-core/Audio/EqualPowerPan.h`, `TptSvf.h`, `AgeEnvelope.h` | `mu_core::equalPowerPan`, `mu_core::TptSvf` (per-sample TPT state-variable filter for per-voice cutoff modulation), `mu_core::adsAtAge`; specified in [design-naming.md](design-naming.md#mu-tilate-names-decided-2026-10-30-built-from-2026-10-31). Use them before writing a per-voice pan, filter or age envelope in a product |
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
3. **Product test targets build the real processor from a shared source list.** A console target links no product shell, so the `mu-core` `.cpp`s behind `ProcessorBase` are one list, `mu-core/cmake/ProcessorTestSources.cmake` (`MU_CORE_PROCESSOR_TEST_SOURCES`), included by `mu-core-tests` and by each product's tests (the products' plugin targets link `mu-core-lib` instead). A product adds its own `PluginProcessor*.cpp`, engine and
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
| mu-clid `EuclideanPanel`, `RhythmPanel`, `RhythmCircle`, `RhythmMiniVisual`, `VoiceSection` | Euclidean / sample-trigger engine UI (the sidebar is the shared `LayerSidebar`) |
| mu-tant `SynthVoice`, `WavetableBank` / `WavetableOscillator`, `GatePattern`, `VoicePanel`, `VoiceSidebar`, `GatingDesigner` | Wavetable-drone engine + gate UI |
| each product's `PluginProcessor : ProcessorBase`, `PluginEditor : EditorShellBase` | Product glue |

---

## Stage 33 (complete — build 368) and the first sibling

Stage 33 created the shared library: `ProcessorBase` (owning `fxChain` and `mixerEngine`), the `Layer` base struct that `Rhythm` and every product's layer derive from, and the `MuClidLookAndFeel` to `MuLookAndFeel` rename (its back-compat shim is gone). mu-Tant then added its own `PluginProcessor` and editor on top of the shared `MixerOverlay`, `FXRow`, `ModulatorEditor` and `ModMatrixPanel`. The original file lists and the mu-Tant plan are in [DevelopmentHistory.md](DevelopmentHistory.md).

**Packaging has since changed.** Stage 33 made `mu-core` a CMake INTERFACE library (sources compiled inside every consumer) because JUCE's module-initialisation macros reach a target only through `juce_add_plugin`. Since 2026-11-14 `mu-core` is **one compiled static library** (`mu-core-static`, linked through `mu-core-lib`) that links no JUCE target and reads JUCE headers through `mu-juce-usage`, while the JUCE modules are compiled once in `mu-juce` (`mu_use_shared_juce()`); see "Shared libraries: mu-core and mu-juce".

---

## Architectural rules preserved

- **Audio thread never allocates** — unchanged; `processCoreBlock` allocates nothing.
- **ModulationMatrix is the single reader** — unchanged; still the only path from ControlSequence → VoiceParams.
- **Everything in APVTS** — each plugin defines its own APVTS layout; `ProcessorBase` provides helpers for the common subset (voice params, mixer params, FX params).
- **VoiceSlots are fully self-contained** — no cross-slot modulation. Same rule as current per-rhythm constraint.
- **GPU-backed image caches are heap-allocated and never destroyed** (ruled 2026-10-12) — a function-static or global cache of `juce::Image` under the Direct2D renderer is held by a leaked `new` pointer, not a static object. A static destructor runs at plugin-DLL unload under the loader lock, where releasing the Direct3D device hangs the graphics driver (found with Debug pluginval on mu-Clid; fixed in the `MuLookAndFeel` brushed-grain cache). Clearing such a cache at a size cap is safe only on the message thread, and only if every caller uses the returned reference immediately and never keeps it.
- **State-restore thread contract** (ruled 2026-10-18) — hosts normally call `setStateInformation` on the message thread, and the editor, the Stager and the products' lock-free paths assume that. Only the layout store in `applyComposedState` (the packed layout word under `layoutWriteLock`) is also safe from a host thread. Consequences: (a) the editor polls "is this destination modulated?" through `ModulationMatrix::assignedSnapshot()` (a published, immutable copy, revisions from one process-wide counter so a `Layer::adoptDefaults` swap of matrices never looks current), never the live assignment list; a UI that needs the full list copies `getAssignments()` under the modulation lock; (b) `WavetableBank` reserves `kMaxTables` up front and never reallocates, so a lock-free `findByPath` stays valid, and an import past the cap is refused; (c) any new lock-free reader of state that a restore rewrites must follow one of these two patterns (publish a snapshot, or never move the storage), not read live data.

---

## Modulation targets — family standard

**Rule: modulation depth is a percentage of the target knob's range.** 100% depth from a full-scale modulator moves the target across its whole knob range; 3.2% moves it 3.2%. Same in every product, for every target — no per-target units and no scale factors (owner decision, backlog #1122–#1124).

**One table per product, the only place targets are defined.** Each row is a [`mu_mod::ModTarget`](../mu-core/Modulation/ModTarget.h): `id` (the name saved in presets — never change it once shipped), `label` (dropdown text), `section` (dropdown heading), `param` (the parameter it drives, without the per-channel prefix; `nullptr` = a reserved / retired slot kept so indices don't shift). New targets are **appended as one row**. The knob's own `NormalisableRange` supplies min / max / curve / step — nothing is restated elsewhere.

| Product | Table | Resolve path |
|---|---|---|
| mu-Tant | `kModDestTable` — [MuTantModDest.h](../mu-tant/Source/Modulation/MuTantModDest.h) | `mu_mod::resolveLane` |
| mu-Toni | `kModDestTable` — [MuToniModDest.h](../mu-toni/Source/Modulation/MuToniModDest.h) | `mu_mod::resolveLane` |
| mu-On | one table per lane — [MuOnModDest.h](../mu-on/Source/Modulation/MuOnModDest.h) | `mu_mod::resolveLane` |
| mu-Tilate | `kModDestTable` in `mu-tilate/Source/Modulation/MuTilateModDest.h`; targets listed in [mu-tilate/design.md](mu-tilate/design.md) | `mu_mod::resolveLane` |
| mu-Clid | `ModDest::kTable` — [ModulationDestinations.h](../mu-clid/Source/Modulation/ModulationDestinations.h) | `mu_mod::resolveLaneSeeded` (see below), using the knob ranges in [ModulationSkew.h](../mu-clid/Source/Modulation/ModulationSkew.h) — the APVTS layout is built from the same constants |

**Shared machinery (built, backlog #1771).**
- `mu_mod::ModTarget` gained two defaulted fields: `unitsPerDirection` (stepped-snap units per direction; 0 = continuous) and `steppedOnly` (row hidden while the modulator is Smooth). mu-Tant's octave rule therefore lives on the rows, not in product code.
- `mu_mod::findTarget` / `isValidTarget` have pointer + count forms; every product's validator uses them. The layer-type registry exists only in mu-Tronic, so the standalones keep their own tables behind the one function.
- `LayerTypeInfo` carries `modTargets` / `numModTargets`. `nullptr` = the type publishes none there (mu-Clid's targets depend on effect and step counts).
- [`makeTableDestProvider`](../mu-core/UI/TableModDestProvider.h) builds the destination dropdown provider from a table. Used by mu-Tant, mu-Toni, mu-On, mu-Tilate, mu-Toni MIDI and mu-Tronic's registry providers.
- `mu_mod::resolveLaneSeeded` takes the seed values from the caller; `resolveLane` is the wrapper that reads the parameter atomics. mu-Clid uses the seeded form: it seeds from the rhythm's own fields, so a staged preset applied at a boundary cannot play a block of old values. The Euclid ranges are set per block, and skewed knobs keep their exact curves through `NormalisableRange` lambdas.

**Per-product remainder (family consistency).** mu-Clid keeps its own destination provider, for the effect-send label; it is the only product that does not use `makeTableDestProvider`.

**Coverage rule (owner, 2026-11-02): every continuous layer parameter is a target.** A product ships its target table complete from the start: pitch, filter, envelopes, pan, level, sends, insert knobs and every product-specific knob. Not targets: choice, toggle and integer rows (mode, grid, filter type, insert algorithm) and settings that are not read per block (e.g. mu-Tilate's detector settings). Each exemption is written in the product's test next to its reason, and `mu_mod::checks::unlistedContinuousParams` fails the test for any continuous layer parameter that is neither a target nor exempt. A new parameter therefore brings its row, or an explained exemption, in the same change. The knob's modulation arc comes from the same table (the shared knob binds by name), so no extra UI step.

**How it works.** Each target is seeded into the `ModulationMatrix` as its knob's 0..1 proportion; the matrix adds `source% × depth% / 10000`; the product converts back through the knob's range (clamped at the ends; stepped targets rounded to whole steps by the product). Offset-style targets (mu-Clid's pitch octave / semitones) are seeded 0 and read back as an offset of the same proportion of the knob's range. (Details of the shared resolve helpers follow the table.)

**Preset data.** `serialiseModulators` writes `depthUnits="range"` on every `<Modulators>` tree. Data without it predates the standard; mu-Clid upgrades it on load (Pre / Post Pad depth was % of 12 steps → rescaled to % of 0..63, so old presets move the same number of steps). No other target's old units differed.

**Enforced by:**
- [`mu_mod::checks`](../mu-core/Modulation/ModTargetChecks.h) in each product's unit tests — every row names a real parameter; 10% depth moves each target exactly 10% of its range; every continuous layer parameter is a target or an explained exemption (`unlistedContinuousParams`; mu-Tilate runs it, the other products adopt it as their tables are completed).
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

1. **Capacity is fixed per processor, the count is runtime.** Parameters, mixer channels and the `LayerLayout` are
   declared once, at construction, for the product's own `getLimits().layers` (`mu_limits::ProcessorLimits`, [design-runtime-counts.md](design-runtime-counts.md): 8 for most products, 16 for mu-Tronic, 1 for mu-Host, never above `kAbsoluteMaxLayers` 32). Adding a layer *activates* slot `n`;
   it never allocates parameters.
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
3. **`ProcessorBase` owns the layers** (the storage lives in its slim parent `ProcessorCore`, so it is unit-testable; see the platform table): `mu_core::FlatArray<std::unique_ptr<Layer>> layers` (sized at construction to `getLimits().layers`),
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
   **Reorder semantics follow the host kind (ruled 2026-10-21, T1c review).** A *list* host (`layersAreSlots()` false:
   mu-Clid, mu-Tant, mu-Toni, mu-On) moves a layer by a chain of adjacent `swapLayers`, so the layers in between shift:
   that is what a list means. A *slot* host (`layersAreSlots()` true: mu-Tronic) has fixed slots that are the
   Launchpad rows and the player's muscle memory, so a drag exchanges **exactly two slots** with one `swapLayers(src, dst)`
   (an Empty target takes the layer, the layer's slot becomes Empty) and nothing in between moves. The branch is in the one
   shared `LayerSidebar::commitDrag` and is keyed on `layersAreSlots()`; the list branch is not changed. One call is also
   atomic: a swap is all or nothing, so a timed-out quiesce (design 2.1.2) can never leave a half-rotated chain.
   `swapLayers` returning false leaves every slot as it was; the sidebar then cancels the drag and keeps the selection.
   **A user-initiated edit is not retried**: the grace is 50 ms and a miss means the audio thread is stuck in a very long block;
   a queued reorder applied later would surprise the player. Only edits the system queues itself (bin service, a staged
   commit) retry on the next deferred-work pass.
   **Hook naming:** the "let go of this layer" callback is `onLayerLeaving(slot)` (was `onLayerTypeChanging`): called on the
   message thread, outside every lock, for a slot whose layer is about to be retired (`setLayerType`) or moved
   (a Published `swapLayers`, for both slots). It is a single hook for both because the editor's obligation is the same
   (drop every pointer into that layer). List hosts' TryLock swap does not call it and need not.
   **A layer's slot-tied state follows the layer through `Layer::onMoved(newIndex)`** (and `bindParams` for the cached
   atomics): a noise seed derived from the slot is re-derived there with the same function the creation path uses, as the list
   hosts' `layersMoved` already does for their engines, so two layers never share a seed after a delete and a create, and a
   reset slot still equals a fresh one of its index. Never a product-level `layersMoved` override in a slot host: that would
   switch on layer type in the processor (check-layer-boundary rule 10).
   **Dormant parameter rows (accepted):** after a swap across type letters (Arp `a3_` with Kick `k5_`) the vacated letter's
   rows keep their old values. They are not part of any layer's state, are never saved, and are reset by `setLayerType`
   before any layer of that letter lands there. Automation lanes on a row address the slot, not the layer, so a lane that was on
   a layer's old slot drives whatever sits there afterwards, as after a delete and re-create.
   **Row listeners after a swap (accepted):** a service that listens to a type's rows (`RhythmSlotServices`) applies a row
   write, queued or immediate, to whatever layer of that type now sits in the slot, reading the parameter's current value. The
   swap moves the rows with the layers, so that value is the layer's own and the apply is redundant but harmless. Condition:
   the row writes happen inside the quiesced window, so the dsp-controller checks the window's length with two Rhythm layers
   swapped; if it is long, the writes move after the flag is lowered.
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

### Hooks a hosted type may need beyond creation (ruled 2026-10-20, the Rhythm type in mu-Tronic)

- **`LayerTypeInfo::presetFileToNode` / `presetNodeToFile`: accepted** as the one way for a type whose layer-preset file is not the composed node (mu-Clid's `.muRhythm` keeps `r0_` properties). Pure, message thread, null for every other type, called only inside `loadTypedLayerPreset` / `saveTypedLayerPreset`; the converted node keeps the file's root tag so the type test still reads it. The converters live with the type's description (`mu_clid::rhythmLayerTypeInfo()` in `Sequencer/RhythmType.h`) and reuse the product's own reader, never a second parser. Do not give the host a different file shape or tag: a `.muRhythm` saved by mu-Clid must load in a mu-Tronic slot and the reverse. mu-Clid MIDI shares only the extension; it neither reads nor writes through this path.
- **`LayerServices::deferredWork()` and `mixIntoMaster()`: accepted**, both default to nothing. `deferredWork` runs on the message thread in the deferred-work pass `ProcessorBase::requestLayerServicesWork()` asks for (any thread, allocation-free). `mixIntoMaster` runs on the audio thread once per mixer chunk, never more frames than the prepared size, no allocation, no lock, through `ProcessorCore::audioServices` (a slot written once, release/acquire, never cleared). A services object stays opaque to the base: it may name `ProcessorBase`, never a product.
- **Parameter sync for a type whose layer struct is the truth** (the Rhythm; the allowed difference in step 5 above): the type's services listen to its rows and apply a changed row to the layer on the message thread, queuing from other threads for `deferredWork`. This is the shape for such a type only. A type whose layer reads its rows on the audio thread (Arp, Track, Pattern) keeps `bindParams`; do not move the Rhythm onto `bindParams` (its pattern rebuild allocates) and do not copy the listener shape into a type that does not need it.
- **`stageLayerPresetTree` calls `Layer::preloadStaged`** (message thread, before staging): the contract in the table above, now actually run. A layer keeps what it decoded for the commit at the boundary and keys it to the staged node; the commit does no file work.
- **Swap boundary in a host with no master loop** (mu-Tronic): the layer's own `loopBeats()` (the pattern's length), as Arp, Track and Pattern. A Rhythm that clocks itself from `RenderContext` is therefore not on mu-Clid's master loop, and that is by design; mu-Clid's own behaviour is unchanged.

### Why virtual and not a table

A table of function pointers re-implements the vtable and still needs a base type to hold the shared
data. The hooks are all per-layer, so the call cost is nothing. What a vtable cannot do is create a
layer from a name read out of a file and list the types in a menu, and that is all the registry is for.
It stays out until the combined instance needs it, so there is exactly one place (the base class) where a layer's
behaviour is described. Typed access by `static_cast` after a `typeId` assert keeps product code as
readable as `getRhythm(i)` is today.

### Stages (all built)

The staged plan for layer ownership is complete: derived types as value members, `getLayer`, the persistence virtuals, ownership in `ProcessorBase`, the central edits and the generic `LayerSwapper` (stages 1 to 5) in every product; variable layers in mu-Toni and mu-On and the clip bank (stage 6); mu-Clid on the container (stage 7, below); the registry and `Layer::render` (stage 8), which arrived with mu-Tronic. The stage table, the owner decisions for stage 6, the order of work and the close-out of the hoisting umbrellas are in [DevelopmentHistory.md](DevelopmentHistory.md). The rules they produced are the contract above and the rulings that follow.

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
- **mu-On saved data (extends #1263):** each layer's block is `t{N}_eng` (choice: Kick, Bass, Hat, Snare, Rumble, and from 2026-10-21 FM Perc;
  the existing engines, Hat and Snare being the sample engine with different defaults), `t{N}_src` (Rumble
  source, a layer index), then every engine's parameters **engine-qualified**: `t{N}_kick_*`, `t{N}_bass_*`,
  `t{N}_hat_*`, `t{N}_snare_*`, `t{N}_rumble_*`, `t{N}_fmp_*` (the unqualified names collide: both Kick and Bass have `tune` and
  `drive`, Bass and the samplers `dec`). Legacy migration, through the existing mechanism: old `t{i}_x` becomes
  `t{i}_<engine of lane i>_x`, `t{i}_eng` = i, `numLayers` = 5, `t{i}_src` = 0 for the Rumble layer, layer names
  Kick / Bass / Hat / Snare / Rumble. The mixer sidechain values are carried as saved (they are mixer params); the Bass
  Duck / D.Atk / D.Rel knobs and the pre-wired default go (owner 2026-10-08). Layer-order dependent state remaps on every
  structural edit like the mixer sidechain source: `t{N}_src` follows its layer.
- **mu-On step data (as built, 2026-10-12):** one row of 16 (on / accent) per slot (`getLimits().layers`), kept when the
  engine changes (a Rumble layer ignores its row). Ruling amended: the row (`StepRow`) and the Rumble envelope are members
  of `Track`, not processor arrays, because they are data the layer owns and must move with it ("Slot-keyed audio state
  stays processor-side" is only for state that must outlive the layer or keep a stable address); `StepPattern` holds
  per-slot pointers to the rows, rebound under `layersLock` in `layersMoved`, and the envelope persists only when non-default.
  `GrooveVoices` owns one engine of each type per slot (or lazily, DSP's call) and renders the selected `eng`.
  **Render order: all non-Rumble layers first, then Rumble layers**, so a Rumble source is always rendered this
  block regardless of layer order (to `dsp-controller` for review); a Rumble whose source is itself a Rumble or is out of range
  processes silence.
- **mu-On FM Perc engine (ruled 2026-10-21; Andy approved; names in [design-naming.md](design-naming.md), sound design in [mu-on/research-fm-drums.md](mu-on/research-fm-drums.md)).**
  A sixth Track engine, one engine with an **Algorithm** choice of eight voice topologies (Tom, Conga, Zap, Click, Clank, Wood, Pluck, Burst) over one operator core.
  - *Shape.* `EngineType::FmPerc` appended after `Rumble` (value 5); `TrackEngine` gains a `FmPercEngine` arm; `LayerTypeId::FmPerc` between `Rumble` and `Test`;
    rows `t{N}_fmp_*` appended to `addLayerParams` **after the existing last row (`accent`)**, never inserted among the engines (host parameter order is saved-data-adjacent); the
    `t{N}_eng` choice gains a 6th item. Eleven rows: `algo` (choice, default Tom, not a target, like Bass `wave`) and ten floats. The ten floats are
    the modulation table `kFmPercDests` in exactly the order `FmPercEngine::setParams` takes them: tune, ratio, index, idec, fdbk, dive, dtime, decay, fold, noise.
    `TrackEngine::setParams`' `bassWave` argument becomes a generic `engineChoice` (the Bass wave, or the FM Perc algorithm; `Track` reads the right row per engine).
  - *One fixed set of ten knobs for all eight algorithms* (Andy). Rule that makes it work: **every algorithm gives every knob an audible meaning of the same kind** (Tune = pitch,
    Ratio = modulator ratio or inharmonic spread, Index = FM amount, FM Dec = index-envelope time, Fdbk = feedback, Dive = pitch sweep depth, D.Time = sweep time,
    Decay = amp time, Fold = wavefolder, Noise = noise level). No algorithm hides or relabels a knob; a knob that an algorithm barely needs still has an effect.
  - *Defaults per algorithm, with one APVTS default per parameter.* **Tune and Ratio are absolute** (Hz, and a ratio): they are musical quantities, a tom set is tuned in notes. **The other
    eight floats are normalised 0..1 amounts** that each algorithm scales through its own constant table (`kAlgos[8]` inside `FmPercEngine.h`: ranges and curves in ms, rad, x-dive);
    so the same knob position is a musical value in every algorithm, modulation depth (percent of range, family rule) means the same musical amount in each, and the single APVTS default
    is the Tom's. **Changing the Algorithm never writes another parameter** (no hidden APVTS writes: automation, modulation and undo stay honest; same as switching a track's engine keeping the others' settings).
    Each algorithm's starting set is a shipped **factory `.muTrack` preset** (the paper's tables), and the user loads it from the layer-preset menu. The algorithm is **latched at the trigger**: a hit
    keeps the algorithm it started with, so a change (or an automation lane on `algo`) never clicks or cuts a ringing tail; the next hit uses the new one.
  - *Where the helpers live.* Operators, the pitch-dive and index envelopes, the fold and the sine table stay inside mu-On (`Audio/FmPercEngine.h`, plus `FmPercCore.h` only if the file passes
    about 400 lines) until a second product needs them; then the generic part (operator, oversampler wrapper) moves to `mu-core/Audio/` before the second copy. `ExpDecay`, `MultiModeFilter`,
    `NoiseSeed` and `OversampledProcessor` are the existing mu-core pieces; none is copied. No new mu-core API is needed for the first build.
  - *Saved format.* Additive only: a sixth choice item, eleven new rows per track slot, nothing renamed or reordered. Older projects and presets load unchanged (engine indices 0-4 keep their meaning).
    A project saved with a FM Perc track and opened in an older build clamps the unknown engine; that direction is not supported. The legacy five-lane migration (`kNumLegacyLanes` = 5) is untouched.
    Cost: 11 more host-visible parameters per slot (mu-On with 8 slots: 88; mu-Tronic standalone: 176).
  - *Cost of holding the engine.* An unlocked mu-On track holds every engine (the sixth too), so `FmPercEngine::prepare` allocates only small fixed buffers and an idle voice returns early. The 2x
    oversampling for Zap, Clank and Burst is `dsp-controller`'s call at the DSP review (use `OversampledProcessor` if it fits a per-voice pattern, else a small half-band); it must not
    allocate outside `prepare`.
  - *Interaction with mu-Tronic.* The engine is just a sixth Track type: `trackLayerType(LayerTypeId::FmPerc)` registered after `Rumble`, one line in the chooser order, `hasClipBank` true, `takesNotes` false,
    a valid Rumble source (a Rumble on a tom is legal; `isRumbleSourceSlot` already excludes only Empty and Rumble). Per-step pitch does not exist (design-engine); tuned toms use separate tracks or a modulator on Tune.

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

### mu-Clid on the shared container (stage 7; built, then amended by the layer extraction)

Built (build 1407); the plan, steps and risk list are in [DevelopmentHistory.md](DevelopmentHistory.md). Stage 8 (registry and `Layer::render`) was answered 2026-10-15 by mu-Tronic, below. What stands:

- mu-Clid uses the shared container: `mu_core::Rhythm` layers, `keepSpareLayers`, `layersLock`, `LayerEditController`; `RhythmManager`, `numActiveRhythms` and `rhythmsLock` are gone. Since the layer extraction (2026-10-19) `mu_clid::SampleRhythm : mu_core::Rhythm` owns its `VoiceEngine`, `MidiOutputEngine`, play state, modulation snapshot, sample path and retire tail (it renders through `renderAndMix`; retired engines sound out as `mu_clid::EngineTail` in the shared `RetireBin`), so the earlier "engines stay processor-side" ruling no longer applies to it. `RhythmSequencer` stays in `mu-core` as the shared pattern bookkeeping and **binds `Rhythm*` per slot** (`bindRhythm`); `removeSlotState` / `swapSlotState` move its per-slot state while the pointer moves belong to `ProcessorCore`.
- Two `ProcessorBase` hooks exist for it, with defaults that leave the other products untouched: `virtual void layersReordered(LayerEdit kind, int a, int b)` (`LayerEdit { Remove, Swap }`), called under `layersLock` immediately before `layersMoved`, carrying whether the data at a slot moved from elsewhere; and `virtual bool layerHoldsParamTruth() const` (default false) with `virtual void syncSlotParams(int slot)`: when true the base skips the APVTS copy of an edit and calls `syncSlotParams` for every slot it touched, on the message thread, still suspended, not under `layersLock` (the one allowed difference of contract item 6.5). `Rhythm::adoptDefaults(Layer&)` exchanges the Euclidean data so `resetLayer` resets a whole rhythm; `createLayer(slot)` returns a default `Rhythm` and `layerAdded(index)` applies `_default.muRhythm`.
- mu-Clid does not call `captureComposedState` / `applyComposedState`, so its saved data (`r0_` prefixes, `PresetIO` shapes, `MuClidState`, `.muClid`, `.muRhythm`) does not change and it does not save `numVoices` / `voiceColours`.
- **`HotSwapStager` stays on `mu_hotswap::Stager`** (decided 2026-10-13; not moved onto `layerSwapper`): the shared behaviour is already shared through `Stager`, mu-Clid's payload is a prebuilt `Rhythm` + `VoiceEngine` + retire tails, and forcing it through the `ValueTree` driver would only move code and risk the timing that `SwapCommitTimingTests` pins. Revisit only if a second product needs engine-carrying payloads (generalise `LayerSwapper` first).
- The MIDI product owns its one `Rhythm` as a member of `MidiRhythmCore`, binds it to slot 0, does not adopt the layer container, and keeps its own `rhythmsLock`.
- Rules that outlive the plan: on a failed try-lock the block renders silence but still advances the transport and boundary detection (hot-swap principle 4); CLAP does not honour `suspendProcessing`, so `layersLock` plus `layerEditInFlight` is the barrier and the audio path tests both; the locked section moves pointers and engines but never constructs or frees one (parked engines are released after the unlock); anything that held a `Rhythm*` / `ControlSequence*` across an edit is unbound first (the `LayerEditController` unbind hook). `mu-clid-tests` and `mu-clid-midi-tests` are built on demand by target name, so a green full build proves nothing about them.

### mu-Tronic: the combined instance is scheduled (ruled 2026-10-15)

**Supersedes the "park" recommendation of Stage 8 above.** Andy asked for mu-Tronic: a fifth product with 16 empty layer slots (paged 1-8 / 9-16), each
able to hold a layer of any of the four existing types, on the shared mixer, FX and modulation, with buses in the mixer. The full ruling, staged plan
(T0 to T9) and the short list still open are in [mu-tronic/design.md](mu-tronic/design.md). **Andy's answers, 2026-10-15:** a fifth product; **Standalone only** (no
VST3 or CLAP, so no host parameter-list constraint); **freeware** (no licence key, no Support prompt); name mu-Tronic, code `MTrn`; **3 buses with 2 inserts
each**; every mu-On engine (Kick, Bass, Hat, Snare, Rumble) is its own layer type; scenes; mu-link left as it is; all four extractions (mu-Toni, mu-On, mu-Tant, mu-Clid) come before any mu-Tronic product work
(re-prioritised by Andy later the same day; plan: [mu-tronic/extraction-plan.md](mu-tronic/extraction-plan.md)). The structural rulings that bind other work:

- **Layer = voice.** The code and docs keep *layer*; *voice* stays the polyphonic voice inside an engine. A layer is engine + sequencer + modulation + insert + parameters +
  presets / clips + UI content; the mixer strip and the transport are not part of it.
- **`Layer::render` with a per-block `RenderContext`** is the contract (Stage 8 option C), plus a registry (`LayerTypeInfo`) that only creates and describes. The engine
  lives in the layer object; a replaced layer fades in a shared `RetireBin`. This amends Stage 7 ruling 1 for hosts that render through `Layer::render`.
- **Per-product layer libraries, nothing moves into `mu-core`:** `mu-clid-layer`, `mu-tant-layer`, `mu-toni-layer`, `mu-on-layer` (INTERFACE) are each product's
  `Audio/ Sequencer/ Modulation/ Persistence/ UI/` minus `UI/SettingsOverlay.*`. A library file includes nothing under `Plugin/` or `License/`, takes no
  `PluginProcessor&` (the UI uses a `<Type>Host` interface), and every relative path is unique across `mu-core` and the libraries
  (`tests/scripts/check-layer-boundary.ps1`, to be written in Stage T0). Each product's parameter table is `addLayerParams(layout, slot)` in its `Persistence/`.
- **Each standalone product moves onto the shared render path** in the same stage in which its layer is extracted, so per-block glue exists once.
- **Capacity:** a product's counts are its own `mu_limits::ProcessorLimits` constant (`kLimits`: layers, buses, inserts per bus, host sidechain), given to `ProcessorBase` and to the parameter layout when the processor is built; mu-core is ONE compiled library and reads no per-product setting (`check-core-boundary.ps1` forbids the old `MU_MAX_*` macros). Clid/Tant/Toni/On/Tilate 8 layers, mu-Tronic 16 with 3 buses, mu-Host 1. Fixed ceilings (`kAbsoluteMaxLayers` 32, buses and inserts 9) bound the few fixed arrays; layer-library code shared with the hosts sizes to the ceiling.
- **Buses:** `MixerEngine::BusState` (a `ChannelState` plus a chain of `kBusInserts` layer inserts), opt-in counts `ProcessorLimits::buses` (0 except mu-Tronic's 3) and
  `busInserts` (2), `ChannelState::busAssign`,
  graph channels -> buses -> returns -> master, no bus-to-bus, a channel's sidechain source is never a bus. The existing `outputBus` is the DAW direct out and is relabelled
  "Out" in the UI (not offered in mu-Tronic). All of it is `mu-core`.
- **Layer types are finer than products:** the registry lists each mu-On engine (Kick, Bass, Hat, Snare, Rumble, and from 2026-10-21 FM Perc) as its own `LayerTypeId` over the one `Track` class
  (engine locked in mu-Tronic); `LayerTypeInfo::group` names the product for the chooser and `typeFromPreset` picks the type from a preset file. Hence mu-On goes
  per-layer (engine, step grid, swing/accent) in its own standalone too.
- **Scenes:** `ProcessorBase::launchScene(n)` and `ControlAction::LaunchScene(n)` launch clip n on every layer in one staged batch; with `PagePrev` / `PageNext` they
  are generic actions in `mu-core/Control/`.
- **Product shape:** Standalone-only, freeware product: `FORMATS Standalone`, an empty `License/` (`.gitkeep`), no `LicenseKey.h`. Plugin code `MTrn` (JUCE needs an
  upper-case letter).
- **Static libraries, licence follows the install (Andy, 2026-10-15):** the layer libraries are compiled into mu-Tronic; engines are **not** loaded at runtime from the installed
  apps and need not update independently (releases are suite-wide; mu-Tronic is rebuilt and released whenever an app's engine changes). Each app writes a small manifest
  (supporter flag, checkout URL, content dir) at install and at every launch; mu-Tronic carries no key code. Three E1 constraints keep a module ABI possible at no cost:
  `RenderContext` mirrors a C struct, layer state is text, `render` makes no host callback. Analysis: [mu-tronic/extraction-plan.md](mu-tronic/extraction-plan.md) sections 8 and 9.
- **mu-link:** coexists, no freeze, revisit later (Andy: "leave it as is for now"); mu-Tronic is a mu-link client; no clock / MIDI-out code is duplicated.
- **Timing:** all four extractions are scheduled before mu-Tronic product work, in the order Toni, On, Tant, Clid, behind a golden-render / compat / equivalence safety net ([mu-tronic/extraction-plan.md](mu-tronic/extraction-plan.md)).

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

**Migration** (tokens and the two `ProcessorBase` changes, the sidebar rename and subclass removal, `LayerPanel` adopted in mu-Toni, mu-Tant and mu-Clid, mu-On sidebar only until it adopted `LayerPanel` with its per-layer engine selector) is done in every product; the order and the design-future check are in [DevelopmentHistory.md](DevelopmentHistory.md). Nothing here closes off the controller work (`mu-control` reads the same `ProcessorBase` layer API) or the standard UI sizing (the three tokens are that standard).

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
| `mu_ui::ShellChrome` | `mu-core/UI/ShellChrome.{h,cpp}` | The processor-free half of `EditorShellBase`, as a **helper object, not a base class**: constructed with the host `juce::Component&` and a `ShellBackend&`, it owns and parents to the host the `MuLookAndFeel`, `TransportBar`, `StatusBar`, `AboutOverlay`, `SaveDialog`, `PresetBrowser`, the mixer / settings overlay slots and the upgrade banner; it holds the overlay state machine (`hideAllOverlays`, `fadeSwitch`, `show*`), the layout (transport / optional sidebar / main / status), `setMetalStyle`, `setScrews`, `forwardKnobStatus`, the update check, the play / stop key, and the preset flow (load, save with overwrite confirm, new with discard confirm, dirty flag). |
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
`MidiFullPresetsOverlay`, `MixerOverlay`, `LayerSidebar`, `LayerPresetHeader`, `ParamKnobGrid`, the APVTS binding half of `MixerChannel`
(`MixerChannel_Bindings.cpp`), `MasterInsertBinding` (the master-insert binder; `MixerChannel_Insert.cpp` was deleted
2026-10-12), `InsertSubsection`. **Rule:** a file in the
shared chrome list below never includes `Plugin/ProcessorBase.h`, `Plugin/ProcessorCore.h` or `UI/EditorShellBase.h`
(checked by `check-core-boundary.ps1`).

**The chrome source list: one definition.** `mu-core/UIChromeSources.cmake` sets `MU_CORE_CHROME_SOURCES` (absolute paths to
the `.cpp` files that need no processor: `MuLookAndFeel`, `MuTheme`, the components, `TransportBar`, `StandardSettingsOverlay`,
`SettingsOverlayBase`, `ShellChrome`, `AboutOverlay`, `SaveDialog`, `PresetBrowser`, `ModalDialog`, `MixerChannel.cpp`,
`InsertProcessor`, and so on). `mu-core/CMakeLists.txt` includes it into the source list compiled into `mu-core-static`, and `mu-link` and `mu-core-tests` add the same variable to their own targets. This keeps "mu-link does not link the
compiled mu-core library" true without a hand-kept second list that drifts. mu-link adds only what is genuinely its own.

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
| S3 | `ShellChrome` part A: look and feel, bar, status bar, About, Save, browser, slots, overlay state machine, layout, banner, update check; `EditorShellBase` delegates | same children, same parent, same order |
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
It lives next to `MidiClockTempo.h` and, like that file, it is **header-only and not in mu-core's
source list** (so not in `mu-core-static`). That is required, not just tidy: mu-link includes mu-core headers without linking the
compiled library, and plugin builds that don't include the header don't get its thread or
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
  app, and `mu-control`'s drivers. Nothing in mu-core's source list, and never `ProcessorBase`,
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

**7. Several apps, one pad (ruled 2026-10-14, replaces the earlier "hub through mu-link" note).** Ruled in
[Several apps, one pad](#several-apps-one-pad-ruled-2026-10-14): a shared-file focus registry between the apps, no hub process; the pad is owned by one app
at a time and top buttons 1 and 2 hand it over. Hardware finding (design-launchpad 3.5): on this machine two processes opened the same port pair at once, so
the exclusivity claim is not assumed either way; the registry works with both. A mixed-engine rig's shared master loop and clock stay with mu-link.

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
the stager's index. New names: `mu_core::ClipBank` (the store), `mu_limits::kClipsPerLayer = 8` (in `MuLimits.h`), XML tags
`Clips` / `Clip`, actions `LaunchClip` / `ClipNext` / `ClipPrev` (already declared, `ControlAction.h`), and in the UI a `ClipStrip` (for the
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
- **As built (C2, 2026-10-14).** `addLayer`, `removeLayer` (the vacated top slot) and `resetLayer` clear `clips` and `activeClip` after the lock block. mu-On
  overrides `resetLayer` (a sound reset that keeps the engine, step row and envelope, so it cannot use `createLayer` + `adoptDefaults`) and so cannot reach the
  shared clear. **Ruling: one protected non-virtual `ProcessorBase::clearLayerClips(int layer)` (clears the bank and sets `activeClip = -1`, null-safe)**, called by the three
  base functions and by the mu-On override; the four hand-written copies of the two lines go. Any later product override of `resetLayer` calls it too. No new hook:
  a virtual would only move the same call.
- **`Layer::activeClip` (as built).** A plain `int` on `Layer` (-1 = none), written by `captureComposedState` as an optional `activeClip` property of the `<Slot>` node and read back
  by `applyComposedState` (an empty or out-of-range clip loads as -1). It is a **hand-off to C5, not the live truth**: nothing sets it before C5. At C5 `ProcessorBase` writes
  `LiveState`'s `playingClip` into it at each launch commit (and `-1` at every non-clip `stageLayerPresetTree`, the choke point below), so a host session saved mid-performance
  restores the lit pad. Because a layer-preset load in the composed products goes through `applyLayerNode`, which does not touch the `Layer`, the plain member is **not** reset by
  a load by itself; the choke point must do it. (Corrected 2026-10-14 at C4: mu-Clid's `installRhythm` does **not** reset it either; it carries the slot's value across the `Rhythm`
swap, because a clip launch goes through the same install and must keep it. See "As built (C4)".) A **full preset** commit restores the saved `activeClip` into the layer but
  `LiveState` still reports `playingClip = none` (item 6), so the pad is dark until a launch; C5 decides whether a session restore (only) re-lights it, default: no.

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
  (`Layer::activeClip` is the saved shadow of this, see item 3; it never drives the lamps by itself.)
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
  As built (C2) the `<Slot>` node also gains an optional `activeClip="n"` property, written only when a clip is active; it is part of the same **Andy 1** commitment.
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
| C6 | UX | `ClipStrip` in the layer header (built and reviewed 2026-10-14), store / clear commands, wording; then the `mu-control` grid mapping (performance step 4) | `ux-controller` ruling first; the grid is the controller tests of #1276 |
| C7 | S | Clipboard (item 10): `ClipClipboard`, `copyClip` / `cutClip` / `pasteClip` / `canPasteClip`, the shared `clipFitsSlot` check, `CopyClip` / `CutClip` / `PasteClip` actions in `perform`; no UI | `mu-core-tests` with the fake product and the typed fake: copy leaves the bank, cut = copy + clear, paste type check (wrong root tag, wrong slot type, empty clipboard refuse and change nothing), paste deep-copies (a second paste after editing the source is unchanged), paste onto the lit clip relaunches it, Performance mode and a staged full preset refuse, the clipboard survives the source layer's removal |
| C8 | M | Auto-save (item 10): per-layer tracker, 400 ms poll from `SwapPoll`, flush before a clip stage, before a structural edit and before a state capture, the `clipAutoSave` setting; `dsp-controller` reviews | `mu-core-tests`: edit then settle writes the playing clip and only it; an unchanged layer is never written (capture idempotence, **run for every product's real capture: Tant, Toni, On, Clid, each Tronic type**); a launch does not rewrite the clip it loaded (baseline after migration); edits during a queued launch land in the old clip; explicit Store while a preset is staged is not overwritten by the tick; structural edit, type change, Clear, Performance mode, setting off and offline render write nothing; flush on save puts a late edit into the saved bank |
| C9 | UX | Right-click menu on a pad (Copy, Cut, Paste, Clear; enabled states), the "Auto-save clip edits" setting row, the edited-clip cue | `ux-controller` ruling first (wording, enabled states, whether Shift+click Store stays); `ClipStripTests` with the recording sender |

**C3 brief (confirmed 2026-10-14, for the coder).** Reading the three composed products' `saveLayerPreset` / `loadLayerPreset`, the split in item 5 is exact; fixed details:
- `ProcessorBase` virtuals (default empty): `juce::ValueTree captureLayerPresetTree(int layer)` and `void stageLayerPresetTree(int layer, juce::ValueTree tree, bool keepIdentity)`.
  Non-virtual, message thread: `storeClip`, `clearClip`, `loadClipFromFile`, `launchClip`, `uint8_t clipMask(int) const`, and a private `publishClipMasks()` stub until C5. All range-check the layer
  against `numLayers` and the clip against `kClipsPerLayer`. `loadClipFromFile` parses the file, requires the root tag to equal `captureLayerPresetTree(layer).getType()` (so a mu-Tant clip
  never holds a `.muArp`), and stores the parsed tree unmigrated (migration runs at launch, on a copy).
- Per product `captureLayerPresetTree` is `captureLayerNode(layer, tag)`; mu-On adds `node.setProperty("engine", getLayerName(layer), nullptr)` (so a clip carries its engine, as the file does).
  `saveLayerPreset` shrinks to `write(captureLayerPresetTree(...))`.
- Per product `stageLayerPresetTree` is everything after the parse: Tant `migrateXModVoice` + `preloadWavetablesFromVoiceTree` + `useSlot`; Toni `useSlot`; On the legacy
  `hasEngineRow` / `qualifyLegacyNode` step + `useSlot`. `loadLayerPreset` becomes tag check, `ValueTree::fromXml`, `stageLayerPresetTree(slot, tree, false)`. The commit callbacks
  (`applyVoicePresetTree`, `applyLayerTree`, `applyTrackTree`) and `onLayerPresetCommitted` stay as they are.
- `keepIdentity`: in the composed products a layer node carries no name or colour, so the flag is accepted and ignored (`juce::ignoreUnused`); it exists for mu-Clid in C4, whose
  `MuClidRhythm` file carries them. Do not add per-product branching for it in C3.
- **One choke point.** The base-class non-virtual wrapper does the shared work, so no product repeats it: `loadLayerPreset` callers go through the virtual as today, but `launchClip` and every
  non-clip stage first call a private `noteNonClipStage(layer)` which sets `layers[layer]->activeClip = -1` (and at C5 clears the `LiveState` queued/playing clip). To keep it in one place the products'
  `loadLayerPreset` end in `stageLayerPresetTree`, and `ProcessorBase` exposes `final`-style non-virtual `stageLayerPreset(layer, tree, keepIdentity, isClip)` that calls `noteNonClipStage` when
  `!isClip`, then the virtual. Callers (`loadLayerPreset`, `launchClip`, `applyMidiPresetLayer`) use the wrapper, never the virtual directly.
- `launchClip`: `stageLayerPreset(layer, bank.get(i).createCopy(), true, true)` after the checks in item 4; it must not touch `clips` (test: bank identical before and after).
- `LayerSwapper::slotStageSerial(i)` is **C5, not C3**: add it with `LiveState`, in `HotSwap.h` `HotSwapStager` beside `fullSerial` (per-slot array of `uint32_t`, bumped by `stage` and `cancel`, never by `consume`),
  surfaced as `HotSwapper::slotSerial(i)`. C3 has nothing to compare it with.
- Tests are those in the table, plus: after a non-clip layer-preset load `activeClip` is -1; after `launchClip` (stopped) the bank is unchanged; `loadClipFromFile` rejects a wrong tag and an
  unreadable file (`onLoadError` raised, bank unchanged); a `.muPattern` stored via `loadClipFromFile` then launched equals loading the file directly (state compare).

**As built (C3, 2026-10-14).** The brief stands as written; the rulings on the three points the build raised:
- `captureLayerPresetTree` is **public** (not protected like `stageLayerPresetTree`): it is a read-only capture of what `saveLayerPreset` writes, `storeClip` and `loadClipFromFile` (tag
  check) are base callers, and tests and the UI may want the tree. Writes stay behind the base's `stageLayerPreset` wrapper, so exposing the read costs nothing.
- `clearClip` also sets `activeClip = -1` when it clears the clip `activeClip` names (a cleared clip cannot be the remembered one). C5 extends the same place to the live playing clip.
- `loadClipFromFile` stores an **empty name** (the file name is not a clip name); the clip name is for `storeClip` and the C6 UI to set.
- `launchClip` leaves `activeClip` untouched (it is the clip path, so the non-clip choke point is skipped); setting it at the commit is C5's job. Products declare the override with a
  `protected:` / `public:` pair beside the other layer-preset declarations; mu-Clid and mu-Clid MIDI are untouched until C4.

**As built (C4, 2026-10-14).** mu-Clid follows the brief; rulings on what the build settled:
- **Hooks over `PresetIO`.** `buildRhythmPresetTree(index, embedSample, forFile, ...)` is the one body: `saveRhythmPresetToFile` calls it with `forFile` true (metadata, `r0_name`, `r0_colour`),
  `captureRhythmPresetTree` with false (a clip: no metadata, no identity, never audio). `PluginProcessor` overrides `captureLayerPresetTree` (public) and `stageLayerPresetTree` (protected) to call
  `captureRhythmPresetTree` / `stageRhythmPresetTree`. Every `.muRhythm` load (`loadLayerPreset`, `stageRhythmPreset`, the MIDI program change, the default-rhythm reset) reaches
  `ProcessorBase::stageLayerPreset(..., isClip false)`, the one choke point; the direct `PresetIO::applyRhythmPreset` (no staging) clears `activeClip` itself. The shared tail is
  `stageRhythmPresetTree` (stopped: `applyRhythmPresetTree`; playing: `prepareRhythmPresetTree` + `hotSwapStager.stage`). `loadSource` carries the file name into load messages for a file load only.
- **`activeClip` and `installRhythm` (ruling, replaces the `Layer.h` C2 comment).** `installRhythm` saves the slot's `activeClip` before the `Rhythm` swap and restores it after, so a clip launch
  (which installs through it) keeps what C5 will have recorded. Clearing is the choke point's job (non-clip stage), not the install's. The bank is untouched by every install (`SlotBound`).
- **Persistence.** Each `<Rhythm>` node of a full preset and a session gets the shared `ClipBank::writeTo` child and an `activeClip` property (only when it names an occupied clip), via `writeRhythmClips`.
  A preset (not a session) blanks a clip `r0_sample` pointing into the embedded-decode temp folder, as it does for the rhythm's own. The full-preset prepare reads each bank once, drops any clip whose root
  is not `MuClidRhythm`, and validates `activeClip`; the commit moves banks and `activeClip` onto the live layers **after the install and outside the lock** (`ClipBank` gained move operations
  for this), and empties banks of rhythms past the new count. The legacy session reader clears every bank first. A file without `<Clips>` loads with empty banks.
- **Root tag leniency (ruling).** `.muRhythm` loads stay **lenient about the root tag**: `readRhythmPresetFile` and `prepareRhythm` read properties and do not test the tag, and the shipped
  and hand-converted files must keep loading (there is no migration path; see `PluginProcessor_Internal.h`). Only clip **storage** is strict: `loadClipFromFile` and the full-preset prepare require
  `MuClidRhythm`, because a bank must hold one shape. So a file with another root can be loaded but not stored as a clip; accepted, and reported through `onLoadError`.
- **mu-Clid MIDI (Open question 5, resolved).** `Rhythm` is the shared mu-core type, so MIDI carries an unused bank, but its session reader takes only the `<Rhythm>` attributes it knows and never
  names `<Clips>` or `activeClip`: nothing to handle, nothing written. Opening a mu-Clid file with clips in MIDI and re-saving drops them (accepted, as stated for older builds); it preserves nothing.
- **Open for C5.** `activeClip` is saved and restored but still not written by a launch commit; the staged-commit test pins that the commit leaves it.

C1 and C2 can ship with no visible change. C3 and C4 give the feature a screen-driven path (a menu or button calling `launchClip`) before any hardware exists,
so each is demonstrable on its own.

**9. Standing check (design-future).** Easier. The bank is generic `mu-core` data with an opaque per-product payload, so the combined instance (a clip is
a layer preset of that layer's type; the node already carries the product's root tag) and the layer-type registry need nothing new; the Performance
level, the launch surface and the screen share one path; no product gets a copy of the store.

**10. Auto-save of edits and the clip clipboard (ruled 2026-10-21; Andy asked for both).** This changes one earlier rule ("only the screen's store / clear
/ load-into-clip write the bank"): the bank is now also written by the **auto-save**, and only that, and only for the clip that is lit.

*Auto-save: detection.* **Poll and compare, not listeners.** A dirty flag fed by APVTS or `ValueTree` listeners is not generic: mu-Clid's steps and a layer's
extras live outside APVTS and have no listener, and mapping a parameter id to its layer needs product knowledge. The one definition of "the layer changed" that
needs no product code is `captureLayerPresetTree(slot)`, the capture the clip itself is made from. So `ProcessorBase` runs a **message-thread poll** from the
existing 100 Hz `SwapPoll` (no new timer, nothing on the audio thread):
- **What is watched.** A layer with a *lit* clip: `liveState` `playingClip >= 0` (what the pad shows). `Layer::activeClip` stays the saved shadow and does not drive the
  watch, so after a full-preset or session restore (pad dark) nothing is written until a launch, and what the user sees is what is saved. One layer per poll step,
  round robin, every 400 ms per step (so with many lit layers each is visited every `n x 400 ms`; the flushes below close the gap where it matters).
- **Per-layer tracker, message-thread, unsaved, in `ProcessorBase` beside `pendingPresetTree`:** `baseline` (the capture that the stored clip is known to equal
  live), `candidate` (a differing capture seen last step) and the clip index it belongs to. Dirty means *capture differs from `baseline`* (`isEquivalentTo`), **not**
  from the stored tree: a launch migrates a copy and a capture normalises, so comparing to the stored tree would rewrite every clip the moment it is launched
  (the feedback loop). The baseline is **adopted, never written,** whenever there is none: after a launch commit, after a store, after a state restore, after
  any structural edit.
- **Debounce by settling.** A differing capture becomes `candidate`; it is written to the clip (`ClipBank::store` with the clip's existing name, then
  `publishClipMasks`, baseline := candidate) only when the **next** step's capture equals the candidate, i.e. the layer has been still for 400 ms. A knob drag or a
  step paint in flight is never stored half-way. No mouse-state test is needed.
- **When it is skipped (tracker dropped or step passed over):** no lit clip; `activeClip` irrelevant; Performance mode on (the locked show must not mutate banks,
  same reason `StoreClip` is refused there; **Andy 8**); a staged full preset (`hasPendingFullPreset()`); a pending swap that is **not** the clip launch (a
  layer-preset load already cleared the lit clip through `noteNonClipStage`, and a store that adopted a staged tree leaves live != baseline until the commit, so
  the step waits for the commit and re-adopts); the `clipAutoSave` setting off; offline render (`isNonRealtime`).
  A **queued clip launch** (`liveState` `queuedClip` set) does *not* skip: the live layer is still the lit clip until the commit, so its edits keep being saved
  and none are lost to the swap (at most the last 400 ms, see edge cases).
- **Flush (a forced step for one layer: capture, and write if it differs, with no settle wait).** Before `launchClip` stages (so the edits of the clip being left are
  in its bank), before `copyClip` / `cutClip` read the bank, before a structural edit (`addLayer`, `removeLayer`, `swapLayers`, `resetLayer`, a type change: flush all
  lit layers, then drop all trackers; they re-adopt on the next step and `LiveState::remapLayers` already carries the lit clip), and before a state capture
  (`captureComposedState`, full-preset and session save) **when on the message thread** (a host may call `getStateInformation` from another thread: there the bank is
  read as it stands, at most one poll stale, and the flush is not attempted). One helper, `flushClipAutoSave(int slot = -1)`; no product calls it.
- **Typed slot hosts (mu-Tronic).** The capture is the slot's own type's node, so a slot's tracker is keyed by slot index and re-adopted after a `swapLayers` or a type
  change (both clear or move lit clips through `LiveState`, and both flush first). Only a clip of the slot's type can be lit (a launch cannot change type), so the
  type check never fires in the tracker. A Published `swapLayers` exchanges two slots: flush both, drop both trackers.
- **Cost and limits.** One capture and one tree compare per 400 ms step, only for lit layers; a clip capture carries no audio (mu-Clid never embeds a sample into a clip), so
  trees are tens of kilobytes. Memory is two extra trees per lit layer. No new cap on clips. Back-off: if a layer's capture takes more than 4 ms, that layer's visit interval doubles
  (message thread, so this is a UI-hitch limit, not an audio one); `dsp-controller` to confirm the budget on mu-Tant and a loaded mu-Tronic.
- **Saved state.** No file change: the auto-saved bank is the existing `<Clips>` data and it rides every full preset and session (the flush above makes the save
  current). Layer-preset files and performance files are unaffected. The only new persistent thing is the **`clipAutoSave` setting**, a shell preference beside the other
  settings-overlay options (default **on**, as Andy asked), not a parameter, not in a preset or the APVTS, because it is a behaviour of the app, not part of any sound.
- **Side effects of the bank to state plainly (ux-controller to word):** a tweak to a lit clip now overwrites it, so there is no "audition and return". Recommend the
  strip shows a brief "saved" cue on the pad and the setting is easy to find; no history or undo in this round (**Andy 9**). Host automation moving a parameter of a lit
  clip is indistinguishable from a hand edit and will be saved (accepted; switch the setting off for automated sessions).

*Clipboard.* **One process-wide clipboard in mu-core:** `mu-core/Sequencer/ClipClipboard.h`, header-only, namespace `mu_core`, a function-local static accessor
`mu_core::clipClipboard()` (the pattern of `controlAttachmentFactory()`: no static-init order hazard). It holds a deep-copied `ValueTree` (the layer-preset payload, root tag
included), the clip's `name`, and the **payload kind** (the root tag; for a typed slot host also the `LayerTypeId` from `layerPresetType`). It names no processor, so every
instance in a DAW process shares it (copy in one mu-Tant, paste in another) and a slot's removal cannot dangle it. It does not touch the OS clipboard (**Andy 10**); the
accessor is the seam if that is added. Message thread only.
- **Processor API** (non-virtual, message thread, `ProcessorBase`): `bool copyClip(slot, clip)` (refused for an empty clip; flushes the layer first; leaves the bank),
  `bool cutClip(slot, clip)` (copy, then `clearClip`; if the copy fails nothing is cleared), `bool pasteClip(slot, clip)`, `bool canPasteClip(slot) const` (the same check, for
  menu enabling). **Paste type check:** a single shared `clipFitsSlot(slot, tree)` that `launchClip`, `loadClipFromFile` and `pasteClip` all call (list hosts: the tree's root tag equals
  `captureLayerPresetTree(slot).getType()`; typed hosts: `layerPresetType(tree) == slotTypeId(slot)`); the three existing inline checks move into it, so a clip of the wrong product or type
  can never enter a bank by any route. Paste stores a deep copy with the clipboard's name, replacing whatever is there (confirm: the strip's existing overwrite confirm, `ux-controller`).
  **Paste onto the lit clip relaunches it** (`launchClip`), so the lit pad still equals what plays and the tracker re-adopts at the commit; pasting onto any other clip does not launch.
  Paste does not light the clip (a store lights it, a paste is not "what you are playing").
- **Actions.** Three new `ControlActionType`s: `CopyClip`, `CutClip`, `PasteClip`, `layer` + `index` as `StoreClip`; **screen only, exactly like `StoreClip` / `ClearClip`**: absent from the
  name list (no map or surface can name them), refused in Performance mode and while a full preset is staged, handled in `ProcessorBase::perform`. They are actions, not direct calls from
  `ClipStrip`, so the strip keeps its single sender seam and the tests record them like the others. Auto-save has no action (it is not user-invoked).
- **Where the menu lives.** `ClipStrip` (right-click on a pad): Copy, Cut, Paste, Clear; enabled states `ux-controller` rules (Copy, Cut, Clear need an occupied clip; Paste needs
  `canPasteClip`).

*Standing check.* **Easier**: auto-save is generic poll-and-compare over the existing capture, so the combined instance and every future layer type get it with no code
(provided their capture is idempotent, which C8 pins per type); the clipboard being process-wide and payload-typed is the same shape a cross-instance layer move or a layer
library would need. Harder: nothing, with one watch item: the no-product-code rule makes the poll depend on every product's capture being a pure function of live state.
A product that captured a clock, counter or random value into its layer tree would be rewritten forever; that is a defect in the capture, not a reason for a per-product dirty flag.

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
8. **Auto-save in Performance mode (Andy, 2026-10-21).** Recommend **off while locked**: a show tweak on a mapped knob is not written to the bank, the same reason `StoreClip` is refused
   there. The alternative is to keep saving (Ableton-like: what you set is what the clip becomes). Build follows the answer; it is one condition in the tracker.
9. **No undo for an auto-saved overwrite (Andy).** Recommend none now; the setting is the escape. A later single "previous version" per clip would be a `ClipBank` field and changes
   nothing else.
10. **Clipboard scope (Andy).** Recommend in-process only (Standalone to plug-in copy is not covered). The alternative is XML text on the system clipboard with a header, a second step
   behind the same accessor.

Do not: put the bank on `ProcessorBase`, `LiveState` or in the editor; store it as a plain `std::array<ValueTree, 8>` or a plain member of a copyable `Rhythm`;
give clips their own file type or a per-product clip class; make `launchClip` a second load path beside `loadLayerPreset`; write a clip from the audio thread or
read the bank there; let a pad, a launch or a layer-preset load write the bank (only the screen's store / clear / load-into-clip / copy-cut-paste and the item-10 auto-save do);
compare an auto-save capture with the stored clip instead of the baseline; put a per-product dirty flag, a parameter listener map or a second timer under the auto-save; skip the flush before a
launch or a structural edit; give each instance its own clipboard or let a paste skip `clipFitsSlot`; map `CopyClip` / `CutClip` / `PasteClip` to a pad; let a clip carry the mixer
strip, name or colour; clear banks under `layersLock`; store embedded sample bytes in a clip; add a `Clip` suffix type per product; edit the Layer virtuals
for this.

## Controller library (`mu-control`) — family standard (ruled 2026-10-13; backlog #1276)

Structure of the library that holds controller drivers (the Launchpad X first). What the device does, the lights, the staged build order and the
tests per stage are in [design-launchpad.md §3.2d](design-launchpad.md#32d-building-mu-control-ruled-2026-10-13-backlog-1276). This section is the shape
every future driver, and the one-instance rig, relies on.

**1. Kind, name, namespace.** `mu-control/` is a sibling library of `mu-core`, an **INTERFACE library** (sources are compiled in the
consuming target; unlike `mu-core`, which has been one compiled static library since 2026-11-14, it is small and standalone-only, so it was not converted). Namespace **`mu_control`** (one namespace for the whole library; sub-areas nest only
when a device needs private helpers, `mu_control::launchpadx`). Include root is `mu-control/`; because the Standalone target has both include roots, **no
folder or file path under `mu-control/` may equal a path under `mu-core/`** (the folder names below are chosen for that). It uses `mu-core` headers
and nothing else of the family; the only `mu-core` types it names are `ControlAction`, `ControlSink`, `LiveView` and the attachment seam of item 4.

**2. Folders (by role, like `mu-core`; the eight-folder product layout is for instrument products and does not apply).**

| Folder | Holds |
|---|---|
| `Surface/` | The device-independent half: `SurfaceDescription.h`, `SurfaceEvent.h` (control id, press/release/value event, LED state), `SurfaceMapping.{h,cpp}`, `SurfaceModel.{h,cpp}` (including the blink animation) |
| `Driver/` | The port-facing half: `SurfaceDriver.h` (the interface), `DevicePorts.h` (the byte pipe the driver talks to), `JuceDevicePorts.{h,cpp}` (the real ports over `juce::MidiInput` / `MidiOutput`; the only place a JUCE MIDI port type appears; includes the 500 ms hot-plug watcher), `ControllerBackend.h` (`ControllerBackend { ports, driver, detect PortSpec }`, `ControllerBackendFactory`, the declaration of `makeDefaultBackend()` that a `Devices/` file defines: the seam that keeps `Manager/` from including `Devices/`) |
| `Devices/<Device>/` | One folder per device family. `Devices/LaunchpadX/`: `LaunchpadXProtocol.{h,cpp}` (pure byte encode and decode, no ports), `LaunchpadXPalette.h` (hue name to bright and dim colour), `LaunchpadXDriver.{h,cpp}` |
| `Manager/` | `ControllerManager.{h,cpp}` (discovery, hot-plug, lifetime, the refresh timer; implements `mu_core::ControlAttachment`), `ManagedPorts.h` (the wrapper that moves MIDI-thread bytes and connection changes onto the message thread, item 5) and `ControlRegistration.cpp` (item 4) |
| `Tools/` | `mu-control-probe`: a developer console app for the real pad (port list, lighting and input steps, manager demo, colour lab); `EXCLUDE_FROM_ALL`, never built by a normal build nor shipped. It compiles the library sources it needs directly, so a new source file in `mu-control/` that the probe uses must be added to `Tools/CMakeLists.txt` too |
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
  sources, not a static library, so the linker cannot drop it; this is why `mu-control` stays an INTERFACE library). Linking `mu-control` is therefore the whole switch: no `#if`, no product line, no `mu-core`
  include of `mu-control`.
- `mu-core/Plugin/StandaloneShell.h` `App::initialise`, in the **windowed branch only** (never the `--render` branch, which must not touch a MIDI port), after the
  window is up: if the factory is set and the processor's `ShellBackend` returns a non-null `getControlSink()` and `getLiveView()` and
  `supportsPerformanceMode()` is true, it stores `controlAttachment = factory(*sink, *view, SurfaceLook::fromTheme(), settingsFolder)` and calls
  `processor.setControlAttachment(controlAttachment.get())`. `App::shutdown` clears the processor's pointer and resets `controlAttachment`
  **first**, before the preset-name hook, the bridge and the plugin holder, because the manager holds references into the processor and must restore the
  device before the window goes.
- **The quit hook (ruled 2026-10-14, M3 review).** The manager's static `ControllerManager::closeAllNow()` cannot be called from `mu-core` (it names a `mu_control`
  class), so the hook is a **virtual on the seam**: `ControlAttachment::closeNow()` (pure virtual; give the device back at once, pads dark and its own mode, and stay
  closed; harmless when repeated and from the destructor). The manager implements it with the body `closeAllNow` has today; the static `closeAllNow` and the
  `liveManagers()` registry are then removed (nothing in `mu-core` can reach them, and the shell holds the one attachment). The shell calls `closeNow()` at exactly
  these points, in `StandaloneShell.h`: (1) **first line of `App::shutdown`**, before the preset-name hook, then `setControlAttachment(nullptr)` and reset (the
  destructor is the backstop); (2) in `CloseConfirmWindow::closeButtonPressed`, in the **Close and Save branches before `quit()` / `onSaveAndQuit`** (through an
  `onQuitConfirmed` callback the `App` sets on the window; the window does not hold the attachment), so a slow save never keeps the pad in Programmer mode; **not**
  in the Cancel branch and **not** in `systemRequestedQuit` itself (it only opens the same confirm dialog). A `--render` run has no attachment. A crash or kill is the
  accepted R1 case. `ControllerManagerTests` covers `closeNow()` (Live sent once, nothing reopens, status `Released`); `mu-core-tests` covers the shell calling it
  through a fake attachment on both confirm branches and on shutdown.
- **As built, M4 (2026-10-14).** Where the ruling above says "the shell does X", the code is: `mu-core/Control/ControlHost.h` (`mu_core::ControlHost`: `start(backend, look, settingsFolder, publish)`, `closeNow()`, `shutdown()` = device back, `publish(nullptr)`, destroy), used by `StandaloneShell.h`; `mu-core/Control/AttachmentWatch.h` (`mu_core::AttachmentWatch`, used by the Settings group and the TransportBar; `ControlAttachment::Listener::controlAttachmentDestroyed()` tells it the pointer died, so UI in the editor never calls `removeListener` after the shell has destroyed the attachment); `mu-core/Plugin/QuitFlow.h` (`mu_standalone::QuitFlow`, the Close / Save / Cancel order); `mu-core/UI/SurfaceLookFromTheme.h` (`mu_ui::makeSurfaceLook()` replaces the planned `SurfaceLook::fromTheme()`, because `SurfaceLook.h` must stay free of UI includes); `controlStatusText()` beside `ControlStatus`. The order in `ControlHost::shutdown` is load-bearing: the processor's pointer is cleared before the attachment is destroyed, so a watcher told of the destruction re-reads null and does not re-register. **Settings file:** the manager reads `<settings folder>/mu-controller-mapping.xml`, one file for all four apps (design-launchpad 3.2d, Mapping row); the shell's settings folder is the per-user data root, so the name carries the `mu-` prefix. Release builds (`MUFAMILY_WITH_CONTROL` OFF) are unchanged in behaviour: `mu_control_attach` does nothing, the slot is empty, no group or status line is shown.
- The manager's constructor takes **only `ControlSink&`, `const LiveView&` and the settings folder.** It names no `ProcessorBase`, `ShellBackend`, editor, window
  or product. This is what lets a mu-link hub or a one-instance host create one over its own sink and view later (Performance mode item 7); a hub supplies a
  sink that forwards to the focused instance and changes nothing in `mu-control`.
- Rejected: a product `StandaloneApp.cpp` line under `#if` (four copies of the same wiring, and the option would have to be repeated in each product); an
  `#include` of a `mu-control` header from `StandaloneShell.h` (a `mu-core` to `mu-control` dependency, even if guarded); `ProcessorBase` owning the manager (it
  is built in plugins and headless renders).

**5. Threads.** Everything in `mu-control` is message-thread or MIDI-input-thread; **it never touches the audio thread** and needs no `dsp-controller` review
unless that changes. A driver's input callback runs on the MIDI thread: it decodes into `SurfaceEvent`s, pushes them onto a fixed-size lock-free FIFO and
triggers an `AsyncUpdater` (built as `ManagedPorts`, which wraps the driver's ports so the driver and model only ever run on the message thread; connection changes from the hot-plug watcher take the same route); the manager drains on the message thread, runs the mapping and calls `ControlSink::perform` there (the `ControlSink` contract). LED
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
Hot-plug (as built, M3, replacing the `juce::MidiDeviceListConnection` plan): an absent device is found by the manager's 1 Hz name scan; an opened one is watched by `JuceDevicePorts` itself, which polls the port lists every 500 ms and reports a vanish and a return (reopening a fresh pair on return). `release()` closes the session as above and parks the manager (`Released`) until `connect()`; `connect()` rescans and, if
Performance mode is already on, reopens. A crash leaves the device in Programmer mode until it is unplugged (accepted, R1).

**7. Boundary, enforced.** `check-core-boundary.ps1` already scans `mu-control/` for plugin includes and plugin namespaces, and `mu-core` for `mu-control`
includes. All done in `check-core-boundary.ps1` (the `Surface/`/`Manager/` to `Devices/` rule landed with M2 and is clean at M3). The rules: a `mu-control` file must not include `ProcessorBase.h`, `ProcessorCore.h`, `EditorShellBase.h`, `ShellBackend.h` or any
`mu-core/UI/` header, so "depends only on `ControlSink` and `LiveView`" is checked, not remembered; and a file under `Surface/` or `Manager/` must not include
anything under `Devices/`, while `Devices/` must not include `Surface/SurfaceMapping.h` or `SurfaceModel.h`.

**8. Standing check (design-future).** Easier. A new device is one folder under `Devices/` plus a registration line in its driver list; the manager, mapping,
model, seam and tests are shared; the same four parts serve any grid surface; and the hub or the combined instance reuses the manager unchanged (item 4).

Do not: give `ProcessorBase` or the editor a manager; add `#if` wiring or a `mu-control` include to `mu-core`; link `mu-control` to a plugin's shared-code target; open a
port in `--render` or outside Performance mode; let a driver call `ControlSink`, read `LiveView` or write a parameter; let the model or mapping hold device bytes; copy
`MidiControlMap` into `mu-control`; name a product in `mu-control`.

### Several apps, one pad (ruled 2026-10-14; Andy's request: top buttons 1 and 2 switch between mu apps; not built, staged after M4/M5 acceptance)

**The problem.** One physical pad, several mu processes (mu-Clid, mu-Tant, mu-Toni, mu-On; standalones, possibly two of the same product, later plugins in a
DAW). Each app's manager opens the ports only in Performance mode (item 6), so today the second app in Performance mode fights the first: both
receive every press and both write lights (design-launchpad 3.2d decision 3). Exclusive ports are only half of it; **arbitration is needed even where ports
are shared** (design-launchpad 3.5 found two processes can open the pair together). So the answer is "who owns the pad now", not only "who may open it".

**Options evaluated** (criteria: one framework, nothing on the audio thread, standalone + plugin, same-process instances, unplug and replug, Linux/macOS, effort).

| Option | Verdict |
|---|---|
| (a) Surface hub process owning the pad, brokering over a pipe or socket | Right shape only if the pad must work while **no** app owns it, or for DAW-hosted plugins that cannot open ports. Costs a new process family members must install, start and supervise; a hub crash kills the pad for everyone; a second IPC stack beside mu-link's; it must stream every app's `LiveView` (or lighting) to be useful. Not for v1; kept as the last stage (F3). |
| (b) mu-link as the hub | Rejected for the pad. mu-link is the optional audio and clock server (mu-link CLAUDE.md: standalone-only, "zero config", the clock is sacred); making app switching depend on a running audio server couples two unrelated things, the free apps would need it, and its `ClientRegistry` (8 slots, 32-char name) is built for audio clients with no accent, focus or Performance state. mu-link may later **display** the focus; it does not own the pad. |
| (c) One app owns the pad; hand-over by a handshake | **Chosen, generalised**: the handshake runs through a small shared registry file every app maps, not through the pad owner knowing the others (item below). Works across processes **and** in the same process (a shared file is seen by both, which a static singleton in a plugin module is not), with no extra process. |
| (d) No cross-app switching | The v1 behaviour until F1 is built; costs nothing and closes nothing. |

**Ruling: (c), as a focus registry in `mu-core`; no process.** Build in stages; the app-side seam is small and sits where the existing ones do.

1. **`mu-core/Control/SurfaceFocus.h` (new, device-neutral interface) and `mu-core/Control/SharedSurfaceFocus.{h,cpp}` (its implementation).** Names no
   device, product or `mu-control` type. A `juce::MemoryMappedFile` on a fixed file in the per-user temp folder (portable to Windows, Linux and macOS; no Win32
   shared-memory API, unlike mu-link's `SharedMemoryRegion`, which is not reused so the pad works without mu-link). Layout: a versioned header
   (`version`, `focusSlot`, `focusGeneration`, `padHeldBy`) and 8 slots `{ appKey (product id + start sequence), name[32], accentRgb, flags (performanceOn),
   heartbeat }`. Written and read on the **message thread only**; fields are plain atomics, no locks; a version mismatch or a stale heartbeat (older than
   about 3 s) is ignored, never trusted. A crash leaves a stale slot that ages out (R1 is unchanged: the pad stays in Programmer mode until unplugged).
2. **`ControlAttachmentFactory` gains one argument**, `SurfaceFocus&` (the shell passes the one `SharedSurfaceFocus`; tests pass a fake), and `ControlHost::start`
   takes it. `ControllerManager`'s constructor then takes `ControlSink&`, `const LiveView&`, the settings folder and `SurfaceFocus&`; it still names no
   `ProcessorBase`, editor or product. `wantOpen()` gains one condition, "this app holds the focus"; everything else (open, close, Live on every exit path,
   hot-plug) is unchanged. Item 4's "hub supplies a sink" sentence stands for a later F3 hub.
3. **`ControlStatus` gains `Standby`** ("another mu app has the pad"; the lamp and wording are the `ux-controller`'s, design-ui-family 14). Not `InUse` (a foreign
   program) and not `Released` (the user let go): `Standby` is the normal state of every app but one and needs no action. The manager still reads port names for
   `Found` while on standby.
4. **`ControlAction` gains `FocusNext` and `FocusPrev`** (stable names, serialisable like the others). `SurfaceModel` maps the two buttons; the **manager handles them
   itself through `SurfaceFocus`** and the sink never sees them (they are not an app action). Order of the ring: product order mu-Clid, mu-Tant, mu-Toni, mu-On, then
   start sequence; only apps with Performance mode on and a fresh heartbeat are in it. Up = previous, down = next.
5. **Hand-over** (all steps on the message threads, driven by a 10 Hz poll that runs only while Performance mode is on): the owner A presses a button, picks the
   target B, writes `focusSlot = B` and `focusGeneration + 1`, runs its normal `closeDevice()` (pads dark; the ports and Live mode as today), then clears
   `padHeldBy`. B sees `focusSlot == me`, waits for `padHeldBy` to clear (or 300 ms), and opens as if Performance mode had just been entered. Result: a short
   dark moment, no overlap on either port model. **Entering Performance mode takes the focus** (the latest deliberate act wins); **leaving it, quitting
   (`closeNow()`), or a stale heartbeat gives the focus to the next ring member**, or leaves it free. An app that is not in the ring never opens a port.
6. **Lights.** With two or more apps in the ring, top buttons 1 and 2 show the current app's accent (bright), via `SurfaceLook::accentRgb`; with one they are dark
   and inert (below). The exact colours and whether one of them shows the neighbouring app's accent are the `ux-controller`'s.
7. **The mapping conflict (needs Andy).** Version 1 uses all eight top buttons for `PerformanceItem(1..8)` (design-launchpad 3.6, settled 2026-10-13), so buttons 1
   and 2 are items 1 and 2. A rig profile must therefore be **opt-in and static**, never an automatic remap when a second app appears (a mid-gig launch would change
   what a pad means): a "Share the pad between apps" setting, stored once for the device in `mu-controller-mapping.xml` (it describes the pad, not an app),
   default **off**. When on: top 1 and 2 = `FocusPrev` / `FocusNext` (dark and inert while alone in the ring), top 3 to 8 = items 1 to 6. `SurfaceMapping` generates
   this as a second default from the same `SurfaceDescription`; no `if (device)` in `Surface/`.

**What v1 does with buttons 1 and 2.** Nothing new: they stay items 1 and 2 (as built in M4). No reservation, no dark buttons, no registry file is written or read while the
setting is off, so a single-app user sees no change.

**Stages** (each ends with `check-core-boundary.ps1` clean and tests; all on the message thread, so no `dsp-controller` review unless that changes).

| Stage | Work | Tests |
|---|---|---|
| F0 | Nothing (v1 as built). Hardware check recorded in design-launchpad 3.5: two standalones in Performance mode at once, and a DAW holding the ports. | |
| F1 | `SurfaceFocus` interface, `SharedSurfaceFocus`, factory and `ControlHost` argument, `Standby`, `FocusNext/Prev`, the manager conditions, the rig mapping profile and setting | `mu-core-tests`: registry round trip, stale and version-mismatch slots, ring order, take-on-enter; `ControllerManagerTests` with two managers over one fake registry and `FakeDevicePorts` (hand-over order, no overlap, Live once, an unplugged pad, a quitting owner, a vanished owner); `SurfaceModelTests` on both descriptions |
| F2 | Settings group line "pad: mu-Tant" and the accent lamp; `ux-controller` ruling first | UI checks |
| F3 | **Only if needed**: a hub that owns `DevicePorts` and the driver, for plugins in a DAW or to light the pad with no owner. Protocol (below). | When scheduled |

**If F3 is ever built, the protocol carries surface events and lighting frames, not `ControlAction`s.** Down to the app: `SurfaceEvent`s (device-independent ids,
y from the top). Up to the hub: the `LedState` frame (final RGB, already dimmed and blinked, delta-able). Also per app: app key, name, accent, Performance on,
heartbeat; transport state only if the logo transport of M6 is built. This keeps `SurfaceModel`, `SurfaceMapping` and `LiveView` in the app (the hub needs none of
them), makes the hub a dumb pipe plus the driver, and means an app cannot tell a hub from the F1 registry's direct driver. Forwarding `ControlAction`s would force the
hub to know each app's `LiveView` to render lights, which is the expensive half. The transport would be a named pipe or loopback socket through
`juce::InterprocessConnection`.

**Risks.** (1) A frozen app that holds an exclusive port keeps it; others cannot take over (accepted; the heartbeat only frees the focus, it cannot close another process's
port). (2) Brief dark and a Live/Programmer flip on every hand-over; if it is seen on the pad, add a hand-over close that skips the Live message (a driver
option, not a protocol change). (3) The registry file in the temp folder: an antivirus lock, a different user's session, or a cleaned temp folder; the code treats an
unmappable file as "alone in the ring", so the pad behaves as in v1. (4) A layout change between app versions: the version field. (5) Plugins in a DAW do not
link `mu-control` (Standalone-only link, item 3) so they are outside the ring until F3. (6) Two instances of one product share a settings folder and mapping file; the
start sequence in the app key keeps their slots apart.

**Standing check (design-future).** Easier. One interface in `mu-core`, no new process, the manager and the mapping unchanged apart from a focus condition and two actions;
and if the one-instance rig (all engines in one app) arrives the registry simply holds one entry and the feature dissolves, which a hub would not.

Do not: add a hub process, a socket or a pipe for v1; use a static singleton in a module as the registry (it is per process); let `mu-core` include `mu-control`
or name an app in the registry; remap the top row automatically when a second app appears; take the focus for an app that is not in Performance mode; let the
audio thread touch the registry; make mu-link a prerequisite of pad switching.

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

User-facing wording (for the release notes and the manuals, via `/notes` and the `user-documentation-author`):

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
`ProcessorBase::hasPendingFullPreset()`; `LayerSidebar` shows a per-layer badge
from the product's `isPendingSwap(i)` / cancels via `onCancelPendingSwap(i)`. A
product only has to override/​wire those.

---

## Licensing and the support prompt — family standard (ruled 2026-10-11)

> **Superseded in part (Andy, 2026-11-11, #1803).** The model is now a free download that shows the licence reminder **every time an instance loads**; a **fixed-price licence (£20, mu-Tronic £30)** removes it. Pay-what-you-want and the once-per-process rule are gone, and so is "Don't show again" (and its `supportPromptHidden` setting). The policy is `shouldShow(hasLicensing, isSupporter, dismissedThisInstance)`; "Maybe later" sets a per-instance flag on the processor (`isSupportPromptDismissed`, not saved), so the next instance asks again. mu-Host lists only the engines whose product is licensed and, with none, explains itself in a pop-up. The original 2026-10-11 ruling (caps removed, staged order, placement reasoning) is in [DevelopmentHistory.md](DevelopmentHistory.md); what stands is summarised here.

**Current model (Andy, 2026-11-11).** The source is GPLv3. The licensed products (mu-Clid, mu-Tant, mu-Toni, mu-On, mu-Tilate, mu-Tronic) are free downloads with a fixed-price licence (£20, mu-Tronic £30) that removes the reminder; mu-Clid MIDI, mu-Toni MIDI and mu-Link are freeware; mu-Host has no licence of its own and asks for the licence of the engine's product. An unlicensed copy is **fully functional**: no track, step, save or preset limits, no demo banner. A licence key marks a supporter, removes the reminder and unlocks nothing. Licensed products call `initLicensing`; the freeware ones never do, so they never show the reminder.

- **What exists.** `EditorShellBase::showSupportPrompt` shows the reminder every time an instance loads (deferred so it never competes with plugin load). `mu-core/License/SupportPromptPolicy.h` is the testable decision, `shouldShow(hasLicensing, isSupporter, dismissedThisInstance)`; "Maybe later" sets the per-instance, unsaved `ProcessorBase::isSupportPromptDismissed`. `ProcessorBase::isSupporter()` is true for a valid offline `.lic` or an online activation, identically in Debug and Release; nothing in the audio or editor path may branch on it except the support prompt and the About / Activation text. The About panel carries a permanent "Support" / "Enter a key" entry point.
- **What is gone.** The demo caps (`demoMaxChannels`, `demoMaxSteps`, `canAddChannel`, `canSaveLayerPreset`, `maxSteps`), the demo banner, the save gate, `isLicensed()`, `MUFAMILY_REQUIRE_LICENSE` as a feature gate, "Don't show again" with its `supportPromptHidden` setting, and the once-per-process rule.
- **Checkout URL.** `ProcessorBase::LicensingConfig::checkoutUrl`, supplied from each product's `Source/License/LicenseKey.h` (`kCheckoutUrl`) and exposed as `getCheckoutUrl()`. The button opens the checkout in the browser; the plugin handles no money or card data. The values live only in each `LicenseKey.h` and match the website's buy links.
- **Tests.** `SupportPromptPolicyTests` in `mu-core-tests` covers every input combination of `shouldShow`; with no key a Debug build shows the reminder, so it is exercised on every run.

### As built: shared core, thin product side (updated 2026-10-15)

**Status.** Done for every licensed product (mu-Clid, mu-Tant, mu-Toni, mu-On, mu-Tilate, mu-Tronic). The freeware mu-Clid MIDI, mu-Toni MIDI and mu-Link have no licensing.

**Shared, in `mu-core`:** `ProcessorBase::initLicensing(LicensingConfig)` (defined in `mu-core/License/ProductLicensing.h`, included only by a licensed product's processor), `LicenseManager` (offline Ed25519 `.lic`), `OnlineActivation` + `LemonSqueezyClient` + `ActivationStore` + `MachineFingerprint` (online activation), `SupportPromptPolicy`. `EditorShellBase` owns the Support prompt, the activation overlay and the status-bar Support link, so no product writes any licensing UI.

**Product side, the whole of it:**

1. `Source/License/LicenseKey.h` in namespace `mu_<product>`: `kLicenseProductId` (matches the `product=` the signer writes and the Lemon Squeezy product), `kLicenseFilename` (`muclid.lic`, `mutant.lic`, `mutoni.lic`, `muon.lic`), `kLicensePublicKey[32]` (PUBLIC key only), `kCheckoutUrl` (the Pay What You Want page, same link as the website).
2. One call in the processor constructor: `initLicensing({ kLicenseProductId, kLicenseFilename, kLicensePublicKey, "<slug>.activation", kCheckoutUrl })`, with `#include "License/ProductLicensing.h"` in the processor `.cpp`.
3. CMake: the licence sources (`LicenseManager.cpp`, `LemonSqueezyClient.cpp`, `OnlineActivation.cpp`) are in `mu-core-static`, so a product links nothing extra; only a product's tests target, which builds the processor from `MU_CORE_PROCESSOR_TEST_SOURCES`, lists them itself.
4. Nothing else: no feature gate, no licence branch in audio or editor code (the rule above).

**Folder rule.** `License/` is one of the eight Source folders in every product. It holds `LicenseKey.h` in the licensed products; it is a `.gitkeep` only in the freeware ones. A new paid product copies mu-Toni's `LicenseKey.h` and the steps above.

**Keys.** Private signing keys (`muclid.key`, `mutant.key`, `mutoni.key`, `muon.key`) live in the OneDrive `secrets\keys` folder (cloud-backed), never in the repo; `MuLicenseTool` generates the pair and signs each `.lic`. Only the public key is embedded. The Lemon Squeezy API key is in OneDrive `secrets` too, never committed.

**Ruling, 2026-10-15: online activation does not check the key's product, and that is accepted for now.** `OnlineActivation` / `LemonSqueezyClient` validate that the key is a good Lemon Squeezy licence key but do not compare the LS product id with the running product, so a mu-Clid key activates mu-Toni. Accepted because a key only removes the Support prompt and unlocks nothing, so crossing products costs no one anything. The offline `.lic` path is already product-bound (`LicenseManager::check` takes `productId` and the per-product public key). Revisit, as a small change in `mu-core` (add the LS product id to `LicensingConfig`, compare it in `OnlineActivation`, one value per `LicenseKey.h`), if a key ever gains a real entitlement (bundles, paid upgrades, a gated feature). Doing it later is cheap because the config already carries per-product identity.

---

## The MIDI-effect product (the former mu-Clid Lite) — ruled 2026-10-11

**Owner decision (2026-10-11).** mu-Clid Lite is a separate product: a Euclidean MIDI effect (no audio, no samples, no Standalone), not a variant or free tier of mu-Clid. The product is named **mu-Clid MIDI** (owner, 2026-10-11; decisions under Owner decisions below), slug `mu-clid-midi`, namespace `mu_clid_midi`; "the MIDI product" below means this product, and `mu-<slug>` / `<slug>` read as `mu-clid-midi`.

**Done.** The MIDI product left mu-Clid in four staged steps (2026-10-11 to 2026-10-12): the identity rename; the extraction of the pure Euclidean sequencer into `mu-core` (below); the new `mu-clid-midi/` product folder (own CMake, `PluginProcessor`, editor, the eight `Source/` folders, namespace `mu_clid_midi`; mu-Clid lost every `MUCLID_LITE_BUILD` branch); and the retirement of the old Lite installer, deploy and package paths. The staged plan, the per-area migration table and the owner-decision log are in [DevelopmentHistory.md](DevelopmentHistory.md); the rules that still bind are below.

### The shared Euclidean sequencer lives in mu-core (ruled 2026-10-11; done 2026-10-12)

The shared pieces and where they live (a header keeps its `Folder/Name.h` include path, so `#include "Sequencer/Rhythm.h"` in mu-Clid and the MIDI product resolves through the `mu-core` include root):

| From `mu-clid/Source/` | To `mu-core/` | Namespace |
|---|---|---|
| `Sequencer/Rhythm.{h,cpp}` | `Sequencer/Rhythm.{h,cpp}` | `mu_core` (`Rhythm`, `Logic`, `EuclidOverrides`) |
| `Sequencer/SequencerEngine.{h,cpp}` (now `RhythmSequencer`) | `Sequencer/RhythmSequencer.{h,cpp}` | `mu_core` |
| `Sequencer/RhythmPlayState.h` | `Sequencer/RhythmPlayState.h` | `mu_core` |
| `Persistence/RhythmParamTable.h` | `Persistence/RhythmParamTable.h` | `mu_core` (`ParamKind`, `RhythmParamDef`, `kRhythmParamDefs`, `kRhythmParamCount`) |
| `Persistence/PresetHelpers.h` | `Persistence/RhythmPresetHelpers.h` (renamed: it is the kinded-property reader/writer for that table, not a product's preset I/O) | `mu_core` |
| `UI/EuclideanPanel.{h,cpp}` | `UI/EuclideanPanel.{h,cpp}` | `mu_ui` |
| `UI/RhythmCircle.{h,cpp}` | `UI/RhythmCircle.{h,cpp}` | `mu_ui` |

`Sequencer/GeneratorAliases.h` stays in mu-Clid as the alias header for the sequencer and persistence set (`using mu_core::Rhythm;` and so on inside `mu_clid`). Rules for the moved code: **no sample, voice-engine, sidechain, licence or APVTS-id knowledge in `mu_core::Rhythm`**; the euclid modulation-snapshot indices `EuclideanPanel` reads are a panel-local enum, `EuclidSnap` in `EuclideanPanel.h` (mu-Clid's `kSnapEuc*` values are pinned to it by `static_assert`). mu-Clid and the MIDI product both use `mu_core::Rhythm`; the MIDI product compiles nothing from `mu-clid/`.

### Rules for the MIDI product

- **Plugin IDs never change** (see the warning below): `PLUGIN_CODE MCLt`, `PLUGIN_MANUFACTURER_CODE TDP1`, `BUNDLE_ID com.transwarp.mu-clid-lite`, `CLAP_ID com.transwarp.mu-clid-lite`, and the processor state id `MuClidState`. They are saved data (design-naming §3); the product name is display only. The bundle's visible name changes, the identity does not.
- **Layer type stays `Rhythm`** and the layer-preset extension stays `.muRhythm` (shared with mu-Clid so a rhythm made in either loads in the other; mu-Clid's sample binding is ignored by the MIDI product). This is the one named exception to "each product's layer type name is unique" (design-naming §1), because the type is one shared class in `mu-core`, not two.
- **No licensing glue** (it is freeware, like mu-Link): it never calls `initLicensing`, never shows the support prompt, and `License/` holds a `.gitkeep`. (Owner decided 2026-10-11: no prompt.)
- **No modulation targets**: it has no `ModulationMatrix` use today, so there is no `mu_mod::ModTarget` table and `Modulation/` is a `.gitkeep`; if it later gains modulation it adds one table like every product.
- **Formats:** VST3 + CLAP only (owner, 2026-10-11; no AU, no Standalone), `IS_MIDI_EFFECT TRUE`. Free.

### WARNING: changing plugin IDs breaks existing DAW projects

DAWs find a plugin in a saved project by its identity (VST3 class ID, CLAP id, AU type / subtype / manufacturer), and hand the saved state blob back to it. Change `PLUGIN_CODE`, `PLUGIN_MANUFACTURER_CODE`, `BUNDLE_ID`, `CLAP_ID`, the company name, or `MuClidState` and every existing project shows the Lite instance as missing, with its settings lost. Changing the display name alone is safe. The two traps in the rename:

- **Stale duplicate.** The old `mu-Clid Lite.vst3` (and `.clap`) stays on disk after an upgrade, so a DAW sees two plugins with the same ID and picks one unpredictably. The new installer must delete the old bundles: Inno `[InstallDelete]` for `{commoncf64}\VST3\mu-Clid Lite.vst3` and `{commoncf64}\CLAP\mu-Clid Lite.clap`, and the old Start-menu group. Keep the `AppId` GUID of the Lite `.iss` so the upgrade is recognised as an upgrade, not a second install.
- **Name-keyed hosts.** Some hosts (preset databases, Reaper FX names, Bitwig favourites) remember the *display name*. A project still loads (the ID matches), but favourites and saved FX chain presets named "mu-Clid Lite" may need re-picking. Note it in the release notes.

If the owner wants brand-new IDs anyway (a clean break), do it as its own decision with a version note, and expect all existing Lite sessions to need the plugin re-inserted.

### Owner decisions (settled 2026-10-11)

Name **mu-Clid MIDI** (slug `mu-clid-midi`, namespace `mu_clid_midi`, installer and zip names `mu-Clid-MIDI-*`); plugin IDs kept (`MCLt`, `TDP1`, `com.transwarp.mu-clid-lite`, `MuClidState`); `.muRhythm` / `Rhythm` shared with mu-Clid; no licensing; VST3 + CLAP only, free; own icon, site product page and release-notes page. The migration touch list and the extraction-timing decision are in [DevelopmentHistory.md](DevelopmentHistory.md).


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

### Shared libraries: mu-core and mu-juce

Two static libraries are compiled once per configuration and linked by every product, wrapper and test app. Until 2026-11-14 `mu-core` was an INTERFACE library whose ~95 sources were recompiled inside every consumer, with the layer, bus and insert counts as per-binary macros (`MU_MAX_LAYERS` and friends) and a `mu_core_variant()` library per settings set guarded by a layout check; that scheme is gone (ruling and migration: [design-runtime-counts.md](design-runtime-counts.md)).

- **`mu-core-lib`** (an INTERFACE wrapper that links `mu-core-static` plus the embedded assets). `mu-core-static` is one STATIC library built from the source list of the `mu-core` INTERFACE target, which stays as the list holder (and for test apps that compile the sources they need themselves). It links no JUCE target; it reads JUCE headers through `mu-juce-usage`, and `MU_CORE_HAS_CLAP` is defined publicly so the library and every product see one `ProcessorBase` layout. It reads no per-product setting: a product's layer, bus and insert counts are its `kLimits` (`mu_limits::ProcessorLimits`, [mu-core/MuLimits.h](../mu-core/MuLimits.h)) given to `ProcessorBase` and to its parameter layout at construction, and its version string is its `getVersionString()`. `check-core-boundary.ps1` fails if a count macro or `mu_core_variant()` returns.
- **`mu-juce`** (the JUCE modules: core, events, graphics, gui, audio, dsp, animation and the rest; 44 source files). A JUCE module target adds its source wrapper to every target that links it, so before this each of ~40 targets recompiled JUCE. Now a directory's `CMakeLists.txt` ends with `mu_use_shared_juce()`: that links `mu-juce` into the directory's targets and marks the module wrappers header-only there, so the targets still link `juce::juce_*` for include paths and definitions but do not compile them. **A new product or tool directory must call it** or it recompiles JUCE and the link fails with duplicate symbols. `juce_audio_plugin_client` and the CLAP wrapper are not in the set: they stay per target.
- **What is shared, so what must not differ per target:** the JUCE flags every target already agrees on (`JUCE_WEB_BROWSER=0`, `JUCE_USE_CURL=0`, `JUCE_VST3_CAN_REPLACE_VST2=0`, `JUCE_USE_MP3AUDIOFORMAT=1`), plus `JUCE_MODAL_LOOPS_PERMITTED=1` (only adds methods, so tests that use modal loops and products that do not agree on the class layout) and `JUCE_ASIO=1` when `MUFAMILY_WITH_ASIO` is ON. The one JUCE code path that read a per-product define, `JucePlugin_Name` (the accessibility application name), reads `mu_core_shared_product_name()` instead (`SharedJuceName.h`, registered by `AboutOverlay::setProductInfo`; `SharedJuceNameTests` pins it). Nothing else in the compiled modules reads a `JucePlugin_*` or `JUCE_STANDALONE_APPLICATION` value (audited against the pinned JUCE; re-check on a JUCE bump).
- **Measured** (clean full Debug, this PC): 923 s and 26.7 GB before, 442 s and 17.6 GB after. Binary sizes unchanged within 1 percent; Release `mu-juce` plus bench link 67 s; mixer benchmark unchanged.

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

Owner rule, 2026-10-09. The family tracks the latest stable JUCE release rather than staying on an old one. The version is pinned in **one place**, [juce-version.txt](../juce-version.txt): every CI workflow checks out that tag and configure fails if the local `JUCE_PATH` checkout differs, so a bump is a one-line edit and local builds cannot drift from CI. To bump: read JUCE's `BREAKING_CHANGES.md` between the old and new version, grep the tree for each affected API, review the [local patch register](#local-juce-patches-register), bump the pin, then run the unit tests, the round-trip listening tests and pluginval on the build PC. When writing code, use current JUCE APIs and do not build on deprecated ones. Version in use: JUCE 9.0.5 today. `pwsh tests/scripts/check-juce-version.ps1` reports how far the pin is behind the latest release.

#### Local JUCE patches (register)

A local patch to `JUCE_PATH` is a last resort (Andy, 2026-10-31) and is always recorded here, with the diff saved in the repo because the JUCE checkout is not in it. **At every JUCE bump, go through this table first:** if the new release contains the upstream fix, delete the patch file and the table row and remove the `mu-family local patch` comment from the JUCE tree (`git -C $JUCE_PATH diff` must be clean); otherwise re-apply it (`git -C $JUCE_PATH apply <file>`) and keep the row. CI checks out the pinned tag without patches, so a patch must never be something a CI build depends on.

| Patch file | What and why | Upstream | Remove when |
|---|---|---|---|
| *(none)* | The MIDI joinable guard (JUCE issue 1769) was dropped at the 9.0.5 bump: 9.0.5 contains the upstream fix 5a318999. | | |

#### JUCE features to adopt

JUCE features the family had not adopted yet, checked against the JUCE 9.0.4 source (2026-10-30). **Approved for implementation (Andy, 2026-10-30); adopted ones are marked below.** Shared behaviour goes in `mu-core` (never per product), and only current APIs are used. Items 1 to 4 and 8 are mu-Tilate building blocks and are listed where they are used; the family-wide ones are:

- **`juce_animation`** (new in JUCE 9): **adopted.** `mu-core-assets` links it (every target that compiles the shell chrome already needs it), and `mu-core/UI/Components/ComponentMotion.h` replaces `juce::ComponentAnimator` for the sidebar and the shell chrome. Animation timing stays in `MuTheme`, not in product code; the mu-Tilate timeline scrolling uses it too ([mu-Tilate engine notes](mu-tilate/design-engine.md#juce-building-blocks)).
- **`juce::VBlankAttachment`** (`juce_gui_basics/windows/juce_VBlankAttachment.h`): display-synchronised repaint callbacks. Zero uses today against about 41 `Timer`-based animation and meter uses. To evaluate: replace the `Timer`-driven meters and scopes with it, once, in the shared `mu-core` components per the standard UI rules (no per-product conversions). Check that it behaves in plugin hosts and the Standalone on Windows and Linux before converting.
- **`juce::MidiDeviceListConnection`**: used by `mu-control` (`Manager/ControllerManager`) for hot-plug; see [design-launchpad.md §3.2d](design-launchpad.md#32d-building-mu-control-ruled-2026-10-13-backlog-1276).

### Version numbering

The live products (mu-Clid, mu-Tant, mu-link, mu-Clid MIDI) use [version.txt](../version.txt), currently 1.2 (1.1 was the UI rework, build 972; 1.2 is the compiled shared mu-core library with runtime counts, Andy 2026-11-14, #1811). [version-pre.txt](../version-pre.txt) is 1.2 too since 2026-10-15 when every product moved to one family version. Plugins show `v1.2.0.NNN`, commits `v1.2.NNN`. The build number never resets on a major/minor change (the Release ≤ Debug guard and the in-plugin update check rely on it). Change a base **only** by editing its file, and only when Andy says so.

### Shipping a Release (two ways, every time)

Release builds stay local (nothing is deployed to testers). (a) a zip built by `release.yml` and uploaded to the GitHub "latest release" so website download
links resolve; (b) **release notes promoted**: move accumulated "Next release · In testing" items into a new dated `v1.2.NNN` section in each affected product's notes page
under `site/`, clear the In-Testing section, and bump the hardcoded version default in `site/download.html`; (c) `site/latest.json` written and the site published (`publish-site.yml`).
The procedure is the `/release` skill.

Artefacts land at `build/<product>/<target>_artefacts/<Config>/<Format>/`. mu-clid-midi has its own folder (`build/mu-clid-midi/...`).

### Update check — family standard (ruled 2026-10-19; built, shipping from v1.2.0.1490)

**History.** The first check asked the GitHub "latest release" API from `ShellChrome::startVersionCheck()` (no opt-out, no daily limit, no dismissal, a detached thread, GitHub's anonymous rate limit). This design replaced it and is built: `mu-core/Plugin/UpdateCheckPolicy.h`, `UpdateChecker.{h,cpp}`, `mu-core/UI/UpdateNotice.h`, `scripts/write-latest-json.ps1`, `tests/scripts/check-latest-json.ps1`. `upgradeBanner` and `kUpgradeBannerH` are retired (ruled with the `ux-controller`, 2026-10-19; look in design-ui-family §6 "Update notice"): the notice is a link in the status bar, `StatusBar::setUpdateLinkVisible(bool)` plus the `onUpdateClicked` callback, in mu-core and shared by every product.

**Scope.** Every product, because every product hosts `ShellChrome`: every product that ships, including mu-Link (through `LinkShellBackend`), in VST3, CLAP and Standalone, on Windows. The `APPLE` paths keep compiling in principle (the platform key below simply has no macOS entry). No product writes any update code.

1. **Version data: `site/latest.json`, served at `https://transwarp.me/latest.json`.** A static file written by `/release` (`scripts/write-latest-json.ps1`), schema 1 (the live file is [site/latest.json](../site/latest.json)):
   ```json
   { "schema": 1, "build": 1490, "released": "2026-11-15",
     "products": {
       "mu-clid": { "version": "v1.2.1490", "build": 1490,
                    "download": "https://transwarp.me/download" } } }
   ```
   Keys under `products` are the product slugs (`mu-clid`, `mu-clid-midi`, `mu-tant`, `mu-toni`, `mu-toni-midi`, `mu-on`, `mu-tilate`, `mu-tronic`, `mu-link`, `mu-host`). The top-level `build` is the family build of the release; each product carries its own `build` because a product that did not ship in a release keeps its older entry, and a product with no entry (a pre-release product not yet published) never sees a notice. `windows` is optional per product (Windows only; no `linux` or `macos` key while macOS is on hold). Unknown fields are ignored; `schema` changes only for a breaking change and a client that does not know the number stays silent. `download.html` should read the same file instead of the GitHub API (separate site task). The site is served from GitHub Pages (published by `publish-site.yml`), so the file is cached by the CDN for a few minutes.
2. **Comparison.** Integer compare of the product's entry `build` with the compiled `BUILD_NUMBER`: newer means an update. Only the build number is compared, because it never resets across major and minor bumps (Version numbering above). The live (`version.txt`) versus pre-release (`version-pre.txt`) split needs no logic: `version` in the entry is display text only (for example `v1.2.1490`), and each product is compared with its own entry. A running build equal to or above the entry (a Debug build) is silent.
3. **Where the code lives.** Two files in `mu-core/Plugin/`, namespace `mu_core::update_check`, on the processor side because they use settings and threads:
   - `UpdateCheckPolicy.h`: header-only, `juce_core` only, the testable part, in the style of `SupportPromptPolicy.h`. Parse `latest.json` for a slug into `struct Latest { int build; juce::String version, notes; }` (rejects bad schema, non-integer build, an oversized body), `isNewer(running, latest)`, `isDue(nowMs, lastCheckMs)` (true when the stamp is missing, 24 h old, or in the future), `shouldShow(enabled, latestBuild, dismissedBuild, running)`, and the settings-key constants.
   - `UpdateChecker.{h,cpp}`: a `juce::Thread` held through `juce::SharedResourcePointer<UpdateChecker>` by `ShellChrome` (one fetch per process however many instances, the thread joined when the last user goes, never detached). The `.cpp` is listed in each product's plugin and tests targets like the other `mu-core/**/*.cpp` files.
   - Persistence through three `ProcessorBase` accessors in the style of `setSupportPromptHidden` and surfaced to the shell through `ShellBackend` (so mu-Link's backend supplies them too): `get/setUpdateCheckEnabled`, `get/setUpdateLastCheck` and latest-known build, `get/setUpdateDismissedBuild`. The slug comes from `ShellBackend::getProductSlug()`, returning the product's `kProductSlug` constant (a new line in each product's `Plugin/`). `ShellChrome` starts the check with a short call into these (the old `startVersionCheck` name goes). The status-bar link opens the product's `download` URL from `latest.json` if it starts with `https://transwarp.me/`, else the fixed `kDownloadUrl`; no URL from the file is opened unchecked.
4. **Threading.** The fetch runs on the checker's own thread, never on the message thread and never touching the audio thread or `processBlock` (the check starts from the editor, so a headless instance never fetches). Connection timeout 3000 ms, read cap 64 KB, one attempt, no retry. Any failure (offline, timeout, bad JSON, wrong schema, unknown slug) leaves the stored state unchanged and shows nothing. The result reaches the UI by `MessageManager::callAsync` with a `WeakReference` to the `ShellChrome`, checked before use. The thread honours `threadShouldExit` and the destructor waits at most the connection timeout.
5. **Privacy and opt-out.** A plain HTTPS GET of one static URL: no query string, no product, version, build, platform or machine identifier, no cookie, no body (the comparison happens on the user's machine). The web host sees an ordinary request (IP address, as for any web page); the manuals say so. The opt-out is a `SegmentControl` "Update check" On/Off in Settings General, default on, key `updateCheckEnabled`, stored machine-wide in the same app-settings store as Display size (not per product, not APVTS, so it never travels in a preset or project). Off means no request at all and no link.
6. **Rate limit and dismissal.** At most one request per 24 hours per product per user: `updateLastCheck` (epoch ms) and `updateLatestBuild` are stored; on editor open, a fetch happens only when `isDue`, otherwise the link is decided from the stored latest build, so it also disappears at once after the user updates. Clicking the link counts as seen and stores `updateDismissedBuild`; the right-click menu "Hide this notice" stores it too. The link stays hidden until a build above it appears, and is never shown in Performance mode.
7. **`/release` publishes it.** A new step after the CI run has finished and the GitHub release assets exist (not at dispatch time, or users would be told of a download that is not there yet): `scripts/write-latest-json.ps1` builds the file from `build_number.txt`, `version.txt` / `version-pre.txt` and the products actually released, then the file is committed with the site push of the release and published with `publish-site.yml`. `tests/scripts/check-latest-json.ps1` checks schema, that each entry's `build` is at most `build_number.txt`, the URLs, and that every slug is a real product; `/release` runs it before the push.

Do not: add a second fetch path in a product, call the GitHub API from the plugin again, put the opt-out in APVTS, or detach the thread.

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
- [mu-tronic/design.md](mu-tronic/design.md) — the combined instance: 16 layer slots holding any product's layer, buses in the shared mixer, the layer libraries, staged plan
- [design-future.md](design-future.md) — inter-plugin sync (μ family), MIDI CC control
