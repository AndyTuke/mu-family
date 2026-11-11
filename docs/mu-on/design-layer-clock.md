# mu-On layer clock: Layer Length, Speed, Play Mode, Reset

Backlog row #1802. Andy's rulings (2026-11-11), built the same day. Layers can now run out of step with each other for polyrhythms.

## What each control does

| Control | Where | Range | Saved as |
|---|---|---|---|
| **Sequence Length** | Steps box, bottom strip ("Length (bars)") | 1 to 8 bars of 16 steps | global `seqBars` (default 1) |
| **Reset (bars)** | Steps box, bottom strip | Off, then 1 to 32 bars | global `seqReset` (default Off) |
| **Layer Length** | LAYER box ("Length", LED number with arrows) | 1 step to the sequence length | the row's own length (`len`, as before) |
| **Speed** | LAYER box | the Repeat Rate list: 1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32, 1/32T, 1/64; default 1/16 | `t{N}_speed` (choice) |
| **Play Mode** | LAYER box | Forwards, Backwards, Random | `t{N}_mode` (choice) |

- **Play Mode is a modulation target** (`perf.mode`, the new last closing row of every engine's table, after Fill). The modulated value is rounded to the nearest of the three modes. Speed is a choice and not a target.
- A layer cannot be longer than the sequence: the row plays `min(its own length, the sequence length)` and keeps its own length, so a longer sequence brings it back.
- The pager's Page count follows the sequence length. mu-Tronic has no sequence (one Track layer per slot), so there the strip's Length stays the row's length; the LAYER box controls are the same in both products.

## Clock model

`StepClock` keeps no position of its own any more: every block derives the steps from the beat (`ClockSetup`: step length in beats, reset period in beats, mode, seed).

- **Speed:** a step lasts `stepBeats` (1/16 = 0.25 beat). Swing delays odd slots by half a step of the current Speed.
- **Reset:** the layer's step count restarts at every multiple of the reset period, so every layer is back at its first step at the same moment. A step that would land after the period's end is not played. The pass counter keeps counting across resets, so step conditions and probability keep moving.
- **Play Mode:** the timeline slot is mapped to the row step: Forwards is the slot, Backwards counts down from the layer's last step, Random picks a step from a hash of (track seed, pass, slot), so the display and the audio agree. Ties and the tied-note hold follow the row's order, so only Forwards has them. Step conditions "previous played" look back in row order.
- **Playhead:** `Track::positionAt(beat)` gives the row step, the slot and the pass; the grid's playhead and the dimmed steps use it, so each row shows its own place.
- Without a Reset and with every layer at 16 steps and 1/16, the output is the same as before; the one deliberate difference is that a locate back to the start inside the first sixteenth now fires step 0 again (the old cursor skipped it).

## Limits and notes

- A row holds up to 128 steps (`StepRow::kMaxSteps`), eight bars. A `StepPattern` keeps its rows on the heap because eight rows of 128 steps were too big for a test's stack.
- The Play Mode seen by the step peek (p-locks) is last block's value, the same limit as a modulated Fill.
- Not built: showing the modulated Play Mode on the dropdown, and Speed/Reset/global length in mu-Tronic's scene model.
