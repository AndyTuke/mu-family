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

### 🟢 New product: loop slicer (name not chosen)

Load a loop (usually drums), find its transients, cut it into slices, and place the slices on a timeline sequencer. First wireframe, parts and open questions: [design-slicer-wireframe.md](design-slicer-wireframe.md). Needs a new product on `mu-core` (a slice-player engine and a timeline sequencer; the shell, mixer, FX and sample playback are shared) plus an offline transient detector. Nothing scheduled; name, layer type and file extensions are Andy's call.

### 🟢 Standalone: Bounce to WAV (user-facing render)

Expose the headless render pipeline (introduced for the listening-test harness) as a user-facing "File → Bounce…" dialog in the standalone. User picks duration, sample rate, output file → gets a WAV of the currently-loaded patch playing for N seconds. Useful for sample-pack workflows where the user wants to commit a pattern to disk without round-tripping through a DAW.

- **Foundation in place:** [mu-core/Plugin/ProductRender.h](../mu-core/Plugin/ProductRender.h) already drives `processBlock` headlessly for every product and writes WAV via `juce::WavAudioFormat`. The CLI path (`--render`) bypasses the GUI; the user-facing version reuses `mu_core::render_mode::runProduct` from a dialog instead of from `JUCEApplication::initialise`.
- **Suggested implementation:** new `BounceDialog` modal in the standalone's menu bar. Form fields — filename (default `<preset name>_bounced.wav`), duration (default 8 s), sample rate (default current device rate), tail length (extra silence past playback to capture FX tails). On confirm: temporarily pause the live transport, call `RenderMode::execute` against a fresh PluginProcessor seeded from the current APVTS state, write the WAV, resume.
- **Plugin / standalone parity question:** does the VST3/CLAP plugin also expose Bounce? Probably no — most DAWs have their own bounce/render workflow; a plugin offering a competing one is confusing. Standalone-only is the right scope.
- **Family reuse:** the dialog code can live in `mu-core/UI/` so every product inherits Bounce automatically (each plugin's own `RenderMode`, or the shared render path, plugs into the same dialog). Only mu-clid has a `RenderMode` today; generalising it to mu-core is the prerequisite.

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

### 🟡 Sample-accurate MIDI timing (mu-Clid MIDI)

Today MIDI timing is block-quantised. Every note-on is emitted at sample offset 0 of the block, and several sequencer steps inside one block collapse into one note because there is one `firedMask` bit per rhythm. Jitter is up to one buffer (about 46 ms at 2048 samples / 44.1 kHz), and steps are dropped at fast tempos with large buffers.

- **Idea:** carry the in-block step offset from the sequencer into `trigger`, so each note lands at its true sample offset and nothing collapses (the per-rhythm fired state would need to hold several offsets per block, not one bit).
- **Why 🟡:** the sequencer's step-fired result and the trigger path both change shape (`firedMask` becomes a small per-block list of offsets). Keep it allocation-free on the audio thread.
- **Fit with the other ideas:** the same offset-carrying path would serve the other products, and it serves the MIDI sync groundwork. (Ableton Link, which would have exposed the jitter, was dropped 2026-10-20.)
- **Feature, not a fix:** ask Andy before implementing.

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

### 🟡 One framework, one instance — combine the engines in one place

**Scheduled 2026-10-15 as the fifth product mu-Tronic** (owner's brief): 16 layer slots (paged 1-8 / 9-16), any of the four layer types per slot, the shared mixer, FX and modulation, plus buses in the mixer; it may replace mu-link as a performance tool. Design, staged plan and open questions: [mu-tronic/design.md](mu-tronic/design.md). It answers the open questions below: a fifth product, 16 layers, **Standalone only and freeware (no licence key)**, and every mu-On engine is its own layer type. Andy's answers of 2026-10-15 are recorded in the design's section 9; the mu-Clid and mu-Tant extraction waits until after their next releases. The text below stays as the background.

**The idea:** the four apps are already one framework wearing four faces. The mixer, FX, transport, modulation engine, preset logic, hot-swap, MIDI handling and editor shell are all shared `mu-core` code. The only things that differ per product are the **sound engine** and the **sequencer** (the pair a layer is made of). The aim is to make that literal: **a single instance (one plugin / one standalone) that can hold layers of any engine type side by side** — for example a mu-Tant drone layer, a mu-On kick layer, a mu-Toni arp layer and a mu-Clid rhythm layer in one session, through one mixer, on one transport, with one preset.

- **Why:** one window, one clock, one set of presets for a whole live rig, without running four apps and a bus (mu-link would remain for combining separate apps and outboard gear). Every shared improvement (sync fixes, preset format, mixer) lands once and reaches every engine.
- **Groundwork already in place:** `mu-core` owns `ProcessorBase`, `EditorShellBase`, the mixer and FX chain, the transport resolver, the modulation matrix and the composed slot state (one layer = one saved unit, so a layer preset is already engine-shaped, not product-shaped). The engine swap-point pattern in [design-plugin-family.md](design-plugin-family.md) is the intended seam. Variable 1–8 layers with a central add / reorder / delete (backlog #1240) and a per-layer engine selector (#1241) are the first steps toward it.
- **The shape of it (owner, 2026-10-09):** `Layer` is the parent class; each product's layer type derives from it (mu-Clid `Rhythm`, mu-Tant `Pattern`, mu-Toni `Arp`, mu-On `Track`) and adds only its sequencer data and engine binding. Today only mu-Clid's `Rhythm` derives from the existing base (`Layer`); the other three keep parallel arrays. See [design-naming.md](design-naming.md).
- **What stands in the way (why 🟡):** parameter ids are prefixed per product and layer count is capped by one shared limit (`mu_limits::kMaxLayers`, 8); each product's `Source/` tree, state layout and editor panels assume a single engine type; the sequencer and engine are paired inside each product rather than registered as a pluggable layer type; modulation targets are per-product tables (`mu_mod::ModTarget`). A layer-type registry (engine + sequencer + panel + param table + modulation targets) would be the central piece.
- **Direction for current work:** put anything generic in `mu-core`, name product-specific code under the product namespace, and do not add new per-product copies of shared behaviour (the family consistency rule). The standalone products would stay as presets of the combined instance (one layer type each) rather than separate code.
- Open questions for when this is scheduled: how a session's layers are limited (CPU, 8 layers), how per-engine sequencer panels share the main panel, which product's key covers a combined instance (all four paid products are Pay What You Want; mu-Clid MIDI is freeware), and whether the combined instance is a fifth product or a mode of an existing one.

### 🟡 Android tablet app (standalone only, simplest scope)

Run the family on an Android tablet. **Scope (owner, 2026-10-09): standalone only, no MIDI, no mu-link, no licence, keep it simple.** Start with mu-Toni (freeware, light; compile the licence path out), internal clock only, as a spike on one real tablet. **The goal is to measure, not ship:** an Android app target hosting the existing processor, a fit-to-screen scale of the current fixed-size editor, then latency and CPU on the tablet. Parked On Hold by the owner (confirmed 2026-10-11); the backlog row was closed on 2026-10-20 and this entry keeps the idea.

- **Why 🟡:** the code is portable JUCE C++, so the engines, FX, modulation and sequencers carry over, but the editor is a fixed 1170×870 desktop layout built for mouse hover and right-click. **The UI is the main work** after the spike: touch-sized controls, no right-click or hover dependence, and a tablet layout driven by the standard sizing below (backlog #1172).
- **Smaller items:** an Android app target (the plugin targets do not apply; no VST3 or CLAP there), Android SDK / NDK on the build PC, keeping audio alive when the screen locks, a file picker and storage for presets and samples, the licence path compiled out, and a JUCE licence check for mobile.
- **Why on hold:** macOS and Linux are lower priority and untestable; Android would be testable on a device, so it is the likelier next platform when the owner schedules one.
- **Fit with the other ideas:** Android has no inter-app audio or shared MIDI, so one combined instance (above) suits it better than four apps. No sync is planned there (Ableton Link was dropped 2026-10-20; `mu-link` does not apply).
- **Rule for current work:** keep new shared UI code free of desktop-only assumptions (mouse-only input, hard-coded pixel sizes).

### 🟢 Standard UI sizing across the family

**The aspiration:** every app has the same panel sizing in the layout — the same preset strip, main panel, modulator panel and mixer heights and proportions — so mu-Clid, mu-Tant, mu-Toni and mu-On line up when placed side by side, and a layer, a sidebar or a modulator panel is the same size wherever it appears.

- Owner intention (2026-10-07, looking at mu-Clid and mu-Tant side by side). Currently parked **On Hold** as backlog #1172 — the owner will schedule it; do not attempt it before then.
- Fits with the one-framework idea above: a shared sizing contract lives in `MuLookAndFeel` / the editor shell (no per-product layout constants), and each product supplies only its main-panel content.

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

The mu-tant wavetable **system** is shipped (Serum/Vital mono-WAV loader + FFT mip-mapping, oscillator/voice/APVTS wiring, user `.wav` disk import, the in-repo `tools/wavetable-gen` generator, `clm ` chunk parsing, and dropdown sub-folder categories — all landed across **v1.0.764–769**). What remains is **distribution**: bundle the 25 generated factory `.wav` tables *with the installer* so users get the library out of the box, instead of generating them into the user's own `…/muTant/Wavetables` content folder on first run.

- Blocked on the code-signing item above — the owner wants the installer + artefacts signed before shipping bundled content.
- Implementation when unblocked: add the generated `.wav` set to the installer payload (or embed via `juce_add_binary_data` + unpack on first run), pointing the loader's content-folder scan at the installed copy.

---

## Notes

If implementing any of the items above, also revisit the family `CLAUDE.md` "Critical
architectural rules" and the relevant product `CLAUDE.md` — some rules (e.g. the atomic
pointer for hot-swap, sidebar variable ordering, the engine swap-point pattern) were
introduced specifically to keep these doors open.
