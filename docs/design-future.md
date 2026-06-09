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

### 🟢 Standalone: Bounce to WAV (user-facing render)

Expose the headless render pipeline (introduced for the listening-test harness) as a user-facing "File → Bounce…" dialog in the standalone. User picks duration, sample rate, output file → gets a WAV of the currently-loaded patch playing for N seconds. Useful for sample-pack workflows where the user wants to commit a pattern to disk without round-tripping through a DAW.

- **Foundation in place:** [mu-clid/Source/Plugin/RenderMode.{h,cpp}](../mu-clid/Source/Plugin/RenderMode.h) already drives `processBlock` headlessly and writes WAV via `juce::WavAudioFormat`. The CLI path (`--render`) bypasses the GUI; the user-facing version reuses `RenderMode::execute` from a dialog instead of from `JUCEApplication::initialise`.
- **Suggested implementation:** new `BounceDialog` modal in the standalone's menu bar. Form fields — filename (default `<preset name>_bounced.wav`), duration (default 8 s), sample rate (default current device rate), tail length (extra silence past playback to capture FX tails). On confirm: temporarily pause the live transport, call `RenderMode::execute` against a fresh PluginProcessor seeded from the current APVTS state, write the WAV, resume.
- **Plugin / standalone parity question:** does the VST3/CLAP plugin also expose Bounce? Probably no — most DAWs have their own bounce/render workflow; a plugin offering a competing one is confusing. Standalone-only is the right scope.
- **Family reuse:** the dialog code can live in `mu-core/UI/` so every product inherits Bounce automatically (each plugin's own `RenderMode`, or the shared render path, plugs into the same dialog). Only mu-clid has a `RenderMode` today; generalising it to mu-core is the prerequisite.

### 🟢 Demo build (mu-clid)

A separate distribution binary with full plugin functionality but reduced limits, for shareware-style trials. **Distinct from `mu-clid-lite`** (which is a permanent single-rhythm MIDI-only product, not a demo).

- Cap at 2 rhythms (vs 8 in full version)
- Disable Save (Load still allowed so users can audition shared patches)
- Built from same source via CMake option `-DMU_CLID_DEMO=ON` defining a preprocessor flag; gates `addRhythm` past 2 and disables Save buttons in the UI
- About-panel "Demo" badge
- If pursued, generalise the gate to a family pattern (`-DMU_<PRODUCT>_DEMO`) so siblings get demos the same way.

### 🟡 Additional Euclid sequences per rhythm (mu-clid)

More than three generators per rhythm (currently `genA`, `genB`, `genC`). Useful for richer polyrhythms within a single slot.

- **Blocker:** `Rhythm.h` stores `genA/B/C` as fixed named members.
- **Refactor needed:** switch to `std::vector<HitGenerator>`. Contained within `Rhythm.{h,cpp}` plus the UI panels that reference the generators by name.
- **Rule going forward:** do not add further named fixed members like `genD` — switch to a vector at that point.

### 🟢 Longer sequences (mu-clid)

Step counts above 64 per ring. The sequencer engine is not the bottleneck — `SequencerEngine` uses `std::vector<bool>` patterns with no hard cap. Only constraint is the UI range in `EuclideanPanel` (1–64), trivially widened.

### 🟡 MIDI CC remote control of knobs

Generic CC → APVTS-parameter mapping so a hardware controller can drive any plugin knob. Distinct from MIDI program change → preset (already done family-wide via the shared `MidiPresetMap`).

- **No foundation in place yet.** *(An earlier `ControlSequence::InputSource::MIDI_CC` / `midiCCNumber` data-model stub was removed in a later modulation refactor — this would be built fresh.)*
- **Wiring needed:** read incoming CC values in `processBlock`, hold a CC→param-id map (MIDI-learn or a settings table), and either drive the `APVTS` parameter directly or feed values into the `paramValues` map that `ModulationMatrix::process()` reads from.
- Belongs in `mu-core` (shared `processBlock`/modulation path) so all products inherit it.

### 🟡 Inter-plugin sync — see **mu-link**

Run multiple μ-family standalones and have them share a clock + summing bus for synchronised live performance. **This is no longer an open idea — it is the dedicated sibling product `mu-link`**, which owns one hardware output, publishes a sample-accurate `TransportBlock` over shared memory, and slaves every connected client to it. See [docs/mu-link/design-mulink.md](mu-link/design-mulink.md). Foundation (IPC ring + transport clock + tests) is scaffolded; the shared-memory mapping, audio server, and GUI are staged increments.

- **Already live:** mu-clid and mu-tant standalones consult `mu_core::readHostTransport()` and slave their beat to the mu-link clock when attached (the bridge is the header-only `mu-core/Link/MuLinkBridge.h`, compiled only into each `StandaloneApp.cpp`). mu-on / mu-toni are not yet bridge-wired (the hook is a one-liner per product when they are).
- **Remaining gaps:** the Win32 shared-memory audio server + GUI; bridge-wiring the remaining siblings; and plugin-mode sync (mu-link is standalone-only by design — in a DAW the host owns the clock, so there is nothing to add).

---

## Release & distribution (deferred until public release)

These are understood and scoped but parked until the family ships publicly. They are
release infrastructure, not features, and were moved here from the active backlog.

### 🔵 Code signing (EV certificate)

The installer `.exe`, standalone `.exe`, VST3 `.vst3`, and CLAP `.clap` must be signed with an **EV (Extended Validation) code-signing certificate** to pass Windows Defender SmartScreen without user intervention.

- Steps: (1) purchase EV cert from Sectigo or DigiCert (~£200/yr, delivered on a hardware token); (2) install `signtool.exe` (Windows SDK); (3) add a `SignTool=` directive to `installer/mu-Clid.iss` pointing at the cert; (4) add a post-build `signtool sign` step (in CMakeLists.txt or a separate signing script) for the VST3/CLAP/Standalone artefacts before the installer step. A placeholder comment is already in `mu-Clid.iss`.
- macOS equivalent (Apple notarisation + signing for the AU/VST3/standalone) is the matching task on that platform.
- **Gates the wavetable-library shipping item below.** Deferred until public release.

### 🔵 Ship mu-tant factory wavetables with the installer

The mu-tant wavetable **system** is shipped (Serum/Vital mono-WAV loader + FFT mip-mapping, oscillator/voice/APVTS wiring, user `.wav` disk import, the in-repo `tools/wavetable-gen` generator, `clm ` chunk parsing, and dropdown sub-folder categories — all landed across **v1.0.764–769**). What remains is **distribution**: bundle the 25 generated factory `.wav` tables *with the installer* so testers/users get the library out of the box, instead of generating them into the user's own `…/muTant/Wavetables` content folder on first run.

- Blocked on the code-signing item above — the owner wants the installer + artefacts signed before shipping bundled content.
- Implementation when unblocked: add the generated `.wav` set to the installer payload (or embed via `juce_add_binary_data` + unpack on first run), pointing the loader's content-folder scan at the installed copy.

---

## Notes

If implementing any of the items above, also revisit the family `CLAUDE.md` "Critical
architectural rules" and the relevant product `CLAUDE.md` — some rules (e.g. the atomic
pointer for hot-swap, sidebar variable ordering, the engine swap-point pattern) were
introduced specifically to keep these doors open.
