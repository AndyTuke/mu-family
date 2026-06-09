# μ-On — Sequencer design

A 909-style step sequencer drives the four **stepped** instrument lanes (Kick/Bass/Hat/Snare).
The fifth lane, **Rumble**, is not sequenced — it processes the Kick's audio (see
[design-engine.md](design-engine.md)) — so the grid stays 4 tracks.

## Model — `StepPattern`
[mu-on/Source/Sequencer/StepPattern.h](../../mu-on/Source/Sequencer/StepPattern.h) — a grid of
**4 tracks × 16 steps**. Each cell carries an **on/off** and an **accent** flag. Cells are
`std::atomic<bool>` so the message thread (grid editing) and the audio thread (sequencer read)
share them without a lock or a dropped block. A `loadDefaultGroove()` seeds a four-on-the-floor
kick, backbeat snare, straight-8th hats and an off-beat bass so a fresh instance grooves.

**Persistence:** serialised as a `<Pattern>` child of the APVTS state tree (mirrors mu-tant's
gate-pattern persistence — *not* 64 automatable APVTS params). It rides along in
`get/setStateInformation`, so it saves into the host project and `.muOn` presets.

## Clocking — `GrooveSequencer`
[mu-on/Source/Sequencer/GrooveSequencer.{h,cpp}](../../mu-on/Source/Sequencer/GrooveSequencer.h) —
16 steps = one bar (each step = a 16th = 0.25 beat). Each block, `process(beatStart, numSamples,
bpm, fire)` walks the step boundaries that fall in `[beatStart, beatEnd)` and, for each active
cell, calls `fire(track, velocity, sampleOffset)`. It is driven off the product's free-running
internal transport beat (`getInternalBeatPos`); the processor runs it **before** the channel
render so a step's engine is armed for the same block. A jump guard re-syncs `nextGlobalStep` if
the transport scrubs or restarts, so it never burst-fires a backlog.

- **Swing** (`seq_swing`) delays the **odd** 16ths by up to half a step — the groove "shuffle".
- **Accent** (`seq_accent`) sets the velocity of accented cells; plain cells use a lower fixed
  velocity. Velocity is passed to the engine trigger (louder hit / harder transient).

`currentStep(beat)` gives the playhead's local step for the UI grid.

## UI — `GrooveGrid` / `GroovePanel`
[mu-on/Source/UI/GrooveGrid.{h,cpp}](../../mu-on/Source/UI/GrooveGrid.h) draws the 4-row × 16-cell
grid in the lane colours: **left-click toggles** a step on/off, **right-click toggles accent**
(shown as a bright inner pip), beat groups are tinted, and a **30 Hz playhead** column tracks the
transport. The **Swing** and **Accent** rotary sliders are bound to `seq_swing`/`seq_accent` via
APVTS attachments. `GroovePanel` stacks the grid over the per-lane `EnginePanel`; the sidebar
selection highlights the grid row and switches the engine controls. The shared sidebar pulses a
lane when the sequencer fires it.

## Future
Variable pattern length (1–N bars) + per-step velocity ramps; **pattern bank + chaining** (the
classic 909 A/B + song mode); per-step **pitch** for the bass lane (unlocks glide + note-off);
sample-accurate onset using the per-trigger `sampleOffset` already computed.
