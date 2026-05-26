# mu-tant — Voice Engine

Sketch / pre-implementation design. Subject to revision until first code lands.

Family rules apply: read [design-plugin-family.md](../design-plugin-family.md) for the platform contract (`mu-core` boundary, `ProcessorBase`, `VoiceSlot`) before changing any structural decision below.

---

## Scope

This document covers **mu-tant's voice DSP and the gate-sequencer contract**. The drawable gate pattern UI and sequencer-rate handling live in the sibling doc `design-sequencer.md` (TBD); this one defines the voice DSP, its parameters, and what the gate stage receives.

---

## Concept — what mu-tant *is*

A **drone instrument**, not a note instrument. Each of 8 slots runs **two oscillators continuously** — they never start or stop, they never have an amp envelope, their phase is free-running. What you hear is a continuously evolving tone shaped by:

- **Scale-quantised pitch** per oscillator (step mods snap to scale notes; smooth mods glide cleanly through them)
- **FM and oscillator sync** between the two oscs, modulatable for timbral evolution
- **A drawable gate pattern** that chops bursts out of the drone with per-step decay envelopes

There is **no pitch envelope, no amp envelope, no filter envelope**. All time-varying behaviour comes from the modulator section (LFOs, control sequences) plus the gate pattern that chops the output.

---

## High-level shape

- **8 voice slots**, parallel and independent — same architecture as mu-clid's rhythm slots
- **Two wavetable oscillators per slot**, both **always running** (no note-on / note-off)
- **Built-in wavetable bank** in `mu-tant/resources/wavetables/` (Serum / Vital format, see "Wavetable osc" below)
- **Voice chain (no envelopes):** `Osc1 + Osc2 → Filter → Gate → Insert → mixer bus`
- **The "sequencer" is a gate pattern** — drawable per-step with selectable decay length (64th / 32nd / 16th / 8th)
- **Reuses mu-core unchanged** for the filter, the insert, the mixer, the modulation matrix, the control-sequence editor — and the entire visual identity

---

## Per-slot voice chain

```
[Osc1] ─┐
        ├─ X-mod (FM / Sync) ─→ Filter ─→ Gate ─→ Insert ─→ mixer bus
[Osc2] ─┘
```

Differences from mu-clid:
- **No `ampEnv`** — the gate stage replaces it.
- **No retrigger** — oscs never reset phase; they're continuous from `prepareToPlay`.
- **Filter is the same** (`mu-core::VoiceFilter`, same algorithm list).
- **Insert is the same** (`mu-core::InsertProc`, same algo set).

---

## Pitch — scale-quantised, modulator-driven

### Per-slot tonal centre

A slot has a shared **tonal-centre setting** (one knob each):

| Param | Range | Notes |
|---|---|---|
| `pitch.root` | C, C#, D, … B | Root note class |
| `pitch.octave` | 0..8 | Octave of the root |
| `pitch.scale` | Major, Minor, Dorian, Phrygian, Lydian, Mixolydian, Locrian, Harmonic-Minor, Pentatonic-Major, Pentatonic-Minor, Blues, Chromatic | Initial set; easy to extend |

The two oscillators share root + octave + scale (one tonal centre per slot). Each oscillator has its **own** scale-degree position (so the two oscs sit at different notes within the same scale).

### Per-osc pitch

Each oscillator has:

| Param | Range | Notes |
|---|---|---|
| `osc<N>.scaleDegree` | 0..N continuous | Position within the scale; integer = on-scale, fractional = glide between notes |
| `osc<N>.fine` | ±100 cents | Off-grid detune |

**Step modulator** on `scaleDegree` → integer output → snap to scale notes (clean jumps).
**Smooth modulator** on `scaleDegree` → fractional output → smooth pitch glide through frequency space; the glide passes through off-scale frequencies between two adjacent scale notes, which sounds like a glissando rather than a chromatic walk.

### Frequency computation

```
degreesPerOctave = len(scale)
d_int  = floor(scaleDegree)
d_frac = scaleDegree - d_int

semitone(d) = scaleOffsets[d % len] + (d / len) * 12
midi        = root + 12*octave + lerp(semitone(d_int), semitone(d_int+1), d_frac) + fine/100
freq        = 440 * 2^((midi - 69) / 12)
```

Linear interpolation in MIDI space gives a perceptually-smooth glide (logarithmic in frequency).

---

## Wavetable oscillator

### Format

**Serum / Vital style**: each wavetable is `2048 samples × 256 frames` of single-cycle waveforms, concatenated as a 24-bit mono WAV. The de facto standard — vast free banks already exist; ships natively in a format we don't have to convert from.

Storage: `mu-tant/resources/wavetables/<category>/<name>.wav`.
Loaded into RAM at startup as `std::vector<float>` (≈ 2 MB per table). The full shipped bank is the only RAM cost — no per-voice copies, no per-trigger allocation.

### Anti-aliasing

**Mip-mapped tables per octave**, computed once at bank-load time (FFT per frame, zero bins above mip's target Nyquist, IFFT). 10 mip levels cover the full MIDI range. Runtime is a table lookup per sample — no realtime FFT.

### Read

- **Position** (0..1, modulatable): which frame within the table to read.
- **Linear cross-fade** between the two adjacent frames at the current position.
- **Phase increment**: `freq / sampleRate` step through the 2048-sample frame.
- **Free-running phase** — never reset.

---

## Two-oscillator cross-modulation

| Param | Range | Notes |
|---|---|---|
| `osc.xmod` | 0..1 | Cross-mod amount, modulatable for evolving timbre |
| `osc.xmodMode` | `Off` / `FM` / `Sync` | FM = Osc1 modulates Osc2's phase increment; Sync = Osc2 hard-syncs to Osc1's phase zero-crossing |
| `osc.mix` | 0..1 | Balance Osc1 vs Osc2 (0 = all Osc1, 1 = all Osc2) |

Direction is one-way: Osc1 → Osc2. (Bidirectional adds complexity for little real-world benefit; Serum and Vital are both one-way.)

Per-osc levels live on `osc.mix` rather than separate `osc1.level`/`osc2.level` knobs — fewer knobs, equivalent expressivity. (Open question: should this be three knobs — `osc1.level`, `osc2.level`, separately — for finer control? Single mix knob is the cheaper default.)

---

## Gate stage (the "sequencer" output)

The gate stage sits between the filter and the insert. It receives a **0..1 envelope value per sample** from the gate sequencer and multiplies the post-filter signal by it. The continuous drone is left intact when the gate is 1.0; bursts are carved out when the gate descends from a step's trigger.

### Gate pattern model

Per slot the user draws a pattern with:
- **N steps** (initial: 16 or 32, TBD — same options as mu-clid's `stepCount`)
- **Per step: active flag** + **decay length** (one of: 64th, 32nd, 16th, 8th note)
- The pattern loops at the host tempo

When an active step is reached:
1. A new decay-envelope instance is spawned (attack = 0, decay = the step's note length).
2. The decay envelope retriggers any in-flight envelope (last-trigger wins; no overlap accumulation — clean chops).
3. The gate signal = current envelope value (1.0 immediately on trigger, decaying to 0).

Between steps with no trigger, the gate sits at 0 — i.e. silent — until the next active step. So the drone is **only audible during decay bursts**. (Open question: alternative "gate floor" knob that holds the drone at e.g. 0.2 between bursts? Defaulting to 0 — pure chops — for v1.)

### Decay-envelope shape

Linear from 1.0 → 0.0 over the chosen note length. (Open: exponential might feel more musical; offer a curve knob in v2.)

---

## What's NOT in mu-tant

Explicit decisions to keep the design tight:

- **No amp envelope** (no ADSR). The gate stage replaces it.
- **No pitch envelope.** Pitch evolves via the modulator section (LFOs / control sequences targeting `scaleDegree`).
- **No filter envelope.** Filter cutoff / resonance evolve via the modulator section.
- **No user samples.** Built-in wavetable bank only.
- **No granular.** Pure wavetable.
- **No note-on / note-off.** No MIDI input drives the voices (TBD — MIDI may still drive program change for preset selection, like mu-clid).
- **No unison stacks.** A single Osc1 + Osc2 pair per voice. Stack width via the 8 slots themselves.

---

## Modulation destinations (per slot)

The modulator section is **retained from mu-core unchanged** (LFOs + control sequences). New mu-tant-specific destinations:

| Destination | Range | Notes |
|---|---|---|
| `osc1.scaleDegree` | 0..N continuous | Scale-snapped under step mod; glides under smooth mod |
| `osc2.scaleDegree` | 0..N continuous | Same |
| `osc1.position` | 0..1 | Wavetable scan |
| `osc2.position` | 0..1 | |
| `osc1.fine` | ±100 cents | |
| `osc2.fine` | ±100 cents | |
| `osc.xmod` | 0..1 | Cross-mod amount — modulate this for the evolving FM timbre |
| `osc.mix` | 0..1 | Osc1 vs Osc2 |
| `filter.cutoff`, `filter.resonance`, `filter.lowCut` | same as mu-clid | Inherited from mu-core voice chain |
| `insert.p1..p4` | 0..1 | Insert algo params |
| `gate.floor` (if added) | 0..1 | Drone-bleed-through level |

No `amp.*` or `*.envelope.*` destinations (they don't exist).

Mod-display rule from #641 applies: skewed sliders use proportion-space modulation; linear sliders use additive-in-display.

---

## UI

- **Same screen real-estate as mu-clid's sequencer panel** — the rhythm-circle / euclidean-panel area gets replaced.
- **Top half**: oscillator controls (root / octave / scale, per-osc wavetable / scaleDegree / fine / position, cross-mod, mix) + filter + insert. Same `KnobWithLabel` / `SegmentControl` / `DropdownSelect` widgets from `mu-core` — no one-off controls.
- **Bottom half**: drawable gate pattern grid (per-step active + per-step decay length).
- **Modulator section retained** at the bottom of the screen (same place as mu-clid).
- **Mixer / FX rows / sidebar / transport** unchanged from mu-core.

---

## mu-core reuse map

What mu-tant **owns** (lives in `mu-tant/Source/`):
- `WavetableBank` — loads + holds the static built-in tables (mip-mapped)
- `WavetableOscillator` — single-osc DSP
- `VoiceEngine` (mu-tant's flavour) — two `WavetableOscillator`s + cross-mod + the call into the shared filter/insert + the gate stage
- `GateSequencer` — pattern storage + per-step decay-envelope driver
- `PluginProcessor` (extends `mu-core::ProcessorBase`)
- UI panels (the oscillator/filter/insert area + the gate-pattern grid)
- `Pitch.h` / `Scales.h` — scale table + degree-to-MIDI helper (reused inside the voice; not in mu-core because mu-clid has no use for it)

What mu-tant **reuses from mu-core** (no duplication):
- `VoiceFilter` (same DSP)
- `InsertProc` + the FX algo set (same insert library)
- `MixerEngine`, `MixerChannel`, `MixerOverlay`, `FXRow`, `DelayRow`
- `ProcessorBase` (family base with `apvts` + `mixerEngine`)
- `ModulationMatrix`, `ControlSequence` (LFOs / step / draw modulators)
- `MuLookAndFeel`, all shared widgets
- `RenderMode` pattern (each plugin owns its own, structured the same)
- **MIDI program-change path** — channel mask, async FIFO, drain, `MidiPresetMap` + `MidiFullPresetMap` storage, plus the matching `MidiPresetsPanel` / `MidiFullPresetsPanel` UI. **Currently in mu-clid; needs lifting to mu-core** as a follow-up. The audio-side FIFO + drain + channel-mask gating becomes a `ProcessorBase` capability with virtual hooks (`applyMidiPresetSlot(int, juce::File)`, `applyFullMidiPreset(juce::File)`) that each plugin implements with its own preset-load semantics. The maps + panels are pure data / UI and move wholesale.

---

## Open questions

1. **Tonal centre per oscillator?** Currently shared at the slot level (one root/octave/scale per slot, two scale-degrees). Alternative: each osc has its own root/octave/scale, allowing the two oscs to sit in different keys for bitonal drones. Adds 4 knobs per slot; defaulting to shared.
2. **Osc level vs mix knob.** Currently a single `osc.mix` balance. Separate `osc1.level` + `osc2.level` knobs allow per-osc gain (e.g. mute one without modulating mix). Cheaper default = single mix.
3. **Gate floor.** Should the gate clamp to a user-set floor (e.g. 0.2) between bursts so the drone bleeds through? Or always go to silence? Defaulting to silence — chops are the point.
4. **Decay shape.** Linear vs exponential vs user curve. Defaulting to linear; revisit after first listening pass.
5. **Per-step retrigger vs overlap.** Currently last-trigger wins (clean chops). Alternative: overlap — sum two in-flight decays. Defaulting to clean.
6. **MIDI input.** Does mu-tant respond to incoming MIDI notes at all? E.g. could MIDI note-on set `pitch.root` for live key changes? Or is MIDI strictly for program-change (preset select) like mu-clid? Defaulting to PC-only.
7. **Pattern step count.** Same range as mu-clid (1..64) or tighter (e.g. always 16 or 32)? Defaulting to mu-clid-equivalent (1..64).
8. **Cross-mod direction.** One-way Osc1 → Osc2 only? (Default — matches Serum / Vital.) Bidirectional adds complexity.
