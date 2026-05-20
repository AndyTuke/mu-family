# μ-Clid — Voice Engine Design Reference

## Per-Rhythm Voice Chain

```
SamplePlayer → Pitch ADSR → ResonantFilter → Filter ADSR → Drive → Amplitude ADSR → Channel fader → FX sends → Internal mixer sum
```

## Voice Chain Stages

| Stage | Parameters | Notes |
|---|---|---|
| Sample playback | Mode (one shot/loop), quality (lo-fi/linear/clean), stretch mode | 4-voice polyphonic pool with round-robin steal. Intent is monophonic character: new hit claims the first idle voice, or steals the oldest via round-robin when all 4 are busy. Always plays from beginning. |
| Voice cut | Overlap fade 1–10ms (default 2ms, user-configurable in Settings) | Outgoing fades out, incoming starts immediately. Prevents clicks. Independent of ADSR. **Not yet implemented** — `VoiceEngine` triggers voices directly with no overlap fade. |
| Loop settings | Tempo (musical/free), length (DropdownSelect + triplet/dotted), fit (pitch/stretch) | Loop mode only. Musical uses DropdownSelect. Free uses ms. |
| Pitch ADSR | Attack, decay, sustain, release, depth, retrigger mode (Reset/Legato) | Depth in semitones above base pitch. |
| Resonant filter | Type (16 modes: LP 6 / LP 12 / LP 24 / BP 12 / BP 24 / HP 6 / HP 12 / HP 24 / Notch / Notch 24 / AP 12 / Comb+ / Comb− / Peak / Lo Shelf / Hi Shelf), cutoff, resonance | SVF (types 0–3), LadderFilter (types 4–6), OnePoleLP/HP (types 7/11), feedback comb (type 8), biquad (types 9/12–14), and mirrored Notch/Comb variants |
| Filter ADSR | Attack, decay, sustain, release, depth, retrigger mode | Depth controls sweep above base cutoff. Default Reset. |
| Insert | Character (14 algorithms: None / Soft Clip / Hard Clip / Fold / Bitcrusher / Clipper / 3-Band EQ / Compressor / Limiter / Ring Mod / Tape Sat / Karplus / Vocoder / Vocoder St), drive amount, output trim, tone filter | Placed after filter, before amp. `InsertProcessor` handles all algorithms inline at native sample rate. |
| Amplitude ADSR | Attack, decay, sustain, release, retrigger mode (Reset/Legato), accent | Reset: retriggers from zero. Legato: retriggers from current level. Default Reset. Accent boost applied when step is accented (see below). |

## Accent

A step is **accented** when the Ring C (Euclid C) pattern fires a hit on the same step as a Ring A+B hit. The accent flag is passed from `SequencerEngine` to `VoiceEngine` alongside the normal hit event.

**Accent boost parameter:**

| Parameter | Range | Default | Notes |
|---|---|---|---|
| Accent | 0–+12 dB | 0 dB | Additional gain applied to accented steps only |

**Signal path:** The accent boost is applied as a scalar gain multiplier at the start of the Amplitude ADSR stage — before the ADSR envelope shape is applied. This means the accent raises the peak level of the voice without altering the envelope shape. A 0 dB accent = unity (no change). +6 dB = approximately double the loudness of non-accented steps.

**Implementation:** `VoiceEngine::triggerVoice()` receives an `isAccented` bool. When true, the amplitude gain for that voice is multiplied by `juce::Decibels::decibelsToGain(accentDb)` before the ADSR processes.

**APVTS param per rhythm:**
```
accentDb  — float  0–12 dB, default 0
```

## Insert Stage

Each rhythm has a dedicated insert effect slot placed between the filter and the amp envelope. The insert defaults to None (unity bypass). Implemented in `Source/Audio/InsertProcessor.{h,cpp}`, called from `VoiceEngine::process()` at native sample rate (no oversampling).

**Parameters:**

| Parameter | Range | Default | Notes |
|---|---|---|---|
| Character | None / Soft Clip / Hard Clip / Fold / Bitcrusher / Clipper / 3-Band EQ / Compressor / Limiter / Ring Mod / Tape Sat | None | Algorithm selector |
| Drive | 0–100% | 0% | Pre-gain / threshold / mix depending on algorithm |
| Output | −24–+24 dB | 0 dB | Post-process level trim (Comp/Limiter use +24 dB for makeup) |
| Tone | 20–20000 Hz | 20000 Hz | 1-pole IIR LP on output; also acts as Mid Hz in EQ mode |
| Rate | 100–48000 Hz | 48000 Hz | Bitcrusher sample-rate reduction |
| Dither | 0–100 | 0 | Bitcrusher TPDF dither / EQ High-shelf gain |

**Algorithms (driveChar index):**

| # | Name | Key DSP |
|---|---|---|
| 0 | None | Bypass |
| 1 | Soft Clip | `tanh` waveshaper with ADAA (`ln(cosh(x))` antiderivative), cosh overflow guard |
| 2 | Hard Clip | Brickwall clamp with ADAA (`x²/2` / `|x|−0.5` antiderivative) |
| 3 | Fold | Triangular foldback (direct — ADAA impractical) |
| 4 | Bitcrusher | 1-pole LP pre-filter → hold-and-sample decimation → TPDF dither → fixed-point quantise |
| 5 | Clipper | Brickwall threshold clamp — Drive = ceiling fraction (0–1); no ADAA |
| 6 | 3-Band EQ | Three biquad IIR filters: Low shelf (fixed 200 Hz), Mid peak (sweepable, Tone Hz), High shelf (fixed 8 kHz); Drive/Dither/Output = Low/High/Mid gain ±18 dB |
| 7 | Compressor | Feed-forward peak detector; ratio 4:1; Drive = threshold (0 → −40 dB) |
| 8 | Limiter | Same as Compressor with ratio 100:1 |
| 9 | Ring Mod | `x *= (1 − mix + sin(phase) × mix)`; Tone = carrier frequency (10–5000 Hz) |
| 10 | Tape Sat | Pre-gain → `tanh` → DC block (HP@20 Hz) → 1-pole LP Tone filter → output trim |

**APVTS params per rhythm (suffix):**
```
drvChar   — int    0–10 (algorithm index)
drvDrv    — float  0–100 (drive / threshold / mix)
drvOut    — float  −24–+24 dB (output / makeup gain)
drvTon    — float  20–20000 Hz (tone LPF / mid EQ freq / ring-mod freq)
drvRate   — float  100–48000 Hz (bitcrusher sample rate)
drvDit    — float  0–100 (dither / EQ high-shelf gain)
```

## Interpolation Quality

| Setting | Algorithm | Character |
|---|---|---|
| Lo-fi (default) | Nearest neighbour | Aliasing and grit on transposed notes. Lo-fi techno character. |
| Linear | Linear interpolation | Slight smoothing, still characterful |
| Clean | Cubic interpolation | Smooth, professional quality |

**Implementation status:** Interpolation quality selection is **not yet implemented**. `SamplePlayer` currently uses nearest-neighbour (integer truncation of `playPos`). All three modes will share the same `SamplePlayer::process()` path via a quality enum parameter when implemented.

## Sample File Handling

- Paths stored as **absolute paths** in ValueTree
- All samples reloaded immediately on DAW project load — plugin ready to play on open
- Missing sample detected immediately on load — sample bar shows warning state
- Auto-relocate missing samples: v2 feature (recursive folder search)
- No relative path fallback in v1 — handled by locate button in sample bar
- Supported formats: WAV, AIFF, MP3, FLAC (via `juce::AudioFormatManager`)

**Thread safety for sample load:** `VoiceEngine` uses `juce::ReadWriteLock`. `loadFile()` runs on the message thread: disk I/O happens outside the lock, then the result buffer is swapped under a write lock (O(1)). `process()` runs on the audio thread under a read lock. This means a sample load will briefly stall the audio thread for the lock acquisition only — not for the disk read.

## FX Send Crossfade Behaviour

| Knob range | Effect/Delay send | Reverb send |
|---|---|---|
| 0–50% | Dry 100%, send scales 0→100% | Dry unaffected, reverb scales 0→100% |
| 50–100% | Send 100%, dry scales 100→0% | Dry unaffected, reverb stays 100% |

Reverb is always a pure send — never reduces the dry signal.

## SoundTouch / TimeStretcherBase

`TimeStretcherBase` is a pure virtual interface wrapping SoundTouch. All time-stretching code must go through this interface — never call SoundTouch directly from VoiceEngine or SamplePlayer. This enables RubberBand swap in v2 without refactoring.

SoundTouch ships as a **DLL** (not statically linked) — required for LGPL compliance.
