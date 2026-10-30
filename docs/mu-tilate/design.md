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

**Architecture rulings** (memory budget, saved state, clocking, worker pool, Stretch state, timeline mirror) are in "Rulings (architecture-steward, 2026-10-30)" at the end of this document. Memory in principle (Andy, 2026-10-30): 8 loops in the plugin or standalone, 16 in mu-Tronic (one per slot).

## Persistence note

Per-slice values do not fit one APVTS parameter each (the number of slices varies, up to a maximum). Keep the slice list, as the mu-On step rows do, as ValueTree children of the layer node: start and end in samples, plus the slice settings (including the filter and envelope values) and the pin flag for manual markers. The timeline blocks are saved the same way. Detection settings and the tempo-match mode are layer parameters in APVTS. Everything the user can change is saved with the preset; the loop itself is either embedded or stored by path, following the mu-Clid embedded-sample approach (with the path-safety rules from the audit fix).

## Shared pieces this suggests (for `mu-core`, not per-product)

**Decided** (Andy's standing rule: shared code always goes in `mu-core` unless there is a very good reason not to): these are built in `mu-core`, not inside mu-Tilate, together with the sample-library mechanics in [design-library.md](design-library.md), the product-independent parts of the transient and tempo detectors, and the Bounce dialog and render path: a **zoomable, scrollable waveform view with region tints and markers**, a **time ruler**, and a **drag-scroll / zoom viewport** helper (the timeline reuses it). A drag-and-drop payload for "slice onto timeline" stays product-side. A coded `ux-controller` ruling is needed on the exact tint tokens when this is scheduled.

## Decisions (Andy, 2026-10-30)

Name **mu-Tilate**; time-stretch is a per-layer option (Off / Re-pitch / Stretch); layer type `Phrase` with `.muPhrase` and `.muTilate`; 8 layers with one loop and one sample player each (single-row timeline, no lanes); each layer is **monophonic**: one slice at a time, the next slice cuts the previous one through the short automatic declick fade; no MIDI play; formats VST3 + CLAP + Standalone; automatic mu-Tronic integration; a sample library like mu-Clid's; Bounce to WAV and Export slices are in scope.

## Decisions (Andy, 2026-10-30), addendum

Factory loops are included under the factory-loop licensing rule in [design-library.md](design-library.md); tempo comes from metadata first, then analysis; Export slices writes **one folder per loop**; Bounce to WAV is **Standalone only for now**, and because the dialog is the shared one in `mu-core` (see the Bounce idea), the option to offer it in the plugin formats later stays open at no extra cost.

## Rulings (architecture-steward, 2026-10-30)

The user-visible limits and defaults below are **DECIDED (Andy, 2026-10-30)**; the rest are structural and settled. Only the prototype items are still to prove.

**1. Memory budget for decoded loops.**
- **Format:** decoded to `float32`, in the file's channel count (a mono file stays mono, one channel, half the bytes; no forced stereo), at the file's own sample rate (the player resamples with the interpolator; nothing is resampled on load, so a rate change of the host needs no reload). Not 16-bit: the player, Stretch and the interpolators work in float and a per-sample convert on the audio thread buys only half the memory.
- **Cap (DECIDED, Andy, 2026-10-30):** `kMaxLoopBytes = 11,520,000` per layer (30 s of stereo float32 at 48 kHz; 15 s at 96 kHz; 60 s if mono), a constant in `mu-core` beside `mu_limits`. About 16 bars at 128 BPM or 8 bars at 64 BPM. The limit is in bytes, shown to the user as seconds for the file in question.
- **Worst case:** 8 layers x 11.52 MB = 92.16 MB (87.9 MiB); 16 mu-Tronic slots x 11.52 MB = 184.32 MB (175.8 MiB), both at 48 kHz stereo. Typical use (2 to 4 bars, 5 to 10 s stereo = 1.9 to 3.8 MB) is a tenth of that. The cap is per layer and the total follows from the layer count (`kMaxLayers`), so there is no separate session cap to maintain.
- **Over budget:** the file is **rejected** on the worker thread before decoding (the reader's `lengthInSamples x channels x 4` is checked against the cap), the layer keeps its previous loop, and the status bar says "Loop too long: N s, limit M s for this file". Never truncated (that would break bar length, tempo and slices) and never a silent partial load. Nothing is ever allocated on the audio thread: the buffer is allocated and filled on the worker, published read-only.
- **Sharing:** yes. `mu-core` holds a `LoopCache` (per processor) of immutable decoded buffers keyed by content hash (hash of the file bytes, or of the embedded FLAC bytes). Layers and clips hold `std::shared_ptr<const LoopBuffer>`; the audio thread only reads through a raw pointer swapped at the staged boundary, and the last reference is dropped on the message thread (RetireBin), never the audio thread. Two layers loading the same file decode once.

**2. Saved-state size and mu-Tronic scenes.**
- **Embedding follows the mu-Clid mechanism** (shared `embedded_sample`, FLAC re-encode, same path-safety rules). **Limits (DECIDED, Andy, 2026-10-30):** at most **4 MiB per embedded loop** (about 25 to 30 s of typical 24-bit stereo drums as FLAC, matching the 30 s decode cap) and **32 MiB for the embedded loops of one saved state** (plugin/standalone state or one mu-Tronic session). A loop over its limit is stored **by path** and the save shows a one-line notice; nothing is dropped silently, and a path reference never fails a save.
- **Default (DECIDED, Andy, 2026-10-30):** `Auto`. A loop inside the library folder or the content `Samples/` folder (including factory loops) is stored by path; any other file is embedded, so a preset or DAW project still plays when the file moves. A per-layer "Embed in preset" override (Auto / Embed / Path) is saved with the layer.
- **Scenes and clips:** a scene is only an index into the per-layer clip banks (mu-Tronic design 3.6), so it stores nothing. A **clip is the layer preset, so it stores the loop reference, never embedded bytes**: path, content hash, name, length and tempo. A session that embeds loops writes them **once each** into a state-level `<EmbeddedLoops>` pool keyed by content hash (counted against the 32 MiB), and layers and clips refer to the hash; 8 clips on 16 slots therefore cannot multiply the size. A standalone `.muPhrase` file embeds its own loop under the 4 MiB cap and carries no pool. A clip whose loop cannot be found (path gone, not in the pool) shows "missing, click to find" and is skipped by a scene launch, as a type-mismatched clip is.
- **Launch:** a clip or scene launch is a staged swap; the decode runs on the worker pool from `preloadStaged` and the swap is taken at the boundary only if the buffer is ready, otherwise it waits for the next boundary. The boundary never blocks on I/O.

**3. Clocking and tempo matching.**
- Phrase is **transport-anchored**: timeline position is a function of `RenderContext::startBeat`, so it is block-size invariant, follows locate and host loop through `onTransportJump` (re-anchor, retire the current slice through the declick fade), and under mu-link in the Standalone it follows through the same play-head path as every product. No Phrase code reads a host or mu-link API directly. `loopBeats()` is the timeline length in beats, giving the hot-swap boundary.
- **Tempo ratio** = `ctx.bpm / loopTempo`, where `loopTempo` is the Set override, else metadata, else analysis (design-engine). **Off:** slices play at native speed from their start (slice start is the only tie to the grid). **Re-pitch:** rate = ratio, read each block from `ctx.bpm` and ramped across the block (about 10 ms smoothing) so a tempo ramp does not zipper; slice positions are scaled by the ratio so they stay on the grid. **Stretch:** the ratio is **latched at each slice start**.
- **Tempo change mid-playback:** Re-pitch follows continuously; Stretch takes the new ratio at the **next slice start** (the running slice finishes with its latched ratio and is cut by the next one; a short gap or an early cut up to one slice during a fast tempo ramp is accepted and documented). A jump or locate re-latches immediately.
- **Warp map:** only the Curve shape table (256 points) is built on the message thread when Curve changes. The per-slice solve (plateau rate from hold, ramp, slice length and ratio) is a few multiplications and is done on the audio thread at slice start, because the ratio is only known then; this refines the "computed on the message thread" line in design-engine.
- **Scenes in mu-Tronic:** a scene lands on one bar line (mu-Tronic design 3.6); the new clip's loop, tempo and timeline start together from the timeline's start at that boundary, as mu-On's rows start from step 0. The ratio is recomputed from the new loop's tempo at the swap.
- **Audition** (pad click, library preview) is free-running, Off mode, not tied to the transport; library preview "at project tempo" uses the layer's mode with the current `bpm`.

**4. Shared detection worker pool (`mu-core/Analysis/AnalysisPool`).**
- **One pool per processor** (not per layer, not process-static: plugin instances must not share static threads). Two lanes: **interactive** (2 threads: decode, detection curve, tempo analysis of a loop being loaded) and **background** (1 thread, lowest priority: library list tempo and length scans, which must never queue ahead of a user's load). Three threads at most, for 8 or 16 layers. Thread priority is set through the `juce::ThreadPool` options; the exact JUCE 9 option name is checked at implementation. Jobs use the JUCE 9.0.2 callable signature.
- **Peak picking from the cached curve (Sensitivity, Min gap) is not a job**: it is microseconds, runs on the message thread, and markers follow the knob live.
- **Cancellation:** every layer owns a `std::atomic<uint32_t>` generation held through a `shared_ptr` in the job. Loading a new loop, changing Method or band, or deleting the layer increments it; the job checks it every 16 FFT frames and between stages (`shouldExit` plus generation) and returns early. Jobs hold only the shared ticket and shared buffers, never a pointer to the layer. Deleting a layer cancels and does not wait; the processor's destructor calls `removeAllJobs(true, timeout)`.
- **Handover without locks:** the job builds an immutable `AnalysisResult` (decoded buffer, curve, slice points, tempo, confidence, generation) and posts it with `MessageManager::callAsync` guarded by a weak reference. The message thread drops it if the generation is stale, otherwise stages it as the layer's next state; the audio thread receives it only through the existing staged-swap boundary (one atomic pointer exchange, old result to the RetireBin). The audio thread never waits for, polls or frees a result.

**5. Stretch and interpolator state inside the pre-allocated budget.**
- Allocated in the Phrase layer's `prepare` (which mu-Tronic runs on the message thread before the layer is published, steps 2 and 8 of its swap sequence), so an empty slot or a non-Phrase slot costs nothing: per Phrase layer **two** Signalsmith Stretch instances (current and retiring slice, so a cut-off slice fades out on its own stretcher), each configured once with `configure(channels, block, interval)` for the maximum channel count, plus per channel one `Lagrange` and one `WindowedSinc` interpolator (fixed-size objects, no heap), the 256-point Curve table, and scratch of at most the prepared block size. Nothing is resized in `processBlock`; a sample-rate or block-size change goes through `prepare` again.
- **Memory:** to be measured in the prototype; budget 0.5 MB per layer for both instances (8 MB for 16 layers), which is small against the 11.52 MB loop buffer.
- **CPU:** no figure is assumed. The prototype measures it; the **target** is at most 1.5 percent of one core per stretching layer at 48 kHz and 512 frames, so 16 stretching layers stay under about 25 percent, otherwise the cheaper Stretch preset is used. Layers in Off, or in Re-pitch with ratio 1, run neither stretcher nor sinc.
- **Latency risk (record in the prototype):** Signalsmith Stretch has an input/output latency of tens of ms; a slice cannot start late. The design's Hold section helps: play the hold directly from the sample while the stretcher is primed with `seek` at the slice position, then cross-fade to the stretcher output. Whether the hold is long enough for that is part of the prototype question.
- **Interpolators:** `Lagrange` is the default. **`WindowedSinc` may not be adopted without a `dsp-controller` review** of its CPU at 8 and 16 layers (its memory size is 200). Choice is made at slice start only (never switched mid-slice), and falls back to Lagrange while pitch is modulated.

**6. Timeline and the mirror rule.** Yes, the rule extends. There is **one timeline implementation**: the model (blocks as ValueTree, quantise, hit-test, edit operations, staging at the boundary) in `mu-tilate-layer/Sequencer`, the view and its panels in `mu-tilate-layer/UI`, built on the generic `mu-core` ruler, viewport and waveform. The product's editor and mu-Tronic's content band both host the same components; neither has timeline code of its own. The layer tests run the timeline through the registry host as well as the product host. This is the same shape as mu-On's `StepRow` and `Track`.

## Open questions for Andy

None remaining. Loop cap, embedded limits, `Auto` embedding and Stretch latching the tempo at the next slice start are decided (Andy, 2026-10-30).

## To prove in the prototype

- Whether Signalsmith Stretch follows a varying local rate without artefacts (design-engine), and whether the Hold is long enough to hide the stretcher's latency.
- Real CPU and memory of Stretch and `WindowedSinc` at 8 and 16 layers against the targets in ruling 5 (then `dsp-controller` review).
- The exact JUCE 9 `ThreadPool` priority option name.
