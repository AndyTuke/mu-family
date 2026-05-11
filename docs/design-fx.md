# μ-Clid — FX Chain Design Reference

## FX Slot Overview

| Slot | Name | Type | Send behaviour |
|---|---|---|---|
| 1 | Effect | Insert-style | 0–50% blends wet in. 50–100% fades dry out. |
| 2 | Delay | Insert-style | 0–50% blends wet in. 50–100% fades dry out. |
| 3 | Reverb | Send-style | Pure send. Dry signal unaffected. |

**Critical:** Global FX parameters are NOT valid modulation destinations. FX are global — modulating from a rhythm would break rhythm independence.

## FXSlotBase Interface (all FX must implement this)

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
```

## Intra-FX Routing

Send knobs route signal between FX return channels in the mixer:
- Effect return: Effect→Delay send, Effect→Reverb send
- Delay return: Delay→Reverb send
- Reverb return: no outgoing sends (end of chain)

UI placement: routing knobs in FX return channel strip in mixer, above pan.

## Effect Unit Algorithms (v1)

All distortion algorithms (Soft Clip, Hard Clip, Foldback, Bitcrush) and filter algorithms
(Ladder Filter, Comb Filter) have been moved out of the shared Effect slot. Per-rhythm Drive
is now part of the voice chain — see design-voice.md.

The Effect slot hosts modulation and time-based algorithms only:

| Algorithm | Category | DSP Approach | Key Parameters | Oversampling |
|---|---|---|---|---|
| Chorus | Modulation | Manual delay buffer + per-voice sine LFO | Rate, depth, voices (2–4), spread, mix | None |
| Flanger | Modulation | Short delay line (0.5–10ms) + sine LFO + feedback | Rate, depth, feedback, mix | None |
| Phaser | Modulation | First-order allpass chain + sine LFO + feedback | Rate, depth, stages (up to 12), feedback, mix | None |
| Echo | Time | Stereo delay line, no sync, ping-pong spread | Time, feedback, spread, mix | None |

**Chorus** — multi-voice: 2–4 delay taps each with their own LFO phase offset, panned across the stereo field by spread. Depth controls LFO modulation amount (± delay time). Mix is wet/dry blend.

**Flanger** — single delay line modulated into very short times (0.5–10ms). Feedback creates the characteristic notch comb. Feedback is bipolar: positive flanges, negative produces a softer phasing character. Mix is wet/dry blend.

**Phaser** — all-pass chain (2–12 stages in pairs). LFO sweeps the all-pass hinge frequency. Feedback controls resonance intensity. Mix is wet/dry blend.

**Echo** — simple stereo echo without BPM sync (use the dedicated Delay slot for sync'd delay). R delay = L delay × (1 + spread × 0.1) for stereo widening via ping-pong feel. Feedback controls decay. Mix is wet/dry blend. Intended for short slapback and room-simulation echoes.

**Algorithm switch thread safety:** `EffectSlot::setAlgorithm()` calls `prepare()` internally. It must be called from the **message thread only** — calling it from the audio thread during playback is not safe. Full audio-thread-safe hot-swap (via atomic pointer exchange) is deferred to Stage 10 APVTS wiring.

**No oversampling required:** All four Effect algorithms are linear or operate on delay-line modulation; there is no nonlinear waveshaping that generates out-of-band harmonics.

## Delay Parameters (dedicated Delay slot — unchanged)

| Section | Parameters |
|---|---|
| Time — sync mode | DropdownSelect (1, 1/2, 1/4, 1/8, 1/16, 1/32 + triplet/dotted) + ms display at current BPM |
| Time — free mode | ms with NudgeInput (step sizes 1, 5, 10ms) |
| Repeats | Feedback, spread, dirt (saturation on feedback path) |
| Intra-FX | Delay→Reverb send knob on Delay return channel in mixer |

**Spread implementation:** R delay = L delay × (1 + spread × 0.1). Maximum spread is +10% longer on the right channel. No phase inversion — purely time-based stereo widening.

**Dirt implementation:** Soft saturation on the feedback path (`tanh` based), scaled by dirt amount. At dirt = 0 the feedback path is linear.

**Buffer sizing:** `DelaySlot` pre-allocates `4 × 192000` samples (~4 seconds at 192kHz) for L and R. No reallocation during playback.

**BPM source:** `DelaySlot::setHostBpm()` is called each `processBlock` from `FXChain`. In DAW mode the host BPM is used; in standalone mode `PluginProcessor::internalBpm` is passed. Sync mode recalculates delay samples on each BPM change.

## Reverb Algorithms (v1)

| Algorithm | Library | Character | Parameters |
|---|---|---|---|
| Room | Signalsmith (planned) | Tight natural space | Size, pre-delay, diffusion, damp, mod, dirt |
| Hall | Signalsmith (planned) | Long lush decay | Size, pre-delay, diffusion, damp, mod, dirt |
| Plate | Signalsmith (planned) | Dense metallic | Size, pre-delay, diffusion, damp, mod, dirt |
| Spring | Signalsmith (planned) | Lo-fi mechanical | Size, pre-delay, diffusion, damp, mod, dirt |

Reverb has **no mix knob** — it is always a pure send. Shimmer removed from v1 (requires pitch shifting in feedback loop — v2 feature).

**Current implementation (Stage 15+):** All four reverb algorithms use the Signalsmith FDN reverb (integrated Stage 15). Algorithm selection applies parameter presets that bias `roomMs`/`rt20`/`early`/damping for different characters.

### Stage 15: Signalsmith Reverb Integration Plan

**Library acquisition (manual step required):**
The reverb is in the `signalsmith-audio/basics` repo — **not** `signalsmith-audio/dsp`.

```
git clone https://github.com/Signalsmith-Audio/basics.git ThirdParty/signalsmith-basics
```

Headers needed (both required):
- `include/signalsmith-basics/reverb.h` — the reverb effect
- `include/stfx/stfx-library.h` — the LibraryEffect wrapper (reverb.h depends on it)

The repo also contains `chorus.h` and `freq-shifter.h` — candidates for future effect upgrades (Stage 17+).

**CMakeLists.txt change** — add before `target_sources`:
```cmake
target_include_directories(mu-clid PRIVATE ThirdParty/signalsmith-basics/include)
```

**Include in ReverbSlot.h:**
```cpp
#include "signalsmith-basics/reverb.h"
```

**Class to use:** `ReverbFloat` (= `stfx::LibraryEffect<float, ReverbSTFX>`)

**Real API (verified from source):**

```cpp
// Member — replace juce::Reverb in ReverbSlot
ReverbFloat ssReverb;

// prepare() — configure for current sample rate and block size
ssReverb.configure(sampleRate, maxBlockSize, 2 /*channels*/);

// process() — inputs/outputs use [channel][sample] indexing
// Wrap juce::AudioBuffer<float> with a thin adaptor:
struct JuceBufferAdaptor {
    juce::AudioBuffer<float>& buf;
    float* operator[](int ch) { return buf.getWritePointer(ch); }
};
JuceBufferAdaptor io { buffer };
ssReverb.process(io, io, numSamples);

// reset() — clears internal buffers
ssReverb.reset();
```

Parameters are public members assigned directly. They fade to the new value over ~20ms internally — no manual smoothing needed in ReverbSlot.

**Parameter names and algorithm presets:**

| Member | Range | Room | Hall | Plate | Spring |
|---|---|---|---|---|---|
| `ssReverb.roomMs` | 10–200 | 80 | 180 | 50 | 120 |
| `ssReverb.rt20` | 0.01–30 s | 1.2 | 3.5 | 2.0 | 1.8 |
| `ssReverb.early` | 0–2.5 | 1.0 | 1.2 | 1.8 | 0.6 |
| `ssReverb.detune` | 0–50 | 5 | 15 | 8 | 20 |
| `ssReverb.highCutHz` | 1000–20000 | 6000 | 5000 | 8000 | 3500 |
| `ssReverb.highDampRate` | 1–10 | 3 | 4 | 2 | 6 |
| `ssReverb.wet` | 0–1 | 1.0 | 1.0 | 1.0 | 1.0 |
| `ssReverb.dry` | 0–1 | 0.0 | 0.0 | 0.0 | 0.0 |

The UI params (Size, Decay, Diffusion, Damp, Mod) map to `roomMs`, `rt20`, `early`, `highCutHz`/`highDampRate`, and `detune` respectively.

The existing `dirt` (tanh pre-saturation) and pre-delay buffer in `ReverbSlot` sit before the Signalsmith call — keep both as-is.

Spring character: add a 4-stage first-order allpass dispersion chain (poles at 200/400/800/1600 Hz) before the reverb input to simulate frequency-dependent propagation speed of a physical spring coil.

**Reference:** Geraint Luff's walkthrough: `signalsmith-audio.co.uk/writing/2021/lets-write-a-reverb/`

**Pre-delay buffer:** `ReverbSlot` pre-allocates ~100ms of pre-delay buffer (`MaxPreDelaySamples = 192000 / 10`). Pre-delay is applied before the reverb algorithm. Dirt (mild `tanh` saturation) is applied before the reverb for warmth.

## FXAlgorithmDef Data Class

Stores: algorithm ID, name, category, params (array of `FXParamDef`: id, name, min, max, default, units), and `oversamplingFactor`. Drives both the UI (FXRow populates knobs from `params`) and the DSP (EffectSlot selects oversampling factor from def). A static `FXAlgorithmRegistry` provides the canonical ordered lists for both effect algorithms and reverb algorithms.

The oversampling field is retained in the struct for forward compatibility; all current Effect algorithms set it to 1 (bypass).

**Intra-FX routing (Stage 8 API):** `FXChain` exposes `setEffectToDelaySend()`, `setEffectToReverbSend()`, `setDelayToReverbSend()`. These are wired to Mixer channel strip send knobs in Stage 9. The values default to 0.0 — signal flows in series only until Stage 9.

## Effect Algorithm Files (current)

Distortion and filter algorithms now live in `Source/Audio/InsertProcessor.cpp` (used by both
the per-rhythm voice INSERT slot and the master INSERT slot). The Effect *send* slot only hosts
modulation/time algorithms.

Registered in `FXAlgorithmRegistry::effectAlgorithms()`:
- `Source/FX/Effects/ChorusEffect.h` — multi-voice modulated delay.
- `Source/FX/Effects/FlangerEffect.h` — single modulated delay with feedback, through-zero capable.
- `Source/FX/Effects/PhaserEffect.h` — allpass chain modulated by an LFO.
- `Source/FX/Effects/EchoEffect.h` — simple stereo delay (free time). When `eff_algo == Echo`
  the EffectSlot delegates to its embedded DelaySlot for full sync/spread/dirt capability;
  the EchoEffect class itself is currently dormant for that path.
