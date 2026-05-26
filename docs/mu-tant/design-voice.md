# mu-tant — Voice Engine

Sketch / pre-implementation design. Subject to revision until first code lands.

Family rules apply: read [design-plugin-family.md](../design-plugin-family.md) for the platform contract (`mu-core` boundary, `ProcessorBase`, `VoiceSlot`) before changing any structural decision below.

---

## Scope

This document covers **mu-tant's voice DSP and the gate-sequencer contract**. The drawable gate pattern UI, envelope semantics, and per-envelope options live in the sibling doc [design-sequencer.md](design-sequencer.md); this one defines the voice DSP, its parameters, and how the gate stage plugs into the chain.

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

### Slot-level tonal centre (shared by both oscs)

| Param | Range | Notes |
|---|---|---|
| `pitch.root` | C, C#, D, … B | Root note class — shared |
| `pitch.scale` | Major, Minor, Dorian, Phrygian, Lydian, Mixolydian, Locrian, Harmonic-Minor, Pentatonic-Major, Pentatonic-Minor, Blues, Chromatic | Shared. Initial set; full list TBD |

Both oscillators sit in the same scale. Bitonal drones aren't supported — they'd add UI complexity for niche payoff.

### Per-oscillator pitch

Each oscillator has its own three knobs:

| Param | Range | Notes |
|---|---|---|
| `osc<N>.octave` | 0..8 | Octave of the osc's root note |
| `osc<N>.tone`   | 0..N continuous | Scale degree — steps through the notes of the slot's scale |
| `osc<N>.fine`   | ±100 cents | Off-grid detune |

The two oscs share `root` + `scale`, but each has its own `octave` + `tone` + `fine`. Two oscs an octave apart on different scale degrees gives a fat, intervallic drone within a single tonal centre.

**Step modulator** on `tone` → integer output → snap to scale notes (clean jumps).
**Smooth modulator** on `tone` → fractional output → smooth pitch glide through frequency space; the glide passes through off-scale frequencies between two adjacent scale notes, which sounds like a glissando rather than a chromatic walk.

### Frequency computation

```
degreesPerOctave = len(scale)
t_int  = floor(tone)
t_frac = tone - t_int

semitone(t) = scaleOffsets[t % len] + (t / len) * 12
midi        = root + 12*octave + lerp(semitone(t_int), semitone(t_int+1), t_frac) + fine/100
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
| `osc.xmodMode` | `Off` / `FM` / `Sync` | See direction table below |
| `osc.mix` | 0..1 | Balance Osc1 (A) vs Osc2 (B) — 0 = all A, 1 = all B |

Direction depends on mode (the two cross-mod styles operate in opposite directions):

| Mode | Direction | Effect |
|---|---|---|
| FM   | **B → A** (A FM'd from B) | Osc B's signal modulates Osc A's phase increment |
| Sync | **A → B** (B syncs to A)  | Osc B's phase is forced to 0 each time Osc A wraps |

Single `osc.mix` balance knob (no separate `osc1.level` / `osc2.level`). Fewer knobs, equivalent expressivity.

---

## Gate stage

The gate stage sits between the filter and the insert in the voice chain. It outputs a **0..1 sample-rate envelope** that multiplies the post-filter signal, carving rhythmic bursts out of the otherwise-continuous drone.

The drawable pattern model, per-envelope options, audio-thread contract, and UI layout are documented separately: see [design-sequencer.md](design-sequencer.md). This doc only references the gate as a node in the voice chain.

The gate is **purely a 0..1 multiplier** — volume / pitch / filter modulation all live in the modulator section, not in the gate. Decoupling volume from the gate means a long-form crescendo can be sculpted independently of the rhythmic chops.

---

## What's NOT in mu-tant

Explicit decisions to keep the design tight:

- **No amp envelope** (no ADSR). The gate stage replaces it.
- **No pitch envelope.** Pitch evolves via the modulator section (LFOs / control sequences targeting `osc<N>.tone`).
- **No filter envelope.** Filter cutoff / resonance evolve via the modulator section.
- **No user samples.** Built-in wavetable bank only.
- **No granular.** Pure wavetable.
- **No note-on / note-off.** MIDI is PC-only (program-change picks a pattern). No played-note input.
- **No unison stacks.** A single Osc1 + Osc2 pair per voice. Stack width via the 8 slots themselves.

---

## Modulation destinations (per slot)

The modulator section is **retained from mu-core unchanged** (LFOs + control sequences). Mu-tant-specific destinations:

| Destination | Range | Notes |
|---|---|---|
| `osc1.tone` | 0..N continuous | Scale-snapped under step mod; glides under smooth mod |
| `osc2.tone` | 0..N continuous | Same |
| `osc1.octave` | 0..8 | Integer; step mods snap, smooth mods cross octaves smoothly |
| `osc2.octave` | 0..8 | |
| `osc1.position` | 0..1 | Wavetable scan |
| `osc2.position` | 0..1 | |
| `osc1.fine` | ±100 cents | |
| `osc2.fine` | ±100 cents | |
| `osc.xmod` | 0..1 | Cross-mod amount — modulate this for the evolving FM timbre |
| `osc.mix` | 0..1 | Osc A vs Osc B |
| `amp.level` | -60..+6 dB | Slot-level volume. **This is the volume curve** — the gate is pure 0..1 multiplier, all amplitude shaping happens via a mod targeting this destination. |
| `filter.cutoff`, `filter.resonance`, `filter.lowCut` | same as mu-clid | Inherited from mu-core voice chain |
| `insert.p1..p4` | 0..1 | Insert algo params |

No `amp.attack/decay/sustain/release` or `*.envelope.*` destinations — they don't exist on mu-tant.

Mod-display rule from #641 applies: skewed sliders use proportion-space modulation; linear sliders use additive-in-display.

---

## UI layout

Three stacked bands fill the main content area. Sidebar sits to the left of the upper two bands; the envelope pattern editor at the bottom spans the **full window width** (the sidebar ends above it). Modulators live below the pattern editor in the same place mu-clid puts them.

```
┌─────────────────────────────────────────────────────┐
│  Transport bar (mu-core)                            │
├──────────┬──────────────────────────────────────────┤
│          │  Band 1: Osc 1 + Osc 2  (layer N)        │
│          │  ── per-osc: wavetable / octave / tone / │
│          │     fine / position; shared: root /      │
│ Sidebar  │     scale; cross-mod mode + amount; mix  │
│ (layers) ├──────────────────────────────────────────┤
│          │  Band 2: Filter + Insert  (mu-core)      │
├──────────┴──────────────────────────────────────────┤
│  Band 3: Envelope pattern editor (full width)       │
│  ── 2-bar grid at selected resolution;              │
│     properties strip below for the selected env     │
├─────────────────────────────────────────────────────┤
│  Modulators (mu-core)                               │
└─────────────────────────────────────────────────────┘
```

| Band | Contents | Source |
|---|---|---|
| 1 — Oscillators | Two-osc panel for the selected layer | mu-tant |
| 2 — Filter + Insert | Below the oscillators | `mu-core::VoiceFilter` + `mu-core::InsertProc` |
| 3 — Envelope pattern editor | Full-width 2-bar grid + properties strip — see [design-sequencer.md](design-sequencer.md) | mu-tant |
| Modulators | Bottom of window | `mu-core::ControlSequence` + `ModulationMatrix` |

Layer = mu-tant's term for a slot (a single voice configuration + its gate pattern). Eight layers per preset, selected via the sidebar.

Same `KnobWithLabel` / `SegmentControl` / `DropdownSelect` widgets from `mu-core` throughout — no one-off controls.

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
- **MIDI program-change path** — channel mask, async FIFO, drain, `MidiPresetMap` + `MidiFullPresetMap` storage, plus the matching `MidiPresetsPanel` / `MidiFullPresetsPanel` UI. **Lifted into mu-core in #660** — mu-tant just implements the four virtuals (`getPerSlotPresetDir/Extension`, `getFullPresetDir/Extension`) and the apply hooks. PC ch 1–8 picks a pattern per slot; ch 9 picks a full preset.

---

## File formats + MIDI

| Scope | Extension | Loaded via |
|---|---|---|
| Per-layer pattern (one slot's state) | `.muPattern` | PC ch 1–8, slot N = channel N. Also: pattern dropdown in the slot's UI. |
| Full preset (all 8 slots + mixer + FX) | `.muTant` | PC ch 9. Also: preset dropdown in the transport bar. |

Wired into the ProcessorBase virtuals from #660:
- `getPerSlotPresetExtension()` → `"muPattern"`
- `getFullPresetExtension()`    → `"muTant"`

### Hot-swap timing

Pattern loads (PC ch 1–8 or UI) **defer to the loop point**, same mechanism as mu-clid's rhythm hot-swap (#653 staged-prebuild + commit-at-boundary). Mu-tant reuses the `HotSwapStager` machinery — the prepared-pattern payload is mu-tant-specific (oscillator state + gate pattern + modulator state) but the staging lifecycle is identical.

This is also what makes the **"on staged-for-change only"** envelope option work — the gate engine reads the same `hasPendingPreset()` flag the stager exposes.

### Wavetable bank

V1 wavetables will be **generated by a custom tool** (sibling utility in `tools/wavetable-gen/` or similar). Not exporting from Vital / Serum — keeps the ship list curated and copyright-clean. Tool details TBD; bank size, category list, naming scheme to be decided as the tool is built.

---

## Open questions

Voice-DSP questions are resolved. Remaining mu-tant-wide questions:

- **Wavetable bank** — custom generation tool to be built; bank size, category list, naming scheme, output format details TBD as the tool comes together. See "Wavetable bank" above.
- See [design-sequencer.md](design-sequencer.md) for gate-pattern open questions (probability seed scope, multi-select bulk edit, visual indication of envelope options).
