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
is now part of the voice chain — see [mu-clid/design-voice.md](mu-clid/design-voice.md).

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

**Algorithm switch thread safety:** `EffectSlot::setAlgorithm()` calls `prepare()` internally. It must be called from the **message thread only** — calling it from the audio thread during playback is not safe.

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
| Room | Signalsmith FDN | Tight natural space (decay 0.6 s) | Size, decay, pre-delay, diffusion, damp, mod, dirt |
| Hall | Signalsmith FDN | Long lush decay (decay 2.5 s) | Size, decay, pre-delay, diffusion, damp, mod, dirt |
| Plate | Signalsmith FDN | Dense metallic (decay 1.8 s) | Size, decay, pre-delay, diffusion, damp, mod, dirt |
| Spring | Signalsmith FDN | Lo-fi mechanical (decay 0.35 s) | Size, decay, pre-delay, diffusion, damp, mod, dirt |

Reverb has **no mix knob** — it is always a pure send. Shimmer removed from v1 (requires pitch shifting in feedback loop — v2 feature).

**Current implementation (Stage 15+):** All four reverb algorithms use the Signalsmith FDN reverb (integrated Stage 15). Algorithm selection applies parameter presets that bias `roomMs`/decay/`early`/damping for different characters.

**Decay (`rev_decay`):** 0.1–10 s, default 2.5, placed right after Size on the Reverb row. It is the RT60 in seconds (time to −60 dB). `ReverbSlot::setParam("decay")` passes `decay / 3` to Signalsmith's `rt20`, because the library defines `rt20` as the time to −20 dB (its own comment, "decay to -60dB", is `rt20` × 3). The per-algorithm preset decays (Room 0.6, Hall 2.5, Plate 1.8, Spring 0.35 s) are RT60 values; they were once passed straight to `rt20`, which made the old Hall tail about 7.5 s. Size sets only the room length and is independent of decay (the Signalsmith feedback is normalised per loop).

**Presets and the algorithm change:** choosing an algorithm from the editor menu pushes that algorithm's preset values, including decay, back into the knobs (APVTS). A host automating the algorithm parameter instead pushes the current knob values into the slot, so the knobs stay the source of truth.

**Parameter manifests:** adding `rev_decay` changed the frozen parameter manifests (`tests/manifests`); they are regenerated with `tests/scripts/regenerate-frozen-baselines.ps1 -Reason`. Projects and presets saved before it do not carry a decay value, so they load with the knob's current value (the default 2.5 s on a fresh load), not the decay of their reverb type; a Spring, Room or Plate project from before plays with a different tail until the knob is set. A hook that sets the decay from the preset when the value is missing is a possible follow-up.

### Stage 15: Signalsmith Reverb (complete)

Integrated the Signalsmith FDN reverb (`signalsmith-audio/basics`) as a pimpl in `ReverbSlot`. Headers in `ThirdParty/signalsmith-basics/include/` added as `SYSTEM PRIVATE` in `CMakeLists.txt` to avoid `signalsmith-dsp/windows.h` shadowing the Windows SDK.

**Vendored-library patches (record here; ThirdParty has no patch list, 2026-10-12):** `ThirdParty/signalsmith-basics/.../reverb.h` `ReverbSTFX::reset()` carries one local line, `delay4.reset();`, which upstream omits so a reset left its last output behind; `ReverbSlot::reset()` relies on it. Re-apply after any re-vendoring (grep for `mu patch`).

**Implementation:** `signalsmith::basics::ReverbFloat` wrapped behind a pimpl to keep Signalsmith headers out of `ReverbSlot.h`. Pre-delay buffer and `dirt` (tanh saturation) remain before the reverb call. Algorithm presets bias `roomMs`/`rt20` (from decay)/`early`/`detune`/`highCutHz`/`highDampRate` for each character. UI params (Size, Decay, Diffusion, Damp, Mod) map to those member fields.

**Pre-delay buffer:** ~100ms pre-allocated (`MaxPreDelaySamples = 192000 / 10`), applied before the reverb. Wet scratch buffers also pre-allocated in `prepare()` — no audio-thread allocation.

## FXAlgorithmDef Data Class

Stores: algorithm ID, name, category, params (array of `FXParamDef`: id, name, min, max, default, units), and `oversamplingFactor`. Drives both the UI (FXRow populates knobs from `params`) and the DSP (EffectSlot selects oversampling factor from def). A static `FXAlgorithmRegistry` provides the canonical ordered lists for both effect algorithms and reverb algorithms.

The oversampling field is retained in the struct for forward compatibility; all current Effect algorithms set it to 1 (bypass).

**Intra-FX routing:** `FXChain` exposes `setEffectToDelaySend()`, `setEffectToReverbSend()`, `setDelayToReverbSend()`. Wired to Mixer return-channel send knobs. Default 0.0 — no routing unless explicitly set.

## Effect Algorithm Files (current)

Distortion and filter effect files (`SoftClipEffect`, `HardClipEffect`, `FoldbackEffect`,
`BitcrushEffect`, `LadderFilterEffect`, `CombFilterEffect`) remain on disk but are no longer
registered in `FXAlgorithmRegistry::effectAlgorithms()` and are not instantiated — these
algorithms live in `InsertProcessor` for the voice insert slot instead.

`Source/FX/Effects/FlangerEffect.h` — registered and active in the Effect slot.
`Source/FX/Effects/PhaserEffect.h` — registered and active in the Effect slot.
`Source/FX/Effects/ChorusEffect.h` — registered and active in the Effect slot.
`Source/FX/Effects/EchoEffect.h` — registered and active (Echo = Delay algorithm in Effect slot).
