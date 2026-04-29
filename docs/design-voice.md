# μ-Clid — Voice Engine Design Reference

## Per-Rhythm Voice Chain

```
SamplePlayer → Amplitude ADSR → ResonantFilter → Filter ADSR → Sidechain gain reduction → Channel fader → FX sends → Internal mixer sum
```

MIDI mode bypasses the voice chain entirely. Hit event → MidiOutputEngine → MIDI note on/off (user-definable note, fixed velocity or from Euclid C).

## Voice Chain Stages

| Stage | Parameters | Notes |
|---|---|---|
| Sample playback | Mode (one shot/loop), quality (lo-fi/linear/clean), stretch mode | Monophonic — new hit cuts previous with short overlap fade. Always plays from beginning. |
| Voice cut | Overlap fade 1–10ms (default 2ms, user-configurable in Settings) | Outgoing fades out, incoming starts immediately. Prevents clicks. Independent of ADSR. |
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

## Sample File Handling

- Paths stored as **absolute paths** in ValueTree
- All samples reloaded immediately on DAW project load — plugin ready to play on open
- Missing sample detected immediately on load — sample bar shows warning state
- Auto-relocate missing samples: v2 feature (recursive folder search)
- No relative path fallback in v1 — handled by locate button in sample bar
- Supported formats: WAV, AIFF, MP3, FLAC (via `juce::AudioFormatManager`)

## FX Send Crossfade Behaviour

| Knob range | Effect/Delay send | Reverb send |
|---|---|---|
| 0–50% | Dry 100%, send scales 0→100% | Dry unaffected, reverb scales 0→100% |
| 50–100% | Send 100%, dry scales 100→0% | Dry unaffected, reverb stays 100% |

Reverb is always a pure send — never reduces the dry signal.

## SoundTouch / TimeStretcherBase

`TimeStretcherBase` is a pure virtual interface wrapping SoundTouch. All time-stretching code must go through this interface — never call SoundTouch directly from VoiceEngine or SamplePlayer. This enables RubberBand swap in v2 without refactoring.

SoundTouch ships as a **DLL** (not statically linked) — required for LGPL compliance.
