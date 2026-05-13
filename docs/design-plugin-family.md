# μ-family Plugin Architecture

This document covers the shared-code strategy for building additional plugins in the μ-family (mu-tant, and future siblings) that reuse mu-clid's voice chain, modulation system, and mixer.

---

## Goal

A **mu-tant** plugin will use the same:
- Voice engine (sample playback, ADSR, filter, insert processing)
- Modulation system (ControlSequence LFO/step modulators, ModulationMatrix assignments)
- Mixer (per-channel sends, sidechain, returns, master insert, FX chain)

…but swap in a **different sequencer / trigger engine** in place of mu-clid's Euclidean generators.

The code must be structured so these shared components live in a single `mu-core` library that both mu-clid and mu-tant (and any future μ plugin) link against, with zero duplication.

---

## Current state (pre-Stage 33)

All source files are compiled directly into the `mu-clid` plugin target. `mu-clid-lite` recompiles the same files from scratch — there is no shared library. The modulation stack (`ModulationMatrix`, `ControlSequence`) lives inside the `Rhythm` struct, tightly coupling it to the Euclidean rhythm concept.

### What is already well-decoupled

| Component | File(s) | Status |
|---|---|---|
| `VoiceEngine` | `Audio/VoiceEngine.{h,cpp}` | ✅ No PluginProcessor dependency; pure audio unit |
| `MixerEngine` | `Audio/MixerEngine.{h,cpp}` | ✅ Takes voice array + FXChain by reference; self-contained |
| `InsertProcessor` | `Audio/InsertProcessor.{h,cpp}` | ✅ Parameterised entirely via `VoiceParams` |
| `ModulationMatrix` | `Modulation/ModulationMatrix.{h,cpp}` | ✅ Generic source→assignment; no rhythm knowledge |
| `ControlSequence` | `Sequencer/ControlSequence.{h,cpp}` | ✅ Timing evaluator; no Euclidean dependency |
| `FXChain` + slots | `FX/` | ✅ Generic send/return/insert chain |
| All UI components | `UI/Components/` | ✅ No audio coupling |
| `MixerChannel`, `MixerOverlay`, `FXRow` | `UI/` | ✅ Driven by `MixerEngine` state; no sequencer knowledge |

### What is mu-clid-specific

| Component | Why specific |
|---|---|
| `SequencerEngine` | Euclidean pattern generation, rhythm slot management |
| `Rhythm` struct | Contains `HitGenerator genA/B/C` (Euclidean); also hosts modulation (to be extracted) |
| `HitGenerator` | Euclidean algorithm |
| `EuclideanPanel`, `RhythmPanel`, `RhythmCircle`, `RhythmSidebar` | Euclidean UI |
| `ModMatrixPanel`, `ModulatorEditor` | Currently reference Rhythm directly — become shared once #259 lands |

---

## Stage 33 changes

### #258 — `mu-core` CMake STATIC library

Create a `juce_add_library(mu-core STATIC ...)` target in `CMakeLists.txt`. Move the shared source files into it. Both `mu-clid` and `mu-clid-lite` (and future mu-tant) call `target_link_libraries(... mu-core)`.

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
Source/UI/Components/**
Source/UI/FXRow.{h,cpp}
Source/UI/MixerChannel.{h,cpp}
Source/UI/MixerOverlay.{h,cpp}
```

**mu-clid only:**
```
Source/Sequencer/SequencerEngine.{h,cpp}
Source/Sequencer/Rhythm.{h,cpp}          ← until #259 splits this
Source/Sequencer/HitGenerator.{h,cpp}
Source/UI/EuclideanPanel.{h,cpp}
Source/UI/RhythmPanel.{h,cpp}
Source/UI/RhythmCircle.{h,cpp}
Source/UI/RhythmSidebar.{h,cpp}
Source/UI/ModMatrixPanel.{h,cpp}         ← until #259 confirms they're generic
Source/UI/ModulatorEditor.{h,cpp}
Source/PluginProcessor.{h,cpp}
Source/PluginEditor.{h,cpp}
```

### #259 — `VoiceSlot` base struct (generalise `Rhythm`)

Extract non-Euclidean members from `Rhythm` into a `VoiceSlot` base:

```cpp
// Sequencer/VoiceSlot.h  (goes into mu-core)
struct VoiceSlot {
    VoiceParams                   voiceParams;
    std::vector<ControlSequence>  controlSequences;
    ModulationMatrix              modulationMatrix;
    juce::SpinLock                modLock;
    bool                          mute   = false;
    juce::String                  name;
    juce::String                  colour;
};

// Sequencer/Rhythm.h  (stays mu-clid only)
struct Rhythm : public VoiceSlot {
    HitGenerator genA, genB, genC;
};
```

After this change:
- `SequencerEngine` stores `std::vector<Rhythm>`
- mu-tant's engine stores `std::vector<VoiceSlot>` (or a derived type with its own trigger data)
- `ModMatrixPanel` and `ModulatorEditor` can be templated / updated to take `VoiceSlot&` — making them shared too

### #260 — `ProcessorBase` shared skeleton

A base class (or mixin) that handles the per-block work common to all μ plugins:

```cpp
// mu-core ProcessorBase
class ProcessorBase : public juce::AudioProcessor {
protected:
    FXChain     fxChain;
    MixerEngine mixerEngine;

    // Called from derived processBlock — applies modulation and mixes voices.
    // Derived class provides: active voice engines, active VoiceSlots, numSamples.
    void processCoreBlock(juce::AudioBuffer<float>& buffer,
                          std::unique_ptr<VoiceEngine>* voices,
                          VoiceSlot** slots,
                          int numSlots,
                          int numSamples,
                          double beatPosition);
};
```

`PluginProcessor` (mu-clid) calls `processCoreBlock` from its own `processBlock` after the Euclidean sequencer has fired triggers.
mu-tant's processor calls the same after its own trigger engine fires.

### #261 — `MuClidLookAndFeel` → `MuLookAndFeel`

Rename the class and move it into `mu-core`. Add a backward-compat typedef in mu-clid:

```cpp
using MuClidLookAndFeel = MuLookAndFeel;  // in mu-clid only
```

All colour tokens, control sizes, and paint helpers stay identical. Update all `#include "MuClidLookAndFeel.h"` sites.

---

## mu-tant plugin structure (Stage 34+)

Once Stage 33 is complete, a new `mu-tant/` plugin directory can be created. It will:

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

- [design-voice.md](design-voice.md) — voice chain, ADSR, filter, InsertProcessor details
- [design-fx.md](design-fx.md) — FX chain, slot interface, intra-FX routing
- [design-presets.md](design-presets.md) — preset storage; mu-tant will share the `.muRhyth` voice-slot format
- [design-future.md](design-future.md) — inter-plugin sync (μ family), MIDI CC control
