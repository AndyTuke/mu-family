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

## Architecture as of Stage 33

Stage 33 introduced the `mu-core` INTERFACE library, `VoiceSlot` base struct, `ProcessorBase`, and the `MuLookAndFeel` rename. See "Stage 33 (complete)" section below for details.

### Shared (mu-core) components

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

### mu-clid-specific components

| Component | Why specific |
|---|---|
| `SequencerEngine` | Euclidean pattern generation, rhythm slot management |
| `Rhythm : VoiceSlot` | Adds `HitGenerator genA/B/C` (Euclidean) on top of the shared `VoiceSlot` base |
| `HitGenerator` | Euclidean algorithm |
| `EuclideanPanel`, `RhythmPanel`, `RhythmCircle`, `RhythmSidebar` | Euclidean UI |
| `ModMatrixPanel`, `ModulatorEditor` | Take `VoiceSlot&` — shared once mu-tant targets them |

---

## Stage 33 (complete — build 368)

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

- [design-voice.md](design-voice.md) — voice chain, ADSR, filter, InsertProcessor details
- [design-fx.md](design-fx.md) — FX chain, slot interface, intra-FX routing
- [design-presets.md](design-presets.md) — preset storage; mu-tant will share the `.muRhyth` voice-slot format
- [design-future.md](design-future.md) — inter-plugin sync (μ family), MIDI CC control
