# μ-Clid — Sequencer Design Reference

## Euclidean Layer Parameters (HitGenerator)

| Parameter | Range | Notes |
|---|---|---|
| Steps | 1–32 | Total steps including all padding |
| Hits | 0–steps | Euclidean distribution |
| Rotate | 0–steps-1 | Offset applied after distribution |
| Pre pad | 0–12 | Empty steps forced at start |
| Post pad | 0–12 | Empty steps forced at end |
| Insert start | 0–steps-1 | Start of insert pad zone |
| Insert length | 0–8 | Length of insert zone |
| Insert mode | Mute / Pad | Mute = hits distributed but silenced. Pad = gap excluded from distribution. |
| Mute | On/Off | Exclude layer from combined result |

**Logic** (how A and B combine): OR, AND, XOR, A only, B only. Applied globally between the two generators, shown between Euclid panels.

**Reset steps**: 1–256 or INF (`std::nullopt`). After N steps both generators reset to step 0, re-aligning the polyrhythm. INF = free-running. Display cycle uses LCM of A and B step counts when INF (capped at 256 for display).

## Euclid C — Accent Layer

Third independent euclidean layer dedicated to accents. No logic relationship with A/B. Only fires an accent when it coincides with an A+B hit (firing on a non-hit step has no effect). Not yet in data model (v1 deferred — `Rhythm.h` has genA and genB only, genC to be added).

| Parameter | Range | Notes |
|---|---|---|
| Steps | 1–32 | |
| Hits | 0–steps | |
| Rotate | 0–steps-1 | |
| Advance mode | Steps / Hits | Steps: advances on every step. Hits: advances only when A+B fires. |
| Accent level | 0 to +12dB | Level boost on coincident hits |
| Accent velocity | 0–127 | MIDI velocity for accented hits in MIDI mode |
| Mute | On/Off | |

## DAW Position Sync

- Step position = `(host_ppq / step_length_beats) % step_count`
- Uses global song timeline from DAW (not clip-relative)
- Formula recalculates from absolute position every block — handles tempo automation
- Scrubbing: position updates silently, no triggers
- Playback start mid-step: wait for next step boundary before first hit
- Song position 0.0.0.0 = all rings with step 1 at top (12 o'clock)
- Global setting: sync to host position OR reset on play. Per-rhythm override available.
- Ring rotation: animates only during active playback; static when stopped or scrubbing

## Control Sequence Parameters

| Parameter | Range | Notes |
|---|---|---|
| Mode | Smooth / Stepped | Smooth = drawable LFO curve. Stepped = bar graph. |
| Polarity | Unipolar / Bipolar | Unipolar: 0 to +100. Bipolar: -100 to +100. Switching to unipolar preserves negative values in memory (non-destructive). |
| Loop length note value | 1/1, 1/2, 1/4, 1/8, 1/16, 1/32 + triplet/dotted | Uses TimeSelector component |
| Loop length multiplier | 1–16 | Multiplied by note value |
| Step length note value | same | Stepped mode only |
| Step length multiplier | 1–16 | Step count = loop length / step length (auto-calculated) |
| Step values | float array -100 to +100 | Stepped mode. Stored in ValueTree. Truncated on step count decrease. |
| Curve points | (x,y) array + bezier handles | Smooth mode. x ∈ [0,1], y ∈ [-1,1] (maps to -100 to +100). Default: two nodes at (0,0) and (1,0). |

**Curve interaction:** Click on line to add node. Right-click node to remove. ALT-click segment to add bezier handle (stored as offset from segment midpoint). Drag handle to bend segment.

## Modulation Signal Flow

```
Base parameter value (from APVTS)
+ ControlSequence output × assignment depth (from ModulationMatrix)
= Final value → audio engine
```

Processing order in ModulationMatrix:
1. Evaluate all ControlSequence outputs for current song position
2. Sort assignments by dependency (assignments targeting other assignment depths come after their source)
3. Apply each: `destination += source_output × depth`
4. Clamp all destinations to valid parameter ranges

Circular dependencies detected and rejected at assignment creation time.
Meta-modulation (targeting another assignment's depth) is architecturally supported — UI for it is v2.
