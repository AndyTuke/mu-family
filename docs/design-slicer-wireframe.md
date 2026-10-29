# Loop slicer: wireframe (working title, no product name yet)

Status: **idea and first wireframe only** (Andy, 2026-10-29). Nothing is scheduled or built. It is listed under Unscheduled ideas in [design-future.md](design-future.md); ask before implementing.

## The idea

Load a loop (usually a drum loop, but any audio). The product finds the transients and cuts the loop into slices. A sequencer then places those slices on a timeline, so the loop can be rearranged, repeated, reversed, thinned out or re-pitched.

## How it fits the family

Follows [design-plugin-family.md](design-plugin-family.md) and [design-ui-family.md](design-ui-family.md): a new product links `mu-core` and supplies only its own engine, sequencer and content band. The wireframe below uses the standard shell and shared components; the product-specific parts are marked ★.

- **Shell (shared, from `EditorShellBase`):** TransportBar, StatusBar, About / Save / Preset overlays, `MuLookAndFeel`, metal style.
- **Layers:** one layer is one loop with its slices and its own placement lane set. Sidebar, add / delete / reorder, mixer strip and FX rack come from `mu-core` like the other products.
- **Engine ★:** a slice player (a `SamplePlayer` per voice, each slice with its own start, end, gain, pitch, reverse, fade).
- **Sequencer ★:** a timeline (not a 16-step grid): slices placed as blocks on a beat grid.
- **Transient detection ★:** an analysis step run when the loop is loaded or the sensitivity changes, off the audio thread; the result is a list of slice points stored in the layer's state.

## Wireframe: main view

```
┌──────────────────────────────────────────────────────────────────────────────────────────────┐
│ TRANSPORT  [▶] [■]  BPM 124.0  Loop 2 bars  Sync [Host|Int]  ·····  Preset ▾  Save  Mixer  ⚙ │  shared
├────────────┬─────────────────────────────────────────────────────────────────────────────────┤
│ LAYERS     │  SOURCE ★                                                                        │
│            │  [Load loop…]  amen_break.wav   2 bars · 4 beats/bar · detected 124.0 BPM  [Set] │
│ ▣ Break    │  ┌────────────────────────────────────────────────────────────────────────────┐  │
│   (lamp)   │  │ ▏█▌ ▏  ▌█▏   ▏█▌ ▏   ▌▏█  ▏  ▌█▏   ▏█▌  ▏  ▌▏█   ▏  ▌█▏   ▏█▌ ▏  ▌█   │  │
│ ▢ Perc     │  │ ┃1    ┃2     ┃3     ┃4    ┃5     ┃6    ┃7     ┃8     ┃9    ┃10    ┃11 ┃12 │  │
│ ▢ Vocal    │  │ waveform, with a marker ┃ at every slice point (drag to move, double-click    │  │
│            │  │ to add, right-click to delete); the beat grid is a faint line behind it     │  │
│ [ + ]      │  └────────────────────────────────────────────────────────────────────────────┘  │
│            │  Sensitivity (•)   Min gap (•)   Snap to grid [Off|1/16|1/8]   [Re-detect] [Even] │
│            │  Slices: 12     Mode [Transients | Even divisions | Manual]                      │
│            ├─────────────────────────────────────────────────────────────────────────────────┤
│            │  SLICES ★                                    │ SLICE 5 (selected) ★              │
│            │  ┌───┬───┬───┬───┬───┬───┬───┬───┐           │ Start ▏ 0.412 s  End ▏ 0.538 s    │
│            │  │ 1 │ 2 │ 3 │ 4 │ 5 │ 6 │ 7 │ 8 │  pads,    │ Gain (•)  Pitch (•)  Pan (•)      │
│            │  ├───┼───┼───┼───┼───┼───┼───┼───┤  click to │ Fade in (•) Fade out (•)          │
│            │  │ 9 │10 │11 │12 │   │   │   │   │  audition │ [Reverse] [Loop] [Choke group ▾]  │
│            │  └───┴───┴───┴───┴───┴───┴───┴───┘           │ Play: [One-shot | Gate]           │
│            ├─────────────────────────────────────────────────────────────────────────────────┤
│            │  TIMELINE ★                         Length [ 4 ] bars   Grid [1/16 ▾]   Zoom ─●─  │
│            │       1       2       3       4       5       6       7       8                  │
│            │  ┌────────────────────────────────────────────────────────────────────────────┐  │
│            │  │ Lane A │▐1█▌│▐2█▌│     │▐3█▌│▐1█▌│▐2█▌│▐4█▌│  │▐5█▌│▐6▌ │▐7█▌│▐8█▌│          │  │
│            │  │ Lane B │      │▐5▌│          │  ▐9▌│          │▐9▌│            │▐12▌│        │  │
│            │  │ Lane C │                                                                   │  │
│            │  └────────────────────────────────────────────────────────────────────────────┘  │
│            │  [Original order]  [Randomise]  [Clear]            playhead ▼ follows transport  │
├────────────┴─────────────────────────────────────────────────────────────────────────────────┤
│ STATUS  Slice 5: 0.126 s · kick · snaps to 1/16                                       v1.1.NNN │  shared
└──────────────────────────────────────────────────────────────────────────────────────────────┘
```

## Parts and behaviour

### Source ★
- **Load loop:** drag-and-drop or button; WAV first (AIFF, FLAC later, via the existing decoders).
- **Length and tempo:** the loop's length in bars and the detected tempo are shown; `Set` lets the user correct them. The timeline's time base is beats, so the loop can follow the host tempo (the slices are re-timed by the sequencer, not stretched, unless Stretch is turned on later; see open questions).
- **Waveform:** the shared waveform drawing, with slice markers on top. Markers are grabbable, snap to the grid when Snap is on, and are shown in the layer colour.
- **Detection controls:** *Sensitivity* (how strong a transient must be), *Min gap* (no slices closer than this), *Mode* (Transients, Even divisions of N, or Manual with no analysis). *Re-detect* re-runs the analysis and keeps manually moved markers locked (a locked marker shows a small pin).

### Slices ★
- A pad grid, one pad per slice, numbered in time order. Clicking a pad auditions it; the selected pad's settings show on the right.
- **Per-slice settings:** start and end, gain, pitch, pan, fade in and out, reverse, loop, choke group, one-shot or gate play. These are plain APVTS-backed values per slice (see the persistence note below).
- MIDI: pads map to consecutive notes from a base note (default C1), so the slices can also be played live.

### Timeline ★
- **Lanes:** a few lanes (3 to start) so slices can overlap (a hat slice over a kick slice). All lanes play together into the layer's voice pool.
- **Blocks:** each block is a slice placed at a beat position. A block's length is the slice's length (it can be shortened by dragging its right edge, which acts as an early stop).
- **Editing:** click an empty cell to place the selected slice; drag to move; drag the edge to trim; Alt-drag to copy; Delete to remove. Grid sets the snap (1/32 to 1/4, plus Off).
- **Length:** 1 to 16 bars (the timeline wraps like a mu-On row; paging, if needed, uses the shared pager).
- **Helpers:** *Original order* lays the slices out as they were in the source, *Randomise* shuffles them, *Clear* empties the lane.
- **Playhead:** follows the transport; the timeline is internally clocked like the other products (mu-link and host sync as elsewhere).

## Proposed layer vocabulary (to confirm with `design-naming.md`)

Layer type name and file extensions are not chosen. Following the existing pattern (per-layer preset = camelCase noun, full preset = product name camelCase), something like `Slicer` layers with a `.muSlices` layer preset. **The product name and these extensions are Andy's decision.**

## Persistence note

Per-slice values do not fit one APVTS parameter each (the number of slices varies, up to a maximum). Keep the slice list, as the mu-On step rows do, as ValueTree children of the layer node: start and end in samples, plus the slice settings. The timeline blocks are saved the same way. Everything the user can change is saved with the preset; the loop itself is either embedded or stored by path, following the mu-Clid embedded-sample approach (with the path-safety rules from the audit fix).

## Real-time and engine notes

- Transient analysis (onset detection, e.g. spectral flux or an energy-envelope detector with a peak-picking threshold) runs on the message or a worker thread when the loop loads or Sensitivity changes; never on the audio thread. The audio thread only reads the finished slice list.
- Slice playback reuses `SamplePlayer` and the shared retire-tail mechanism (so a slice that is cut off by the next one fades, no click).
- Boundary edits (moving a marker while playing) use the same hot-swap approach as other products: staged on the message thread, applied at a loop boundary.

## Open questions for Andy

1. **Name** and the layer / file extension.
2. **Time-stretch:** should the product follow a different host tempo by re-timing slices only (classic slice-and-rearrange), or also stretch each slice to keep its pitch? Re-timing only is the simpler first version.
3. **Lanes:** how many lanes to start with, and whether each lane gets its own output / mixer strip.
4. **One loop per layer, or several?** The wireframe has one loop per layer; a layer per loop keeps it consistent with the family.
5. **Live play:** pads / MIDI note mapping in the first version, or timeline only?
6. **Export:** a "Bounce to WAV" and "Export slices to a folder" fit the existing Bounce idea in design-future.md.
7. **Plugin formats:** VST3 + CLAP + Standalone like the others, or Standalone first?
