# Manual Additions Draft

Content below needs to be merged into `docs/mu-Clid User Manual.docx`.
Each section is marked with its heading level for insertion.

---

## Insert Algorithms — Karplus-Strong (algorithm 11)

**Karplus-Strong** is a physical-modelling plucked-string synthesiser. Rather than processing an external sample, it *generates* a pitched string tone from an impulse, using a short feedback delay line with a one-pole low-pass filter. The result is the characteristic decaying pluck of a guitar, harp, or bass string.

**Controls:**

| Knob | Range | Description |
|------|-------|-------------|
| Note | C, D, E, F, G, A, B | Target pitch note name |
| Octave | 0 – 3 | Target octave (0 = ~32 Hz / SPN C1, 3 = ~494 Hz / SPN B4) |
| Feedback | 0 – 100% | Sustain of the string ring. Below ~95% the tone dies quickly; at 100% it rings indefinitely. |
| LPF | 20 Hz – 20 kHz | Feedback low-pass cutoff. Lower values produce a darker, faster-decaying pluck; 20 kHz effectively bypasses the filter. |
| Output | -24 – 0 dB | Output level of the Karplus-Strong signal. |

**Note:** Pitch changes are smoothed over ~15 ms so modulating the Note or Octave produces a smooth slide rather than a click.

---

## Insert Algorithms — Vocoder (algorithm 12)

**Vocoder** is a voice/carrier analysis-synthesis effect. The incoming sample audio acts as the modulator signal; a pitched internal carrier is generated using the Note and Octave parameters. The carrier's spectrum is shaped by the envelope of the 20 analysis bands extracted from the modulator. The result is the recognisable "robot voice" or vocoder sound.

**Controls:**

| Knob | Range | Description |
|------|-------|-------------|
| Note | 0 – 6 (C–B) | Carrier pitch note |
| Octave | 1 – 5 | Carrier octave |
| Unison | 0 – 6 | Number of detuned carrier voices stacked (0 = one voice, 6 = seven voices). Adds width and richness at the cost of CPU. |
| Wave | Sine, Saw, White, Pink | Carrier waveform. Sine and Saw are pitched (Note/Octave controls active); White and Pink are noise carriers — Note/Octave/Unison are greyed out for noise modes. |
| Output | -24 – 0 dB | Output level. Default is -20 dB to compensate for the 20-band summation gain. |

---

## Insert Algorithms — Vocoder Stereo / VocoderSt (algorithm 13)

**VocoderSt** is the stereo version of the Vocoder (#12 above). It performs independent analysis on left and right modulator channels and applies separate carrier envelopes per channel, preserving stereo width from the source sample. Controls are identical to Vocoder.

Use Vocoder for mono material or when CPU is a concern; use VocoderSt for stereo samples where you want to preserve spatial information.

---

## Pattern Legato (Reset Steps)

μ-Clid's Euclidean sequencer normally runs in **free mode**: generators A and B loop independently at their own step counts, and the combined rhythm never resets.

**Fixed-length mode** (pattern legato / Reset Steps) locks the combined pattern to a specific step count. When the step counter reaches that count, both generators reset to step 0 simultaneously, creating a repeating phrase of a defined length. This is useful for building groove patterns that line up with bars.

*Note: The Reset Steps value is persisted in presets via the `rstSt` APVTS parameter. A dedicated UI control for setting it from the sequencer panel is planned.*

---

## Intra-FX Routing (Effect → Delay → Reverb)

The FX chain provides three serial processing slots — **Effect**, **Delay**, and **Reverb** — plus two inter-slot sends that let you route processed signal between them:

- **Effect → Delay send**: a portion of the Effect output is blended into the Delay input before Delay processing.
- **Delay → Reverb send** (the "Reverb send" knob on each channel strip): a portion of the Delay output is blended into the Reverb input.

By combining these sends you can create effect chains like chorus → slapback → room reverb without an external routing matrix. The sends are post-effect / post-delay dry signal; the dry signal always flows to the next stage in full.

---

## Hot-Swap (Rhythm Preset Hot-Swap During Playback)

You can load a new rhythm preset onto a slot **while the transport is playing** without stopping playback.

**How it works:**

1. Click a rhythm preset in the preset dropdown while the sequencer is running.
2. A loading badge (🔄) appears on the rhythm's sidebar item.
3. At the next loop boundary of that rhythm slot, the new preset is committed: the new rhythm starts playing immediately.
4. If the old rhythm had a sample with a long tail (pad, long cymbal), the tail continues playing briefly while the new rhythm starts — this produces a smooth crossover rather than an abrupt cut.

**Swap mode** (the swap toggle on the rhythm panel) controls when the commit happens:
- **Bar**: commit at the next 1-bar boundary (DAW sync required).
- **Pattern**: commit at the next pattern-end boundary of the current rhythm.
- **Immediate**: commit on the next audio block (~1 ms latency).

Hot-swap requires the transport to be running. If the transport is stopped, the preset loads immediately with no badge.
