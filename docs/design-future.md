# μ-Clid — Future Feature Ideas

Ideas not scheduled for any current stage. **Always ask before implementing.**
When working on current stages, avoid architectural decisions that foreclose these.

For shipped features see [DevelopmentHistory.md](DevelopmentHistory.md).

---

## Status legend

- 🟢 **Open** — not started, no architectural blockers
- 🟡 **Partial blocker** — current code shape needs a localised refactor before this can land

---

## Unscheduled ideas

### 🟢 Demo build

A separate distribution binary with full plugin functionality but reduced limits, for shareware-style trials. **Distinct from `mu-clid-lite`** (which is a permanent single-rhythm MIDI-only product, not a demo).

- Cap at 2 rhythms (vs 8 in full version)
- Disable Save (Load still allowed so users can audition shared patches)
- Built from same source via CMake option `-DMU_CLID_DEMO=ON` defining a preprocessor flag; gates `addRhythm` past 2 and disables Save buttons in the UI
- About-panel "Demo" badge

### 🟡 Additional Euclid sequences per rhythm

More than three generators per rhythm (currently `genA`, `genB`, `genC`). Useful for richer polyrhythms within a single slot.

- **Blocker:** `Rhythm.h` stores `genA/B/C` as fixed named members.
- **Refactor needed:** switch to `std::vector<HitGenerator>`. Contained within `Rhythm.{h,cpp}` plus the UI panels that reference the generators by name.
- **Rule going forward:** do not add further named fixed members like `genD` — switch to a vector at that point.

### 🟢 Longer sequences

Step counts above 64 per ring. The sequencer engine is not the bottleneck — `SequencerEngine` uses `std::vector<bool>` patterns with no hard cap. Only constraint is the UI range in `EuclideanPanel` (1–64), trivially widened.

### 🟢 MIDI CC remote control of knobs

Generic CC → APVTS-parameter mapping so a hardware controller can drive any plugin knob. Distinct from #127 (MIDI program change → rhythm preset, already done).

- **Foundation already in place:** `ControlSequence::InputSource::MIDI_CC` and `midiCCNumber` exist in the data model.
- **Wiring needed:** read incoming CC values in `processBlock`, feed them into the `paramValues` map that `ModulationMatrix::process()` reads from.
- No structural changes required.

### 🟡 Inter-plugin sync (μ family)

Run multiple μ-family plugins (e.g. mu-clid + future siblings) and have them share a clock for synchronised live performance.

- **Current state:** standalone internal transport (`internalBpm` / `internalBeatPos` as plain doubles on `PluginProcessor`) is isolated per plugin instance.
- **Options:** shared-memory mechanism, VST3 inter-plugin bus, or agreed MIDI-clock master/slave scheme.
- No shared-clock hook point exists yet, but the architecture does not foreclose it. Easiest to add a process-global clock object that multiple processors can register with.

---

## Notes

If implementing any of the items above, also revisit `CLAUDE.md` "Critical architectural rules" — some rules (e.g. atomic pointer for rhythm hot-swap, RhythmSidebar variable ordering) were introduced specifically to keep these doors open.
