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

| Algorithm | Category | DSP Approach | Key Parameters | Oversampling |
|---|---|---|---|---|
| Soft clip | Distortion | `std::tanh` waveshaper + IIR tone LP | Drive, output, tone | Optional (1x in v1) |
| Hard clip | Distortion | `juce::jlimit` clamp + IIR tone LP | Drive, threshold, output, tone | Required — 4x |
| Foldback | Distortion | Triangular fold math + IIR tone LP | Drive, folds, output, tone | Required — 4x |
| Bitcrush | Distortion | Fixed-point quantise + rate decimator + IIR tone LP | Bits, rate, output, tone | Required — 2x |
| Ladder filter | Filter | `juce::dsp::LadderFilter` | Cutoff, resonance, drive, mode (LP/HP/BP) | Drive path — 2x |
| Chorus | Modulation | Manual delay buffer + per-voice sine LFO | Rate, depth, voices (2–4), spread, mix | None |
| Phaser | Modulation | First-order allpass chain + sine LFO + feedback | Rate, depth, stages (up to 12), feedback, mix | None |
| Comb filter | Filter | Feedback delay line | Freq, feedback, output, mix | None |

**Tone parameter (distortion effects):** v1 implements tone as a `juce::dsp::IIR::Filter` (ProcessorDuplicator) low-pass filter after the waveshaper, driven by a single cutoff frequency. The full LP/HP/BP/peak multi-mode selector is a v2 enhancement.

**Algorithm switch thread safety:** `EffectSlot::setAlgorithm()` calls `prepare()` internally. It must be called from the **message thread only** — calling it from the audio thread during playback is not safe. Full audio-thread-safe hot-swap (via atomic pointer exchange) is deferred to Stage 10 APVTS wiring.

**Oversampling:** Implemented via `OversampledProcessor`, which wraps `juce::dsp::Oversampling<float>` with FIR equiripple half-band antialiasing filter. Factor 1 = bypass (no allocation). Factor 2 = one stage (2x). Factor 4 = two stages (4x). The algorithm's `prepareInner()` and `processInner()` receive the already-upsampled block and the oversampled rate.

## Delay Parameters

| Section | Parameters |
|---|---|
| Time — sync mode | TimeSelector (1, 1/2, 1/4, 1/8, 1/16, 1/32 + triplet/dotted) + ms display at current BPM |
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

**Current implementation (Stage 8):** All four reverb algorithms use `juce::Reverb` (Freeverb) as a placeholder. Algorithm selection applies parameter presets that bias size/damp/diffusion/pre-delay for different characters. Replace with Signalsmith Reverb (MIT, header-only) in a future stage — `ReverbSlot` is self-contained and the swap requires only changes inside that class.

**Pre-delay buffer:** `ReverbSlot` pre-allocates ~100ms of pre-delay buffer (`MaxPreDelaySamples = 192000 / 10`). Pre-delay is applied before the reverb algorithm. Dirt (mild `tanh` saturation) is applied before the reverb for warmth.

## FXAlgorithmDef Data Class

Stores: algorithm ID, name, category, params (array of `FXParamDef`: id, name, min, max, default, units), and `oversamplingFactor`. Drives both the UI (FXRow populates knobs from `params`) and the DSP (EffectSlot selects oversampling factor from def). A static `FXAlgorithmRegistry` provides the canonical ordered lists for both effect algorithms and reverb algorithms.

**Intra-FX routing (Stage 8 API):** `FXChain` exposes `setEffectToDelaySend()`, `setEffectToReverbSend()`, `setDelayToReverbSend()`. These are wired to Mixer channel strip send knobs in Stage 9. The values default to 0.0 — signal flows in series only until Stage 9.
