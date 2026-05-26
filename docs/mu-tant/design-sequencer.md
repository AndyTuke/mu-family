# mu-tant — Sequencer (Gate Pattern)

Sketch / pre-implementation design. Subject to revision until first code lands.

Sibling doc: [design-voice.md](design-voice.md) — voice DSP, oscillator chain, pitch model. The gate stage described here sits between the filter and the insert in that chain.

---

## Scope

mu-tant doesn't have a "sequencer" in the mu-clid sense — there's no per-step trigger of voices, no Euclidean rhythm, no per-step pitch. The oscillators run continuously as a drone. What this document describes is the **gate pattern**: a drawable 2-bar grid of decay envelopes that chop bursts out of the otherwise-continuous output.

The gate is **purely a gate** (0..1 multiplier). Volume / pitch / cross-mod / filter all evolve via the modulator section (LFOs and control sequences from `mu-core`, unchanged from mu-clid).

---

## Pattern model

- **Length: fixed 2 bars** in host tempo. Loops continuously.
- **Resolution selectable** per pattern, via the same dropdown UX the modulator section uses for its rate. Options: **64th, 32nd, 16th, 8th, plus triplet** variants.
- **One pattern per voice slot.** Eight slots → up to eight independent gate patterns running in parallel.

### Drawing

- **Click-drag** across a range of grid cells → produces a series of decay envelopes, one per cell at the selected resolution.
- **ALT-drag** on an envelope → bends its decay curve. Concave (faster initial drop, audible gap) ↔ convex (sustain, less gap).
- Default new-envelope shape: linear 1.0 → 0.0 across the cell.

---

## Envelope semantics

Each envelope is a decay shape locked to one resolution cell:

- **Always reaches 0** within its cell — no carry-over into the next cell.
- **Adding an overlapping envelope shortens the underlying one** so it ends precisely when the new one starts. Clean hand-off, no overlap-sum, no last-trigger-wins truncation; the underlying envelope's decay is rescaled to fit the shortened span.
- **Bending the curve down** (more concave) produces an audible **gap** within the cell — the envelope hits 0 before the cell ends, so the drone is silent for the remainder.
- **Bending up** (more convex) sustains closer to 1.0 for longer, only dropping at the end of the cell — minimal gap, more sustain.

---

## Per-envelope options

Each envelope carries metadata that gates whether it actually fires on a given pass through the pattern. Initial set (expandable as use-cases emerge):

| Option | Effect |
|---|---|
| **Reverse** | Plays back as an attack instead of a decay (0 → 1 over the cell) |
| **Probability** (0..100%) | Coin-flip each pass; only fires when the roll passes |
| **Loop-N-of-M** | E.g. "play on loop 1 of 4 loops" — fires only on the chosen pass within the M-pattern cycle. **M is 1..8** (max cycle = 8 × 2-bar pattern = 16 bars before the long-form repeats). |
| **First only** | Fires only on the first pass after the pattern starts (or after a pattern change) |
| **On staged-for-change only** | Fires only while a pattern hot-swap is staged-but-not-yet-committed — useful for transition fills landing exactly at the swap point. Reads the same `hasPendingPreset()` flag the stager exposes. |

---

## Properties strip

The currently-selected envelope's options live in a **properties strip below the pattern grid**. Click an envelope to focus it; the strip refreshes to show its toggle / knob / dropdown controls. No right-click context menu, no modal dialog — direct edit in-place. Multi-select + bulk-edit is a v2 consideration.

---

## UI placement

The pattern editor is the **third (bottom) band** of the main content area, **full window width** — sitting below the oscillator panel and filter / insert row, above the modulator section. See the UI layout diagram in [design-voice.md](design-voice.md) for the full screen map.

Click an envelope to focus it; the per-envelope **properties strip** sits directly below the grid (within the same band).

---

## Audio-thread contract

The gate stage runs in `VoiceEngine::process()` between the filter and the insert. Per sample:

1. Compute the pattern's current position from the host transport (musical time, modulo 2 bars).
2. Determine which cell the current sample falls in.
3. Look up the active envelope (if any) for that cell, taking into account: the envelope's options (probability roll, loop-N-of-M, etc.), and whether an overlapping envelope further forward in the cell is shortening it.
4. Evaluate the envelope's curve at the within-cell phase → 0..1 gate value.
5. Multiply the post-filter sample by the gate value.

No allocation, no map lookups beyond bounded array indexing. Pattern data is laid out as a flat array of envelope structs (cell index + curve params + options); the audio thread reads a pre-built lookup of "for cell i, which envelope is active?" that's rebuilt on the message thread whenever the pattern is edited.

### Probability + Loop-N-of-M evaluation

These resolve **once per pass through the pattern** (at the loop boundary), not per-sample. The message thread (or a low-rate audio-side check) sets a per-envelope `firingThisPass` flag at each pattern wrap; the per-sample path just reads the flag. Coin flips happen on the message thread; the audio thread sees a precomputed boolean.

---

## What lives where

| Concern | Where |
|---|---|
| Oscillator + cross-mod DSP | `mu-tant/Source/Audio/VoiceEngine.{h,cpp}` ([design-voice.md](design-voice.md)) |
| Gate pattern storage + sample-rate evaluator | `mu-tant/Source/Sequencer/GatePattern.{h,cpp}` |
| Pattern editor UI (canvas + draw / ALT-drag) | `mu-tant/Source/UI/GatePatternEditor.{h,cpp}` |
| Properties strip below editor | `mu-tant/Source/UI/EnvelopePropertiesStrip.{h,cpp}` |
| Pattern hot-swap staging | reuses `mu-clid`'s `HotSwapStager` (or its mu-core successor); pattern payload is mu-tant-specific |
| Resolution dropdown widget | `mu-core::DropdownSelect` (shared) |
| Modulator section (volume / pitch / filter mods) | mu-core, unchanged |

---

## Open questions

- **Probability seed scope** — each envelope independent? Or per-pattern-pass deterministic (so probabilistic patterns sound the same across replays of the same preset)?
- **Multi-select bulk edit** of envelope options — v1 or v2?
- **Visual indication of options** — should an envelope render differently in the grid if it has e.g. probability < 100% or a loop-N-of-M filter active? Small icon overlay? Tinted fill? Defer until first listening pass.
