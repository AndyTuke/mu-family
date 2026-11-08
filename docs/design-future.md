# μ Family — Future Feature Ideas

Ideas not scheduled for any current work. **Always ask before implementing.**
When working on current features, avoid architectural decisions that foreclose these.

This doc is family-wide. Most items below originated with μ-Clid but apply across the
family (mu-clid / mu-tant / mu-toni / mu-on) where the relevant subsystem is shared in
`mu-core`; product-specific items name their product.

For shipped features see [DevelopmentHistory.md](DevelopmentHistory.md).

---

## Status legend

- 🟢 **Open** — not started, no architectural blockers
- 🟡 **Partial blocker** — current code shape needs a localised refactor before this can land
- 🔵 **Deferred — release-gated** — understood and scoped, but parked until public release / external action

---

## Unscheduled ideas

### 🟢 New product: mu-Tilate (loop slicer)

Load a loop (usually drums), find its transients automatically (spectral flux / SuperFlux detector with threshold, min gap, band and pre-roll controls, written in-house on `juce::dsp::FFT`), show the slices as tinted regions in a scrollable, zoomable waveform, and place them on a drag-scrollable timeline with a time ruler (drag from the sample window or the pad grid, grid snap with Alt for free placement, grabbable block edges). A slice player panel gives each slice filter, amp and filter envelopes and pitch; every slice has an automatic few-ms declick fade; per-layer tempo matching is Off / Re-pitch / Stretch (Signalsmith Stretch chosen, MIT: only its notice goes into the third-party notices and About; Rubber Band and SoundTouch rejected). Stretch is transient-weighted, modelled on Bitwig: the attack plays unstretched, then the stretch eases in to an even plateau, set by Transient hold, Curve and Strength controls; driving the stretcher with a varying rate is unproven and needs a prototype and `dsp-controller` review. Same mixer, FX and modulation as every other product via `mu-core`. Wireframe, parts and open questions: [mu-tilate/design.md](mu-tilate/design.md). Needs a new product on `mu-core` (a slice-player engine and a timeline sequencer) plus the detector. Layer type `Phrase`, layer preset `.muPhrase`, full preset `.muTilate`; 8 layers as elsewhere, each with one loop, one sample player and a single-row timeline (no lanes); no MIDI play (self-driven sequencer); VST3 + CLAP + Standalone; the `Phrase` layer must drop into mu-Tronic as a plain registry entry (`mu-tilate-layer` library, `LayerTypeId::Phrase`; items for an `architecture-steward` ruling are listed in the wireframe doc). Each layer is monophonic (the next slice cuts the previous through the declick fade). An own sample library (separate folder setting and content from mu-Clid's, shared mechanism where practical) is wanted (folder browser, preview, path or embedded storage; for loops it adds length and tempo columns, audition at project tempo, drag-to-layer with automatic slicing); mu-Clid's library code is product-specific today, so by Andy's standing rule (shared code always goes in `mu-core` unless there is a very good reason not to) it is lifted into `mu-core` as part of this work and mu-Clid is refactored to use it. The same goes for the waveform view, ruler, drag-scroll viewport, the product-independent detector code and the Bounce dialog. Only the byte budget and saved-state size policy remain for the architecture-steward. Bounce to WAV and Export slices to a folder are in scope. Decided: a small free factory loop pack (CC0 / redistributable only, licence manifest per loop), loop tempo from file metadata then in-house analysis on the detection curve (confidence shown, user override saved), one export folder per loop, Bounce Standalone-only for now. No open questions remain. Nothing scheduled.

### 🟢 Demo build (mu-clid)

**Dropped 2026-10-11:** the apps are now free and fully functional when unlicensed, so no demo build or demo gate is wanted; see [design-plugin-family.md](design-plugin-family.md#licensing-and-the-support-prompt--family-standard-ruled-2026-10-11). The text below is kept only as history.

A separate distribution binary with full plugin functionality but reduced limits, for shareware-style trials. **Distinct from `mu-clid-lite`** (which is a permanent single-rhythm MIDI-only product, not a demo).

- Cap at 2 rhythms (vs 8 in full version)
- Disable Save (Load still allowed so users can audition shared patches)
- Built from same source via CMake option `-DMU_CLID_DEMO=ON` defining a preprocessor flag; gates `rhythms.add` past 2 and disables Save buttons in the UI
- About-panel "Demo" badge
- If pursued, generalise the gate to a family pattern (`-DMU_<PRODUCT>_DEMO`) so siblings get demos the same way.

### 🟡 Additional Euclid sequences per rhythm (mu-clid)

More than three generators per rhythm (currently `genA`, `genB`, `genC`). Useful for richer polyrhythms within a single slot.

- **Blocker:** `Rhythm.h` stores `genA/B/C` as fixed named members.
- **Refactor needed:** switch to `std::vector<HitGenerator>`. Contained within `Rhythm.{h,cpp}` plus the UI panels that reference the generators by name.
- **Rule going forward:** do not add further named fixed members like `genD` — switch to a vector at that point.

### 🟢 Longer sequences (mu-clid)

Step counts above 64 per ring. The sequencer engine is not the bottleneck — `RhythmSequencer` uses `std::vector<bool>` patterns with no hard cap. Only constraint is the UI range in `EuclideanPanel` (1–64), trivially widened.

### 🟡 MIDI CC remote control of knobs

Generic CC → APVTS-parameter mapping so a hardware controller can drive any plugin knob. Distinct from MIDI program change → preset (already done family-wide via the shared `MidiPresetMap`).

- **No foundation in place yet.** *(An earlier `ControlSequence::InputSource::MIDI_CC` / `midiCCNumber` data-model stub was removed in a later modulation refactor — this would be built fresh.)*
- **Ruled 2026-10-10** (full detail: [design-plugin-family.md, MIDI control mapping](design-plugin-family.md#midi-control-mapping--family-standard-ruled-2026-10-10)): the CC sets the **APVTS base value** (gesture-safe, on the message thread; the audio thread only coalesces and queues, never writes a parameter), not the `ModulationMatrix` input. The map is one global per-user file per product (not in the preset), targets by parameter id, in `mu-core/Control/` with a device-independent `ControlAction` vocabulary shared with `mu-control`. Quantise (off / beat / bar, default bar) applies to mute, solo and stop only.
- **Performance level (proposed, owner 2026-10-09):** a list of 8 pointers to presets (each up to 8 layers, each layer 8 clips) for playing live; a performance is the natural container for the one-instance rig below. See [design-launchpad.md](design-launchpad.md).
- **First user: the Novation Launchpad X** (owner's controller, for playing live). Its full MIDI implementation, a proposed performance layout and the risks are in [design-launchpad.md](design-launchpad.md); backlog #1274-#1279 and #1281.

### 🟡 Inter-plugin sync — see **mu-link**

Run multiple μ-family standalones and have them share a clock + summing bus for synchronised live performance. **This is no longer an open idea — it is the dedicated sibling product `mu-link`**, which owns one hardware output, publishes a sample-accurate `TransportBlock` over shared memory, and slaves every connected client to it. See [docs/mu-link/design-mulink.md](mu-link/design-mulink.md). Foundation (IPC ring + transport clock + tests) is scaffolded; the shared-memory mapping, audio server, and GUI are staged increments.

- **Already live:** mu-clid and mu-tant standalones consult `mu_core::readHostTransport()` and slave their beat to the mu-link clock when attached (the bridge is the header-only `mu-core/Link/MuLinkBridge.h`, compiled only into each `StandaloneApp.cpp`). mu-on / mu-toni are not yet bridge-wired (the hook is a one-liner per product when they are).
- **Remaining gaps:** the Win32 shared-memory audio server + GUI; bridge-wiring the remaining siblings; and plugin-mode sync (mu-link is standalone-only by design — in a DAW the host owns the clock, so there is nothing to add).

### Ableton Link (dropped)

**Dropped by Andy on 2026-10-20.** Not planned and not to be built; the plan is kept as history in [design-ableton-link.md](design-ableton-link.md). Do not design for a Link clock source. The MIDI sync groundwork stands on its own merits.

### 🟢 Standard UI sizing across the family

**The aspiration:** every app has the same panel sizing in the layout — the same preset strip, main panel, modulator panel and mixer heights and proportions — so mu-Clid, mu-Tant, mu-Toni and mu-On line up when placed side by side, and a layer, a sidebar or a modulator panel is the same size wherever it appears.

- Owner intention (2026-10-07, looking at mu-Clid and mu-Tant side by side). Currently parked **On Hold** as backlog #1172 — the owner will schedule it; do not attempt it before then.
- Fits with the one-framework idea above: a shared sizing contract lives in `MuLookAndFeel` / the editor shell (no per-product layout constants), and each product supplies only its main-panel content.

---

### 🟢 mu-On sequencer: what other machines offer (researched 2026-11-08)

Andy asked what other sequencers have. This is the field, taken from the Elektron machines (Digitakt, Analog Rytm, Octatrack), Polyend Tracker, Torso T-1, Drambo and the Ableton-side tools, with what mu-On already has and a suggested order. Nothing here is scheduled; Andy picks.

**Already in mu-On (after 2026-11-08):** per-step probability, loop conditions (Bars and Length, the A:B idea), Fill / Pre / !Pre conditions with a momentary Fill button, per-step Level, repeats with a Rate and a Fade (Elektron's retrig), per-track step length and paging (polymeter), ties and glide, accent and ghost steps, swing.

**Worth considering, in the order I would do them**
1. **Parameter locks (per-step knob values).** The feature people name first on Elektron machines: hold a step and turn a knob, and that step plays with that value (a filter cutoff, a decay, a tune). In mu-On this is a per-step override of a chosen voice parameter, shown on the step; it is the biggest sound-design gain per step and extends the Step panel's pattern (Octave and Note are already two per-step overrides).
2. **Micro-timing (nudge a step off the grid).** A per-step offset of a fraction of a step, early or late, for human feel; swing is global, this is per hit. Cheap in the clock (an offset on the step's beat) and a single knob or drop-down in the Step panel.
3. **Per-step gate length.** How long a step holds (Bass and Chord use it; drums are one-shot). Pairs with ties.
4. **More conditions.** NEI (play depending on the neighbouring track's last step), 1ST (first pass only) and the negations, and a fill that is automatic on the last bar of a loop. The condition drop-down already has the slot.
5. **Mutate / randomise.** Random a row, or nudge it a little each loop ("mutate"), with a depth knob; Torso's T-1 and Digitakt's randomise are popular for generating variations.
6. **Euclidean and rotate tools for a row.** Fill a row with a Euclidean pattern and rotate it (mu-Clid already has the Euclid engine); one-click row shift left or right.
7. **Pattern chaining and a song mode.** Chain patterns or scenes into a song with repeat counts (mu-Tronic already has scenes; mu-On has no pattern bank, listed on its page as not yet).
8. **Per-track speed (a clock multiplier).** Play a track at 2x or half the master, another route to polyrhythm besides step length.
9. **Step copy, paste, clear and a nudge-the-whole-row command.** Housekeeping people expect once rows get rich.
10. **Per-step slide / accent shapes.** A slide flag on any step (Bass has glide on tied steps), a second accent level.

**Not recommended for mu-On:** sample-slicing locks (that is mu-Tilate), a tracker-style grid (a different product), arpeggiator-style note repeat modes (mu-Toni).

**Sources:** Elektron parameter locks, trig conditions and retrig behaviour from the Elektronauts forum threads and the Analog Rytm manual; Drambo's documentation (step components, parameter locks); MusicRadar's Torso T-1 review; Polyend Tracker comparisons.

---

## Release & distribution (deferred until public release)

These are understood and scoped but parked until the family ships publicly. They are
release infrastructure, not features, and were moved here from the active backlog.

---

## Notes

If implementing any of the items above, also revisit the family `CLAUDE.md` "Critical
architectural rules" and the relevant product `CLAUDE.md` — some rules (e.g. the atomic
pointer for hot-swap, sidebar variable ordering, the engine swap-point pattern) were
introduced specifically to keep these doors open.
