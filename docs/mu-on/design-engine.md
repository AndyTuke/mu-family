# μ-On — Engine design

μ-On is a 909-style groove sequencer with one to eight **tracks** (layers), each running one of five
**engines**: Kick (synthesis), Bass (deep synth — the focus), Hat (sample), Snare (sample), and Rumble
(processes another track's audio rather than stepping). Every track has a step row; a Rumble track
ignores its row. Each track is a shared-contract layer (`Track : Layer`,
[Sequencer/Track.h](../../mu-on/Source/Sequencer/Track.h)) rendered by the shared block driver
(`ProcessorBase::renderAndMix`, [design.md 2.1](../mu-tronic/design.md)); the shared mixer then applies the
per-channel strip, the sidechain (set up in the mixer, e.g. bass ducking the kick), sends, and master.
**A track owns its engines**: all five (Kick, Bass, Hat, Snare, Rumble), each a `TrackEngine`
([Audio/TrackEngine.h](../../mu-on/Source/Audio/TrackEngine.h), one interface over the engine classes),
prepared and held for the track's life; it plays the one its `t{N}_eng` parameter names, so a change of that
parameter from any thread (host automation included) takes effect in the same block. A track whose host fixes
its engine (`engineLocked`, a mu-Tronic layer) holds only that one, so a 16-slot instance does not carry five
engines per slot. `Track::bindParams` caches the
slot's APVTS atomics once (no per-block string lookups); `Track::render` resolves the selected engine's
params **through the track's own `ModulationMatrix`** (proportion-space seed, matrix, write-back; try-lock,
zero-cost when unused; the last values are held on a lock miss), clocks the track's step row and triggers
the engine at the step's sample offset, then renders it, with no audio-thread allocation. Engines are built,
prepared and freed only on the message thread (`Track::syncEngine`) and installed under the track's engine
lock; for a locked track a missing engine renders silence until built. The engines travel with the track
through add / delete / reorder (a moved track's engines are all reset to their prepared state). Render order: a Rumble is a priority-1
layer, so every other track renders first and a Rumble reads its source's block through
`RenderContext::peer(slot)` (post-insert, pre-strip); a Rumble whose source is out of range, not live or
itself a Rumble processes silence, so its tail still dies away.

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

**Gate, release, tied steps and glide.** A step holds the note to the end of the step, or of the run of tied steps it starts (the clock passes the hold in samples), then the amp envelope releases (`rel`, 5 to 2000 ms). A step is tied by dragging across it and the step before (`StepRow` tie flag on the later step, saved as `tie`); a tied step does not retrigger, the voice carries on and slides to the new Tune over `glide` (0 to 500 ms, 0 = jump). `rel` and `glide` are appended modulation targets. The voice panel puts the oscillator and filter on the first row and the amp envelope (Atk, Dec, Sus, Rel) and Glide on a second row (`ParamKnobGrid::Spec::newRow`; the grid drops a knob size when its rows do not fit the height).

### Chord — `ChordEngine` (a chord player for dub-techno stabs)

The seventh engine (`EngineType::Chord`, layer type `Chord`, ids `t{N}_chord_*`: `wave` Saw / Square / Pulse, `cut`, `res`, `drive` (on the filter input), `env`, `edec`, `atk`, `dec`, `sus`, `rel`; the same nine closing modulation rows as every engine). Up to six band-limited (PolyBLEP) oscillators, one for each tone of the chord, sum into one low-pass `MultiModeFilter` with its own decay envelope on the cutoff, then one A/D/S/R amp envelope; the chord is held to the end of its step (or tied run) and released. The track insert follows as for every engine.

**The step names the chord** (`StepRow`: `chord`, `inv`, `spread`, plus the shared `note` and `oct`, saved only when not default). The Note is the root, a degree of the master scale (0 = the key note) and the Octave moves it; `mu_on::voiceChord` ([ChordVoicing.h](../../mu-on/Source/Audio/ChordVoicing.h)) turns the step into MIDI notes: the chord list (Scale 7th = stacked thirds of the scale, so the key gives the chord; then Major, Minor, Sus2, Sus4, Maj7, Min7, Dom7, Min9, Maj9, Dom9, Min11, 6th, Min6, Add9, Dim, Aug, Power, built on the root), each inversion lifts the lowest tone an octave, and Spread 0 to 4 lifts the tone at position i by floor(i x spread / 2) more octaves. A chromatic scale has no chord of its own, so Scale 7th is a minor 7th there. The Step panel of a Chord track has two columns (Root, Octave, Chord, Play; Prob, Inv, Spread) and a step cell shows its root and chord.

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
`t{N}_rumble_drive`, `_d1`/`_d2`/`_d3`, `_size`, `_revmix`, `_revlp`, `_cut`, `_res`, `_inlp` (input low-pass, 20 Hz to 20 kHz, fully open = off; a plain 12 dB LP on the feed before the drive).

**Master key and scale (global `root`, `scale`, the ids mu-Tant uses; default C Chromatic).** The Kick and Bass `tune` knobs stay Hz (exponential travel, so every semitone takes the same turn) and are snapped to the nearest note of the master key and scale in `TrackEngine::setParams` (`TuneLock`, `mu_audio::snapMidiToScale`), then moved by the engine's `fine` (cents, ±100, not a modulation target). A modulator on Tune therefore steps through the scale's notes. A host with no `root` / `scale` (unbound rows) locks to C Chromatic, a semitone snap. The tonal row UI (Root / Scale, `kTonal*` tokens, the mu-Tant layout) is placed with the per-track layout rework.
The reverb tone + the block-rate envelope are deliberately basic for now (framework stage). *(Note:
distinct from the Bass lane's "rumble" voicing above, which is a tonal character of `BassEngine`,
not this lane.)*

### FM Perc — `FmPercEngine` (ruled 2026-10-21, not yet built)
The sixth engine (`EngineType::FmPerc`, `t{N}_fmp_*`): toms and techno percussion by phase-modulation FM, **one engine with an Algorithm choice** of eight voice topologies (Tom, Conga, Zap,
Click, Clank, Wood, Pluck, Burst) over one operator core, **ten knobs fixed for all algorithms**: Tune, Ratio, Index, FM Dec, Fdbk, Dive, D.Time, Decay, Fold, Noise. Sound design, source notes and the
per-algorithm starting values are in [research-fm-drums.md](research-fm-drums.md) (every number there marked [D] is a listening-pass starting point). Structure and rulings (parameter shape, normalised
knobs scaled per algorithm, no write on algorithm change, algorithm latched at the trigger, factory `.muTrack` presets, saved format, mu-Tronic) are in
[design-plugin-family.md](../design-plugin-family.md) "mu-On FM Perc engine"; names in [design-naming.md](../design-naming.md). It steps like Kick and Bass (a step row, velocity scales Index and Dive and
the level), takes no source, and is a valid Rumble source.

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
