# μ-Clid — Voice Engine Design Reference

## Per-Rhythm Voice Chain

```
SamplePlayer → Pitch ADSR → ResonantFilter → Filter ADSR → Drive → Amplitude ADSR → Channel fader → FX sends → Internal mixer sum
```

MIDI mode bypasses the voice chain entirely. Hit event → MidiOutputEngine → MIDI note on/off (user-definable note, fixed velocity or from Euclid C).

**MidiOutputEngine defaults (Stage 8):** note = 36 (C2), channel = 1, note duration = 20ms fixed. Note duration is currently not tempo-linked; it will become configurable (or tied to step length) in Stage 10 when MIDI mode is fully wired. The trigger call from `processBlock` is gated by the MIDI mode flag, which defaults to sample mode — so MidiOutputEngine produces no output until Stage 10 wiring.

## Voice Chain Stages

| Stage | Parameters | Notes |
|---|---|---|
| Sample playback | Mode (one shot/loop), quality (lo-fi/linear/clean), stretch mode | 4-voice polyphonic pool with round-robin steal. Intent is monophonic character: new hit claims the first idle voice, or steals the oldest via round-robin when all 4 are busy. Always plays from beginning. |
| Voice cut | Overlap fade 1–10ms (default 2ms, user-configurable in Settings) | Outgoing fades out, incoming starts immediately. Prevents clicks. Independent of ADSR. Not yet implemented — current `VoiceEngine` triggers voices directly with no overlap fade; add in Stage 11 polish. |
| Loop settings | Tempo (musical/free), length (DropdownSelect + triplet/dotted), fit (pitch/stretch) | Loop mode only. Musical uses DropdownSelect. Free uses ms. |
| Pitch ADSR | Attack, decay, sustain, release, depth, retrigger mode (Reset/Legato) | Depth in semitones above base pitch. |
| Resonant filter | Type (LP/HP/BP/notch), cutoff, resonance | `juce::dsp::StateVariableTPTFilter` |
| Filter ADSR | Attack, decay, sustain, release, depth, retrigger mode | Depth controls sweep above base cutoff. Default Reset. |
| Drive | Character (Soft/Hard/Fold/Bit), drive amount, output trim, tone filter | Placed after filter, before amp. See Drive section below. |
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

## Drive Stage

Each rhythm has a dedicated drive insert placed between the filter and the amp envelope. Drive is
always in the signal path but defaults to unity (drive = 0%, output = 0 dB, no tone cut).

**Parameters:**

| Parameter | Range | Default | Notes |
|---|---|---|---|
| Character | None / Soft / Hard / Fold / Bitcrusher | None | Algorithm selector; see below |
| Drive | 0–100% | 0% | 0% = unity through all characters |
| Output | −24–0 dB | 0 dB | Post-drive level trim to compensate for loudness increase |
| Tone | 20–20000 Hz | 20000 Hz | First-order IIR low-pass on the driven signal; at 20kHz = flat |

**Character algorithms** (same DSP as the removed shared Effect algorithms):

| Character | Algorithm | Oversampling |
|---|---|---|
| None | Bypass (unity) | 1× (bypass) |
| Soft | `std::tanh` waveshaper | 1× (bypass) |
| Hard | `juce::jlimit` clamp | 4× |
| Fold | Triangular foldback | 4× |
| Bitcrusher | Fixed-point quantise + rate decimator | 2× |

Oversampling applies to the waveshaping only; the drive stage is otherwise inline at the voice's
native sample rate. Each active voice runs its own `OversampledProcessor` instance. Because there
are at most 4 concurrent voices per rhythm and at most 8 rhythms, the maximum simultaneous
oversamplers is 32 — acceptable at 2× or 4×.

**Implementation:** Add a `DriveStage` class in `Source/Audio/` that wraps `OversampledProcessor`
and switches algorithm on character change (message-thread only, same constraint as
`EffectSlot::setAlgorithm`). `VoiceEngine` holds one `DriveStage` per voice slot and routes
audio through it after the filter ADSR.

**APVTS params per rhythm (suffix):**
```
driveChar   — int  0=None, 1=Soft, 2=Hard, 3=Fold, 4=Bitcrusher
driveDrive  — float 0–100
drvOut      — float -24–0 dB
driveTone   — float 20–20000 Hz
```

## Interpolation Quality

| Setting | Algorithm | Character |
|---|---|---|
| Lo-fi (default) | Nearest neighbour | Aliasing and grit on transposed notes. Lo-fi techno character. |
| Linear | Linear interpolation | Slight smoothing, still characterful |
| Clean | Cubic interpolation | Smooth, professional quality |

**Implementation status:** Interpolation quality selection is not yet implemented. `SamplePlayer` currently uses nearest-neighbour (integer truncation of `playPos`). Quality selector wiring is Stage 10. All three modes will share the same `SamplePlayer::process()` path via a quality enum parameter.

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
