# μ-Clid — Voice Engine Design Reference

Authoritative description of the per-rhythm voice chain. Updated for the post-T12 signal flow (v1.0.609+). When in doubt, the source of truth is [mu-core/Audio/VoiceEngine.cpp](../../mu-core/Audio/VoiceEngine.cpp); this doc is a guided summary.

---

## Signal chain

```
SamplePlayer (with per-sample pitch ratio)
   ↓ raw audio per voice
   ↓ MULTIPLE voices accumulate into tempBuffer (no per-voice env shaping)
tempBuffer
   ↓ MultiModeFilter      (filter type + cutoff modulated by filter ADSR + low-cut HPF)
   ↓ ampEnv               (engine-level ADSR multiplied into the buffer)
   ↓ InsertProcessor      (insert FX algorithm — 14 options, runs at native SR)
   ↓ × dBToGain(ampLevel) × accentGain (per-block scalar)
output bus
```

**Why the env sits between filter and insert** ([VoiceEngine.cpp:180-195](../../mu-core/Audio/VoiceEngine.cpp#L180-L195)):

- Filter resonance / comb feedback in the filter stage IS env-gated — at env idle the filter output goes to silence, so resonant tails fade with the envelope's release (matches T11 expectation).
- Insert-stage FX receive env-shaped INPUT but their own internal state (delay-line feedback, biquad memory) decays at the FX's own natural rate, NOT the env's. So Karplus rings past env idle, Vocoder analysis tails through, etc. (matches T12 expectation).
- Retired engines (hot-swap) hold a 2 s post-env-idle drain budget on `retireDrainBlocks` so audible insert tails complete before engine destruction. The env-gate at the filter output keeps the retired engine's filter resonance from leaking into the next active rhythm (#416 "overlay" prevention).

**Engine-level amp envelope (#627 / Stage 34)**: there is ONE `ampEnv` per VoiceEngine, not one-per-voice. The original per-voice array (`ampEnvs[MaxVoices]`) was collapsed to a single ADSR so the post-filter chain has a single sensible env to gate against. Trade-off: rapid retriggers reset the engine env — fine for drum-style triggers, but overlapping pad voices would re-attack. Pattern Legato (#419) is the escape hatch for the pad case.

---

## Pitch

Per-sample pitch ratio is computed in [VoiceEngine.cpp:161-175](../../mu-core/Audio/VoiceEngine.cpp#L161-L175) from base + envelope + modulation, then passed to `SamplePlayer::process` as a buffer of ratios so the pitch envelope is sample-accurate.

| Param          | Range                  | Unit  | APVTS suffix |
|----------------|------------------------|-------|--------------|
| Octave         | ±3                     | oct   | `pitchOct`   |
| Semitone       | ±12                    | semi  | `pitchSemi`  |
| Fine           | ±100                   | cent  | `pitchFine`  |
| Env Attack     | 0–10                   | s     | `pEnvAtk`    |
| Env Decay      | 0–10                   | s     | `pEnvDec`    |
| Env Sustain    | 0–100                  | %     | `pEnvSus`    |
| Env Release    | 0–10                   | s     | `pEnvRel`    |
| Env Depth      | 0–24                   | semi  | `pEnvDep`    |

Combined static shift `pitchOct·12 + pitchSemi + pitchFine/100` is clamped to ±48 semitones at the engine ([VoiceEngine.cpp:161](../../mu-core/Audio/VoiceEngine.cpp#L161)). Modulation adds on top via `pitchMod` (semitones).

---

## Filter (multi-mode)

[mu-core/Audio/MultiModeFilter.{h,cpp}](../../mu-core/Audio/MultiModeFilter.h) — owns SVF / Ladder / 1-pole / biquad / comb state. Type-dispatch is by integer index but persisted as a stable name string via [kFilterTypeNames](../../mu-core/Audio/AlgorithmNames.h).

| #  | Name        | Implementation                                            |
|----|-------------|-----------------------------------------------------------|
| 0  | LP12        | `juce::dsp::StateVariableTPTFilter` low-pass              |
| 1  | HP12        | SVF high-pass                                             |
| 2  | BP12        | SVF band-pass                                             |
| 3  | Notch12     | SVF: dry − BP (via scratch buffer)                        |
| 4  | LP24        | `juce::dsp::LadderFilter` LPF24                           |
| 5  | HP24        | LadderFilter HPF24                                        |
| 6  | BP24        | LadderFilter BPF24                                        |
| 7  | LP6         | `OnePoleLP` per channel                                   |
| 8  | CombPlus    | Circular delay line, positive feedback                    |
| 9  | AP12        | 2nd-order all-pass biquad                                 |
| 10 | Notch24     | LadderFilter BPF24, dry − BP                              |
| 11 | HP6         | `OnePoleHP` (= x − OnePoleLP)                             |
| 12 | Peak        | Biquad peak (Audio EQ Cookbook), Q from `filterRes`       |
| 13 | LoShelf     | Biquad low shelf                                          |
| 14 | HiShelf     | Biquad high shelf                                         |
| 15 | CombMinus   | Comb with negative feedback                               |

| Param          | Range          | Unit | APVTS suffix |
|----------------|----------------|------|--------------|
| Type           | 0–15 (name)    | —    | `fltType`    |
| Cutoff         | 20–20000       | Hz   | `fltCut`     |
| Resonance      | 0–0.99         | —    | `fltRes`     |
| Low Cut (HPF)  | 0–1000         | Hz   | `fltLoCut`   |
| Env Attack     | 0–10           | s    | `fEnvAtk`    |
| Env Decay      | 0–10           | s    | `fEnvDec`    |
| Env Sustain    | 0–100          | %    | `fEnvSus`    |
| Env Release    | 0–10           | s    | `fEnvRel`    |
| Env Depth      | 0–48           | semi | `fEnvDep`    |

`modCutoff = filterCutoff · 2^(filterEnvVal · filterEnvDepth / 12)`, clamped 20–20000 Hz ([VoiceEngine.cpp:208-210](../../mu-core/Audio/VoiceEngine.cpp#L208-L210)). The low-cut is a separate 4-pole HPF inline after the main filter; bypassed when `fltLoCut ≤ 0.5 Hz`.

---

## Amp + Accent

Amp envelope is the SINGLE ADSR `ampEnv` ([VoiceEngine.h:93-104](../../mu-core/Audio/VoiceEngine.h#L93-L104)) applied to the post-filter signal BEFORE the insert ([VoiceEngine.cpp:226](../../mu-core/Audio/VoiceEngine.cpp#L226)). The output mix multiplies by `dBToGain(ampLevel) · accentGain` ([VoiceEngine.cpp:234-237](../../mu-core/Audio/VoiceEngine.cpp#L234-L237)).

| Param            | Range     | Unit | APVTS suffix |
|------------------|-----------|------|--------------|
| Level            | −60..+6   | dB   | `ampLvl`     |
| Env Attack       | 0–10      | s    | `aEnvAtk`    |
| Env Decay        | 0–10      | s    | `aEnvDec`    |
| Env Sustain      | 0–100     | %    | `aEnvSus`    |
| Env Release      | 0–10      | s    | `aEnvRel`    |
| Accent           | 0–12      | dB   | `accentDb`   |

**Release-to-end mode**: setting Release to its max (10 s in storage) flips `ampRelToEnd = true`. In that mode the env is NOT note-off'd on retire; the sample plays through to its natural end at the current env level. `isFullyDrained` switches to a voices-only drain criterion in this mode ([VoiceEngine.cpp:265-273](../../mu-core/Audio/VoiceEngine.cpp#L265-L273)).

**Accent**: when the rhythm's Euclid Ring C lands a hit on the same step as an A+B hit, the trigger arrives with `isAccented = true` and `accentGain = dBToGain(accentDb)` is applied as a per-block multiplier ([VoiceEngine.cpp:79](../../mu-core/Audio/VoiceEngine.cpp#L79)). 0 dB = unity (no boost).

**Storage unit (#598 Step 0, v1.0.607+)**: `ampLevel` is stored in dB across UI + APVTS + voiceParams. The engine converts to linear gain once per block via `juce::Decibels::decibelsToGain` at the output multiply. Sliders and APVTS values are 1:1 — no conversion lambda in the UI.

---

## Insert (post-amp-env)

[mu-core/Audio/InsertProcessor.{h,cpp}](../../mu-core/Audio/InsertProcessor.h) dispatches over the 14 algorithms (count = `mu_audio::kInsertAlgorithmCount`). Each algorithm reads 4 generic slot parameters from `VoiceParams::insertParam[0..3]` (normalised 0..1) and de-normalises through `mu_ui::normToActual(insertParam[N], insertAlgo, N)` using the per-algo config in [mu-core/Audio/InsertSlotConfig.h](../../mu-core/Audio/InsertSlotConfig.h).

| #  | Algorithm | Slot 0      | Slot 1       | Slot 2       | Slot 3        |
|----|-----------|-------------|--------------|--------------|---------------|
| 0  | None      | bypass      | —            | —            | —             |
| 1  | SoftClip  | Drive 0–100 | Output −24–0 dB | —         | LPF 20–20k Hz |
| 2  | HardClip  | Drive 0–100 | Output       | —            | LPF           |
| 3  | Fold      | Drive 0–100 | Output       | —            | LPF           |
| 4  | Bitcrusher| Bits 1–16   | Rate 100–48k Hz | Dither 0–100 % | LPF      |
| 5  | Clipper   | Thresh 0–100| Output       | —            | LPF           |
| 6  | EQ        | Low ±18 dB  | Mid ±18 dB   | Mid Hz 200–8k| High ±18 dB   |
| 7  | Compressor| Thresh 0–100| Output ±24 dB| Attack 0–100 ms | Release 20–2000 ms |
| 8  | Limiter   | Ceiling 0–100 | Output     | Attack       | Release       |
| 9  | RingMod   | Mix 0–100 % | —            | —            | Freq 10–5000 Hz |
| 10 | TapeSat   | Drive 0–100 | Output       | —            | Tone 200–20k Hz |
| 11 | Karplus   | Note 0–11 (C–B) | Octave 0–3 | Feedback 0–100 % | LPF       |
| 12 | Vocoder   | Wave 0–3 (Sine/Saw/White/Pink) | Unison 0–6 | Octave 1–5 | Note 1–12 |
| 13 | VocoderSt | (same as Vocoder)              |          |          |              |

| APVTS suffix | Storage           | Notes |
|--------------|-------------------|-------|
| `drvChar`    | Algorithm name (string in preset XML; int in APVTS)    | 0..13; index ≤ kInsertAlgorithmCount−1 |
| `insP1..P4`  | float 0..1 normalised  | De-normalised per-algo at process time |

The single generic-slot model (Stage 36 / #611) replaced an earlier 9-named-field design (`drvDrv`, `drvOut`, `drvTon`, `drvBits`, `drvRate`, `drvDit`, `eqMidGain`, `eqLowGain`, `eqHighGain`). The named-field design overloaded the same slots with different semantics depending on algorithm; the new model gives each algorithm clean independent slots with their own ranges + skews.

---

## Polyphony

`MaxVoices = 4` voices per VoiceEngine. `VoiceEngine::trigger` ([VoiceEngine.cpp:75-130](../../mu-core/Audio/VoiceEngine.cpp#L75-L130)) claims the first inactive slot or steals the oldest via round-robin when all four are busy. When `voiceMono = true` every hit forces voices[0], skipping the search.

All four voices share the same downstream filter / lowCut / ampEnv / insert chain — there is one filter, one ampEnv, one insert instance per VoiceEngine. The reason ([design-627-filter-then-amp.md](design-627-filter-then-amp.md#architectural-choice--engine-level-vs-per-voice-amp-envelope)): per-voice filter+insert would 4× the DSP cost and state for a feature most patterns don't exercise (μ-Clid is primarily a drum-trigger sequencer).

---

## Hot-swap retirement

`markRetired()` ([VoiceEngine.cpp:243-273](../../mu-core/Audio/VoiceEngine.cpp#L243-L273)) is called when the message thread swaps a rhythm during playback. The retired engine keeps rendering into the same mixer channel until `isFullyDrained()` returns true:

- `!ampRelToEnd`: `ampEnv.noteOff()` triggers the release phase. Drain completes when env hits idle AND `retireDrainBlocks` (≈ 2 s of process() calls) have elapsed past env idle. The drain budget covers feedback-based insert tails (Karplus etc.) which decay independently of env.
- `ampRelToEnd`: env is NOT noteOff'd. Drain completes when all four sample-player voices finish naturally.

Filter envelope + pitch envelope are always noteOff'd. Filter / insert state is NOT reset (#627) — they ring through the env release rather than getting truncated.

---

## Persistence

All voice-chain state round-trips through APVTS via the declarative [mu-clid/Source/Persistence/RhythmParamTable.h](../../mu-clid/Source/Persistence/RhythmParamTable.h) table. Adding a new per-rhythm parameter means: (1) add one entry there + (2) one matching APVTS layout line in `createParameterLayout`. Every preset save/load/sync path iterates the same table.

Preset format details + algorithm-name contracts: [docs/preset-format.md](preset-format.md).

---

## Related documents

- [design-627-filter-then-amp.md](design-627-filter-then-amp.md) — Stage 34 signal-flow refactor + the T12 chain reorder.
- [design-seamless-hotswap.md](design-seamless-hotswap.md) — Stage 34 rhythm hot-swap mechanics (closed).
- [../design-fx.md](../design-fx.md) — global FX chain (Effect / Delay / Reverb sends + master insert; family-shared).
- [preset-format.md](preset-format.md) — `.muRhythm` / `.muClid` XML schema.
