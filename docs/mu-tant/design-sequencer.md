# mu-tant — Sequencer (Gate Pattern)

Implemented design. The drawable gate editor + per-envelope shapes described
here are live; the per-envelope probability / loop-N options noted under
**Deferred** are not.

Sibling doc: [design-voice.md](design-voice.md) — voice DSP, oscillator chain,
pitch model. The gate stage described here sits between the filter and the
output in that chain (post-filter, pre-pan).

---

## Scope

mu-tant doesn't have a "sequencer" in the mu-clid sense — there's no per-step
trigger of voices, no Euclidean rhythm, no per-step pitch. The oscillators run
continuously as a drone. What this document describes is the **gate pattern**:
a drawable 2-bar grid of **attack/decay envelopes** that chop bursts out of the
otherwise-continuous output.

The gate is **purely a volume gate** (0..1 multiplier). Each envelope opens the
gate to at most 100% and back to silence; the regions between envelopes are
silent. Pitch / cross-mod / filter all evolve via the modulator section (LFOs
and control sequences from `mu-core`, unchanged from mu-clid). **Output volume
is controlled by a modulator** (the per-voice level destination), not by the
gate — the gate only shapes *when* and *how* sound passes.

---

## Pattern model

- **Length: fixed 2 bars** in host tempo (8 beats in 4/4). Loops continuously.
- **Resolution selectable** per pattern via the grid dropdown: **1/4, 1/8,
  1/16 (default), 1/32**. The resolution sets the cell width — the unit a
  pencil click fills.
- **One pattern per voice slot.** Eight slots → up to eight independent gate
  patterns running in parallel.

### Envelope = one attack/decay region

Each envelope occupies a contiguous **region** of one or more cells and is a
single attack→peak→decay shape:

| Field | Meaning |
|---|---|
| `startCell` | 0-based subdivision index where the region begins |
| `lengthCells` | region span in cells (≥1); a pencil click makes 1, glue makes more |
| `split` | peak position 0..1 within the region — the attack/decay split. `0` = instant attack (pure decay); `1` = pure attack |
| `attackBend` | −1..+1 bend of the rising attack line (− concave, + convex) |
| `decayBend` | −1..+1 bend of the falling decay line (− concave, + convex) |
| `reverse` | mirror the shape in time, swapping attack and decay |

The gate value across a region is, in region-phase `t` ∈ [0,1]:

- attack: `t ≤ split` → rises 0 → 1, bent by `attackBend`
- decay:  `t > split` → falls 1 → 0, bent by `decayBend`
- `reverse` evaluates the shape at `1 − t`, which swaps attack and decay.

Bend curve is `pow(x, 2^(−bend·2))`: `bend > 0` bulges the line up (sustain),
`bend < 0` bulges it down (faster move, audible gap).

Default new envelope: `split = 0` (instant attack), both bends `0` (linear
decay 1 → 0) — i.e. the classic gate decay, then the user drags to taste.

### Regions are non-overlapping

Each cell belongs to at most one envelope. Drawing or gluing trims/removes any
envelope it overlaps so the regions never overlap. Cells covered by no
envelope are **silent** (gate 0).

---

## The Gap dial

A per-voice **Gap** knob (0–100%) forces the **end of every envelope region to
silence**, producing a cleaner gate. It's a percentage of the *whole region*:
the envelope shape is squeezed into the leading `(1 − gap)` of the region and
the trailing `gap` fraction outputs 0. At `gap = 0` the envelope fills the
region; at `gap = 50%` the shape completes in the first half and the second
half is silent.

Gap is an APVTS parameter (`v{N}_gate_gap`, 0..1) so it saves with the project
and is per-voice like the rest of the voice params. The audio gate evaluator
and the editor renderer both apply it identically via `GateEnvelope::value`.

---

## Toolbox

Four radio-grouped tools in the gating header (procedural vector icons, no
assets):

| Tool | Action |
|---|---|
| **Pencil** | Click an empty cell → draw a default 1-cell envelope filling that cell. Click+drag a **grab handle** on an existing envelope to reshape it (see below). |
| **Eraser** | Click an envelope → erase it. |
| **Glue** | Click+drag across several envelopes → merge them into one envelope filling the dragged region; its `split` / `attackBend` / `decayBend` are the **average** of the merged envelopes. |
| **Reverse** | Click an envelope → flip its attack and decay. |

### Pencil grab handles

When the Pencil is active, each envelope exposes draggable handles. Hovering a
handle turns the cursor into a **hand**; dragging it edits in place:

- **Top point** (at the peak) — drag horizontally to move the **attack/decay
  split** within the region.
- **Attack line mid-grab** — drag vertically to bend the attack line up/down.
- **Decay line mid-grab** — drag vertically to bend the decay line up/down.

Clicking the pencil anywhere that isn't a grab handle draws a new 1-cell
envelope.

---

## Audio-thread contract

The gate stage runs in `PluginProcessor::processBlock` between the per-voice
render and the pan/sum, per sample:

1. Compute the pattern's position from the internal transport (musical beats,
   modulo 2 bars).
2. Determine which cell the current sample falls in, and which envelope (if
   any) covers that cell.
3. No envelope → gate 0 (silent). Otherwise evaluate the envelope's shape at
   the region-phase, applying the per-voice Gap → 0..1 gate value.
4. Multiply the post-filter sample by the gate value.

No allocation, no map lookups beyond a bounded linear scan (≤ 64 cells). The
evaluator caches the cell→envelope lookup so per-sample cost is O(1) except on
cell changes.

Block-level behaviour (shared `applyGateBlock`, used by the audio engine **and**
the `GateStageTests` audio harness):

| State | Output |
|---|---|
| **Gater bypass** (`v{N}_gate_bypass`) on | raw drone passes — audition / configure |
| transport **stopped** | **silent** (gate closed — nothing audible on load) |
| playing, **no envelopes** | **silent** (nothing drawn → nothing passes) |
| playing, has envelopes | per-sample envelope gate |

So on load (stopped, no envelopes, not bypassed) the plugin is silent. The gate
is **per-voice** — gating one voice does not silence the others.

Editing happens on the message thread under the pattern's `editLock` spin-flag;
the audio thread `tryLock`s for its gate pass and leaves the block ungated on
contention.

---

## What lives where

| Concern | Where |
|---|---|
| Oscillator + cross-mod DSP | `mu-tant/Source/Audio/SynthVoice.{h,cpp}` ([design-voice.md](design-voice.md)) |
| Gate pattern storage + evaluator | `mu-tant/Source/Sequencer/GatePattern.{h,cpp}` |
| Pattern editor UI (grid + toolbox + grab-handle drag) | `mu-tant/Source/UI/GatingDesigner.{h,cpp}` |
| Gap knob + per-voice binding | `mu-tant/Source/UI/VoicePanel.{h,cpp}` (APVTS `v{N}_gate_gap`) |
| Resolution dropdown widget | `mu-core::DropdownSelect` (shared) |
| Modulator section (volume / pitch / filter mods) | mu-core, unchanged |

---

## UI placement

The gate editor is the **third (bottom-ish) band** of the voice panel,
**full window width** — below the oscillator and filter rows, above the
modulator section. The Gap knob sits in the gating band beside the grid.

---

## Filter envelope layer

A second pattern layer (toggled [GATE|FILT] in the editor header) shapes the
**filter cutoff** instead of amplitude. Envelope value 0 → 20 Hz (filter
closed); 1 → base cutoff (fully open). Uncovered cells close the filter (0).
Only active while transport is playing; when the pattern is empty the filter
cutoff is unchanged.

Ghost rendering: the inactive layer is drawn at 20% alpha in its own colour
(gate = coral `knobFxSend`, filter = teal `knobPostPad`) so both layers can be
aligned visually.

## Toolbox — Arrow tool

A fifth tool (`Arrow`, leftmost in the toolbox) selects an envelope by clicking
it. The selected envelope is highlighted and its per-envelope properties appear
in the **properties strip** below the grid.

## Properties strip

A 40 px band below the grid, always present:
- **Prob** — horizontal slider 0..100 %. Probability this envelope fires each
  loop pass. Uses a deterministic hash of `(loopCount, startCell)` so the
  decision is stable across the whole loop and varies each pass.
- **Loop** — `N` dropdown (1..8) and `M` dropdown (1..8). Fires on loop `N-1
  mod M` of the pattern cycle. `1 / 1` = every loop (default). `1 / 2` = play
  the first of every two loops.

When no envelope is selected the strip shows "Select an envelope with the Arrow
tool to edit its properties".

## Deferred (not yet implemented)

- **Pattern hot-swap staging** (reuse mu-clid's `HotSwapStager`).
- **On-staged-for-change** / **First-only** per-envelope fire rules.
