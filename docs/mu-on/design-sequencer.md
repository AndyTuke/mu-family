# μ-On — Sequencer design

A 909-style step sequencer drives every track (layer), one row of 16 steps per layer slot. A track
running the **Rumble** engine is not sequenced — it processes another track's audio (see
[design-engine.md](design-engine.md)) — and ignores its row, which is kept for when the track's engine
changes.

## Model — `StepRow`, and `StepPattern` as the grid view
A row is 16 steps; each cell carries an **on/off** and an **accent** flag, as `std::atomic<bool>` so the
message thread (grid editing) and the audio thread (clock read) share them without a lock or a dropped
block. A `StepRow` is owned by its `Track` ([Sequencer/Track.h](../../mu-on/Source/Sequencer/Track.h)),
so it travels with the track through add / delete / reorder, and the track's clock reads it inside the
layer's render call. `StepPattern`
([StepPattern.h](../../mu-on/Source/Sequencer/StepPattern.h)) is only the standalone's view: a grid of
**8 rows × 16 steps** (`mu_limits::kMaxLayers` rows) whose slots point at the tracks' rows (`bind`,
redone after every structural edit under the layer lock). Each engine has a starting groove
(`StepRow::loadDefaultGroove`): four-on-the-floor kick, off-beat bass, straight-8th hats, backbeat
snare, nothing for Rumble; a fresh instance is one Kick track with the kick groove, and a new track
takes the groove of its engine.

**Persistence:** each track saves its row as a `<Track i on accent>` child of its layer node (always,
whatever its engine), so a row rides along in `get/setStateInformation`, `.muOn` and `.muTrack`
presets — *not* 128 automatable APVTS params. States from the five-lane build keep their root
`<Pattern>` and it is moved into the layer nodes on load.

## Clocking — one `StepClock` per track
16 steps = one bar (each step = a 16th = 0.25 beat; the layer's `loopBeats()` is 4). **Every track clocks
its own row** with a `StepClock` ([Sequencer/StepClock.h](../../mu-on/Source/Sequencer/StepClock.h); the row is
[StepRow.h](../../mu-on/Source/Sequencer/StepRow.h)). In `Track::render` the
clock's `processSpan(row, startBeat, n, beatsPerSample, swing, accent, fire)` walks the step boundaries
that fall in this call's beats, taken from the shared `RenderContext` (`startBeat`, `beatsPerSample`),
and for each active cell fires the track's engine at the step's sample offset, before the engine renders
so a step sounds in its own block. There is no separate sequencer pass and no shared clock object: the
only shared clock is the transport in the context. A jump guard re-syncs the clock's cursor if the
transport scrubs or restarts, so it never burst-fires a backlog. A track parked in a spare slot keeps its
clock in step (`onParkedBlock`), so it fires on time once live. `GrooveSequencer` (the whole-grid
clock over a `StepPattern`) is test support in `Tests/`; the arithmetic is the same.

- **Swing** (`t{N}_swing`, per track) delays the **odd** 16ths by up to half a step — the groove "shuffle".
- **Accent** (`t{N}_accent`, per track) sets the velocity of accented cells; plain cells use a lower fixed
  velocity (0.72; an accented cell plays at 0.72 + knob × 0.28, so an accent is never quieter than a plain step). Velocity is passed to the engine trigger (louder hit / harder transient).

Swing and accent are per-track parameters (`t{N}_swing` 0..1 default 0, `t{N}_accent` 0..1 default 1, the last rows of the `t{N}_` table): each track copies
its own into its row at the top of every call. **Ruled and built 2026-10-20 ([design 2.1.11](../mu-tronic/design.md) point 4, T1b step 2b):** the global
`seq_swing` / `seq_accent` rows are gone, an older state's global values are ignored (no migration), and the Swing and Accent knobs move the shown track only.

`StepClock::currentStep(beat)` gives the playhead's local step
for the UI grid.

## UI — `StepRows` / `TrackContent`
[mu-on/Source/UI/StepRows.{h,cpp}](../../mu-on/Source/UI/StepRows.h) draws one 16-cell row per live
track in the track colours, every row the same height (steps-box height / `kMaxLayers`), top-aligned:
**left-click toggles** a step on/off, **right-click toggles accent** (a hotter centre), beat groups
are tinted, and a **30 Hz playhead** column tracks the transport; the selected track's row is
outlined, a click in a row selects its track, and a Rumble row is dimmed and not editable.
Both read the processor only through `TrackHost` ([UI/TrackHost.h](../../mu-on/Source/UI/TrackHost.h)), which `PluginProcessor` implements; they are the `mu-on-layer` library's UI sources (`MU_ON_LAYER_UI_SOURCES`).
`TrackContent` ([TrackContent.h](../../mu-on/Source/UI/TrackContent.h)) is the content band of the
shared `LayerPanel`: the ENGINE box (Engine dropdown, the Rumble's Source, the engine's knobs) over
the Groove box (the track's own **Swing** and **Accent**, bound to `t{N}_swing`/`t{N}_accent`, or the Rumble
envelope) and the step rows. The 16-cell drawing, hit test and edit gesture live once in `TrackStepRow` ([UI/TrackStepRow.h](../../mu-on/Source/UI/TrackStepRow.h)):
`StepRows` calls its static `paintCells` / `stepAt` / `editStep` per row, and a locked track (a mu-Tronic layer) shows the one-row `TrackStepRow` component instead. The shared sidebar pulses a track when the sequencer fires it.

## Future
Variable pattern length (1–N bars) + per-step velocity ramps; **pattern bank + chaining** (the
classic 909 A/B + song mode); per-step **pitch** for the Bass engine (unlocks glide + note-off);
sample-accurate onset using the per-trigger `sampleOffset` already computed.
