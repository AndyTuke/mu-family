# Layer extraction programme — plan (2026-10-15)

**Status:** plan only. No source, backlog or CLAUDE.md change is made by this document. Written by the Architecture Steward after Andy re-prioritised
on 2026-10-15: *"mu-tronic won't do anything useful without the voices from the other 4, so the detachable voice change needs to be done first."*

**What it supersedes.** [design.md](design.md) section 6 ("Order after Andy's timing answer"), risk 9, decision 5 and section 10 said the mu-Tant and
mu-Clid extraction (T5, T7) waits until after their next releases. That ordering is **withdrawn**: all four products are extracted **before any
mu-Tronic product work**, the two live products included. Everything else in design.md (the contract of section 2, the self-containment rules, the
Scales.h renames, the tests of section 7) stands; this document re-orders and expands the stages and adds the safety and verification machinery the live
products now need. Pointers are added to design.md and design-plugin-family.md in the same turn.

**Reviews merged (2026-10-15).** The `dsp-controller` (verdict: pass with notes on the design) and the `test-steward` reviewed this plan in parallel; their
findings are folded in below and summarised under "Review notes merged" at the end. Two structural consequences: (1) the **safety-net tests and the
four design fixes (C1 to C4 of section 2.1a) come before any contract code**, so the programme starts with E0 (tests) and E0d (design), not with E1; (2) the test
design of record is the test-steward's [tests.md "Layer extraction safety net"](../../tests.md#layer-extraction-safety-net) (golden harness
`mu-core/Tests/GoldenRender.h`, `LayerEquivalence.h`, compat corpus, parameter manifests, `AllocationProbe.h`, `check-rt-safety.ps1`). This document
**references it and does not duplicate it**; where an earlier draft of this plan proposed a WAV/SHA matrix, that proposal is dropped in favour of tests.md.

Read with: [design.md](design.md) (sections 1.3, 2, 3.5, 6, 7), [design-plugin-family.md](../design-plugin-family.md) (Layer ownership, Stage 7 plan and
proof list, hot-swap), [design-naming.md](../design-naming.md), [design-future.md](../design-future.md).

**Naming used here.** Programme stages are **E0 to E6** (extraction). They map onto design.md's stage ids as: E1 = T0 (limits, ids, checker) + the
product-facing half of T3; E2 = T4 (Toni); E3 = T6 (On); E4 = T5 (Tant); E5 = T7 (Clid). T1 (empty unit), T2 (buses), T8a, T8b, T9 keep their ids and
come after (section 6).

---

## 0. Standing check (design-future)

Easier: this is the "one framework, one instance" idea itself, and it removes the per-product copies of per-block glue (four copies of "read modulation,
push params, render, copy to mixer, retire tail" collapse into one `renderLayers`). Harder: nothing, **provided the extraction does not change what
any product sounds like or saves** — the whole plan is built around proving that. One thing is made deliberately harder: adding product glue to
`PluginProcessor` (the checker of section 4.5 rejects it from library files).

Scope guard (one-framework direction vs shortcut): E1 builds **only the half of the contract the four products need to adopt** (`Layer::render`,
`RenderContext`, `renderLayers`, `RetireBin`, `loopBeats`, `bindParams`, `preloadStaged`, `onTransportStopped`, `renderPriority`, `typeId`, the limits
macros). `setLayerType`, `LayerServices`, scenes and buses are mu-Tronic-only machinery with no customer until T1 and are **not** built here (section 2.1).

---

## 1. Goal and definition of done

### 1.1 Goal

Each of the four products' layer ("voice") is a self-contained library that a second host can use, and the product's own app is itself a host of that
library on the same code path. After the programme there is exactly one copy of: the per-block render glue, the modulation resolve, the retire/fade
logic, the parameter table of each layer type, the held-note / Note-mode / swing state of each type.

### 1.2 Definition of done (programme)

All must hold, for each of Toni, On, Tant, Clid:

| # | Criterion | Proven by |
|---|---|---|
| D1 | The layer class implements `Layer::render` (and `loopBeats`, `preloadStaged` where the type needs them). The product's `processBlock` contains **no** per-layer render, modulation-resolve, trigger or retire code; it resolves the transport, builds the `RenderContext`, calls `ProcessorBase::renderLayers`, and calls the mixer. | code review (architecture-steward); `check-layer-boundary.ps1` rule "glue" (section 4.5) |
| D2 | `mu-<product>-layer` (INTERFACE) exists, lists exactly `Audio/ Sequencer/ Modulation/ Persistence/ UI/` minus `UI/SettingsOverlay.*`, and **no file in it includes anything under `Plugin/` or `License/`, names `PluginProcessor`, or includes another product's header.** | `check-layer-boundary.ps1`; a per-library test target built from library sources only (4.5) |
| D3 | The product's standalone and plugin formats run on that same path; there is no second render path kept "for the standalone". | D1 + golden comparison (4.1) |
| D4 | Layer UI content takes `ProcessorBase&` plus a `<Type>Host` interface, never the product `PluginProcessor&`. | checker (grep) + the editor still builds |
| D5 | The layer's parameter table is `addLayerParams(layout, slot)` in `Persistence/`; `createParameterLayout` calls it. | parameter manifest diff (4.2) |
| D6 | Product-global state of the type (Tant Note mode, Toni held notes and MIDI channel, On swing/accent/grid/clock) lives in the layer, with defaults that reproduce today's sound and saved result. | pinning tests written **before** the move (4.4) |
| D7 | **Nothing user-visible changed:** rendered audio is sample-identical for the golden set; saved state is byte-identical for the same input; every parameter id, version, range, default and name is unchanged; every existing test suite is green; pluginval passes on each VST3. | all gates of section 4 |
| D8 | design-naming §3/§4/§5 and the product CLAUDE.md files describe the new tree; the stale Stage 7 "engines stay processor-side" ruling is amended in design-plugin-family. | architecture-steward at E6 |

Out of scope (so that "done" is finite): mu-Tronic itself; buses; scenes; `setLayerType` with real type changes; the shared wavetable-bank and
`SampleLibrary` as `LayerServices` (each standalone keeps its own, handed to the layer through the host interface); any sound, UI or format change.

---

## 2. Work breakdown

### 2.0 E0 — Safety net, seed fix and design fixes (before any contract code)  **size M, about 8-10 working days of test work plus 1-2 sessions of design**

Nothing here changes a shipping binary except the one approved, tiny seed commit (E0.2). The test work is specified in
[tests.md "Layer extraction safety net"](../../tests.md#layer-extraction-safety-net) section 7 (items numbered below as #n); this table only sequences it
against the programme. About 12 to 13 working days of test work in total; **items 1 to 6 (about 6 days) are the minimum before any code moves.**

| Step | What | tests.md | Leaves green |
|---|---|---|---|
| E0.1 | Tag `pre-extract-all` on the shipped v1.1 commit; choose and record the **baseline commit** from which every golden is recorded (the commit on which tests.md section 2 is signed off; chosen when the harness item starts). Per-product tags `pre-extract-<toni\|on\|tant\|clid>` are set just before each product's first code-moving commit. | | n/a |
| E0.2 | **Seed fix, its own tiny commit** (approved by the main conversation as Andy's delegate; `dsp-controller` signs): fixed seed for mu-Tant `NoiseGen` and mu-Toni `ToniVoice::rng` (both clock-seeded `juce::Random` today, so noise renders cannot repeat). This is the only live-product source touch before the contract exists; it changes noise from non-repeatable to repeatable and nothing else. Goldens are recorded **after** it. | #15 | all suites |
| E0.3 | **Parameter-id manifest** per product with frozen dumps (`ch{N}_`, `mstr_` and per-layer ids included). | #1 | per-product suites |
| E0.4 | **Compat corpus** collected from the baseline, `MANIFEST.txt` of hashes; one parameterised test per product: loads clean, values equal, **re-save byte-identical**, idempotent second save, cross-product layer presets. | #2 | per-product suites |
| E0.5 | **`GoldenRender.h` + `update-golden.ps1`**: scenario loader, FNV-1a fingerprint per 256-frame segment of master and per-layer stems, equality rule (bit-identical, tolerance 0, same machine class/config/compiler, separate files per config), self-repeat test (twice in-process, twice cross-process), diff dump to `tests/_out/golden/`. Goldens live in git under `tests/golden/<product>/<scenario>/`; only `update-golden.ps1` changes them, never in a code-moving commit. Per-product `GoldenRenderTests.cpp`. | #3 | harness self-test |
| E0.6 | **First goldens: mu-Toni then mu-On** (about 10 scenarios each); then **mu-Tant and mu-Clid** (recorded now while the baseline is clean, used at E4/E5); the pins that describe behaviour a stage will move (mu-On per-engine tests, global swing/accent, Rumble source read and bass-to-kick sidechain; mu-Toni `readVoice`, held notes, root-by-MIDI, MIDI-trigger; mu-Tant Note-mode pin for stem and with-FX final mix). | #4, #5, #7, #8, #9 | per-product suites |
| E0.7 | **`AllocationProbe.h`** wired into every golden run (zero allocations and zero frees inside `processBlock`, `Layer::render`, `renderLayers`, including the first block after a swap, type change and stop). | #6 | golden runs |
| E0.8 | **Mixer block-value test** in `mu-core-tests` (exact level, pan, sends, sidechain, mute/solo through `MixerEngine`; pins the sidechain tap order, C13); generalised **lock probe and two-thread stress**; `check-rt-safety.ps1` report-only; `check-layer-boundary.ps1` fixture. | #11, #12, #13 | core suites |
| E0.9 | **Approved test accessors:** one test-only per-channel tap on `ProcessorBase` (what each channel hands the mixer; fallback: soloed stems), and the **`useLayerRender` switch** (section 2.7a), both approved by the main conversation. `LayerEquivalence.h` follows once E1 exists. | #10 | with E1 |
| E0d | **Design fixes C1 to C4** (section 2.1a) written into [design.md](design.md) section 2.1 and signed by the `dsp-controller` before E1 is coded; the secondary requirements C5 to C18 are in the same table and are tested at the stage named there. | | docs |

E0 is **not optional**: without E0.5 to E0.7 "sample-identical" cannot be asserted, because the old path is gone after each stage. The CPU measurement
plan (C18, section 4.3) is also written here, so budgets are ratified from numbers rather than guessed.

### 2.1a Contract requirements from the real-time review (C1 to C18)

Added to design.md section 2.1 on 2026-10-15. They are requirements on the contract, not on a product (numbered C, not R, to keep them apart from the risk
register of section 5). Two of them (C1, C7) concern features only mu-Tronic uses (type change, scenes); they are **specified now** because the shape of
`renderLayers` and `RetireBin` must not have to change when they arrive.

| # | Requirement | Stage / test |
|---|---|---|
| **C1** | **Type changes and scene commits must not use `ScopedSuspend` / `layerEditInFlight` and must not mute other layers.** Build, prepare, default-parameter and `bindParams` the new layer first; publish it by **one atomic pointer store**. The audio thread loads an immutable 16-entry layer view **once per block** (`renderLayers` reads layer pointers through one accessor, so replacing the try-lock by the published array later is a one-function change); `layersLock` stays off that render path or has a tested bounded hold. Parameter values are never moved after publication. The existing structural edits (add/remove/swap/reset) of the four products keep today's protocol until a product asks otherwise. | specified E0d; accessor shape E1; implemented with `setLayerType` (T1 stage); stress test |
| **C2** | **`RetireBin` is a proven state machine.** Per-slot states Empty, Parked, Draining, Done with release/acquire transitions; the message thread frees only after seeing Done **and** one audio-block epoch has passed; capacity N entries per slot or a global ring of 16 to 32 (two retargets within 25 ms must not drop, free early or block); the gain ramp to zero is a fixed number of samples rescaled at `prepare`, plus a **hard maximum lifetime** (reverb and comb tails); flushed at `prepareToPlay` and `releaseResources`. | E1 (`RetireBinTests`, transitions checked exhaustively, N-deep) |
| **C3** | **A retiree reads no APVTS and no matrix.** `Layer::beginRetire()` snapshots everything the render path reads (a Kick retired and a Bass created in the same slot share the `t{N}_` prefix, so a live read would mix them). `LayerServices` (wavetable bank, `SampleLibrary`) **outlive every layer including retirees**. | E1 test with the same-prefix case; services at T1 |
| **C4** | **The time model is in `RenderContext` before any product moves** (otherwise Toni's runner clock is reworked twice): POD with `sampleRate`, **actual** `blockSize` (0 and 1 allowed), `playing`, `bpm`, unwrapped `double startBeat`, `beatsPerSample`, `justStarted`, `justStopped`, `transportJumped`, a **filtered MIDI view (no copy)**, `slot`, `peer()`. Layers derive event positions from the absolute beat; their state is limited to the last-evaluated beat and beat-domain pending note-offs; **no free-running phase**. New `onTransportJump()` beside `onTransportStopped()`. Tempo changes between blocks stay phase-continuous. | spec E0d; struct E1; first customer E2 (Toni runner reworked once) |
| C5 | The shared wavetable bank is published as an **immutable atomic array** or each layer keeps a per-layer fallback; a try-lock miss is **never silence**. | E2 (Toni), E4 (Tant bank lock); T1 for the shared bank |
| C6 | No reference-counted last release on the audio thread (no `shared_ptr` final release in `render` or the bin). | E1 review, `check-rt-safety.ps1` |
| C7 | **Scene commit is pointer publication at a block start**, new state fully prebuilt at stage time (the stager's flag is atomic, but `commit()` runs on the message thread at about 100 Hz, so layers would land up to about 10 ms apart). The test measures the first-event audio position per layer. | T8a (mu-Tronic) |
| C8 | `renderLayers` **chunks oversize blocks** in the base (`GrooveVoices::renderAll` truncates silently today); pinned by a test first, fixed by the shared path. | E1 (chunking), E3 test |
| C9 | `bindParams` pointers are **rebound on `onMoved`**, and the Rumble source index is remapped generically; layers cache atomics and **never call `getRawParameterValue` in `render`**. | E1 base, E2 to E5 |
| C10 | The `peer()` tap point is **post-insert, pre-strip**. | E1 doc and test, E3 |
| C11 | `ScopedNoDenormals` is owned by `renderLayers`. | E1 |
| C12 | **Hold-last-value on a `modLock` miss** (a missed matrix lock never snaps a parameter to default). | E2 to E5, per-product test |
| C13 | **Sidechain tap defined**: pre-duck, pre-fader snapshot, or written index order (mutual ducking is order-dependent today); the chosen rule is pinned by E0.8's mixer test before any change. | E0.8, decided E1 |
| C14 | `getLatencySamples() == 0` asserted for every insert algorithm (buses make delay compensation a standing constraint). | E0.8 test |
| C15 | Skip Empty strips in mixer phases 1 and 3. | T1/T2 (mu-Tronic; mixer code) |
| C16 | Per-layer UI atomics (`modLive`, snapshots) are cache-line aligned and fetched **by slot each tick**; **UI content is torn down before the old layer enters the bin**. | E2 to E5 (UI), T1 |
| C17 | `setLayerType` specifies `scEnv` zeroing; **re-prepare of all layers, retirees and bus inserts** on a sample-rate or block-size change. | T1 |
| C18 | **CPU measurement plan** in E0/E1: worst case 16 mixed layers + 3 buses x 2 inserts + 3 returns + master, at 44.1 and 192 kHz, blocks 64 and 512. Budgets are **ratified from the numbers**; the proposal is: 16 heaviest layers below 50% of one core at block 128, p99 and max reported, an empty layer about 0, and the real worst case measured as **16 simultaneous retirees = 32 engines for about 25 ms**. Also record insert memory and prepare time for 24 `InsertProcessor`s (each owns every algorithm instance). | E0 plan, E1/T1 numbers |

Per-stage real-time gates added by the review: **E1** fake heavy layer with an allocation counter, a stress thread, N-deep retire, `onMoved` rebinding, service
lifetime, denormal silence cost, large-block chunking. **E2 to E5** equivalence at three block sizes, locate/loop-back with held notes, a tempo ramp, a mid-block
start, `startBeat` 1e6, and for mu-On Kick to Bass in one slot with a tail. **T1**: 16 empty layers allocate nothing. **T2**: the bus tests. **T8a**: scene
first-event position per layer.

### 2.1 E1 — The shared contract  **size M, 3-5 sessions**  (all `mu-core`, additive)

Design.md section 2.1 is the specification. E1 builds only what the products consume. Files:

| File | Change |
|---|---|
| `mu-core/MuLimits.h` | `kMaxLayers = MU_MAX_LAYERS` (default 8), `kMaxBuses = MU_MAX_BUSES` (default 0) and `kBusInserts` (default 2) declared but **unused** until T2, `kLayersPerPage` (default 8). Compile-time only; the four products and `mu-core-tests` get the same constants as today. Add `mu-core-tests-16` (same sources, 16). |
| `mu-core/Sequencer/LayerTypeInfo.h` (new) | `enum class LayerTypeId`, `LayerTypeInfo` (describe-only fields: id, name, group, `paramPrefixLetter`, `presetExtension`, `addLayerParams`, `create`). No registry list yet; each product declares its one `LayerTypeInfo` constant and uses `create`/`addLayerParams` itself. |
| `mu-core/Sequencer/Layer.h` | New virtuals, all with do-nothing defaults: `typeId`, `prepare`, `beginRetire` (C3), `onTransportJump` (C4), `bindParams(apvts, slot)`, `render(const RenderContext&, AudioBuffer<float>&, int)`, `loopBeats`, `preloadStaged`, `onTransportStopped`, `renderPriority`, `tailBlocks`. `Layer` stays a copyable value; **engines are held in `SlotBound` / `unique_ptr` members that a copy leaves alone**, as `Arp::insert` and `Pattern::insert` already are. |
| `mu-core/Sequencer/RenderContext.h` (new) | The POD of design 2.1 as amended by C4: `sampleRate`, actual `blockSize` (0 and 1 allowed), `playing`, `bpm`, unwrapped `double startBeat`, `beatsPerSample`, `justStarted`, `justStopped`, `transportJumped`, a filtered MIDI **view** (no copy), `slot`, `peer(slot)` (post-insert, pre-strip, C10). **This is settled before E2 so Toni's runner clock is reworked once.** |
| `mu-core/Plugin/ProcessorBase.{h,cpp}` | `renderLayers(const RenderContext&)`: owns `ScopedNoDenormals` (C11), chunks oversize blocks (C8), reads layer pointers once per block through **one accessor** (C1: today the existing `layersLock` try-lock and `layerEditInFlight` check, later a published array), then render priority-0 then priority-1 layers into per-slot scratch buffers (allocated in `prepareToPlay`), then supply the `MixerEngine::RenderChannelFn` as a copy from scratch. `RetireBin` as the C2 state machine (Empty/Parked/Draining/Done, release/acquire, epoch-delayed free, 16 to 32 entry ring, hard maximum lifetime, flushed at prepare/release; `beginRetire` snapshot, C3): `park(slot, unique_ptr<Layer>)`, audio thread renders parked layers under a gain ramp for `tailBlocks()`, sets a drained flag, message thread frees (the mu-Tant `retiring` / mu-Clid `retiredReadyForCleanup` pattern, once). A layer with `tailBlocks() == 0` is **not** parked, which keeps today's behaviour for products that currently drop the voice at once (check per product in its stage). |
| `tests/scripts/check-layer-boundary.ps1` (new) + `test-check-layer-boundary.ps1` fixture | Rules of 4.5; wired into `/build` beside `check-core-boundary.ps1`. |
| `mu-core/Tests/LayerRenderTests.cpp`, `RetireBinTests.cpp` | A fake heavy layer type: the C-list gates (allocation counter, stress thread, N-deep retire, `onMoved` rebinding, service lifetime, denormal cost, chunking), priority order, empty skipped, lock miss renders silence but transport and boundary detection still advance, retire fade then free off the audio thread, no allocation under the lock (counting allocator), Rumble-style `peer` read of a priority-0 layer. |

What E1 deliberately does **not** do: no change to `MixerEngine` (no `Phase 3b`, `MaxChannels` untouched), no `setLayerType`, no `LayerServices`, no
registry list, no change to any product source. The only product-visible change is that `ProcessorBase` has new unused members.

**Why this is the right cut.** Design.md's T3 asked for the full contract before any product, so it would be proven on a fake type rather than guessed
from one product. Keep that, but prove the *consumed* half on the fake type and let the pathfinder (Toni, E2) prove the rest by adopting it. Anything the
pathfinder shows to be wrong is a core change in E2, at the cheapest moment.

**Leaves green:** every suite of every product, `mu-core-tests`, `mu-core-tests-16`, byte-identical goldens (E1 touched no product path), pluginval.
dsp-controller review: **yes** (the contract and `RetireBin` are audio-thread code; this is the review that matters most).

### 2.2 E2 — mu-Toni (pathfinder)  **size M, 3-5 sessions**

Why it is the pathfinder. `Arp : Layer` already owns `ArpVoiceRunner` (arp clock + `ToniVoice`) and its insert; Toni has no `HotSwapStager` payload with
I/O, no retire tail today (a deleted layer's voice is `reset`, not faded), and is pre-release, so a mistake costs no shipped session. The
processor is 513 lines plus 105 (preset) and the glue to move is `readVoice` (about 70 lines), `updateHeldNotes`, the `vp`/`modDestAtoms`/`modLive` tables and
`renderChannelCb`. Total Toni source 3,955 lines including tests.

Steps in order (each its own commit; the golden set and the manifest/state diffs run at every step):

| # | Step | Files | Leaves green |
|---|---|---|---|
| E2.1 | Parameter table out of the processor: `Plugin/LayerParamIds.h` to `Persistence/LayerParamIds.h`; `addLayerParams(layout, slot)` and the global part split from `createParameterLayout` into `Persistence/ArpParams.{h,cpp}`. Pure move. | `PluginProcessor.cpp`, `LayerParamIds.h`, `ApvtsLayoutTests` | manifest diff empty |
| E2.2 | `ArpHost` interface in `UI/` (`getArp(i)`, `getModLive`, bank access, the few calls `ArpContent` makes today); `ArpContent` takes `ProcessorBase&` + `ArpHost&`; `PluginProcessor` implements `ArpHost`. | `UI/ArpContent.{h,cpp}`, `PluginProcessor.h` | editor builds; UI tests |
| E2.3 | **Held notes and MIDI channel become layer state** (D6): `ArpContext`, held-note stack, noteOnEdge into `Arp`; the MIDI-in channel read through the shared per-layer setting. Defaults (Omni) reproduce today. The E0.8 pins must pass unchanged. | `Arp.h`, `ArpVoiceRunner.h`, `PluginProcessor.cpp` | pins + golden (MIDI-script cases) |
| E2.4 | `Arp::bindParams` (the `vp` pointer table and `modDestAtoms`), `Arp::render` (= `readVoice` + runner update + `runner.render` + insert; `modLive` lives in `Arp`). `renderChannelCb` becomes a trivial adapter calling the layer; the processor still builds `ArpContext` once per block. Both old callers and the new method exist for **this commit only**; the `useLayerRender` equivalence test (4.1, section 2.7a) proves them equal, then the old code is deleted in the next commit. | `Arp.h`, `Sequencer/Arp.cpp` (new), `PluginProcessor.cpp` | equivalence test, golden |
| E2.5 | Delete the processor-side copies. `processBlock` = transport, `RenderContext`, `renderLayers`, `processCoreBlock`, `flagSwapBoundaries(loopBeats)`. `Arp::loopBeats()` replaces the bar-line assumption. | `PluginProcessor.cpp/.h` | goldens, `TONI_swap`, `TONI_slot_swap`, `TONI_host_lock` listening tests |
| E2.6 | Library: rename `Audio/Scales.h` to `ToniScales.h` (mu-Toni is pre-release, so the rename is free now); `mu-toni-layer` INTERFACE target in `mu-toni/CMakeLists.txt`; `mu-toni` and `mu-toni-tests` link it; add the library-only test target. `check-layer-boundary.ps1` passes with Toni registered. | CMake, `Audio/ToniScales.h` | checker; all suites |

**Resting points:** safe after E2.1, E2.2, E2.3, E2.5 and E2.6. Not safe to leave in E2.4 (two code paths).

**Pathfinder output** (a deliverable of E2): a short "what the contract got wrong" list folded back into E1's files before E3 starts, plus design.md
updated. If the list is long, E3 does not start until it is empty.

dsp-controller review: **yes** (E2.4 to E2.5: held-note state shared between UI-thread MIDI scan and audio thread, `render` allocation-free).

### 2.3 E3 — mu-On  **size L, 6-9 sessions**

Why second. mu-On is pre-release (free to change), and it is the **hardest contract test**: five engine types, per-layer engine allocation, the only
cross-layer audio read (Rumble), and the largest amount of global state to make per-layer. Doing it second means the contract is stressed while only
pre-release data is at risk, and before the live products are touched. Sizing: processor 360+210+324 lines, `GrooveVoices.h` 336 (all engines of all
slots, modulation resolve, trigger), `StepPattern.h` 161 + `GrooveSequencer` 113 (one global grid and clock), engines 77+125+122+208, `TrackContent`
330 + `StepRows` 184 + 55, `TrackParams.h` 117, tests 5,222 lines in total with `EngineMigrationTests` 313 and `SlotStateTests` 226.

| # | Step | Notes | Leaves green |
|---|---|---|---|
| E3.1 | `Plugin/TrackParams.h` (`EngineType`, ids, `layerParamPrefix`) to `Persistence/TrackParams.h`; `addLayerParams` split out. The engines, `MuOnModDest.h`, `StepPattern.h`, `GrooveVoices.h` stop including `Plugin/`. | manifest diff empty |
| E3.2 | `TrackHost` interface; `TrackContent`, `StepRows` onto `ProcessorBase&` + `TrackHost&`. | UI builds |
| E3.3 | **Per-layer step row first, still behind the global grid** (D6): `StepRow` (16 steps, length, swing, accent) owned by `Track`; `StepPattern` becomes a view over the tracks' rows; saved state unchanged (the global `seq_swing`/`seq_accent` stay saved and seed every track's value on load, a unit test pins that). The E0.8 pin (a 5-lane preset with swing and accent) must pass unchanged. | pins, golden, `SequencerTests` |
| E3.4 | `GrooveSequencer` clocks one row per track; the shared clock is `RenderContext::startBeat`; swing/accent applied per row. Same timing arithmetic, so goldens at several block sizes decide this step. | goldens, `ON_roundtrip`, `ON_swap`, `ON_midi_clock*` |
| E3.5 | **Per-layer engine allocation.** `Track` holds exactly one engine (`unique_ptr`) for its selected `EngineType`; an engine change on the message thread builds the new engine outside the lock and swaps it (the existing hot-swap-style rule); `EngineMigrationTests` keeps passing. `GrooveVoices` splits: engine ownership into `Track`, resolve into `Track::bindParams`, trigger into `Track::render`. Memory: 5 engines x 8 slots drops to 8 engines. | goldens (all engine types), `EngineTests`, `GrooveVoicesTests` rewritten to the new shape |
| E3.6 | `Track::render` in priority order: `renderPriority()` 0 for Kick/Bass/Hat/Snare, 1 for Rumble; Rumble reads its source through `RenderContext::peer(slot)` (still restricted to the mu-On types in the standalone). `remapLayerReferences` becomes the generic `layersReordered` use. Delete `GrooveVoices`. | goldens including a Rumble-sidechained-to-Kick preset; `SlotStateTests`, structural-edit script |
| E3.7 | `engineLocked` flag (mu-Tronic wants it; add the field and the hidden selector now, default false, so the standalone is unchanged); `typeFromPreset` helper; the five `LayerTypeInfo` constants over the one class. | tests of the five types |
| E3.8 | Library: `mu-on-layer` target, checker, library-only test, Scales (none), `Track.h` loses `OwnerLink<PluginProcessor>`. | checker, all suites |

**Resting points:** after E3.1, E3.2, E3.3 (grid per layer behind the old clock), E3.4, E3.8. **Do not rest in E3.5/E3.6**: engine ownership and
Rumble priority change together; finish both in one session block.

dsp-controller review: **yes**, specifically E3.5 (engine swap while rendering, no allocation) and E3.6 (priority-1 reading a priority-0 buffer; lifetime of the scratch buffer).

### 2.4 E4 — mu-Tant  **size L, 6-9 sessions**  (first live product)

Sizing. `PluginProcessor.cpp` is 835 lines of which about 300 are the render chain (`renderVoice`, `applyModulation`, `applyFilterEnvelope`,
`applyPitchEnvelope`, `readConfig`, `renderRetiringVoice`, `renderVoiceCb`); `PluginProcessor_APVTS.cpp` 207, `_Preset.cpp` 320, `PluginProcessor.h` 383,
`PatternContent` 817+249, `GatingDesigner` 927+190 (UI, mostly independent of the processor), `GatePattern` 258+260, `SynthVoice` 121+145. 9,178 lines
in total. Less glue than On, but a **live product** and the Note-mode global (mixed into the final mix, not a layer).

| # | Step | Notes | Leaves green |
|---|---|---|---|
| E4.0 | **Gate: pre-conditions of 3.1** (tag, goldens recorded, E0.6 Note-mode pin, release decision taken). | |
| E4.1 | Parameter table: `createParameterLayout` per-voice part to `Persistence/PatternParams.{h,cpp}` as `addLayerParams`; `SwapBoundary.h` from `Plugin/` to `Sequencer/`. Pure moves. | manifest diff empty |
| E4.2 | `PatternHost` interface; `PatternContent` (23 `proc.` uses of APVTS and the `PluginProcessor::` id helpers) onto `ProcessorBase&` + `PatternHost&`; the id helpers move to `Persistence/`. `Pattern.h` loses `OwnerLink<PluginProcessor>`. | editor builds |
| E4.3 | **Note mode per layer** (D6). `midiNoteMode`, the held-note stack and the final-mix gate ramp (`applyNoteModeGate`) become `Pattern` state; the ramp applies to the layer's own output; the setting is saved per layer. **Behaviour risk:** today the gate is applied to the *master mix* after all voices and before... the mixer already ran (so after FX); per layer, the ramp acts before the strip, a different point in the chain. The pin from E0.8 decides: either the per-layer ramp is arithmetically equal for the single-layer case (then ship), or the master-mix gate is kept as a mu-Tant shell post-stage (decision A3: **the old gate also gated the FX return and any second layer, so per-layer changes the final mix with FX tails**). | pins; golden with a MIDI script in Note mode |
| E4.4 | Cached atomic pointers (`voicePtrs`, `modDestAtoms`) to `Pattern::bindParams`; `voiceSnap` and the `MuTantModSnap` into `Pattern`; `readConfig`, `applyModulation`, `applyFilterEnvelope`, `applyPitchEnvelope` into `Pattern::render` (+ private helpers). The processor keeps `renderVoiceCb` as an adapter for this commit only (`useLayerRender` equivalence test). Beat wrap: the resolver's 64-beat wrap (`kMaxPatBeats`) becomes the layer wrapping its own `startBeat`; the arithmetic must yield the same value, golden over a long (more than 64 beats) render and a `--host-start-beat` case. | equivalence, golden |
| E4.5 | Engine array and `bankLock` handling: the engine moves into `Pattern`; the wavetable bank stays processor-owned and is handed through `PatternHost` (not yet a `LayerServices`); the layer's try-lock on the bank is kept with the same acquire order as today (layersLock, then bankLock). Delete the processor-side render chain. | golden, `SynthDSPTests`, `GateStageTests` |
| E4.6 | **Retire fade replaced by `RetireBin`**: today only a count-reducing swap fades a voice; make a removed `Pattern` park in the bin with the same fade length (the `retiring[]` ramp). Check: golden render of a count-reducing hot-swap (`TANT_preset`, `HotSwapBoundaryTests`) byte-identical. | those tests |
| E4.7 | Library target `mu-tant-layer`, `Audio/Scales.h` to `TantScales.h`, checker, library-only test, adopt `renderLayers`/`flagSwapBoundaries(loopBeats)`; `swapLoopBeats`/`blkPatternBeats` removed. | checker, pluginval |

**Resting points:** after E4.1, E4.2, E4.4 (with the adapter), E4.7. The Note-mode step (E4.3) is a single commit; never leave it half.

### 2.5 E5 — mu-Clid  **size XL, 9-14 sessions**  (riskiest; last)

Sizing. This reverses the Stage 7 ruling (engines stay processor-side). The affected code: `PluginProcessor.cpp` 546 + `.h` 478 + `_Modulation.cpp`
**444** + `_APVTS.cpp` 395 + `_Layers.cpp` 213, `HotSwapStager` **119 + 265**, `PresetIO.cpp` **1,048** + `_HostState.cpp` 245 + `.h` 139, `SampleLibrary`
60+72, `RhythmContent` 137+703, and tests of about 7,000 lines (`ClipLaunchTests` 868, `SwapCommitTimingTests` 807, `ProcessorSeamTests` 665,
`ClidLayerStructureTests` 464, `ClidEditContinuityTests` 353, `ClidScriptedEditRenderTests` 236). The glue is not one chain but several lifecycles
(trigger, MIDI note-off ownership, retire tails with drain flags, sample paths, previous-state modulation flags, a stager that prepares a `Rhythm` +
`VoiceEngine` including file I/O at stage time). `mu_core::Rhythm` is also used by **mu-Clid MIDI**, which must keep compiling and sounding unchanged.

| # | Step | Notes | Leaves green |
|---|---|---|---|
| E5.0 | **Gate** as E4.0, plus a fresh `mu-clid-tests` build (the target is not in ALL) and the Stage 7 listening baselines. | | |
| E5.1 | `SampleLibrary` stops taking `PluginProcessor&` (takes a path provider + the ProcessorBase storage root); `RhythmHost` interface; `RhythmContent`, `VoiceSection`, `MidiOutSection`, `SampleBrowser`, `RhythmMiniVisual` onto `ProcessorBase&` + `RhythmHost&`. | UI builds |
| E5.2 | `PresetIO` off `PluginProcessor&`: it shapes onto `Layer::writeExtras/applyExtras` + `captureLayerPresetTree` plus a small `RhythmPresetContext` for the sample folder; `PreparedFullPreset` payload kept. This is the largest single file (1,048 lines) but a *pure refactor with a byte-compare of saved files*, so it is cheap to verify and early to do. | state bytes (E0.6), `PresetXMLRoundTripTests`, `PresetMigrationTests` |
| E5.3 | `mu_clid::SampleRhythm : mu_core::Rhythm` (not copyable; `Rhythm` itself and mu-Clid MIDI untouched) holding the `VoiceEngine`, `MidiOutputEngine`, play state, modulation snapshot, sample path, previous-state flags and the retire tail. The engines still *also* exist in the processor arrays for this commit only; the new type is constructed but not rendered. | all suites |
| E5.4 | **Move the per-block glue into `SampleRhythm`** in this order, one commit each, goldens after each: (a) modulation resolve and write-back (`_Modulation.cpp`, 444 lines) into `bindParams`/`render`; (b) trigger firing and the Euclid override; (c) MIDI engine and `orphanOffs` (owed note-offs) — `onTransportStopped` and the layer-removal path; (d) retire tails into `RetireBin` (the `retiredVoiceEngines`/`retiredReadyForCleanup` arrays disappear). | goldens (retire-tail, structural-edit script), `MidiOutputModeTests`, `ClidEditContinuityTests` |
| E5.5 | The master-loop `RhythmSequencer` stays as the trigger source (`firedMask`); its interface to the layers is the `RenderContext` (fired bits per slot supplied through the context, not through the processor). `loopBeats()` for the swap boundary. | `RhythmSequencer*Tests`, `SwapCommitTimingTests` |
| E5.6 | **Stager.** `HotSwapStager` stays **for the standalone** (the Stage 7 step 5 decision stands) but its payload preparation uses `SampleRhythm::preloadStaged` so the sample I/O is one function. The swap timing is not touched. Replacing the stager is explicitly **not** in this programme (a later option once `SwapCommitTimingTests` has run green for a release). | `SwapCommitTimingTests`, `HotSwapCancelTests`, swap listening tests |
| E5.7 | Delete the processor-side arrays and glue; `processBlock` becomes transport, context, `renderLayers`, mixer. Library target, checker, library-only test. | everything below |

**Resting points:** after E5.1 and E5.2 (both are pure and independently valuable: the UI and the persistence stop depending on the processor);
after E5.4a, b, c, d (each is a self-contained move behind the golden). **Do not stop between E5.3 and E5.4(a)** (two copies of the engines exist) or between E5.6
and E5.7. The Stage 7 rule applies unchanged: on a failed proof, revert; do not carry it.

Mu-Clid also needs the strictest process (3.2 branch): this is the stage where a long-lived divergence from `main` is most likely.

### 2.6 E6 — Close-out  **size S, 1-2 sessions**

Docs: design-naming §4/§5 marked done; design-plugin-family Stage 7 amended; product CLAUDE.md files; `design.md` section 6 rewritten to the real order;
`tests.md` and the test-steward's catalogue. `check-layer-boundary.ps1` with all four libraries registered; `mu-core-tests-16`; a combined
"all four libraries compile into one throw-away target" job (the first time the include collisions of design 2.5 are exercised: build a
`mu-layer-link-test` console target that links all four `INTERFACE` libraries and includes one header of each, no code run). That target is the last
guard that mu-Tronic T1 can start.

### 2.7 Order and rationale

**Recommended order: E0, E1, E2 (Toni), E3 (On), E4 (Tant), E5 (Clid), E6.**

| Principle | Result |
|---|---|
| **Pathfinder first** | Toni proves `Layer::render`, `renderLayers`, per-layer MIDI state and the library target at the smallest cost. |
| **Stress before exposure** | On exercises priority, per-layer engines and cross-layer reads while only pre-release data is at risk. Anything wrong with the contract is found before a live product is touched. |
| **Risk last, within the live pair** | Tant's chain is long but local (one processor file). Clid's is distributed across lifecycles and carries the Stage 7 reversal. By then the contract has been used three times and the golden machinery has been exercised for weeks. |
| **Debt retired where cheap** | E5.1 and E5.2 (UI and persistence off the processor) are pure and valuable; they could be hoisted before E4 if sessions are available, but they stay with Clid to keep each product's change set in one place (family consistency). |

Alternatives considered. (1) *Riskiest first* (Clid before Tant): rejected, it would find contract problems in the product with the most to lose. (2) *Tant before
On*: Tant is smaller in glue and live, so the temptation is to get a "fast win" on a paying product; rejected because On is where the contract is least
certain and where a change is free. (3) *Do `1, 2` of every product first (UI hosts + param tables, all pure moves), then the render moves*: attractive
because the pure moves are the safest (golden-proved by manifest and state diffs alone) and could be done for all four before any audio change. The cost
is four products open at once and a longer time before the first end-to-end library. **Recommended as an optional variation for Tant and Clid only**:
do E4.1 and E4.2, and E5.1 and E5.2, early (they touch no audio) so the audio steps that follow are shorter, but still after E3.

### 2.7a The `useLayerRender` switch (test-steward requirement, approved)

While a stage moves a product's render chain, the old chain and `Layer::render` coexist in one binary behind a processor-level switch, **`useLayerRender`**, controlled
only by tests (default off until the stage gate). `LayerEquivalence.h` (tests.md section 3) builds two identical processors, flips the switch on one, and requires
tolerance-0 equality of master and every pre-mixer channel. The coder must provide the switch in E2.4, E4.4 and E5.3 to E5.4 (and per engine type in E3.5), and delete the old
chain **and the switch** in the following commit once the gate passes. A switch that survives a stage is a duplicated-glue defect. A test-only per-channel tap on
`ProcessorBase` is approved for the comparison.

**Where a product can be left half-done safely:** after any "resting point" listed per product. The general rule: a product is safely half-done if
(a) it has exactly one render path, (b) all gates pass at that commit, and (c) its library target either does not exist yet or passes the checker.
mu-Tronic can use a product only when its E?.last step (library target) is done, so a half-extracted product costs mu-Tronic that type and nothing else
(risk 2 of design.md). The four extractions are independent of each other after E1/E2 (no product's step depends on another's, only on the contract).

---

## 3. Safety for the live apps

### 3.1 Pre-conditions before any live-product audio path is touched (E4.0 / E5.0)

1. The golden baselines for that product recorded from the baseline commit (E0.5, E0.6) and the harness self-repeat test green on the unmodified tree twice in a row (proves determinism: if two runs
   of the *same* binary differ, nothing else can be concluded, and the case is removed or fixed first).
2. The parameter manifest and the compat corpus committed (E0.3, E0.4).
3. The previous release's installed binaries kept (their installers are the user-visible rollback), and the release tag verified to exist for the shipped
   build.
4. Release decision taken (3.3).

### 3.2 How main stays releasable

**Recommendation: incremental commits on `main` behind the gates for E1 to E4; a short-lived branch for E5 only.**

| Stage | Method | Why |
|---|---|---|
| E0, E1 | `main`, incremental | additive; no product audio path |
| E2, E3 | `main`, incremental | pre-release products; every commit gated |
| E4 (Tant) | `main`, incremental, **each commit gated** (goldens + suites + manifest) | the steps are small enough that any commit is releasable; a gate failure is reverted at once |
| E5 (Clid) | branch `extract-clid`, merged **fast-forward only** after the full gate at E5.7, with `main` merged *into* the branch weekly and any mu-Clid hotfix applied to both | the intermediate states (E5.3 to E5.6) carry two copies of the engines; they must never be on a commit that could be released |

Alternative considered: a branch for everything. Rejected: the pre-release products gain nothing from it and a long branch diverges from the build-number and
backlog churn of `main` (the build number is shared across products and rises on every full Debug build). Alternative: `main` for Clid too. Acceptable
only if every E5 commit is releasable; E5.3 to E5.6 are not, so the rule above stands.

**What a release freeze means** (proposed, for Andy to accept): while E4 or E5 is in progress, a mu-Tant or mu-Clid **release is cut only from a commit
tagged `extract-<stage>-gate`** (a commit at which the full gate passed, set at each resting point of section 2). A hotfix to the live product is made on
`main` (E4) or on `main` and merged into `extract-clid` (E5) and released from a gate-passing commit. There is no freeze on the other products' work, on the
site or on documentation. Because every E4 commit is gated, the freeze is a labelling rule, not a stop.

### 3.3 Cut a mu-Clid / mu-Tant release before the first stage that touches their audio path?

E1 does not touch their audio path (additive `ProcessorBase` members, no mixer change). E2 and E3 do not touch their product source. The first stage that changes a
live audio path is E4 (Tant) and E5 (Clid). The first release (v1.1) went live on 2026-10-15.

**Recommendation:** no extra release solely for the programme, because the shipped v1.1 tree is already tagged and is the rollback point. Instead:
(a) tag `pre-extract-all` now; (b) if any user-visible fix is pending for either live product, ship it **before E4.0 / E5.0 respectively**, from the
baseline, so the next release carries the extraction on its own (one change type per release is easier to attribute); (c) the first release that
contains an extracted live product should be a **named patch release with release notes saying nothing audible changed** and a quiet changelog, after one
full soak of the Debug and Release builds by Andy. Decision A1 below.

### 3.4 Rollback story

| Event | Action |
|---|---|
| A step fails a gate | revert that commit; no carry-over (the Stage 7 rule). |
| A product is half-done and the programme stops | stop at the last resting point (section 2); the product has one render path and passes all gates; nothing to undo. |
| A regression is found in a released extracted product | `git revert` the stage's commit range (each stage is a contiguous, labelled range tagged `extract-<stage>-start`/`-gate`); rebuild; ship a patch. Because **no saved-data change** is part of any stage (D7), rollback loses no user data, and saved files from the extracted build load in the previous release and vice versa. A gate (4.2) tests this both ways. |
| `main` is needed for something else mid-stage | the stage is on `main` only if every commit is gate-passing (E4), so nothing special; for E5 it is already on a branch. |
| Floating-point results differ only in the last bit after a legitimate re-ordering | do not accept silently: Andy decides (A2). The default is that any non-zero diff in the golden set is a failure. |

---

## 4. Verification gates

### 4.1 Sample-identical comparison (the harness is the test-steward's)

The design of record is [tests.md "Layer extraction safety net"](../../tests.md#layer-extraction-safety-net) sections 2 to 6; this plan adds only how the stages
use it. Summary of what the gate is:

1. **Golden render (tests.md section 2).** In-process `GoldenRender.h` scenarios per product (about 10 each), master plus per-layer stem fingerprints in 256-frame
   segments, MIDI-out events for mu-Clid, post-render mixer peaks, `insertGainReduction` and a `getStateInformation` hash. **Bit-identical, tolerance 0**, same
   machine class, configuration and compiler (Windows x64 Debug is the gate; Release and Linux goldens are recorded when first needed). Recorded once from the baseline
   commit, changed only by `update-golden.ps1` in a goldens-only commit signed by the `dsp-controller`; **a stage never changes a golden in the code-moving commit.**
   Block schedules: fixed 512 and a ragged one (1, 7, 63, 64, 129, 512, 1024, 3, repeated) with a block larger than prepared size; the review adds, for the
   extracted layers, three block sizes, locate / loop-back with held notes, a tempo ramp, a mid-block start, `startBeat` 1e6.
2. **Equivalence while both paths exist (tests.md section 3), the `useLayerRender` switch.** In each stage that moves a render chain, the old chain and the new
   `Layer::render` path **coexist in one binary behind a processor-level, test-controlled switch `useLayerRender` (default off until the stage gate)**; the
   test builds two identical processors, drives both with the same schedule and events, and compares master and each channel's pre-mixer output (through the
   approved test-only tap) with tolerance 0. The old chain is **deleted in the next commit after the gate passes**, and the switch with it. This is the mechanism
   that makes "no duplicated glue" reachable, and it is a **requirement on the coder for E2 (E2.4), E4 (E4.4) and E5 (E5.3 to E5.4)**; for E3 it covers each engine type.
   `LayerEquivalence.h` is kept after the stage as the mu-Tronic test of design section 7.
3. **Allocation and lock probes, two-thread stress, RT scan, denormal sweep, retire-bin and CPU tests (tests.md section 6)** run with every golden and equivalence
   run; the CPU test uses the switch to compare against the unextracted path in the same process (at or below 1.15 x).
4. **Self-containment proof (library-only host).** Per library, `mu-<product>-layer-tests` is compiled from `mu-core` + the library sources + a fixture and **no
   `Plugin/` source**: it builds a layer, calls `bindParams` and `render` with a hand-built `RenderContext`, and compares with a recorded block. A secret need for the
   processor fails to link. It becomes the mu-Tronic equivalence test.

Determinism: the seed fix (E0.2) removes the two clock-seeded generators; the harness self-repeat test (twice in-process, twice cross-process) rejects any scenario
that is still not repeatable. mu-Tant's gate-probability generator is still to be located (open check in tests.md).

### 4.2 Saved-data and parameter stability

All from tests.md section 4: the **parameter-id manifest** per product asserted after each stage (an id, range, default or type change breaks saved sessions and
**DAW automation of live plugins**; ids are what the VST3 parameter ids derive from), and the **compat corpus** (loads clean, parameter values equal, **re-save
byte-identical**, idempotent second save, layer presets across products including `.muTrack` with each engine id, populated clip banks, host sessions as DAWs hold
them). The existing `*_roundtrip` listening tests stay. **Cross-version both ways**: a state saved by the baseline loads in the new build (the corpus), and one
saved by the new build loads in the baseline (run once per product at its gate), which the rollback story relies on.

### 4.3 Existing suites and listening pipeline

- Every product suite and `mu-core-tests` at every commit that touches that product (`mu-clid-tests` built explicitly; it is not in ALL). Targeted during a
  stage, full set at each resting point and at the stage gate (Andy's test-cadence rule).
- Listening pipeline (`tests/scripts/run-listening-tests.py`, `analyse.py`, `tests/expectations/*.json`): run the whole product set at each stage gate
  (A1 to A9, T11 to T13, `CLID_*`, `TANT_*`, `TONI_*`, `ON_*`, `TS_swap`). These measure musical properties, not sample equality, so they catch a class
  of failure the golden set could mask only if both runs are wrong in the same way (e.g. baseline recorded with a bug). They also cover paths the
  golden matrix does not enumerate.
- pluginval (`tests/scripts/run-pluginval.ps1`, `-Strictness 5`, VST3) on each product at the stage gate and before any release: it fuzzes parameters at
  many sample rates and buffer sizes and does a save-restore identity check, which directly catches an un-persisted moved parameter and an allocation or
  denormal issue. Monitor and kill hung runs (known 53-minute GUI hang; use `-SkipGui` for the routine run and the GUI pass once per stage).
- `check-core-boundary.ps1`, `check-mod-targets.ps1`, `check-comment-refs.ps1`, plus the new `check-layer-boundary.ps1` (run as part of `/build`).

### 4.4 Pin first, then move (D6 discipline)

For every piece of product-global state that becomes per-layer (section 5, risk R7), the test that describes today's behaviour is written and passes **on the
unmodified tree**, and only then is the state moved; the same test must pass after the move **without being edited**. If it has to be edited, the move
changed behaviour and the change goes to Andy, not into the commit.

### 4.5 `check-layer-boundary.ps1` (new in E1)

Fails on: (1) a library file including `Plugin/` or `License/`; (2) a library file naming `PluginProcessor` or including another product's header;
(3) a duplicate relative path across `mu-core` and all registered libraries; (4) a global-namespace type in a library; (5) **"glue"**: `Plugin/*.cpp` or
`PluginProcessor*` defining a function named `render*`, `read*Config`, `applyModulation` or containing `voiceEngines`/`retiring` identifiers for a
registered type (a coarse grep that keeps D1 honest; the test-steward owns its fixture).

### 4.6 What the reviewers do at each stage

| Stage | dsp-controller | test-steward adds / confirms | architecture-steward |
|---|---|---|---|
| E0 | signs the seed fix (E0.2); reviews design fixes C1 to C4 before E1 is coded | owns the work in tests.md section 7 items 1 to 9, 11 to 13 and the harness self-test | confirms D6 pin list against design 3.5 |
| E1 | **full review**: `renderLayers` try-lock behaviour, no allocation (scratch sized in `prepareToPlay`), lock-miss silence and transport advance, `RetireBin` ordering (acquire/release on the drained flag, free on the message thread, no free under `layersLock`), `tailBlocks()==0` not parked, denormal handling in fades | `LayerRenderTests`, `RetireBinTests`, a counting-allocator test, `mu-core-tests-16` | contract vs design 2.1; scope guard |
| E2 | held-note state crossing threads (UI/MIDI vs audio), `Arp::render` allocation-free, no param-by-string in `render` | ArpHost UI test, A/B test, library-only test, Toni pins, Toni structural-edit script | boundary, naming, the "contract got wrong" list |
| E3 | engine swap while rendering (build off-lock, swap under lock, free off-thread), Rumble `peer` lifetime, priority-1 reading scratch, per-layer sequencer vs block-boundary triggering sample offsets | per-engine golden, `GrooveVoices` tests rewritten, Rumble silence cases (empty / priority-1 / removed source) | per-layer StepRow shape, `engineLocked` |
| E4 | `Pattern::render` allocation, the layersLock/bankLock acquire order, Note-mode ramp, 64-beat wrap equivalence, `RetireBin` replacing `retiring[]` | Note-mode pin and golden, count-reducing swap golden, structural-edit script | PatternHost, `SwapBoundary.h` placement |
| E5 | trigger-to-render ordering within a block (sample-accurate trigger offsets), `orphanOffs` ownership on remove, retire tail drain/cleanup order, sample I/O never on the audio thread (`preloadStaged`), `SwapCommitTimingTests` unchanged | the whole Stage 7 proof list, `mu-clid-midi-tests` | `SampleRhythm` vs `Rhythm`, stager decision, `mu-clid-layer` contents |
| E6 | final pass over the combined contract | combined link test, catalogue and status in `tests.md` | docs reconciled |

Reviewers' findings go on the existing backlog rows (not new rows), per the standing rule.

---

## 5. Risk register

| # | Risk | Likelihood / impact | Mitigation |
|---|---|---|---|
| R1 | **Audio-thread allocation or lock** introduced by the contract (scratch buffers, bin, per-layer engine construction) | M / H | scratch sized in `prepareToPlay`; engines built **outside** `layersLock` and swapped by pointer; counting-allocator test in E1; dsp-controller at E1, E3, E4, E5; pluginval |
| R2 | **Hot-swap and clips** interact with the new path: `loopBeats()` semantics differ from the processor's per-product boundary reference (Tant `blkPatternBeats`, Toni/On one bar, Clid master loop and swap mode), and a clip launch is a staged swap | M / H | `loopBeats()` returns exactly today's number per type, pinned by `SwapBoundary`/`SwapCommitTimingTests`/`HotSwapBoundaryTests`; goldens for `--swap-*` at all three timings; the clip tests per product; `HotSwapStager` kept for Clid (E5.6) |
| R3 | **Modulation matrix ownership**: `ModulationMatrix` is "the single reader"; moving the resolve into `Layer::render` must not add a second reader or change the beat the control sequences see | M / H | the resolve moves **as is** into `bindParams`/`render`; `check-mod-targets.ps1`; `ModulationTests`/`ModArcTests` per product; Tant's 64-beat wrap equivalence (E4.4) with a > 64-beat golden; mu-Clid's previous-state flags move together with the resolve (E5.4a) |
| R4 | **Parameter id / range / default stability** for saved presets and **DAW automation of live plugins** | L / H (live) | manifest and state-bytes goldens (4.2) at every stage; no `ParameterID` literal is edited in a move commit (the checker is the diff itself); pluginval; cross-version load test |
| R5 | **UI content classes take `PluginProcessor&`** (Toni `ArpContent`, On `TrackContent`/`StepRows`, Tant `PatternContent` with 23+ `proc.apvts` uses and `PluginProcessor::` id helpers, Clid `RhythmContent`/`VoiceSection`/`MidiOutSection`/`SampleBrowser`/`RhythmMiniVisual`) | H / M | `<Type>Host` interfaces first (steps E?.2 / E5.1), pure refactor, compile + UI tests, screenshot of the standalone (a copy, per the agent hygiene rule) compared with the baseline by `ux-controller` |
| R6 | **Engine into layer reverses Stage 7** (Clid: 1,580 references, retire tails, orphan note-offs, stager) | H / H | Clid last; derived `SampleRhythm` (core `Rhythm` and mu-Clid MIDI untouched); branch with fast-forward merge only; Stage 7 proof list in full; pure steps (E5.1, E5.2) first; revert-don't-carry |
| R7 | **Product-global state that must become per-layer** | M / H | pin-first discipline (4.4): **Tant** Note mode (`midiNoteMode`, `heldNotes`, final-mix `noteGateGain` ramp; the gate acts after the mixer today, so a per-layer ramp is a different point in the signal path; see decision A3); **Toni** held-note stack, `ArpContext`, MIDI-trigger mode, `midiInChParam`; **On** `StepPattern` grid, `GrooveSequencer` clock and phase, global `seq_swing`/`seq_accent` params (saved as globals, seed each track), the five-engine pool; **Clid** master loop and swap timing (stay global) |
| R8 | **Rumble reads another layer's audio** (cross-layer read, scratch lifetime, order) | M / M | priority-1 after priority-0; `peer(slot)` read-only; silence for empty/priority-1/removed source; tests of those three; the source slot index is remapped by `layersReordered` |
| R9 | **Include collisions** when four `Source/` roots share one include path (`Audio/Scales.h`; relative paths unique) | M / M | rename `ToniScales.h` (E2.6), `TantScales.h` (E4.7); checker rule (3); the combined link test (E6) |
| R10 | **`RetireBin` changes audible behaviour** (a product that today cuts a removed voice would start fading it, or the reverse) | M / M | `tailBlocks()==0` means not parked; per product the tail length reproduces today's (Tant fade, Clid engine tail, Toni/On none); structural-edit script goldens |
| R11 | **Floating-point differences** from re-ordered operations (rendering all layers first then mixing) | L / M | per-layer arithmetic is unchanged (the mixer already treats channels independently; copying to scratch adds no arithmetic); 0-difference pass criterion; the `useLayerRender` equivalence test catches it at the commit that introduced it |
| R12 | **Determinism of the harness itself** (timers, thread timing, uninitialised memory) | M / M | harness self-repeat test (E0.5) and the seed fix (E0.2); deterministic scripted blocks; a flaky case is removed, not tolerated |
| R13 | **Schedule risk on one developer**: the programme blocks mu-Tronic and other work for weeks | H / M | resting points (section 2); each product independent after E1/E2; mu-Tronic starts as soon as Toni and On are done (section 6) |
| R14 | **Build-number and backlog churn on `main`** during a branch (Clid) | L / L | merge `main` into the branch weekly; the build number is a generated file, never edited by hand |
| R15 | **Test-tool blind spots**: golden and equivalence tests prove equality to the baseline, not correctness of the baseline | L / M | listening-test expectations (musical assertions) still run; baseline recorded only after the unmodified tree passes everything |

---

## 6. Schedule and milestones

### 6.1 Units

**A "session"** here = one focused working session of Claude with Andy around (or an unattended round that ends in a gated commit): about half a day of
wall time including builds, test runs and reviewer rounds. **Day** ~ 2 sessions. **Week** ~ 5 to 8 sessions at Andy's current pace, longer if the full
Debug build is slow or reviews bounce. Estimates are in sessions with a range; the uncertainty is real and **skewed upward** (Clid especially), because the
hard part of each stage is not the code that moves but the diff that does not come out zero. Treat the central number as "likely" and the upper as "plan for it".

### 6.2 Estimate

| Stage | Size | Sessions (range, central) | Calendar at ~6 sessions/week |
|---|---|---|---|
| E0 Safety net, seed fix, design fixes | M | 8-12 (10) | 1.5 to 2 weeks if serial; about 1 week on the critical path, because only tests.md items 1 to 6 (about 6 working days) must precede code and the rest overlaps E1 |
| E1 Contract | M | 4-6 (5) | 0.9 week (heavier than first estimated: C-list gates, time model, N-deep bin, CPU plan) |
| E2 Toni | M | 4-6 (5) | 0.9 week (the runner is reworked once onto the C4 time model) |
| E3 On | L | 6-10 (8) | 1.4 weeks |
| E4 Tant | L | 6-9 (7.5) | 1.25 weeks |
| E5 Clid | XL | 10-16 (12) | 2 weeks |
| E6 Close-out | S | 1-2 (1.5) | 0.25 week |
| **Total** | | **39-61 (about 49)** | **7 to 11 weeks serial, likely 8 to 9; about 1 week less where test work overlaps coding** |

**Honest uncertainty.** The first estimate (29 to 46 sessions) is superseded: the real-time review added contract requirements (C1 to C18) and the test
review added about 12 to 13 working days of safety-net work that the first draft treated as 1 to 2 sessions. The upper bound is "plan for it" (a session
that ends in a non-zero golden diff costs the next session). **Review grade:** use the strongest available reviewer (Opus-grade) for the contract (E0d and E1) and
for the mu-Clid extraction (E5); the middle stages can use the normal review. A reviewer's missed finding in the contract is repaid four times, once per product.

Reviewer rounds are inside these numbers. What is **a day or less**: E0.1, E0.2, E0.3, E1's limits and ids, E2.1/E2.2/E2.6, E3.1/E3.2, E4.1/E4.2, E5.1.
What is **a week or more**: E5 as a whole, and E3.5/E3.6 and E5.4 as blocks (each is the equivalent of two to three sessions of tightly coupled changes that
cannot be split).

### 6.3 Milestones and what they unblock

| Milestone | After | Meaning | Unblocks (mu-Tronic stages) |
|---|---|---|---|
| **M0** | E0 | Safety net exists (harness, goldens, corpus, manifests, probes), design fixes C1 to C4 signed; no code moved. | nothing visible |
| **M1** | E1 | Contract proven on a fake type; checker live. | **T1 (empty unit) may start**, see 6.4 |
| **M2** | E2 | Toni is a library; contract validated by a real type. | T1 can register `Arp`; the first real layer type exists |
| **M3** | E3 | The five mu-On types exist. | **mu-Tronic becomes useful** (Arp + Kick/Bass/Hat/Snare/Rumble); T8a can start; T1 + T2 usable |
| **M4** | E4 | Pattern exists. | Pattern registered; T8b Tant importer |
| **M5** | E5 | Rhythm exists. | all nine types; T8b Clid importer; T9 release decision: one release with everything |
| **M6** | E6 | Docs, combined link test, boundary checker in `/build`. | T9 |

### 6.4 When can mu-Tronic T1 (the empty unit) start, and can it run in parallel?

T1 needs only `MU_MAX_LAYERS` (E1) and the sidebar/mixer paging (core UI); it has no dependency on any product extraction, and it changes none of the four
products (it does touch `LayerSidebar`/`MixerOverlay` in core for paging, which the four products also use, so it needs the same golden/UI gates). So it **can**
run in parallel in the sense that it is independent of E2 to E5. But it is **not recommended before M2**: Andy has said mu-Tronic is not worth doing without
voices, a single developer cannot truly parallelise, and starting T1 at M2 costs a few sessions of delay in exchange for registering the first real type at the
same time, which tests the T1 shell with a real layer, not with Empty only. **Recommendation: start T1 after M2 (about sessions 20 to 25, week 3 to 4), interleaved with E3 only
when E3 is blocked** (a long build, a reviewer round); T1's own code must not touch anything E3 has open.

Stages with a different constraint:

| mu-Tronic stage | Earliest | Blocked by |
|---|---|---|
| T1 | M1 (recommended M2) | E1 |
| T2 buses (`mu-core` mixer) | any time after M1, **but** it edits `MixerEngine` audio code that all four products render through, so **do not run it during E4 or E5** (confounds golden failures). Recommended slot: after M3 or after M5. A bus golden for mu-Tronic and byte-identical for the four apps (buses = 0). | E1 only for technical dependency |
| `setLayerType`, `LayerServices`, registry list (design T3 remainder) | with T1 | E1 |
| T8a (scenes, pages, MIDI-in per layer, type-carrying clips) | M3 | E2, E3 + T1 |
| T8b importers | each with its type: Toni and On at T8a, Tant at M4, Clid at M5 | E4, E5 |
| T9 release | M5 + T1/T2/T8a (or earlier with Arp + On types, Andy's open question 1) | |

### 6.5 Suggested cadence

Weeks 1-2: E0, E1, E2 (M2). Weeks 3-4: E3 (M3), T1 interleaved. Weeks 4-5: E4 (M4). Weeks 5-7: E5 (M5). Week 7-8: E6 (M6), T2. mu-Tronic
becomes usable at M3 (week ~4) with 6 layer types; at M5 with all 9. If the schedule slips the order does not need to change: stop at any milestone and
the product set is consistent.

---

## 7. Decisions needed from Andy

| # | Decision | Options | Recommendation |
|---|---|---|---|
| **A1** | Release before extracting the live apps? | (a) none, the shipped v1.1 tag is the rollback; (b) ship any pending fix for mu-Clid/mu-Tant first from baseline; (c) cut a mu-Clid and mu-Tant release purely as a clean baseline before E4/E5. | (b): ship pending fixes before E4.0 / E5.0 if there are any; otherwise (a). The first release containing extracted live code is a named patch release with "nothing audible changed" notes after Andy's own soak. |
| **A2** | Pass criterion | (a) 0 differing samples at 24-bit; (b) <= 1 LSB (the existing roundtrip tolerance). | (a). A non-zero diff goes to Andy with the cause; the programme does not absorb it silently. |
| **A3** | **Tant Note mode (decision for Andy, live product).** Today the Note-mode gate multiplies the **final mix**, so it also gates the FX return (delay and reverb tails stop with the note) and the sum of any second layer. Moving it per layer (design 3.5) gates only that layer's own signal before its strip: **with FX tails or a second layer the final mix changes**, so "nothing audible changed" (D7) would no longer hold for Note-mode sessions. | (a) **Preserve the old behaviour exactly**: the final-mix gate stays as a mu-Tant shell function (one documented exception to D3 for one function, the library layer has a per-layer gate that the standalone leaves off), final mix bit-identical, the live product unchanged; (b) **accept the change**: per-layer gate everywhere, a named behaviour change in release notes, golden updated by `update-golden.ps1` with Andy's sign-off, which affects existing Note-mode sessions (FX tails now ring on after the note ends). | (a) for mu-Tant's own app (a paying product, "nothing audible changed" is the promise of this programme), with (b) available later as a deliberate change. The E0.6 Note-mode pin (stem and with-FX final mix) decides how large the difference is before this is final. |
| **A4** | Branch strategy for Clid | `main` incremental vs short-lived branch with fast-forward merge | branch for E5 only (3.2). |
| **A5** | Is the "release freeze" rule acceptable? | release only from a gate-tagged commit while E4/E5 are in progress | yes. |
| **A6** | T1 timing | after M1 vs after M2 vs after M3 | after M2, interleaved (6.4). |
| **A7** | T2 (buses) slot | after M3 vs after M5 | after M5 (keeps E4/E5 diagnosis clean); after M3 if Andy wants buses in early mu-Tronic use. |
| **A8** | Replace mu-Clid's `HotSwapStager`? | keep for the standalone (Stage 7 step 5 stands) vs fold into `layerSwapper` | keep; revisit only after a release of the extracted Clid. |
| **A9** | Scope of "completely self contained": do the wavetable bank and `SampleLibrary` become `LayerServices` now, or are they handed through the host interface and unified at T1? | now vs at T1 | at T1 (keeps E2 to E5 about the layer, not about services; both standalones keep their own). |
| **A11** | **Seed fix (E0.2), FYI/confirm.** Fixing the seed of mu-Tant `NoiseGen` and mu-Toni `ToniVoice::rng` makes noise identical on every note and run instead of different each time. Approved by the main conversation as Andy's delegate so goldens can be repeatable. | confirm; or seed per layer slot (differs per layer, still repeatable) | per-slot seed if the pin shows no problem, otherwise one fixed seed; dsp-controller signs. |
| **A10** | Variation: do the pure steps for both live products early (E4.1, E4.2, E5.1, E5.2 right after M3) | yes / no | yes, optional, it shortens the audio steps and has no audible risk. |

---

## Review notes merged

Both reviews are folded into the body (2026-10-15); this is the index.

- **dsp-controller (design: pass with notes).** Four items fixed before contract code: C1 (no suspend / no muting for type changes and scenes, atomic
  publication), C2 and C3 (`RetireBin` state machine, retiree snapshot, services outlive retirees), C7 (scene commit as block-start publication), C4 (time
  model in `RenderContext`, `onTransportJump`, no free-running phase). Secondary items C5, C6, C8 to C18 are in section 2.1a with the stage that tests them.
  Per-stage RT gates are in section 2.1a and 4.6; the CPU plan is C18. design.md section 2.1 points to section 2.1a here; the full text moves into design.md when E0d is signed.
- **test-steward.** The safety net is tests.md "Layer extraction safety net"; this plan sequences it as E0.1 to E0.9, adopts `useLayerRender` (2.7a),
  the approved test-only channel tap, and the seed fix; dropped the earlier WAV/SHA matrix. About 12 to 13 working days of test work, about 6 before any
  code moves. Open: mu-Tant gate-probability generator location.
- **architecture-steward re-check:** pending reviewers' sign-off on the merged text (Pass or Not yet).
