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

## Completed features

Features shipped during development — kept here for reference.

- Mixer channel faders at 50% of original height
- Sample file browser remembers and returns to last used file location
- Filter frequency display: 0–999 Hz as integer Hz; 1–9.99 kHz as x.xx kHz; ≥10 kHz as x.x kHz
- Delay row redesign: Sync toggle replaced with Free/1/32/1/16/1/8/1/4 dropdown; 3-position Straight/Dotted/Triplet rotary; Free mode hides note/modifier controls and shifts remaining controls left
- Mixer channel strip send controls match standard knob size

---

## Unscheduled Ideas

### Preset & Rhythm File System

**Preset format `.mu`** — full plugin state saved as a single file. Two save modes selected at save time:
- **Link samples** — preset stores absolute or relative paths to sample files; preset file is small (XML/JSON state only); breaks if samples move.
- **Embed samples** — preset is a single self-contained archive (e.g. ZIP-style container) holding the state + all referenced sample audio data. Larger but portable.

**Rhythm format `.mur`** — single-rhythm export/import. Same embed/link option as `.mu`. Useful for sharing rhythm patches between projects without taking the whole preset.

**Default file locations** — all configurable in the Settings overlay; defaults below assume Windows. macOS equivalent uses `~/Documents/TDP/mu-Clid/...`.
- Presets: `<My Documents>\TDP\mu-Clid\Presets`
- Rhythms: `<My Documents>\TDP\mu-Clid\Rhythms`
- Samples: `<My Documents>\TDP\mu-Clid\Samples` (default browse location for sample loader)

**Auto-loaded defaults**:
- **`_default.mu`** in the preset folder — loaded on plugin instantiation. User can overwrite this from a Save dialog ("Save as default") to customise the startup state.
- **`default.mur`** in the rhythm folder — used as the template when a new rhythm slot is added. User can overwrite this similarly.

**Architecture notes**:
- Existing `PluginProcessor::getStateInformation`/`setStateInformation` already serialises full state; the file format work is mostly serialisation framing + a sample-archive container.
- `loadedSamplePaths` is a `juce::StringArray` indexed by rhythm; embed mode needs to inline the audio bytes into the archive and rewrite paths on load to point into a temp extraction (or stream from the archive).
- For `.mu`/`.mur`, prefer JUCE's `ZipFile` + a manifest XML inside, so the format stays inspectable and trivially extensible. Each entry: `manifest.xml` + `samples/<id>.wav` (if embedded).
- Settings overlay needs three new path fields with browse buttons; all paths created on first run if missing.

### Demo / Distribution

**Demo build** — limited-functionality binary for distribution:
- Maximum 2 rhythms (vs 8 in full version)
- Saving disabled (presets and rhythms) — Load is still allowed so users can audition shared patches
- Built from the same source via a CMake option (`-DMU_CLID_DEMO=ON`) that defines a preprocessor flag; gates `addRhythm` past 2 and disables Save buttons in the UI
- Splash watermark or About-panel "Demo" badge so users know which version they're on

**Installer** — deployable installer for end users (Windows: NSIS or WiX; macOS: pkg). Installs:
- VST3 to standard plugin folder (Windows: `Program Files\Common Files\VST3`; macOS: `/Library/Audio/Plug-Ins/VST3`)
- Standalone executable to Program Files / Applications
- SoundTouch DLL alongside the VST3 (LGPL ship-as-DLL requirement)
- Creates default folders under `<My Documents>\TDP\mu-Clid\{Presets,Rhythms,Samples}` and seeds them with stock presets/rhythms (including `_default.mu` and `default.mur`)

### Other ideas

**Sidechain ducking** — every mixer channel (8 rhythm channels + Effect/Delay/Reverb returns + Master) gets a sidechain input selector and an amount knob. The selector chooses any other channel as the trigger source (e.g. channel 2 ducks from channel 1's signal). Amount controls how much the channel's output is attenuated when the sidechain source is active. This allows standard kick-ducking-bass patterns and more creative cross-rhythm gating. Architecture notes: MixerEngine already holds per-channel level values; sidechain detection (peak/RMS with fast attack, variable release) can run after individual channel processing and before the summing bus. Each channel needs a `sidechainSource` index (−1 = off, 0–10 = channel index) and `sidechainAmount` (0–1). No cross-rhythm modulation concern — this is post-voice mixing, not sequencer modulation.

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

