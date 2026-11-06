# mu-Tilate — CLAUDE.md

Product-specific guidance for working in `mu-tilate/`. Read alongside the family-wide [/CLAUDE.md](../CLAUDE.md).

## Project overview

**μ-Tilate** is a JUCE/C++ **loop slicer and sample-loop phrase player** by Transwarp Development Project. Load a loop (usually drums, but any audio); the product finds the transients, cuts the loop into slices, and a sequencer places those slices as blocks on a **timeline** (not a 16-step grid), so the loop can be rearranged, repeated, reversed, thinned out, re-pitched, or matched to the host tempo (Off / Re-pitch / time-Stretch). Each of the **8 layers** is a **`Phrase`**: exactly one loop, its slices, **one monophonic sample player** (one slice at a time; the next slice cuts the previous through a short automatic declick fade), **one timeline row** and one mixer strip with FX sends and modulation. To layer sounds, use another layer. No MIDI play: pads only audition on click. Formats: **VST3 + CLAP + Standalone**.

The editor will inherit `mu-core/UI/EditorShellBase.h` and use the shared `LayerSidebar` + `LayerPanel` + `LayerEditController` + `MixerOverlay`, so shell, mixer, FX rack and modulators come from `mu-core`; the product supplies the slice engine, the timeline sequencer and the content band (`PhraseContent`: source waveform, slices, slice player, timeline).

## Status

**In build, staged (Andy, 2026-10-31: "start it now"); the plan and stage status are in [docs/mu-tilate/design-buildplan.md](../docs/mu-tilate/design-buildplan.md). Built: the `mu-tilate-layer` library, the `mu-tilate` plugin target (VST3 + CLAP + Standalone), `mu-tilate-tests`, the slice player, detection, Phrase and timeline engine, persistence, and the editor (Sequencer / Layer / Slice views). Layer controls are absolute, slice controls are offsets from them. mu-Tronic hosting is wired (headless tests pass).** Ask Andy before starting any of those; open work is in [backlog.md](../backlog.md).

## Decisions (Andy, 2026-10-30)

- Name mu-Tilate; folder `mu-tilate/`; namespace `mu_tilate::`. Layer type **`Phrase`**, layer preset **`.muPhrase`**, full preset **`.muTilate`**. Names in [docs/design-naming.md](../docs/design-naming.md) ("mu-Tilate (planned)").
- 8 layers, each one loop + one monophonic sample player + one timeline row + one mixer strip.
- To be hosted in **mu-Tronic** (not yet wired) through a `mu-tilate-layer` static library (engine, sequencer, persistence, the layer's own UI panels; never `Plugin/` or `License/`) plus one `LayerTypeInfo` registry row and `LayerTypeId::Phrase` in `mu-core`. Memory: 8 loops in the product, 16 in mu-Tronic; byte budget and the other architecture rulings are in [docs/mu-tilate/design.md](../docs/mu-tilate/design.md) (Rulings, architecture-steward, 2026-10-30).
- **Shared code always goes in `mu-core`** unless there is a very good reason not to. Built in `mu-core`, with mu-Clid refactored onto them (no duplicate left behind): the sample-library mechanism (folder service, `SamplePreview`, embedded-sample encode/decode and path safety, a generic `SampleBrowser` with an extra-info column hook), the zoomable/scrollable waveform view with region tints and markers, the time ruler, the drag-scroll/zoom viewport, the product-independent parts of the transient and tempo detectors, the Bounce dialog and render path, and the stretch engine. mu-Tilate keeps only its bindings (the Phrase player, the "slice onto timeline" drag payload).
- Time-stretch uses **Signalsmith Stretch** (MIT, header-only) in `ThirdParty/` beside `signalsmith-basics/`, with a notice in `THIRD_PARTY_NOTICES.txt` and the About credits. Whether a transient-weighted varying rate works with it is unproven: prototype, then `dsp-controller` review before adoption.
- Own sample library (own folder setting, own browser instance, loop length and tempo columns); free **CC0 / explicitly redistributable** factory loops with a licence and credit manifest per loop. Export slices (one folder per loop) and Bounce to WAV (Standalone only for now) are in scope.
- Pre-release: uses [version-pre.txt](../version-pre.txt), like mu-Toni and mu-On.

## Conventions in force

- All product symbols under `mu_tilate::`; `mu-core` never includes a mu-Tilate header (`tests/scripts/check-core-boundary.ps1`). Library files obey `tests/scripts/check-layer-boundary.ps1`.
- `Source/` has exactly the family eight: `{Audio, License, Modulation, Persistence, Plugin, Sequencer, Tests, UI}`, empty ones kept with `.gitkeep`. Placement: slice player and stretch glue in `Audio/`, `Phrase` and the timeline in `Sequencer/`, `PhraseParams.{h,cpp}` and `LayerParamIds.h` in `Persistence/`, `PhraseContent` and `PhraseHost.h` in `UI/`, `MuTilateModDest.h` in `Modulation/`.
- Parameters are APVTS (detection settings, tempo mode, stretch controls, slice-player globals); the **slice list and timeline blocks are ValueTree children of the layer node**, not APVTS parameters, as mu-On's step rows are. Layer parameter prefix `p{N}_`. The loop is embedded (FLAC) or stored by path, with the mu-Clid path-safety rules.
- Modulation targets are one `mu_mod::ModTarget` table; depth is a % of the knob's range. Channels are self-contained; `ModulationMatrix` is the single reader.
- Transient and tempo analysis run on a worker thread, never the audio thread, results cached; the audio thread only reads finished slice lists and warp tables. The audio thread never allocates (stretch state preallocated in `prepare`).
- Marker and block edits are staged on the message thread and applied at a loop boundary (shared hot-swap). Shift while dragging means free movement (no grid snap); Ctrl-drag copies; dragging a block edge trims it, Alt-drag on an edge retimes (stretches/pitches) it.
- Mirror rule with mu-Tronic (as for mu-On's step rows): any change to the timeline or waveform code must work in both hosts; put it in shared layer code and never fix one host only.
- Content dir `Documents/TDP/muTilate` with `Presets/`, `Phrases/` and `Samples/`.

## Design documents

| Doc | Covers |
|---|---|
| [docs/mu-tilate/design.md](../docs/mu-tilate/design.md) | Overview, layer vocabulary, mu-Tronic integration and open rulings, persistence, shared pieces, decisions |
| [docs/mu-tilate/design-ui.md](../docs/mu-tilate/design-ui.md) | Wireframe and panel behaviour (source, slices, slice player, timeline) |
| [docs/mu-tilate/design-engine.md](../docs/mu-tilate/design-engine.md) | Transient detection, tempo matching, transient-weighted stretch, tempo analysis, real-time notes |
| [docs/mu-tilate/design-library.md](../docs/mu-tilate/design-library.md) | Sample library, export, factory loops and licensing |

## Still to wire (checklist)

Done: the CMake target, product source and tests; licensing (`Source/License/LicenseKey.h`, key in OneDrive secrets); Signalsmith Stretch notice and About credit; the product-listing scripts, `ci.yml`, `release.yml` (fixed-name `mu-Tilate-Windows.zip` with factory loops); six original CC0 factory loops in `factory/` (regenerate with `scripts/make-tilate-factory-loops.py`). There is no installer, as for mu-Toni and mu-On (zip only). Remaining:

1. ~~mu-Tronic hosting~~ done (Phrase registered, prefix `p`, `PhraseSlotHost`); built and tested headless, not yet seen or heard in the mu-Tronic window.
2. At release time: `scripts/write-latest-json.ps1 -Products mu-tilate` (supported), the website and release-notes page via the `web-designer`.
3. Manual via the `user-documentation-author` (`docs/mu-tilate/create_manual.ps1` and `images/`).
