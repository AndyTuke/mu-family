# mu-Tilate: product design

Status: **design only, nothing built or scheduled** (idea and first wireframe by Andy, 2026-10-29; named mu-Tilate and requirements added 2026-10-30; scaffold and doc set created 2026-10-30). It is listed under Unscheduled ideas in [../design-future.md](../design-future.md); ask before implementing. Documents: [design-ui.md](design-ui.md) (wireframe and panels), [design-engine.md](design-engine.md) (transient detection, tempo matching and time-stretch, tempo analysis, real-time notes), [design-library.md](design-library.md) (sample library, export, factory loops). Product rules: [mu-tilate/CLAUDE.md](../../mu-tilate/CLAUDE.md).

## The idea

Load a loop (usually a drum loop, but any audio). The product finds the transients and cuts the loop into slices. A sequencer then places those slices on a timeline, so the loop can be rearranged, repeated, reversed, thinned out, re-pitched or matched to a new tempo.

## How it fits the family

Follows [design-plugin-family.md](../design-plugin-family.md) and [design-ui-family.md](../design-ui-family.md): a new product links `mu-core` and supplies only its own engine, sequencer and content band. The wireframe in [design-ui.md](design-ui.md) uses the standard shell and shared components; the product-specific parts are marked ★. No per-product layout constants: sizes come from `MuLookAndFeel`, depth from `MuTheme::Lighting`, controls from the shared library (`KnobWithLabel`, `SegmentControl`, `DropdownSelect`, `NudgeInput`, `SlideSwitch`).

- **Shell (shared, from `EditorShellBase`):** TransportBar, StatusBar, About / Save / Preset overlays, `MuLookAndFeel`, metal style.
- **Mixer, FX and modulation (shared):** the same mu-family mixer, FX rack and modulation section as every other product, all from `mu-core`. The product adds only its modulation targets, as one `mu_mod::ModTarget` table (filter cutoff, pitch, gain, pan, tempo-match amount and so on; rules in [design-plugin-family.md](../design-plugin-family.md#modulation-targets--family-standard)). Every knob that is a row of that table shows the live modulation arc.
- **Layers:** 8 layers as in every product; one layer is one loop with its slices, one sample player and one row of placed slices. Sidebar, add / delete / reorder, mixer strip and FX rack come from `mu-core` like the other products.
- **Engine ★:** a slice player (a `SamplePlayer` per voice, each slice with its own start, end, gain, pitch, reverse, fade, filter and envelopes), plus an optional tempo-match stage.
- **Sequencer ★:** a timeline (not a 16-step grid): slices placed as blocks on a beat grid.
- **Transient detection ★:** an analysis step run on a worker thread when the loop is loaded or a detection control changes; the result is a list of slice points stored in the layer's state.

## Layer vocabulary (decided by Andy, 2026-10-30; recorded in [design-naming.md](../design-naming.md))

Product name **mu-Tilate**. The layer type is **`Phrase`** (not "Slice", because a slice is a section of the sample), layer preset **`.muPhrase`**, full preset **`.muTilate`**. A Phrase layer is one loop, its slices, one sample player and one row of timeline blocks.

## Formats

VST3 + CLAP + Standalone, like mu-Clid, mu-Tant, mu-Toni and mu-On.

## mu-Tronic integration (automatic)

Andy: a Phrase layer must appear in mu-Tronic automatically. Per [docs/mu-tronic/design.md](../mu-tronic/design.md) and [mu-tronic/CLAUDE.md](../../mu-tronic/CLAUDE.md), mu-Tronic hosts other products' layers as static libraries compiled in and registered in its layer-type registry. "Automatic" therefore means the product is built so that adding it to mu-Tronic is a registry entry and nothing else:

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

## Shared pieces this suggests (for `mu-core`, not per-product)

**Decided** (Andy's standing rule: shared code always goes in `mu-core` unless there is a very good reason not to): these are built in `mu-core`, not inside mu-Tilate, together with the sample-library mechanics in [design-library.md](design-library.md), the product-independent parts of the transient and tempo detectors, and the Bounce dialog and render path: a **zoomable, scrollable waveform view with region tints and markers**, a **time ruler**, and a **drag-scroll / zoom viewport** helper (the timeline reuses it). A drag-and-drop payload for "slice onto timeline" stays product-side. A coded `ux-controller` ruling is needed on the exact tint tokens when this is scheduled.

## Decisions (Andy, 2026-10-30)

Name **mu-Tilate**; time-stretch is a per-layer option (Off / Re-pitch / Stretch); layer type `Phrase` with `.muPhrase` and `.muTilate`; 8 layers with one loop and one sample player each (single-row timeline, no lanes); each layer is **monophonic**: one slice at a time, the next slice cuts the previous one through the short automatic declick fade; no MIDI play; formats VST3 + CLAP + Standalone; automatic mu-Tronic integration; a sample library like mu-Clid's; Bounce to WAV and Export slices are in scope.

## Decisions (Andy, 2026-10-30), addendum

Factory loops are included under the factory-loop licensing rule in [design-library.md](design-library.md); tempo comes from metadata first, then analysis; Export slices writes **one folder per loop**; Bounce to WAV is **Standalone only for now**, and because the dialog is the shared one in `mu-core` (see the Bounce idea), the option to offer it in the plugin formats later stays open at no extra cost.

## Open questions for Andy

None remaining.
