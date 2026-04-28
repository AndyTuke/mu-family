# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Design document

Full design specification (architecture, UI, DSP, feature list): [docs/design.md](docs/design.md)

Read it on demand when working on a specific subsystem. Key decisions are distilled below.

---

## Project overview

**μ-Clid** is a JUCE/C++ Euclidean rhythm sequencer and sample trigger plugin (VST3 + CLAP + Standalone) by Transwarp Development Project. Up to 8 simultaneous polyrhythmic rhythm slots, each with two euclidean hit generators, sample playback, voice chain (ADSR + filter), up to 8 drawable LFO/step modulators, and a shared FX chain (Effect/Delay/Reverb) with mixer.

## Prerequisites

JUCE is not vendored. Set `JUCE_PATH` to a local JUCE checkout before configuring:

```powershell
$env:JUCE_PATH = "C:\path\to\JUCE"
```

## Build commands

```bash
cmake -B build                              # Configure (once, or after CMakeLists changes)
cmake --build build --config Debug          # Debug build
cmake --build build --config Release        # Release build
```

Artefacts land in `build/mu-clid_artefacts/Debug/` or `.../Release/`.

## Planned source layout

```
Source/
├── PluginProcessor.h/.cpp
├── PluginEditor.h/.cpp
├── Sequencer/          EuclideanGenerator, HitGenerator, ControlSequence, Rhythm, SequencerEngine
├── Audio/              SamplePlayer, TimeStretcherBase, SoundTouchStretcher, VoiceEngine, MixerEngine
├── FX/                 FXSlotBase, FXAlgorithmDef, EffectFX, DelayFX, ReverbFX, FXChain
├── Modulation/         ModulationMatrix, ModulationAssignment
└── UI/
    ├── Components/     MuClidLookAndFeel, KnobWithLabel, SegmentControl, NudgeInput,
    │                   TimeSelector, StepEditor, LFOEditor, PresetBrowser, StatusBar, ...
    ├── RhythmCircle, RhythmPanel, RhythmSidebar, SidebarItem
    ├── EuclideanPanel, ModulatorPanel, ModMatrixPanel
    ├── MixerOverlay, MixerChannel, VUMeter, FXRow
    └── TransportBar, SettingsOverlay, AboutPanel
Tests/
    EuclideanGeneratorTests.cpp, HitGeneratorTests.cpp, ControlSequenceTests.cpp, ModulationMatrixTests.cpp
```

## Critical architectural rules

These are load-bearing decisions — do not deviate without good reason:

- **Everything in APVTS** — if it's not in the ValueTree it won't save. Each rhythm lives in its own subtree for per-rhythm preset save/load.
- **Audio thread never allocates** — all allocation in `prepareToPlay`, never in `processBlock`.
- **ModulationMatrix is the single reader** — the audio engine reads only from ModulationMatrix, never directly from APVTS or ControlSequence.
- **Rhythms are fully self-contained** — ControlSequences may only target parameters within their own rhythm. No cross-rhythm modulation. Global FX parameters are not valid modulation destinations.
- **ControlSequence lengths are independent** — never couple loop lengths or rates to rhythm step counts.
- **FXSlotBase interface for all FX** — enables VST3 plugin hosting in v3 without refactoring.
- **TimeStretcherBase wraps SoundTouch** — enables RubberBand swap in v2 without refactoring.
- **Atomic pointer for rhythm hot-swap from day one** — required for v2 live swap feature.
- **RhythmSidebar item order supports variable ordering from day one** — required for v2 drag-to-reorder.
- **All colours and sizes in MuClidLookAndFeel only** — no hardcoded values in component drawing code. Define the full `ColourIds` enum in `MuClidLookAndFeel.h` before writing any component drawing code.
- **All UI uses the shared component library** — never build a one-off version of a standard control.
- **ModulationMatrix processes in dependency order** — detects and rejects circular dependencies at assignment creation time.
- **SoundTouch ships as a DLL** (not statically linked) — required for LGPL compliance.

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

## Build stages (implementation order)

| Stage | What | Key files |
|---|---|---|
| 1 | Euclidean logic | EuclideanGenerator, HitGenerator, Rhythm |
| 2 | DAW sync + step triggering | SequencerEngine, PluginProcessor |
| 3 | Sample playback | SamplePlayer, TimeStretcherBase, SoundTouchStretcher, VoiceEngine |
| 4 | Control sequences + modulation | ControlSequence, ModulationMatrix |
| 5 | UI component library | All UI/Components/, MuClidLookAndFeel, StepEditor, LFOEditor |
| 6 | Sidebar + rhythm panel | RhythmSidebar, RhythmPanel, RhythmCircle, EuclideanPanel, VoiceSection |
| 7 | Modulator panel + matrix | ModulatorPanel, ModMatrixPanel, MidiOutputEngine |
| 8 | FX chain | All FX/ files, FXChain, FXRow, oversampling wrappers |
| 9 | Mixer overlay + VU meters | MixerEngine, MixerOverlay, MixerChannel, VUMeter |
| 10 | Transport, presets, settings | TransportBar, PresetBrowser, SaveDialog, SettingsOverlay, AboutPanel |
| 11 | Polish | StatusBar, animations, ring arc animations |

## Third-party libraries

| Library | Purpose | Notes |
|---|---|---|
| JUCE | Core framework | Via `JUCE_PATH` env var |
| SoundTouch | Time stretching (v1) | LGPL — ship as DLL |
| Signalsmith Reverb | Room/hall/plate reverb | MIT, header-only |
| FVerb | Plate reverb (alternative) | Header-only |
| RubberBand | Time stretching (v2) | Wrapped behind `TimeStretcherBase` — no refactor needed when upgrading |

## Window sizing

```cpp
setResizeLimits(780, 580, 2400, 1600);
```

All UI elements scale proportionally. No hardcoded pixel values except in `MuClidLookAndFeel`.

## Knob colour coding (from MuClidLookAndFeel)

| Category | Hex | Used for |
|---|---|---|
| Euclidean | `#7F77DD` purple | Steps, hits, rotate, pitch |
| Padding / filter | `#1D9E75` teal | Pre/post pad, cutoff, resonance, delay params |
| Insert pad / modulation | `#D4537E` pink | Insert start/length, modulator controls |
| Level / amplitude / accent | `#EF9F27` amber | Amplitude ADSR, accent, Euclid C controls |
| FX sends | `#D85A30` coral | Effect/delay/reverb sends, intra-FX routing |
| Reverb params | `#378ADD` blue | Size, diffusion, damp, pre-delay |
| Pan | `#888780` grey | Pan |
