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
| Soft clip | Distortion | WaveShaper + tanh | Drive, output, tone (LP/HP/BP/peak + freq/res/mix) | Optional |
| Hard clip | Distortion | WaveShaper + clamp | Drive, threshold, output, tone | Required — 4x |
| Foldback | Distortion | WaveShaper + fold math | Drive, folds, output, tone | Required — 4x |
| Bitcrush | Distortion | Custom quantise | Bits, rate, output, tone | Required — 2x |
| Ladder filter | Filter | `juce::dsp::LadderFilter` | Cutoff, resonance, drive, mode (LP/HP/BP) | Drive path — 2x |
| Chorus | Modulation | DelayLine + LFO | Rate, depth, voices, spread, mix | None |
| Phaser | Modulation | Allpass chain + LFO | Rate, depth, stages, feedback, mix | None |
| Comb filter | Filter | DelayLine + feedback | Freq, feedback, output, mix | None |

## Delay Parameters

| Section | Parameters |
|---|---|
| Time — sync mode | TimeSelector (1, 1/2, 1/4, 1/8, 1/16, 1/32 + triplet/dotted) + ms display at current BPM |
| Time — free mode | ms with NudgeInput (step sizes 1, 5, 10ms) |
| Repeats | Feedback, spread, dirt (saturation on feedback path) |
| Intra-FX | Delay→Reverb send knob on Delay return channel in mixer |

## Reverb Algorithms (v1)

| Algorithm | Library | Character | Parameters |
|---|---|---|---|
| Room | Signalsmith | Tight natural space | Size, pre-delay, diffusion, damp, mod, dirt |
| Hall | Signalsmith | Long lush decay | Size, pre-delay, diffusion, damp, mod, dirt |
| Plate | Signalsmith / FVerb | Dense metallic | Size, pre-delay, diffusion, damp, mod, dirt |
| Spring | Custom | Lo-fi mechanical | Size, pre-delay, diffusion, damp, drip, mod, dirt |

Reverb has **no mix knob** — it is always a pure send. Shimmer removed from v1 (requires pitch shifting in feedback loop — v2 feature).

## FXAlgorithmDef Data Class

Stores: algorithm ID, name, description, category, rows, params, `visibleWhen` conditions, oversampling flag per algorithm. `FXAlgorithmDef` drives both the UI (parameter rows, visibility conditions) and the DSP routing.
