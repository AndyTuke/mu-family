# μ-Clid — Future Feature Ideas

Ideas and thoughts about possible new features. **None of these are scheduled for any current stage.**
When implementing current stages, ensure architectural decisions do not foreclose these options.

---

## Pending Stage 10 work (scheduled, not yet implemented)

These features are confirmed and must be done before Stage 11:

- **F4: Rhythm renaming** — click name in RhythmPanel header to edit inline; name propagates to sidebar item and mixer channel label
- **F5: Reset + Delete buttons** — in RhythmPanel header; both show "Are you sure?" `AlertWindow` before acting; Delete shifts sidebar selection to previous rhythm
- **F2: EFX→Delay+Reverb and Delay→Reverb sends** — `FXChain::processSends()` applies `effToDelay`/`effToReverb`/`delToReverb` sequentially; Effect return channel shows two send knobs; Delay return shows one
- **F7: Echo = full Delay algorithm** — when Effect slot algo = Echo, embed a `DelaySlot echoDelay` in `EffectSlot` and show a full `DelayRow echoRow` in MixerOverlay between Effect and Delay rows; add `echo_*` APVTS params
- **F14: Master loop length** — `SequencerEngine` adds a `masterLoopSteps` counter (16–256); all sequences reset to step 0 on the boundary; `TransportBar` shows a `NudgeInput` for loop length
- **APVTS wiring** — full Stage 10 work: all rhythm params, FX params, mixer params bound through APVTS so presets save/restore correctly

---

## Unscheduled Ideas

Ability to add additional Euclid sequences with more complex logic

Ability to have longer sequences

Ability to sync stand alone version to external MIDI

Ability to use midi controllers to remote control knobs via MIDI

Ability to have other mu family plugins, and run them together with an efficient internal sync between them to build a more powerful live performance tool

---

## Architectural notes — what current code does and does not foreclose

### Additional Euclid sequences ⚠️ Partial concern
`Rhythm.h` stores `genA`, `genB`, and `genC` as fixed named `HitGenerator` members. Adding a fourth or fifth generator would require changing `Rhythm` to hold a `std::vector<HitGenerator>` rather than fixed members. This is a moderate refactor but not catastrophic — the change is contained within `Rhythm.h`, `Rhythm.cpp`, and the UI panels that reference `genA`/`genB`/`genC` by name. **Do not add further named fixed members** (e.g. `genD`) when implementing more generators — switch to a vector at that point.

### Longer sequences ✅ Not foreclosed
`SequencerEngine` stores cached patterns as `std::vector<bool>`. `resetSteps` is `std::optional<int>`. No hard upper limit in the sequencer engine itself. The UI range (1–64 in EuclideanPanel) is the only current constraint — easily widened.

### External MIDI sync for standalone ✅ Not foreclosed
`PluginProcessor` has a separate internal transport (`internalBpm`, `internalBeatPos`). MIDI clock input could be handled in `processBlock` via the `MidiBuffer` argument. `ControlSequence::InputSource::MIDI_CC` is already defined in the enum. No changes needed to the core engine.

### MIDI controller remote control ✅ Not foreclosed
`ControlSequence::InputSource::MIDI_CC` and `midiCCNumber` are already in the data model. The mapping just needs to be wired: read incoming CC values in `processBlock`, write them into the `paramValues` map that `ModulationMatrix::process()` reads from. No structural changes required.

### Inter-plugin sync (mu family) ⚠️ Clock is per-instance
The standalone internal transport (`internalBpm` / `internalBeatPos` as plain doubles on `PluginProcessor`) is isolated per plugin instance. Sharing a clock between mu-family plugins would require either a shared-memory mechanism, a VST3 inter-plugin bus, or an agreed MIDI-clock master/slave scheme. **Current architecture does not foreclose this**, but no shared-clock hook point exists yet. When designing inter-plugin sync, consider adding a static or process-global clock object that multiple processors can register with — this is easiest to add before APVTS wiring (Stage 10).

