# mu-Tilate: user interface

Status: design only, nothing built (seeded 2026-10-30 from the first wireframe). Look and feel is owned by the `ux-controller`; this document records the wireframe and the behaviour of each panel. Family rules: [design-ui-family.md](../design-ui-family.md). Product overview and decisions: [design.md](design.md).

Visual polish (bundled typeface, high-DPI bitmaps, motion tokens, cached layers and shadows, SVG assets) applies to this product as to the rest of the family: [design-ui-family.md, Visual polish](../design-ui-family.md#visual-polish-approved-2026-10-30); slice selection animates per P3.

Sizes come from `MuLookAndFeel`, depth from `MuTheme::Lighting`, controls from the shared library (`KnobWithLabel`, `SegmentControl`, `DropdownSelect`, `NudgeInput`, `SlideSwitch`). The parts marked ★ are the product's own content band; everything else is the standard shell.

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

## Panels

### Source ★
- **Load loop:** drag-and-drop or button; WAV first (AIFF, FLAC later, via the existing decoders).
- **Length and tempo:** the loop's length in bars and the detected tempo are shown; `Set` lets the user correct them. The timeline's time base is beats.
- **Tempo matching (per layer):** *Off* (slices play at their recorded speed and are only re-timed by the sequencer), *Re-pitch* (varispeed: speed and pitch follow the ratio of host tempo to loop tempo) or *Stretch* (time-stretch with pitch preserved). Slice start and end are scaled by the tempo ratio so slices stay on the grid in every mode. See [design-engine.md](design-engine.md), "Tempo matching and time-stretch".
- **Waveform with highlighted slices:** the shared waveform drawing. The regions between detected transients are tinted in alternating strengths (two tints, so neighbours are always distinguishable) and the selected slice is emphasised (brighter tint plus the layer-colour outline). Tints come from `MuLookAndFeel` colour tokens at `MuTheme::Lighting` strengths, never literal alphas. Markers show in the layer colour.
- **Scroll and zoom:** a long loop gets a scrollable and zoomable waveform. Mouse wheel scrolls, Ctrl+wheel (and the Zoom slider) zooms around the pointer, and an overview strip with a draggable window sits under the waveform. Zoom and scroll position are view state, not saved with the preset. The same pan / zoom behaviour is reused by the timeline, so it is built once as a shared component (see shared pieces below).
- **Marker editing:** markers are grabbable, snap to the grid when Snap is on (hold Alt for free movement, as in the timeline), and a manually moved marker is **pinned** (shows a small pin) and survives *Re-detect*.


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
