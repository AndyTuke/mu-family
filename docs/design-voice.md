# μ-Clid — Voice Engine Design Reference

## Per-Rhythm Voice Chain

```
SamplePlayer → Amplitude ADSR → ResonantFilter → Filter ADSR → Sidechain gain reduction → Channel fader → FX sends → Internal mixer sum
```

MIDI mode bypasses the voice chain entirely. Hit event → MidiOutputEngine → MIDI note on/off (user-definable note, fixed velocity or from Euclid C).

**MidiOutputEngine defaults (Stage 8):** note = 36 (C2), channel = 1, note duration = 20ms fixed. Note duration is currently not tempo-linked; it will become configurable (or tied to step length) in Stage 10 when MIDI mode is fully wired. The trigger call from `processBlock` is gated by the MIDI mode flag, which defaults to sample mode — so MidiOutputEngine produces no output until Stage 10 wiring.

## Voice Chain Stages

| Stage | Parameters | Notes |
|---|---|---|
| Sample playback | Mode (one shot/loop), quality (lo-fi/linear/clean), stretch mode | 4-voice polyphonic pool with round-robin steal. Intent is monophonic character: new hit claims the first idle voice, or steals the oldest via round-robin when all 4 are busy. Always plays from beginning. |
| Voice cut | Overlap fade 1–10ms (default 2ms, user-configurable in Settings) | Outgoing fades out, incoming starts immediately. Prevents clicks. Independent of ADSR. Not yet implemented — current `VoiceEngine` triggers voices directly with no overlap fade; add in Stage 11 polish. |
| Loop settings | Tempo (musical/free), length (TimeSelector + triplet/dotted), fit (pitch/stretch) | Loop mode only. Musical uses TimeSelector. Free uses ms. |
| Amplitude ADSR | Attack, decay, sustain, release, retrigger mode (Reset/Legato) | Reset: retriggers from zero. Legato: retriggers from current level. Default Reset. |
| Resonant filter | Type (LP/HP/BP/notch), cutoff, resonance | `juce::dsp::StateVariableTPTFilter` |
| Filter ADSR | Attack, decay, sustain, release, depth, retrigger mode | Depth controls sweep above base cutoff. Default Reset. |

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
