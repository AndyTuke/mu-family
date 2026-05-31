# μ-family Plugin Architecture

This document covers the shared-code strategy for building additional plugins in the μ-family (mu-tant, mu-toni, and future siblings) that reuse mu-clid's voice chain, modulation system, and mixer.

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
    controlling parameters. mu-tant has **no pitch/filter/amp envelopes** — the
    Gater is the amp; modulators move parameters. (Its filter cutoff/res/type are
    mu-tant engine controls, not the shared mu-clid filter-with-ADSR.)
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
    Plugin/                PluginProcessor (inherits ProcessorBase), PluginEditor, PresetIO,
                           HotSwapStager, StandaloneApp, RenderMode, BuildNumber.h
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
- `Modulation/ModulationMatrix` + `Sequencer/ControlSequence` — modulation system. Product's slot type inherits `Sequencer/VoiceSlot`. `Modulation/ModulatorSerialise.h` is the shared (de)serialise for a `VoiceSlot`'s ControlSequences + matrix assignments (products inject their own source/dest ID validators).
- `UI/Components/` — every standard widget (knob, dropdown, segment, step editor, LFO editor, VU meter, status bar).
- `UI/MixerChannel`, `UI/MixerOverlay`, `UI/FXRow`, `UI/DelayRow` — shared mixer + FX panels.
- `UI/ChannelSidebar` + `UI/SidebarItem` — the shared left "layers" sidebar (select / add / delete / drag-reorder). Reads channel metadata from `ProcessorBase::getNumChannels/getChannelName/getChannelColourIndex`. The per-layer mini-graphic (and its animation) is the only product-specific part, injected via `createMiniVisual` (mu-clid → `RhythmMiniVisual` wrapping a `RhythmCircle`; mu-tant → a voice glyph). Reorder + hot-swap semantics are product hooks (`onSwapChannels`, `isPendingSwap`, `onCancelPendingSwap`) so a product without hot-swap (mu-tant) leaves them null. Add/delete is driven by the product (`onAddChannel` + a panel delete button → `addVoice`/`removeVoice` in mu-tant, `addRhythm`/`removeRhythm` in mu-clid).
- `UI/ChannelHeaderBar` — the shared per-layer header (colour dot · editable name · reset · delete · per-layer preset dropdown · Save). Product wires the callbacks to its own reset / delete / preset / rename semantics.
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
| `ProcessorBase` | `mu-core/Plugin/ProcessorBase.{h,cpp}` | Abstract base; owns apvts + fxChain + mixerEngine + `processCoreBlock()` + MIDI-PC preset plumbing + `syncGlobalFxParam()` (maps a mixer/FX/return/master/`ch{N}_` APVTS id → fxChain/mixerEngine state; prefix-routes to per-family helpers) |
| `MixerFxParams` | `mu-core/Plugin/MixerFxParams.h` | `mu_mixfx::addGlobalFxParams(layout)` declares the shared global-FX / return / master param set (`eff_`/`dly_`/`rev_`/`eff2*`/`echo_`/`ret_*`/`mstr_lvl`/`mstr_pan`/`mst_ins*`) the MixerOverlay/FXRow/DelayRow bind to. **Split:** the product declares its own per-channel `ch{N}_` strips + product globals (e.g. mu-clid `mstrLoop`); the shared helper owns everything else. **Sync contract:** the product registers a `parameterChanged` listener and routes these ids to `ProcessorBase::syncGlobalFxParam` (both mu-clid + mu-tant) |
| `VoiceEngine` | `mu-core/Audio/VoiceEngine.{h,cpp}` | Per-slot voice chain; no PluginProcessor dependency |
| `MixerEngine` | `mu-core/Audio/MixerEngine.{h,cpp}` | Channel strips / sends / sidechain / returns / master; optional per-channel render hook (`RenderChannelFn`) |
| `InsertProcessor` | `mu-core/Audio/InsertProcessor.{h,cpp}` | Parameterised entirely via `VoiceParams` |
| `MultiModeFilter`, `SamplePlayer`, `MidiOutputEngine` | `mu-core/Audio/` | Generic audio units |
| `FXChain` + slots | `mu-core/Audio/FX/` | Generic send / return / insert chain |
| `ModulationMatrix`, `ControlSequence`, `ModulatorSerialise` | `mu-core/Modulation/`, `mu-core/Sequencer/` | Modulation system + shared (de)serialise; product's slot inherits `VoiceSlot` |
| UI widgets | `mu-core/UI/Components/` | Knob, dropdown, segment, step / LFO editor, VU, status bar, `MuLookAndFeel` |
| `MixerChannel`, `MixerOverlay`, `FXRow`, `DelayRow` | `mu-core/UI/` | Shared mixer + FX panels |
| `ChannelSidebar` + `SidebarItem` | `mu-core/UI/` | Shared layers sidebar (product injects the mini-graphic) |
| `ChannelHeaderBar` | `mu-core/UI/` | Shared per-layer header (name / reset / delete / preset / save) |
| `InsertSubsection` | `mu-core/UI/Voice/` | Shared insert-effect voice subsection (channel-prefix bound) |
| `ModulatorPanel`, `ModMatrixPanel`, `ModulatorEditor` | `mu-core/UI/` | Shared modulator UI (take `VoiceSlot&` + a product `ModDestProvider`) |
| `EditorShellBase`, `TransportBar`, `AboutPanel`, `SaveDialog`, `PresetBrowser`, MIDI-preset panels | `mu-core/UI/` | Shared editor shell + chrome |
| `ConfirmDialog` | `mu-core/UI/ConfirmDialog.h` | `mu_ui::confirmAsync` (reset/delete yes-no) + `mu_ui::confirmQuitAsync` (OK/Save/Cancel close prompt) — one shared implementation so every product's confirmation prompts match |

### Product-specific components (under `<product>/Source/`)

| Component | Why specific |
|---|---|
| mu-clid `SequencerEngine` | Euclidean pattern generation, rhythm slot management |
| mu-clid `Rhythm : VoiceSlot` | Adds `HitGenerator genA/B/C` (Euclidean) on top of the shared `VoiceSlot` |
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
Source/Sequencer/VoiceSlot.h              ← base struct for shared voice data
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

### #259 — `VoiceSlot` base struct (done)

Non-Euclidean members extracted from `Rhythm` into `Source/Sequencer/VoiceSlot.h`. Explicit copy constructor and assignment operator provided because `std::atomic<bool>` is non-copyable:

```cpp
// Sequencer/VoiceSlot.h
struct VoiceSlot {
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
struct Rhythm : public VoiceSlot {
    HitGenerator genA, genB, genC;
    // ...
};
```

`SequencerEngine` stores `std::vector<Rhythm>`. mu-tant's engine will store `std::vector<VoiceSlot>` or a derived type.

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

## Related design documents

- [mu-clid/design-voice.md](mu-clid/design-voice.md) — μ-Clid voice chain, ADSR, filter, InsertProcessor details (mu-tant's voice doc lives at [mu-tant/design-voice.md](mu-tant/design-voice.md))
- [design-fx.md](design-fx.md) — FX chain, slot interface, intra-FX routing (family-shared)
- [mu-clid/design-presets.md](mu-clid/design-presets.md) — μ-Clid preset storage. Each product defines its own preset format under its own `design-presets.md`; the family-level conventions are: per-layer preset in camelCase noun (`.muRhythm` / `.muPattern`), full preset in plugin-name camelCase (`.muClid` / `.muTant`).
- [design-future.md](design-future.md) — inter-plugin sync (μ family), MIDI CC control
