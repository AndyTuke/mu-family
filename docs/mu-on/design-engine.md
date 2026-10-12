# μ-On — Engine design

μ-On is a 909-style groove sequencer with one to eight **tracks** (layers), each running one of five
**engines**: Kick (synthesis), Bass (deep synth — the focus), Hat (sample), Snare (sample), and Rumble
(processes another track's audio rather than stepping). Every track has a step row; a Rumble track
ignores its row. Each track renders into its mixer channel via the shared
`MixerEngine::RenderChannelFn` hook; the shared mixer then applies the per-channel strip, the
sidechain (set up in the mixer, e.g. bass ducking the kick), sends, and master. `GrooveVoices`
([mu-on/Source/Audio/GrooveVoices.h](../../mu-on/Source/Audio/GrooveVoices.h)) owns one engine of
each type for every track slot, routes the sequencer's triggers to the engine the track has selected,
and pulls engine params from cached APVTS atomic pointers each block (no per-block string lookups, no
audio-thread allocation). Render order: every non-Rumble track first, then the Rumble tracks, so a
Rumble's source has been rendered this block whatever the track order; a Rumble whose source is out
of range, not live or itself a Rumble processes silence. The `Track` pointers are re-read under the
layer lock every block and never kept across blocks.

Parameters: every engine's parameters exist on every track, engine-qualified (`t{N}_kick_*`,
`t{N}_bass_*`, `t{N}_hat_*`, `t{N}_snare_*`, `t{N}_rumble_*`), plus `t{N}_eng` (the engine) and
`t{N}_src` (the Rumble's source layer). Switching a track's engine keeps the others' settings.

## Engines

### Kick — `KickEngine` (synthesis)
Sine body + an **exponential pitch envelope** (start tune → base) for the thump, an exponential
**amp envelope**, and a `tanh` **drive** for click/punch. Params: `t0_tune`, `t0_ptch` (pitch amount),
`t0_pdec` (pitch decay), `t0_adec` (amp decay), `t0_drive`.

### Bass — `BassEngine` (the focus)
The deepest, most flexible lane. Main oscillator (**Sine / Saw / Square**) + a **sub osc** one
octave down, an **A/D/S amp envelope**, and a low-pass **mu-core `MultiModeFilter`** with its own
**cutoff decay envelope** + **valve drive**. Together these span the requested range:
- **Deep clean** — Sine/low drive/low resonance, modest filter env, sub moderate.
- **Rumble** — sub up, drive up, resonance up, slower filter-env decay; the valve drive adds the
  harmonic grit that reads as "rumble".

Params: `t1_wave`, `t1_sub`, `t1_tune` (root), `t1_cut`, `t1_res`, `t1_env` (filter-env depth),
`t1_edec` (filter-env decay), `t1_atk`, `t1_dec`, `t1_sus`, `t1_drive`. The filter cutoff is updated
at **block rate** from the current envelope value (per-sample cutoff modulation is a refinement).

**Deliberately not yet exposed (would be inert):** per-step **pitch**, **glide**, and an amp
**Release** — all three need a note-off / pitch event the 16-step on/off grid does not yet emit.
They land with per-step pitch (below) so the UI never shows a control that can't act (audit rule).

**Roadmap to deepen the bass:** per-step pitch + glide (a pitched lane row or a piano-roll);
note-off / gate-length per step (enables Release + sustained vs plucky without the sustain hack);
a dedicated resonant **rumble** stage (saturated feedback / a second detuned sub); per-sample
filter-env; an optional drive *insert* via the shared `InsertProcessor`.

### Hat / Snare — `SampleChannel` (sample-based)
Built on the shared mu-core **`SamplePlayer`**: each lane owns a sample buffer and plays it
one-shot per trigger with **tune** (playback ratio) and a **decay gate** (`h_tune`/`h_dec`,
`t{N}_snare_tune`/`_dec`). The default buffer is **generated procedurally** (bright high-passed noise hat;
noise + 180 Hz body tone snare) so the product makes sound with no shipped assets. **The only seam
to change is the buffer** — "Load .wav…" (a file chooser) + factory `.wav` content (shipped via
`juce_add_binary_data`, gated on code-signing like #99) is the next step, reusing the same player.

### Rumble — `RumbleEngine` (processes another track)
Not sequenced — the classic techno **rumble**. It takes the **rendered audio of its source track**
(the Source dropdown; the first Kick by default) and runs it through, in order: input **drive** (the `MultiModeFilter` valve), three tempo-synced
**delay taps** (1/16·2/16·3/16, each with a level), a **dark reverb** (mu-core `ReverbSlot` + a
low-pass, with size + dry/wet mix), a per-bar **volume envelope** (a drawable smooth curve — the
shared `LFOEditor` — held per track, persisted only when it is not the default flat curve, evaluated
audio-side under a try-lock), and a final **multimode filter**. Params:
`t{N}_rumble_drive`, `_d1`/`_d2`/`_d3`, `_size`, `_revmix`, `_revlp`, `_cut`, `_res`.
The reverb tone + the block-rate envelope are deliberately basic for now (framework stage). *(Note:
distinct from the Bass lane's "rumble" voicing above, which is a tonal character of `BassEngine`,
not this lane.)*

## Bass↔Kick sidechain (the key interaction)
This is **not new DSP** — the shared `MixerEngine` already supports **channel-to-channel
sidechain** (`sidechainSource` can point at another channel, with amount + attack/release + an
envelope follower + `sidechainGR` metering). Since the layers became variable (Andy's 2026-10-08
decision) the sidechain lives in the **mixer** only: there is no pre-wired Bass→Kick default and the
engine panel has no Duck / D.Atk / D.Rel knobs. A state saved by the five-lane build carries its
mixer sidechain values, which load as saved. The strips' sources follow their layer through a delete
or reorder (`remapSidechainSources`), as does a Rumble's source.

## Threading / RT safety
All engine state is audio-thread-only. Buffers (sample one-shots, filter scratch, de-interleave)
are allocated in `prepare`. Param values reach the engines through cached `std::atomic<float>*`
read once per block. Triggers are queued by the sequencer (audio thread) and consumed in the same
block. Onset is currently **block-start** (≤ one block of jitter); sample-accurate onset via the
sequencer's per-trigger sample offset is a refinement (the offset is already computed).
