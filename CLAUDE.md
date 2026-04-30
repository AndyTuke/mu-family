# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Design documents

The full design is split into focused sub-documents. **Read only the relevant one** rather than the monolithic design.md.

| Sub-doc | When to read |
|---|---|
| [docs/design-sequencer.md](docs/design-sequencer.md) | Euclidean params, DAW sync, control sequence params, modulation signal flow |
| [docs/design-voice.md](docs/design-voice.md) | Voice chain, ADSR, filter, interpolation quality, sample handling, SoundTouch |
| [docs/design-fx.md](docs/design-fx.md) | FX algorithms, delay, reverb, intra-FX routing, FXSlotBase interface |
| [docs/design-ui.md](docs/design-ui.md) | All UI panel layouts, control behaviours, RhythmCircle, EuclideanPanel, Mixer, Transport |
| [docs/design-presets.md](docs/design-presets.md) | APVTS wiring plan, preset storage, save/restore, current pre-APVTS state |
| [docs/design-future.md](docs/design-future.md) | Unscheduled future ideas — read to avoid closing off options during current stages |
| [docs/design.md](docs/design.md) | Full original spec — only read if the sub-docs don't cover it |

---

## Project overview

**μ-Clid** is a JUCE/C++ Euclidean rhythm sequencer and sample trigger plugin (VST3 + Standalone) by Transwarp Development Project. Up to 8 polyrhythmic rhythm slots, each with two euclidean hit generators, sample playback, voice chain (ADSR + filter), up to 8 drawable LFO/step modulators, and a shared FX chain (Effect/Delay/Reverb) with mixer.

## Prerequisites

JUCE is not vendored. Set `JUCE_PATH` to a local JUCE checkout before configuring:

```powershell
$env:JUCE_PATH = "D:\JUCE"
```

## Build commands

```bash
cmake -B build                              # Configure (once, or after CMakeLists changes)
cmake --build build --config Debug          # Debug build
cmake --build build --config Release        # Release build
```

Artefacts land in `build/mu-clid_artefacts/Debug/` or `.../Release/`.

## Current implementation status

| Stage | Status | Key files |
|---|---|---|
| 1 | ✅ Done | EuclideanGenerator, HitGenerator, Rhythm |
| 2 | ✅ Done | SequencerEngine, PluginProcessor |
| 3 | ✅ Done | SamplePlayer, VoiceEngine |
| 4 | ✅ Done | ControlSequence, ModulationMatrix |
| 5 | ✅ Done | All UI/Components/, MuClidLookAndFeel, StepEditor, LFOEditor |
| 6 | ✅ Done | RhythmCircle, SidebarItem, RhythmSidebar, EuclideanPanel, VoiceSection, RhythmPanel |
| 7 | ✅ Done | ModulatorPanel, ModulatorEditor, ModMatrixPanel, MidiOutputEngine |
| 8 | ✅ Done | FXSlotBase, FXAlgorithmDef, OversampledProcessor, 8 EffectAlgorithms, EffectSlot, DelaySlot, ReverbSlot, FXChain, FXRow |
| 9 | ⬜ Next | MixerEngine, MixerOverlay, MixerChannel, VUMeter |
| 10 | ⬜ | TransportBar, PresetBrowser, SaveDialog, SettingsOverlay, AboutPanel, APVTS wiring |
| 11 | ⬜ | Polish, animations, ring arc animations |

## Source layout (actual, as built)

```
Source/
├── PluginProcessor.h/.cpp       — audio processor, owns sequencer + voice engines + fxChain
├── PluginEditor.h/.cpp          — root editor: sidebar + rhythmPanel + statusBar
├── Sequencer/                   — EuclideanGenerator, HitGenerator, ControlSequence, Rhythm, SequencerEngine
├── Audio/                       — SamplePlayer, VoiceEngine (TimeStretcherBase stub)
├── Modulation/                  — ModulationMatrix, ModulationAssignment
├── FX/
│   ├── FXSlotBase.h             — abstract interface for all FX slots
│   ├── FXAlgorithmDef.h         — algorithm metadata + FXAlgorithmRegistry
│   ├── OversampledProcessor.h   — RAII wrapper around juce::dsp::Oversampling
│   ├── EffectSlot.h/.cpp        — insert FX slot: hosts one of 8 algorithms, insert blending
│   ├── DelaySlot.h/.cpp         — insert delay: sync/free, feedback, spread, dirt
│   ├── ReverbSlot.h/.cpp        — send reverb: 4 algorithms, pre-delay, pure send
│   ├── FXChain.h/.cpp           — sequences Effect→Delay→Reverb, intra-FX routing API
│   └── Effects/                 — 8 header-only algorithm classes (all extend EffectAlgorithmBase)
│       SoftClipEffect, HardClipEffect, FoldbackEffect, BitcrushEffect,
│       LadderFilterEffect, ChorusEffect, PhaserEffect, CombFilterEffect
└── UI/
    ├── Components/              — MuClidLookAndFeel, KnobWithLabel, SegmentControl, NudgeInput,
    │                              TimeSelector, DropdownSelect, StepEditor, LFOEditor, AddButton, StatusBar
    ├── FXRow.h/.cpp             — one FX row for mixer overlay (on/off, algo dropdown, param knobs)
    ├── RhythmCircle.h/.cpp      — concentric ring display, 30Hz timer, pulse animation
    ├── SidebarItem.h/.cpp       — one sidebar entry with mini RhythmCircle
    ├── RhythmSidebar.h/.cpp     — scrollable left sidebar (82px)
    ├── EuclideanPanel.h/.cpp    — Steps/Hits/Rot/Pre/Post knobs for A and B, logic selector
    ├── VoiceSection.h/.cpp      — amp ADSR, filter, filter ADSR, output mode (compact row)
    ├── RhythmPanel.h/.cpp       — header + sample bar + circle + euclidean + voice + modulator
    ├── ModulatorEditor.h/.cpp   — one modulator: mode/input header, LFO/step editor, timing, assignment list
    ├── ModMatrixPanel.h/.cpp    — all assignments overview table (matrix tab)
    └── ModulatorPanel.h/.cpp    — tab bar (Mod A–H + Matrix) + content area
```

## Critical architectural rules

- **Everything in APVTS** — if it's not in the ValueTree it won't save. Each rhythm in its own subtree. *(APVTS wiring is Stage 10 — UI currently binds directly to Rhythm data as a temporary measure.)*
- **Audio thread never allocates** — all allocation in `prepareToPlay`, never in `processBlock`.
- **ModulationMatrix is the single reader** — audio engine reads only from ModulationMatrix, never directly from APVTS or ControlSequence.
- **Rhythms are fully self-contained** — ControlSequences may only target parameters within their own rhythm. No cross-rhythm modulation. Global FX parameters are not valid modulation destinations.
- **ControlSequence lengths are independent** — never couple loop lengths or rates to rhythm step counts.
- **FXSlotBase interface for all FX** — enables VST3 plugin hosting in v3 without refactoring.
- **TimeStretcherBase wraps SoundTouch** — enables RubberBand swap in v2 without refactoring.
- **Atomic pointer for rhythm hot-swap from day one** — required for v2 live swap feature.
- **RhythmSidebar item order supports variable ordering from day one** — required for v2 drag-to-reorder.
- **All colours and sizes in MuClidLookAndFeel only** — no hardcoded values in component drawing code.
- **All UI uses the shared component library** — never build a one-off version of a standard control.
- **ModulationMatrix processes in dependency order** — detects and rejects circular dependencies at assignment creation time.
- **SoundTouch ships as a DLL** (not statically linked) — required for LGPL compliance.

## Key patterns discovered during implementation

### KnobWithLabel callbacks
`KnobWithLabel` has **two** separate callbacks:
- `onStatusUpdate(name, valueString)` — called automatically from the internal `slider.onValueChange` for status bar display
- `onValueChanged(double)` — also called from the same `slider.onValueChange` lambda; use this for data mutation in panels like `EuclideanPanel`

Never override `getSlider().onValueChange` directly — it replaces both callbacks. Always use `onValueChanged` for data binding.

### Pre-APVTS data binding
Until Stage 10, panels mutate `Rhythm` data directly (e.g. `rhythm->genA.steps = (int)v`). After mutation, call `proc.updatePattern(index)` to refresh the cached pattern in `SequencerEngine`. This is intentional and correct for now.

### PluginProcessor default rhythm
`PluginProcessor` constructor always creates one default rhythm (16 steps, 4 hits). Never add a rhythm unconditionally in `PluginEditor` — check `getNumRhythms() == 0` first (the sidebar constructor calls `refreshItems()` which also reads the existing rhythm).

### RhythmCircle sizing
All ring radii are computed proportionally from `min(width, height) / 2 - margin`. Ring A outer = maxR, width = `maxR * 0.20`. Ring B starts at `ring_A_inner - gap`. Same for ring C (dashed). This makes the circle look correct at both the large (200px) panel size and the small (sidebar ~50px) size.

### juce::Font deprecation warnings
All `juce::Font(float)` constructor calls produce C4996 warnings in this JUCE version. These are acceptable deprecation-only warnings — do not fix them during feature stages. Defer to Stage 11 polish (`FontOptions`-based constructor is the replacement).

## Key interfaces

```cpp
class FXSlotBase {
    virtual void prepare(double sampleRate, int blockSize) = 0;
    virtual void process(juce::AudioBuffer<float>&) = 0;
    virtual juce::String getName() = 0;
    virtual juce::String getCategory() = 0;
    virtual juce::Component* createEditor() = 0;
    virtual void getStateInformation(juce::MemoryBlock&) = 0;
    virtual void setStateInformation(const void*, int) = 0;
};

class TimeStretcherBase {
    virtual void prepare(double sampleRate, int blockSize) = 0;
    virtual void setTimeRatio(float ratio) = 0;
    virtual void setPitchRatio(float ratio) = 0;
    virtual void process(juce::AudioBuffer<float>&) = 0;
};
```

## Third-party libraries

| Library | Purpose | Notes |
|---|---|---|
| JUCE | Core framework | Via `JUCE_PATH` env var |
| SoundTouch | Time stretching (v1) | LGPL — ship as DLL |
| Signalsmith Reverb | Room/hall/plate reverb | MIT, header-only |
| FVerb | Plate reverb (alternative) | Header-only |
| RubberBand | Time stretching (v2) | Wrapped behind `TimeStretcherBase` — no refactor needed when upgrading |

## UI values

Knob colour coding, ring colour coding, window sizing, and all layout constants are in [docs/design-ui.md](docs/design-ui.md).
