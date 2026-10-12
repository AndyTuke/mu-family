# μ-On — Sequencer design

A 909-style step sequencer drives every track (layer), one row of 16 steps per layer slot. A track
running the **Rumble** engine is not sequenced — it processes another track's audio (see
[design-engine.md](design-engine.md)) — and ignores its row, which is kept for when the track's engine
changes.

## Model — `StepPattern`
[mu-on/Source/Sequencer/StepPattern.h](../../mu-on/Source/Sequencer/StepPattern.h) — a grid of
**8 rows × 16 steps** (`mu_limits::kMaxLayers` rows). Each cell carries an **on/off** and an **accent**
flag. Cells are `std::atomic<bool>` so the message thread (grid editing) and the audio thread
(sequencer read) share them without a lock or a dropped block. A row is a `StepRow` owned by its
`Track`, so it travels with the track through add / delete / reorder; the pattern points each slot at
its track's row (`bind`, redone after every structural edit under the layer lock), and the sequencer
reads it inside the processor's layer try-lock. Each engine has a starting groove
(`StepRow::loadDefaultGroove`): four-on-the-floor kick, off-beat bass, straight-8th hats, backbeat
snare, nothing for Rumble; a fresh instance is one Kick track with the kick groove, and a new track
takes the groove of its engine.

**Persistence:** each track saves its row as a `<Track i on accent>` child of its layer node (always,
whatever its engine), so a row rides along in `get/setStateInformation`, `.muOn` and `.muTrack`
presets — *not* 128 automatable APVTS params. States from the five-lane build keep their root
`<Pattern>` and it is moved into the layer nodes on load.

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

## UI — `StepRows` / `TrackContent`
[mu-on/Source/UI/StepRows.{h,cpp}](../../mu-on/Source/UI/StepRows.h) draws one 16-cell row per live
track in the track colours, every row the same height (steps-box height / `kMaxLayers`), top-aligned:
**left-click toggles** a step on/off, **right-click toggles accent** (a hotter centre), beat groups
are tinted, and a **30 Hz playhead** column tracks the transport; the selected track's row is
outlined, a click in a row selects its track, and a Rumble row is dimmed and not editable.
`TrackContent` ([TrackContent.h](../../mu-on/Source/UI/TrackContent.h)) is the content band of the
shared `LayerPanel`: the ENGINE box (Engine dropdown, the Rumble's Source, the engine's knobs) over
the Groove box (the global **Swing** and **Accent**, bound to `seq_swing`/`seq_accent`, or the Rumble
envelope) and the step rows. The shared sidebar pulses a track when the sequencer fires it.

## Future
Variable pattern length (1–N bars) + per-step velocity ramps; **pattern bank + chaining** (the
classic 909 A/B + song mode); per-step **pitch** for the Bass engine (unlocks glide + note-off);
sample-accurate onset using the per-trigger `sampleOffset` already computed.
