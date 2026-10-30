# mu-Tilate: loop slicer wireframe

Status: **idea and first wireframe only** (Andy, 2026-10-29; named mu-Tilate and requirements added 2026-10-30). Nothing is scheduled or built. It is listed under Unscheduled ideas in [design-future.md](design-future.md); ask before implementing.

## The idea

Load a loop (usually a drum loop, but any audio). The product finds the transients and cuts the loop into slices. A sequencer then places those slices on a timeline, so the loop can be rearranged, repeated, reversed, thinned out, re-pitched or matched to a new tempo.

## How it fits the family

Follows [design-plugin-family.md](design-plugin-family.md) and [design-ui-family.md](design-ui-family.md): a new product links `mu-core` and supplies only its own engine, sequencer and content band. The wireframe below uses the standard shell and shared components; the product-specific parts are marked ★. No per-product layout constants: sizes come from `MuLookAndFeel`, depth from `MuTheme::Lighting`, controls from the shared library (`KnobWithLabel`, `SegmentControl`, `DropdownSelect`, `NudgeInput`, `SlideSwitch`).

- **Shell (shared, from `EditorShellBase`):** TransportBar, StatusBar, About / Save / Preset overlays, `MuLookAndFeel`, metal style.
- **Mixer, FX and modulation (shared):** the same mu-family mixer, FX rack and modulation section as every other product, all from `mu-core`. The product adds only its modulation targets, as one `mu_mod::ModTarget` table (filter cutoff, pitch, gain, pan, tempo-match amount and so on; rules in [design-plugin-family.md](design-plugin-family.md#modulation-targets--family-standard)). Every knob that is a row of that table shows the live modulation arc.
- **Layers:** 8 layers as in every product; one layer is one loop with its slices, one sample player and one row of placed slices. Sidebar, add / delete / reorder, mixer strip and FX rack come from `mu-core` like the other products.
- **Engine ★:** a slice player (a `SamplePlayer` per voice, each slice with its own start, end, gain, pitch, reverse, fade, filter and envelopes), plus an optional tempo-match stage.
- **Sequencer ★:** a timeline (not a 16-step grid): slices placed as blocks on a beat grid.
- **Transient detection ★:** an analysis step run on a worker thread when the loop is loaded or a detection control changes; the result is a list of slice points stored in the layer's state.

## Wireframe: main view

```
┌──────────────────────────────────────────────────────────────────────────────────────────────┐
│ TRANSPORT  [▶] [■]  BPM 124.0  Loop 2 bars  Sync [Host|Int]  ·····  Preset ▾  Save  Mixer  ⚙ │  shared
├────────────┬─────────────────────────────────────────────────────────────────────────────────┤
│ LAYERS     │  SOURCE ★                                                                        │
│            │  [Load loop…]  amen_break.wav   2 bars · detected 124.0 BPM [Set]  Tempo [Off|Pitch|Stretch] │
│ ▣ Break    │  ┌────────────────────────────────────────────────────────────────────────────┐  │
│   (lamp)   │  │░░░░░░░▒▒▒▒▒▒▒░░░░░░░▒▒▒▒▒▒░░░░░░░░▒▒▒▒▒▒▒░░░░░░  tinted regions alternate;   │  │
│ ▢ Perc     │  │ ▏█▌ ▏  ▌█▏   ▏█▌ ▏   ▌▏█  ▏  ▌█▏   ▏█▌  ▏  ▌▏█   ▏  ▌█▏   ▏█▌ ▏  ▌█   │  │
│ ▢ Vocal    │  │ ┃1    ┃2     ┃3     ┃4    ┃5     ┃6    ┃7     ┃8     ┃9    ┃10    ┃11 ┃12 │  │
│            │  │ selected slice (5) is brighter; marker ┃ per slice point (drag to move,     │  │
│ [ + ]      │  │ double-click adds, right-click deletes). Scroll = pan, Ctrl+wheel = zoom.   │  │
│            │  └────────────────────────────────────────────────────────────────────────────┘  │
│            │  ◄━━━━━━━━[ overview / scrollbar ]━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━► Zoom ─●─ │
│            │  Method [Transients|Soft-tonal|Even]  Sensitivity (•)  Min gap (•)  Pre-roll (•)  │
│            │  Low-cut (•)  High-cut (•)   Snap [Off|1/16|1/8]   [Re-detect]   Slices: 12      │
│            │  STRETCH (when Tempo = Stretch)  Transient hold (•) ms  Curve (•)  Strength (•)  ╱‾‾ │
│            ├─────────────────────────────────────────────────────────────────────────────────┤
│            │  SLICES ★                       │ SLICE PLAYER ★  slice 5 (selected)              │
│            │  ┌───┬───┬───┬───┬───┬───┬───┬───┐ │ Start ▏0.412 s End ▏0.538 s  Fade in/out (•)(•) │
│            │  │ 1 │ 2 │ 3 │ 4 │ 5 │ 6 │ 7 │ 8 │ │ Gain (•) Pan (•) │ Pitch (•) Fine (•)         │
│            │  ├───┼───┼───┼───┼───┼───┼───┼───┤ │ FILTER  Type ▾ Cutoff (•) Res (•) EnvAmt (•)   │
│            │  │ 9 │10 │11 │12 │   │   │   │   │ │ AMP ENV    A (•) D (•) S (•) R (•)             │
│            │  └───┴───┴───┴───┴───┴───┴───┴───┘ │ FILTER ENV A (•) D (•) S (•) R (•)             │
│            │  drag a pad onto the timeline,     │ [Reverse] [Loop] [Choke ▾] Play [One-shot|Gate] │
│            │  click to audition                 │                                                 │
│            ├─────────────────────────────────────────────────────────────────────────────────┤
│            │  TIMELINE ★   Length [ 4 ] bars   Grid [1/16 ▾]   (Alt = no snap)   Zoom ─●─      │
│            │  ┌────────────────────────────────────────────────────────────────────────────┐  │
│            │  │ 1       2       3       4       5       6       7       8     ← time ruler  │  │
│            │  │ ▐1█▌│▐2█▌│     │▐3█▌│▐1█▌│▐2█▌│▐4█▌│  │▐5█▌│▐6▌ │▐7█▌│▐8█▌│     single row  │  │
│            │  │ (edges grabbable: ◄▐ block ▌►)            drag background = scroll          │  │
│            │  └────────────────────────────────────────────────────────────────────────────┘  │
│            │  [Original order]  [Randomise]  [Clear]            playhead ▼ follows transport  │
├────────────┴─────────────────────────────────────────────────────────────────────────────────┤
│ STATUS  Slice 5: 0.126 s · kick · snaps to 1/16                                       v1.1.NNN │  shared
└──────────────────────────────────────────────────────────────────────────────────────────────┘
```

## Parts and behaviour

### Source ★
- **Load loop:** drag-and-drop or button; WAV first (AIFF, FLAC later, via the existing decoders).
- **Length and tempo:** the loop's length in bars and the detected tempo are shown; `Set` lets the user correct them. The timeline's time base is beats.
- **Tempo matching (per layer):** *Off* (slices play at their recorded speed and are only re-timed by the sequencer), *Re-pitch* (varispeed: speed and pitch follow the ratio of host tempo to loop tempo) or *Stretch* (time-stretch with pitch preserved). Slice start and end are scaled by the tempo ratio so slices stay on the grid in every mode. See "Tempo matching and time-stretch" below.
- **Waveform with highlighted slices:** the shared waveform drawing. The regions between detected transients are tinted in alternating strengths (two tints, so neighbours are always distinguishable) and the selected slice is emphasised (brighter tint plus the layer-colour outline). Tints come from `MuLookAndFeel` colour tokens at `MuTheme::Lighting` strengths, never literal alphas. Markers show in the layer colour.
- **Scroll and zoom:** a long loop gets a scrollable and zoomable waveform. Mouse wheel scrolls, Ctrl+wheel (and the Zoom slider) zooms around the pointer, and an overview strip with a draggable window sits under the waveform. Zoom and scroll position are view state, not saved with the preset. The same pan / zoom behaviour is reused by the timeline, so it is built once as a shared component (see shared pieces below).
- **Marker editing:** markers are grabbable, snap to the grid when Snap is on (hold Alt for free movement, as in the timeline), and a manually moved marker is **pinned** (shows a small pin) and survives *Re-detect*.

### Transient detection ★

**Research summary (to be re-checked at design time).**
- *Detection function:* spectral flux (Bello et al. 2005, "A Tutorial on Onset Detection in Music Signals"; Dixon 2006, "Onset Detection Revisited") is the standard general-purpose choice. **SuperFlux** (Böck and Widmer 2013) adds a max-filter over the frequency axis and a lag, which suppresses false onsets from vibrato and tonal material. An optional cheap **energy-envelope** mode suits pure drum loops.
- *Adaptive threshold:* a moving median or mean of the detection function plus an offset, so quiet and loud passages are judged relatively.
- *Peak picking:* local maxima above the threshold, with a minimum gap between picks.
- *Slice start:* placed slightly before the peak (pre-roll) and snapped to the nearest zero crossing, so the attack is kept and the cut does not click.

**Controls** (all shared knobs / segment controls):
- *Method:* Transients (spectral flux), Soft-tonal (SuperFlux), Even divisions of N (no analysis); manual editing is always available on top.
- *Sensitivity:* the threshold offset.
- *Min gap:* no slices closer than this.
- *Low-cut / High-cut:* the frequency band the detector looks at (for example ignore sub bass, or listen to hats only).
- *Pre-roll:* how far before the peak each slice starts.

**Behaviour.**
- Detection runs on a **worker thread** (never on the audio thread) when the loop loads or a control changes.
- The **detection curve is cached**: moving Sensitivity (and Min gap) only re-picks peaks from the cached curve, so markers update live while the knob moves. Changing Method, band or loop re-computes the curve.
- Manually moved or added markers are pinned and stay put on re-detect; auto markers are replaced.
- **Implementation plan:** write the detector in-house on `juce::dsp::FFT` (about 200 lines), no new dependency.

### Slices ★
- A pad grid, one pad per slice, numbered in time order. Clicking a pad auditions it and selects it; the selected pad's settings show in the slice player.
- **Drag to sequence:** a pad (or a slice region in the waveform) can be dragged onto the timeline, in addition to placing the selected slice by clicking an empty timeline cell.
- **No MIDI play** (Andy, 2026-10-30): there is no pad-to-note mapping and no live play. mu-Tilate is a self-driven sequencer; pads only audition on click.

### Slice player ★
A panel showing the current (selected) slice, built from shared knobs and the family colour coding, in this order:
- **Slice:** start and end (NudgeInput, decimals), fade in and out, gain, pan.
- **Pitch:** semitones and fine.
- **Filter:** type, cutoff, resonance, envelope amount.
- **Envelopes:** amp envelope (ADSR, amber `knobLevel` colour) and filter envelope (ADSR, filter colour), as in the other synth products.
- **Play:** reverse, loop, choke group, one-shot or gate.
- **Automatic declick:** every slice always gets a short fade in and out of a few milliseconds (a fixed minimum, set once in the engine, not shown as a control). The user's Fade in / Fade out values add on top of it, never below it. This avoids clicks at cut points, including slice ends cut by the next slice.

These are plain per-slice values (see the persistence note). Slice settings that are in the `ModTarget` table show the live modulation arc.

### Timeline ★
- **One row per layer** (Andy, 2026-10-30): each of the 8 layers has one mixer strip, FX sends and modulation, exactly one loop and one sample player. Its timeline is a single row of slice blocks played by that one player; there are no lanes. To layer sounds, use another layer. Each layer is **monophonic** (decided): one slice at a time, the next slice cuts the previous one through the short automatic fade.
- **Length:** adjustable, 1 to 64 bars (the Length control). The timeline is **drag-scrollable** (drag the background, or wheel) and zoomable when longer than the view, and keeps its playhead in view while following the transport.
- **Time ruler:** a ruler along the top shows bars and beats (and finer ticks as it is zoomed in); it scrolls and zooms with the row.
- **Placing slices:** (a) click an empty cell to place the selected slice, (b) drag a pad or a slice region from the sample window onto the row.
- **Snap:** drops snap to the **Grid** value (1/32 to 1/4, plus Off). **Holding Alt while dragging slides freely** (snap suspended); releasing Alt returns to the grid. The same rule applies to marker editing in the waveform and to block edges.
- **Block edges:** a placed block's left and right edges can be grabbed with a held mouse click: dragging the right edge extends or trims the end (extending loops or holds per the slice's Play mode; trimming is an early stop), dragging the left edge trims or extends the start of the played part. Edge handles show on hover (resize cursor) and follow the same snap and Alt rules.
- **Editing:** drag a block to move it; Alt is reserved for free sliding, so **Ctrl-drag copies**; Delete removes. Edits are staged and applied at a boundary like other products.
- **Helpers:** *Original order* lays the slices out as they were in the source, *Randomise* shuffles them, *Clear* empties the row.
- **Playhead:** follows the transport; the timeline is internally clocked like the other products (mu-link and host sync as elsewhere).

### Tempo matching and time-stretch
- **Modes (per layer):** Off, Re-pitch (varispeed, cheap, pitch moves with tempo), Stretch (pitch preserved).
- **Library for Stretch: Signalsmith Stretch (MIT, header-only), decided (Andy, 2026-10-30).** Rubber Band (GPL / commercial) and SoundTouch (LGPL, weaker on drums) are rejected. The only licence obligation is to include Signalsmith Stretch's copyright and MIT notice. The family already ships third-party notices in `THIRD_PARTY_NOTICES.txt` (copied into every release package and installer) and credits them in the About overlay; the Signalsmith reverb headers live in `ThirdParty/signalsmith-basics/`. Add Signalsmith Stretch to `THIRD_PARTY_NOTICES.txt` and the About credits, and keep its headers beside the others in `ThirdParty/`. Andy's own licence is unaffected. The stretch engine is product-independent and lives in `mu-core`.
- **Scaling:** whatever the mode, slice start / end positions are multiplied by the tempo ratio so slices remain on the grid. For Stretch each slice is stretched as its own short segment, never the whole loop in one go, and inside the segment the stretch is transient-weighted (next item).
- **Transient-weighted stretch (modelled on Bitwig).** The stretch is not applied evenly across a slice. At the slice's start (its transient) the audio plays essentially unstretched, so the attack stays sharp, and the stretch grows with distance from the transient and settles into an even plateau for the rest of the slice. Per slice this is a **time-warp map from output time to input position**:
  1. **Hold:** local rate 1.0 for the *Transient hold* period (a few ms to a few tens of ms).
  2. **Ramp:** a smooth transition whose shape is the *Curve* control (linear to eased, for example an ease-in-out) from rate 1.0 to the plateau rate.
  3. **Plateau:** a constant rate for the rest of the slice.
  The plateau rate is **solved** so the slice's total output length equals the tempo-matched length (output length = input length x tempo ratio): the hold and ramp consume known output time and input span, and the plateau takes the remainder. Edge cases: a slice shorter than the hold plays at rate 1.0 for the hold's worth and the remaining difference is absorbed by the plateau or, if there is no room, by a plain uniform rate; ratio 1.0 is a pass-through (all rates 1.0); **compression** (ratio below 1) works the same way, the plateau rate then being above 1.0; if the solve has no positive plateau (extreme compression of a short slice) the hold is shortened until it does.
- **Controls (per layer, shown under Stretch, in the Source area beside Tempo):** *Transient hold* (ms), *Curve* (shape), and *Strength* (0 = uniform stretch, 1 = full transient protection; it scales the hold and ramp so the user can fade the effect out; judged useful because slices with soft starts, such as pads and vocals, want it low). A small **curve preview** (local rate against slice time) sits beside the knobs, drawn with the shared curve-drawing look and `MuLookAndFeel` colours. All are shared `KnobWithLabel` controls and APVTS-backed layer parameters; Tempo mode, Hold, Curve and Strength are `ModTarget` candidates.
- **How the stretcher is driven (unproven, prototype required).** Signalsmith Stretch's process call takes an input sample count and an output sample count per call, so a varying local rate should be reachable by feeding it a varying input count per output block, following the warp map (about rate 1.0 in the hold, then the ramp, then the plateau). This is **not proven**: it must be confirmed in a prototype that the stretcher's internal analysis follows such a rate change without artefacts (smeared attacks, phase flutter at the hold-to-ramp join), and what block size works. If it does not, fall back to a uniform plateau stretch of the tail with the first few ms played unstretched and cross-faded in. The `dsp-controller` must review it before adoption.
- **Real time:** Stretch runs per voice on the audio thread only with preallocated state (no allocation in `processBlock`); the warp map is computed on the message thread when a slice or a control changes and read by the audio thread as a finished table.

## Layer vocabulary (decided by Andy, 2026-10-30; add to `design-naming.md` when scheduled)

Product name **mu-Tilate**. The layer type is **`Phrase`** (not "Slice", because a slice is a section of the sample), layer preset **`.muPhrase`**, full preset **`.muTilate`**. A Phrase layer is one loop, its slices, one sample player and one row of timeline blocks.

## Formats

VST3 + CLAP + Standalone, like mu-Clid, mu-Tant, mu-Toni and mu-On.

## mu-Tronic integration (automatic)

Andy: a Phrase layer must appear in mu-Tronic automatically. Per [docs/mu-tronic/design.md](mu-tronic/design.md) and [mu-tronic/CLAUDE.md](../mu-tronic/CLAUDE.md), mu-Tronic hosts other products' layers as static libraries compiled in and registered in its layer-type registry. "Automatic" therefore means the product is built so that adding it to mu-Tronic is a registry entry and nothing else:

1. **A `mu-tilate-layer` library** holding the engine, sequencer, persistence and the layer's own UI panels, built exactly like `mu-clid-layer`, `mu-tant-layer`, `mu-toni-layer` and `mu-on-layer`; it never includes `Plugin/` or `License/` (`check-layer-boundary.ps1`). The plugin and the mu-Tronic slot are two hosts of the same `Layer` class.
2. **One registry entry:** a `LayerTypeInfo` row for `Phrase` (group "mu-Tilate", display name "Phrase", parameter prefix, `.muPhrase` extension, `create`), registered in mu-Tronic's `LayerTypes.cpp`. A type is offered in the chooser only if registered in the build.
3. **A `LayerTypeId::Phrase` value** in `mu-core` (naming the family's types in `mu-core` is the existing ruling in the mu-Tronic design, section 2.5), saved by name.
4. **Shared preset format:** a `.muPhrase` saved by mu-Tilate loads into a mu-Tronic Phrase slot and back; type inference reads the extension.
5. **No per-product UI in the host:** the host supplies shell, sidebar, mixer, FX and modulators; the Phrase panels (source waveform, slice player, timeline) are the layer's own components, shown in the host's content band like the other layer types.
6. **Mirror rule** (mu-Tronic CLAUDE.md): any change to the shared timeline or waveform code must work in both hosts; never fix one only.

**Still needs an architecture ruling (`architecture-steward`) before scheduling** (placement of the shared pieces is already decided, see "Shared pieces"; the byte budget and saved-state policy below remain):
- Memory: **decided in principle (Andy, 2026-10-30):** 8 loops loaded in the plugin or standalone on its own, 16 in mu-Tronic (one per slot). The actual byte budget (maximum loop length or size, shared decode format) and the saved-state size policy for embedded samples are still to be set when this is scheduled.
- The layer's embedded sample inside a mu-Tronic scene / preset (size of the saved state).
- How the Phrase layer reads the host tempo under tempo matching in mu-Tronic's clock (including scenes), and the layer-owned worker thread for detection (one shared pool, not a thread per slot).
- The Stretch engine's per-voice state in the pre-allocated layer budget (design-plugin-family real-time rules).
- Whether mu-Tronic's step-row mirror rule needs extending to the Phrase timeline.

## Persistence note

Per-slice values do not fit one APVTS parameter each (the number of slices varies, up to a maximum). Keep the slice list, as the mu-On step rows do, as ValueTree children of the layer node: start and end in samples, plus the slice settings (including the filter and envelope values) and the pin flag for manual markers. The timeline blocks are saved the same way. Detection settings and the tempo-match mode are layer parameters in APVTS. Everything the user can change is saved with the preset; the loop itself is either embedded or stored by path, following the mu-Clid embedded-sample approach (with the path-safety rules from the audit fix).

## Real-time and engine notes

- Transient analysis (detection function, cache and peak picking, above) runs on a worker thread; never on the audio thread. The audio thread only reads the finished slice list.
- Slice playback reuses `SamplePlayer` and the shared retire-tail mechanism (so a slice that is cut off by the next one fades, no click), on top of the automatic few-millisecond declick fade.
- Boundary edits (moving a marker while playing) use the same hot-swap approach as other products: staged on the message thread, applied at a loop boundary.

## Shared pieces this suggests (for `mu-core`, not per-product)

**Decided** (Andy's standing rule: shared code always goes in `mu-core` unless there is a very good reason not to): these are built in `mu-core`, not inside mu-Tilate, together with the sample-library mechanics above, the product-independent parts of the transient and tempo detectors, and the Bounce dialog and render path: a **zoomable, scrollable waveform view with region tints and markers**, a **time ruler**, and a **drag-scroll / zoom viewport** helper (the timeline reuses it). A drag-and-drop payload for "slice onto timeline" stays product-side. A coded `ux-controller` ruling is needed on the exact tint tokens when this is scheduled.

## Sample library ★ (Andy, 2026-10-30: wanted, like mu-Clid)

**Own library, shared mechanism where practical (decided).** mu-Tilate has its **own sample library, separate from mu-Clid's**: its own folder setting (a mu-Tilate Settings entry, not mu-Clid's), its own content (long phrases and loops, where mu-Clid's is mainly hit samples; little overlap) and its own browser instance. Nothing is shared at the content or setting level. Only the engine-independent mechanics are shared code (decided below), so there is no third copy.

**How mu-Clid's library works today.** The user has a primary sample-library folder (a setting in the Settings overlay; unset means the OS Music folder) separate from the content folder, whose `Samples/` subfolder holds factory and preset-linked material. A `SampleBrowser` (a `juce::FileBrowserComponent` with a `FileBrowserListener`) scans and shows the folder; clicking a file previews it through the master output (`SamplePreview`) without assigning it, and choosing it loads it into the layer's voice engine and records its path (`SampleLibrary`, one path per layer). A preset stores the sample either by path (relative name resolved against `<contentDir>/Samples/`, or absolute with a fallback to the same file name in `Samples/`) or embedded (integer PCM WAV/AIFF re-encoded as FLAC, otherwise raw bytes, decoded to a temp file on load). A sample whose file is gone shows "missing, click to find". Path safety on embedded names: the name is reduced to a legal file name, rejected if empty, `.` or `..`, and the temp file must be a child of the temp root.

**What mu-Tilate mirrors:** the primary library folder setting and content-folder `Samples/` resolution; the folder browser and click-to-preview; per-layer path recording with the "missing, click to find" affordance; path or embedded storage, with the FLAC embedding and the same path-safety rules; the same Settings overlay wording.

**What is different for loops:**
- The list shows each loop's **length in bars and detected tempo**, not just a name, so loops can be judged at a glance (detection results cached per file so scrolling stays cheap).
- **Audition at project tempo:** preview plays the loop re-pitched or stretched to the current transport tempo (using the layer's Tempo mode), so it can be judged in context; a toggle plays it at its own tempo.
- **Drag onto a layer:** dragging a loop from the library onto a layer in the sidebar (or the Source waveform) loads it into that layer and **runs transient detection automatically**, so slices appear without further clicks. Double-click on a file loads it into the selected layer the same way. Dropping onto a layer that already has a loop asks nothing and replaces it (hot-swapped at a boundary, undo is the preset).
- Preview of a loop is also stoppable and shows its playhead in a mini waveform.
- Export (below) is the other direction: slices go to a folder the library can then show.

**Is the mechanism generic? No, not today.** The mu-Clid pieces are product code: `SampleLibrary` and `SampleSlotHost` in `mu_clid::` under `mu-clid/Source/Audio`, tied to mu-Clid's `VoiceEngine`; `SamplePreview` in the same folder; `SampleBrowser` in `mu-clid/Source/UI`; `embedded_sample` (`EmbeddedSample.h`) in `mu_clid::`; `resolveSamplePath` and `resolvePresetSample` in `RhythmPresetTree`. Per the one-framework direction, mu-Tilate must not copy them.

**Decided (Andy's standing rule, 2026-10-30: shared code always goes in `mu-core` unless there is a very good reason not to):** the engine-independent parts live in `mu-core`, and mu-Clid's existing product-specific copies are lifted into `mu-core` as part of that work, with mu-Clid refactored to use them (no duplicate left behind). mu-Clid and mu-Tilate (and mu-Tronic's Rhythm and Phrase slots) share one implementation of the mechanism, each instantiated with its own folder setting and content (the library folder setting is a parameter of the shared service, not a shared value): a `SampleSource`/library-folder service (primary folder setting, content `Samples/` resolution, missing-file lookup), `SamplePreview`, the embedded-sample encode/decode and its path-safety helper, and a generic `SampleBrowser` component with an optional extra-info column hook (the loop length and tempo columns). mu-Clid then keeps only its binding to `VoiceEngine` (and mu-Tilate to the Phrase player) behind a small host interface like `SampleSlotHost`. Layout and colours of the shared browser come from `MuLookAndFeel`; the `ux-controller` rules on the extra columns when it is scheduled.

## Export ★ (Andy, 2026-10-30: wanted, in scope)

- **Bounce to WAV:** the Standalone's bounce dialog from the Bounce idea in [design-future.md](design-future.md) (a shared `mu-core` dialog, standalone only), rendering the arrangement of all layers for N bars plus an FX tail. Depends on the generic render path being lifted to `mu-core`.
- **Export slices to a folder:** writes each slice of the selected layer (or every layer) as its own WAV into **one folder per loop** (decided), named `<loop>_<nn>.wav`, with the layer's fade and pitch applied only if the user ticks "Render settings". The folder defaults to the sample-library folder so the result shows up in the browser.

## Decisions (Andy, 2026-10-30)

Name **mu-Tilate**; time-stretch is a per-layer option (Off / Re-pitch / Stretch); layer type `Phrase` with `.muPhrase` and `.muTilate`; 8 layers with one loop and one sample player each (single-row timeline, no lanes); each layer is **monophonic**: one slice at a time, the next slice cuts the previous one through the short automatic declick fade; no MIDI play; formats VST3 + CLAP + Standalone; automatic mu-Tronic integration; a sample library like mu-Clid's; Bounce to WAV and Export slices are in scope.

## Factory loops and licensing (Andy, 2026-10-30: decided)

mu-Tilate ships a small set of free loops in the content folder's `Samples/` so that factory presets can be shipped. Rules: only **CC0 / public-domain** material or material under a licence that **explicitly allows redistribution inside an app**; **no CC-BY-NC and no share-alike-restricted** (CC-BY-SA, GPL-style) material. Each loop has a **licence and credit manifest next to it** in `Samples/` (source, author, licence, URL, date checked). The actual pack choice is made later.

## Tempo analysis ★ (research, 2026-10-30; decided: metadata first, then analysis)

Used for library rows, for loading a loop into a layer, and for the Source `Set` control.

**Order of trust.**
1. **File metadata:** WAV `acid` chunk (beats, tempo) and `smpl` chunk, and AIFF / Apple Loops chunks. If present and plausible (inside the range prior), use it with full confidence.
2. **Analysis** (below).
3. **Filename hint** such as `120bpm`, lowest priority, used only when analysis confidence is low.

**Why loops are a special case.** A loop is normally an exact whole number of beats (usually bars), so the tempo is `60 x beats / duration_seconds`. The problem reduces to finding the beat count, not tracking a drifting tempo.

**Method.**
1. Trim leading and trailing silence first (length must be the musical length).
2. For each candidate beat count (for example 1 to 64), compute the implied BPM and **discard anything outside about 60 to 200 BPM**.
3. Score each remaining candidate by how well the **onset detection curve** (the same cached spectral-flux curve as transient detection, so nothing is analysed twice) fits a beat grid of that tempo, using a comb-filter / autocorrelation / tempogram score. References: Scheirer 1998 (comb filters), Ellis 2007 (dynamic-programming beat tracking), Davies and Plumbley (beat tracking and tempo), and librosa's `beat_track` as a readable reference implementation.
4. Pick the best score. The **range prior plus the loop length** resolves half-tempo and double-tempo errors (both candidates are whole beat counts; the range and the grid fit choose).
5. Find the **downbeat offset** by correlating the curve with the beat grid, so beat 1 lines up with the first strong hit.
6. Keep a **confidence value** (best score against the runner-up) and show it next to the tempo. Low confidence is shown plainly, never hidden.

**Override.** The existing `Set` control in Source lets the user correct tempo and length; the override is **saved with the layer** and beats any later analysis.

**Implementation.** Written in-house on the existing detection curve, about 150 lines, no new dependency. aubio is GPL and Essentia is AGPL, which do not suit a closed product. Runs on the same worker thread as detection, never on the audio thread; results are cached per file for the library list.

## Decisions (Andy, 2026-10-30), addendum

Factory loops are included under the licensing rule above; tempo comes from metadata first, then analysis; Export slices writes **one folder per loop**; Bounce to WAV is **Standalone only for now**, and because the dialog is the shared one in `mu-core` (see the Bounce idea), the option to offer it in the plugin formats later stays open at no extra cost.

## Open questions for Andy

None remaining.
