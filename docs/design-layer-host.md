# Layer host: feasibility assessment (2026-11-03)

Status: **analysis only, nothing decided, nothing built.** Written from a read-only code survey on the laptop. Items marked *inferred* are judgement, not read in code. Not yet reviewed by the `architecture-steward`.

## The idea

Each product is a sequencer + sound engine (a **Layer type**) plugged into the shared mixer, FX rack and modulation. The proposal: one generic **layer host** that loads a layer type through the registry, so every product (mu-Clid, mu-Tant, mu-Toni, mu-On, mu-Tilate) becomes a thin wrapper target (name, licence, layer type, layer count, branding) around the same host code, and mu-Tronic is the same host with 16 slots and every type registered. Modulation is part of the snap-in. This is the concrete form of the one-framework / one-instance direction in [design-future.md](design-future.md) and the end point of backlog #1699 and #1701 ([refactor-plan-2026-10.md](refactor-plan-2026-10.md)).

## Verdict

Moderate work, not a rewrite. **mu-Tronic is already the generic host and the `*-layer` libraries are already the snap-ins.** The job is to make mu-Tronic's host parametric and move the standalone products onto it. Estimate: 4 to 6 weeks focused for four products; the mu-Toni spike about 1 week.

## What already exists (verified in code)

- `Layer` ([mu-core/Sequencer/Layer.h](../mu-core/Sequencer/Layer.h)) owns `voiceParams`, `controlSequences`, `modulationMatrix`, the clip bank and persistence hooks `writeExtras` / `applyExtras`. `Layer::render` takes a plain `RenderContext` (transport, MIDI view and sink, peer taps), so a layer needs no host callback while rendering.
- `LayerTypeInfo` registry ([mu-core/.../LayerTypeInfo.h](../mu-core/Sequencer/LayerTypeInfo.h)): id, `addLayerParams`, `create`, preset extension / folder / tag, UI factories (`createContent`, `createMiniVisual`), per-type services, licence-free `typeTag`.
- **Modulation travels with the layer:** matrix and control sequences are Layer members, saved by the base `writeExtras`; depth comes from the type's `ModTarget` table; the UI side comes from the content's `getModDestProvider()`. The "modulation as part of the snap-in" requirement already holds. Residual coupling: the APVTS param-sync path and the hold-last-resolved-on-lock-miss code, copied three times (#1701 item 1).
- All six processors already render through the shared `renderAndMix`. Rule 5 of `tests/scripts/check-layer-boundary.ps1` bans per-layer render glue in registered products.

## Why products are not thin wrappers yet

| Area | Product mode today | mu-Tronic mode |
|---|---|---|
| Audio-thread layer access | TryLock + `layersMoved` hooks | `LayerAccess::Published` (publish views, retire bin) |
| Layer count | runtime list, add/remove, `keepSpareLayers` | fixed 16 slots with an Empty layer |
| Layer construction | product's `createLayer` constructs its type directly | through the registry |
| Editor | sidebar, `LayerPanel`, content and mixer overlay wired by hand | `EmptySlotChooser`, content built via `createContent` |
| Capacity | `MU_MAX_LAYERS` compile definition per target (8) | 16 |
| Host interfaces | each type's `ArpHost` / `RhythmHost` / `PatternHost` / `TrackHost` / `PhraseHost` implemented by the product's `PluginProcessor` | implemented again by a registry adapter over `ProcessorBase` in `mu-tronic/Source/Persistence/` |

Still supplied by `ProcessorBase`, not the layer: APVTS, mixer strips (`ch{N}_`) and `MixerEngine`, global FX rack, state composition, licensing, transport resolution, MIDI clock / program change / controller router, hot-swap staging, the editor shell.

## Risks and blockers

1. **Parameter ids (biggest).** Layer rows are `<letter><slot>_`; products use the same prefixes as mu-Tronic, so a product is a subset of Tronic's layout. mu-Toni on the host with 8 slots keeps its ids. A true N=1 host would drop 7 of 8 parameter sets and break automation and MIDI maps. **Keep capacity per product as a compile-time property (8), not 1.** mu-On's track types share `t{N}_`; the host declares each prefix once, so that works.
2. **Audio-thread scheme change.** TryLock to Published is a real-time risk: `dsp-controller` review required for that stage.
3. **State and presets.** Layer presets already go through the registry and mu-Tronic imports product presets (`ProductImport.cpp`). Full-state root tags differ (`MuToniState` vs `kStateType`); a per-product tag and a list-to-slot (layer count) migration are needed. *Inferred.*
4. **Plugin identity.** Each product has its own `PLUGIN_CODE`, formats, licence key files, product slug and checkout URL, currently inside the processors. These become per-target defines / a traits struct. Only mu-Tronic is Standalone-only.
5. **DAW bus layout.** mu-Clid's multi-bus counts are fixed at plugin scan time.
6. **Boundary rules.** `check-layer-boundary.ps1` rules 1 to 10 apply; rule 10 forbids mu-Tronic naming a layer type's headers. A generic host must keep that, so the per-type adapters move next to their layer libraries.
7. **Duplication and size.** `ProcessorBase` is about 1,400 lines; mu-Clid's processor has 60+ methods (#1699, #1701).
8. **Tests.** Goldens and frozen baselines exist per product; the refactor plan treats a golden diff as a stop signal. mu-Tronic has a 16-layer variant of the core tests.
9. **UI.** `LayerContent` and the registry factories exist, but product editors bind concrete content directly and do not use them yet.

## Per-product readiness (easiest first)

Plugin glue line counts are approximate (`Plugin/*.cpp+h`).

1. **mu-Toni** (~800): small. One type, no extra buses, little state. Best spike.
2. **mu-Tilate** (~900): small. Scaffold, no legacy state. Needs a Phrase `LayerTypeInfo`, which does not exist yet (`LayerTypeId::Phrase` is reserved, not in mu-Tronic's registry).
3. **mu-On** (~1,200): small to medium (shared `t` prefix, bass-kick sidechain coupling).
4. **mu-Tant** (~1,800): medium (`SwapBoundary.h`, voice bank, own bank-index helper that duplicates #1701 item 3).
5. **mu-Clid** (~2,100): large. Direct-out buses, FX-returns bus, sample library, MIDI-out mode, own beat resolve each block, `HotSwapStager`.
6. **mu-Clid MIDI** (~860): not a candidate (no audio engine; shares only the Rhythm preset format).

mu-Tronic as the N=16 variant of the same host is nearly free.

## Staged plan (effort is an estimate)

- **Stage 0, spike on mu-Toni (3 to 5 days, branch).** `HostProcessor : ProcessorBase` with the Tronic body (`setLayerAccess(Published)`, `registerLayerTypes`, parameter layout from the registry), driven by compile-time N and a type list. Check: an old `.muToni` session loads; the `a{N}_` params exist and read the same; goldens and listening unchanged with no regeneration.
- **Stage 1 (4 to 6 days).** Move the host from `mu-tronic/Source/Plugin` into `mu-core` (`LayerHostProcessor` + parametric `HostEditor`) with a per-product traits struct (name, slug, licence, state tag, N, buses). Move the per-type adapters next to their layer libraries. Update rule 10 and the boundary scripts.
- **Stage 2 (3 to 4 days each).** Port mu-Tilate (new), then mu-Tant and mu-On, from list mode to fixed slots with an Empty / visible-count policy (the visible count becomes UI-only).
- **Stage 3 (7 to 10 days).** mu-Clid: services for direct outs, sample library, MIDI-out and the hot-swap stager.
- **Stage 4 (2 to 3 days).** Remove the old processors; tidy docs and tests.

## Not verified (answer these first in the spike)

- Whether a saved mu-Toni session maps one-to-one onto the host's state format.
- Whether `HotSwapStager` and mu-Tronic's layer swapper have the same swap and staging behaviour.
- How much of mu-Tronic's editor is not slot-specific (the size of the parametric `HostEditor`).

## Next steps if pursued

1. `architecture-steward` ruling: "single-layer host = the mu-Tronic host with a compile-time N and a per-product traits struct", plus where the host lives in `mu-core` without breaking the core boundary.
2. `backlog-administrator`: a row for the spike, ranked against #1699 / #1701 (which it builds on).
3. Run the Stage 0 spike on a branch; record the three unverified items above.
