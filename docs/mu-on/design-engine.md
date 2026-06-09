# μ-On — Engine design

μ-On is a 909-style groove sequencer with **five fixed instrument lanes**: Kick (synthesis),
Bass (deep synth — the focus), Hat (sample), Snare (sample), and Rumble (processes the Kick's
audio rather than stepping). The first four are sequenced on the step grid; Rumble has no steps.
Each lane is an independent engine rendered into its mixer channel via the shared
`MixerEngine::RenderChannelFn` hook; the shared mixer then applies the per-channel strip, the
**bass→kick sidechain**, sends, and master. `GrooveVoices`
([mu-on/Source/Audio/GrooveVoices.h](../../mu-on/Source/Audio/GrooveVoices.h)) owns the five
engines, routes the sequencer's triggers, and pulls engine params from cached APVTS atomic
pointers each block (no per-block string lookups, no audio-thread allocation).

## Lanes

### Kick — `KickEngine` (synthesis)
Sine body + an **exponential pitch envelope** (start tune → base) for the thump, an exponential
**amp envelope**, and a `tanh` **drive** for click/punch. Params: `k_tune`, `k_ptch` (pitch amount),
`k_pdec` (pitch decay), `k_adec` (amp decay), `k_drive`.

### Bass — `BassEngine` (the focus)
The deepest, most flexible lane. Main oscillator (**Sine / Saw / Square**) + a **sub osc** one
octave down, an **A/D/S amp envelope**, and a low-pass **mu-core `MultiModeFilter`** with its own
**cutoff decay envelope** + **valve drive**. Together these span the requested range:
- **Deep clean** — Sine/low drive/low resonance, modest filter env, sub moderate.
- **Rumble** — sub up, drive up, resonance up, slower filter-env decay; the valve drive adds the
  harmonic grit that reads as "rumble".

Params: `b_wave`, `b_sub`, `b_tune` (root), `b_cut`, `b_res`, `b_env` (filter-env depth),
`b_edec` (filter-env decay), `b_atk`, `b_dec`, `b_sus`, `b_drive`. The filter cutoff is updated
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
`s_tune`/`s_dec`). The default buffer is **generated procedurally** (bright high-passed noise hat;
noise + 180 Hz body tone snare) so the product makes sound with no shipped assets. **The only seam
to change is the buffer** — "Load .wav…" (a file chooser) + factory `.wav` content (shipped via
`juce_add_binary_data`, gated on code-signing like #99) is the next step, reusing the same player.

### Rumble — `RumbleEngine` (processes the kick feed)
Not sequenced — the classic techno **rumble**. It takes the **Kick's rendered audio** (stashed by
`GrooveVoices` after the Kick renders, since channels render in index order and Rumble is lane 4)
and runs it through, in order: input **drive** (the `MultiModeFilter` valve), three tempo-synced
**delay taps** (1/16·2/16·3/16, each with a level), a **dark reverb** (mu-core `ReverbSlot` + a
low-pass, with size + dry/wet mix), a per-bar **volume envelope** (a drawable smooth curve — the
shared `LFOEditor` — that sits in the Rumble lane's otherwise-empty step-grid slot, persisted in
state, evaluated audio-side under a try-lock), and a final **multimode filter**. Params:
`r_drive`, `r_d1`/`r_d2`/`r_d3`, `r_size`, `r_revmix`, `r_revlp`, `r_cut`, `r_res`. The mixer strip
pre-wires Rumble's sidechain source to the Kick (amount 0 by default → optional kick-pumping).
The reverb tone + the block-rate envelope are deliberately basic for now (framework stage). *(Note:
distinct from the Bass lane's "rumble" voicing above, which is a tonal character of `BassEngine`,
not this lane.)*

## Bass↔Kick sidechain (the key interaction)
This is **not new DSP** — the shared `MixerEngine` already supports **channel-to-channel
sidechain** (`sidechainSource` can point at another channel, with amount + attack/release + an
envelope follower + `sidechainGR` metering). μ-On pre-wires the **Bass channel's** sidechain
source to the **Kick channel** by default (`ch1_scSrc`→Kick, `ch1_scAmt≈0.4`, release ≈120 ms) in
the parameter layout, so the bass ducks under the kick out of the box. The Bass `EnginePanel`
surfaces **Duck / D.Atk / D.Rel** (= `ch1_scAmt`/`ch1_scAtk`/`ch1_scRel`) so the player dials
**smooth ↔ pumping** without opening the mixer; the same controls also live in the MixerOverlay.

## Threading / RT safety
All engine state is audio-thread-only. Buffers (sample one-shots, filter scratch, de-interleave)
are allocated in `prepare`. Param values reach the engines through cached `std::atomic<float>*`
read once per block. Triggers are queued by the sequencer (audio thread) and consumed in the same
block. Onset is currently **block-start** (≤ one block of jitter); sample-accurate onset via the
sequencer's per-trigger sample offset is a refinement (the offset is already computed).
