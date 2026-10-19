# Layer extraction programme — plan (2026-10-15)

**Status:** plan only. No source, backlog or CLAUDE.md change is made by this document. Andy's answers to the section 7 decisions are recorded (2026-10-15), **including A14 (licence state and engine loading, sections 8 and 9): decided 2026-10-15, static layer libraries, runtime engine modules not adopted.** No decision is open. Written by the Architecture Steward after Andy re-prioritised
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
| E0.2 | **Seed fix, its own tiny commit** (approved by the main conversation as Andy's delegate; `dsp-controller` signs): fixed seed **per layer slot** (Andy, A11) for mu-Tant `NoiseGen` and mu-Toni `ToniVoice::rng` (both clock-seeded `juce::Random` today, so noise renders cannot repeat). This is the only live-product source touch before the contract exists; it changes noise from non-repeatable to repeatable and nothing else. Goldens are recorded **after** it. | #15 | all suites |
| E0.3 | **Parameter-id manifest** per product with frozen dumps (`ch{N}_`, `mstr_` and per-layer ids included). | #1 | per-product suites |
| E0.4 | **Compat corpus** collected from the baseline, `MANIFEST.txt` of hashes; one parameterised test per product: loads clean, values equal, **re-save byte-identical**, idempotent second save, cross-product layer presets. | #2 | per-product suites |
| E0.5 | **`GoldenRender.h` + `update-golden.ps1`**: scenario loader, FNV-1a fingerprint per 256-frame segment of master and per-layer stems, equality rule (bit-identical, tolerance 0, same machine class/config/compiler, separate files per config), self-repeat test (twice in-process, twice cross-process), diff dump to `tests/_out/golden/`. Goldens live in git under `tests/golden/<product>/<scenario>/`; only `update-golden.ps1` changes them, never in a code-moving commit. Per-product `GoldenRenderTests.cpp`. | #3 | harness self-test |
| E0.6 | **First goldens: mu-Toni then mu-On** (about 10 scenarios each); then **mu-Tant and mu-Clid** (recorded now while the baseline is clean, used at E4/E5); the pins that describe behaviour a stage will move (mu-On per-engine tests, global swing/accent, Rumble source read and bass-to-kick sidechain; mu-Toni `readVoice`, held notes, root-by-MIDI, MIDI-trigger; mu-Tant Note-mode pin for stem and with-FX final mix). **Contract pins from the `dsp-controller`'s C1 to C4 review (design 2.1.6), recorded on the unmodified tree:** **P1** mu-Tant: shrink then regrow within 25 ms, shrink-regrow-shrink, and two count-reducing swaps back to back dropping the same voice (E4.6); **P2** mu-Clid: blocks rendered after `isFullyDrained()` at the harness's deferred-work rate, and their peak (E5.4d); **P3** mu-Clid: a count-reducing full preset while tails ring, then a regrow, and `removeLayer` while tails ring (count policy, E5); **P4** the longest retired tail in the golden set, `ampRelToEnd` included (30 s cap); **P5** mu-On: the stop edge with `applyParams` before `reset`, and a 0-frame stop block (E3); **P6** a structural edit mid-play in mu-Tant, mu-On and mu-Clid, recording note state after the missed blocks (no note release; the R5 false-jump fix); **P7** an oversize-block segment per product (re-recorded at adoption in a signed goldens-only commit). **Measurements:** **M1** block-to-block host position deltas under fast tempo automation in Bitwig and Reaper and under MIDI clock with the sync-offset ramp (before T1, jump tolerance); **M2** Debug pluginval for VST3, CLAP and Standalone with the `AudioEpoch` parity assert, plus an offline bounce while playing in Reaper and Bitwig (before E4). | #4, #5, #7, #8, #9; P1 to P7, M1, M2 to be catalogued | per-product suites |
| E0.7 | **`AllocationProbe.h`** wired into every golden run (zero allocations and zero frees inside `processBlock`, `Layer::render`, `renderLayers`, including the first block after a swap, type change and stop). | #6 | golden runs |
| E0.8 | **Mixer block-value test** in `mu-core-tests` (exact level, pan, sends, sidechain, mute/solo through `MixerEngine`; pins the sidechain tap order, C13); generalised **lock probe and two-thread stress**; `check-rt-safety.ps1` report-only; `check-layer-boundary.ps1` fixture. | #11, #12, #13 | core suites |
| E0.9 | **Approved test accessors:** one test-only per-channel tap on `ProcessorBase` (what each channel hands the mixer; fallback: soloed stems), and the **`useLayerRender` switch** (section 2.7a), both approved by the main conversation. `LayerEquivalence.h` follows once E1 exists. | #10 | with E1 |
| E0d | **Design fixes C1 to C4** (section 2.1a) written into [design.md](design.md) section 2.1 and signed by the `dsp-controller` before E1 is coded; the secondary requirements C5 to C18 are in the same table and are tested at the stage named there. **Written 2026-10-15** (design.md 2.1.1 to 2.1.6). Round 1 (2026-10-15): C1 and C3 Pass with notes, C2 and C4 Not yet (R1 to R7), structure kept; revised the same day with R1 to R7, N1 to N7, the accepted amendments and the twelve open-point rulings (design 2.1.6). Round 2 (2026-10-15): C1, C2 Pass with notes, C3 Pass, C4 Not yet (F1: the continuity delta reduced with `std::round`, not `wrapBeat`; the R5 stop rule as one line), both folded in with the round-2 binding notes. **`dsp-controller` sign-off: C1-C3 signed (round 2, with binding notes); C4 accepted once F1 and the R5 rule line are in the doc (dsp-controller: no further round).** E1 may start on C1 to C3. | | docs |

E0 is **not optional**: without E0.5 to E0.7 "sample-identical" cannot be asserted, because the old path is gone after each stage. The CPU measurement
plan (C18, section 4.3) is also written here, so budgets are ratified from numbers rather than guessed.

### 2.1a Contract requirements from the real-time review (C1 to C18)

Added to design.md section 2.1 on 2026-10-15. They are requirements on the contract, not on a product (numbered C, not R, to keep them apart from the risk
register of section 5). Two of them (C1, C7) concern features only mu-Tronic uses (type change, scenes); they are **specified now** because the shape of
`renderLayers` and `RetireBin` must not have to change when they arrive.

| # | Requirement | Stage / test |
|---|---|---|
| **C1** | **Type changes and scene commits must not use `ScopedSuspend` / `layerEditInFlight` and must not mute other layers.** Build, prepare, default-parameter and `bindParams` the new layer first; publish it by **one atomic pointer store**. The audio thread loads an immutable 16-entry layer view **once per block** (`renderLayers` reads layer pointers through one accessor, so replacing the try-lock by the published array later is a one-function change); `layersLock` stays off that render path or has a tested bounded hold. Parameter values are never moved after publication. The existing structural edits (add/remove/swap/reset) of the four products keep today's protocol until a product asks otherwise. | **specified: [design.md 2.1.2](design.md#212-c1-layer-access-and-non-muting-publication)** (2026-10-15; round 2 adds the prepare generation N1, `bool waitFor(s, timeoutMs)` with per-caller fallbacks N2, the overflow view N3, read-modify-write parity N4, no layer read outside `LayerBlock` N5); accessor shape and `AudioEpoch` E1; implemented with `setLayerType` (T1 stage); stress test |
| **C2** | **`RetireBin` is a proven state machine.** Per-slot states Empty, Parked, Draining, Done with release/acquire transitions; the message thread frees only after seeing Done **and** one audio-block epoch has passed; capacity N entries per slot or a global ring of 16 to 32 (two retargets within 25 ms must not drop, free early or block); the gain ramp to zero is a fixed number of samples rescaled at `prepare`, plus a **hard maximum lifetime** (reverb and comb tails); flushed at `prepareToPlay` and `releaseResources`. | **specified: [design.md 2.1.3](design.md#213-c2-the-retirebin)** (2026-10-15; round 2 adds the in-place rules R1, `binMutex` R2, the per-product count policy R3, the packed count-and-serial word R4); E1 (`RetireBinTests`, transitions checked exhaustively, N-deep, R1 to R4 cases) |
| **C3** | **A retiree reads no APVTS and no matrix.** `Layer::beginRetire()` snapshots everything the render path reads (a Kick retired and a Bass created in the same slot share the `t{N}_` prefix, so a live read would mix them). `LayerServices` (wavetable bank, `SampleLibrary`) **outlive every layer including retirees**. | **specified: [design.md 2.1.4](design.md#214-c3-frozen-retire-snapshots)** (2026-10-15; the twin test scrambles every APVTS parameter and compares the tail before the strip, N6); E1 test with the same-prefix case; services at T1 |
| **C4** | **The time model is in `RenderContext` before any product moves** (otherwise Toni's runner clock is reworked twice): POD with `sampleRate`, **actual** `blockSize` (0 and 1 allowed), `playing`, `bpm`, unwrapped `double startBeat`, `beatsPerSample`, `justStarted`, `justStopped`, `transportJumped`, a **filtered MIDI view (no copy)**, `slot`, `peer()`. Layers derive event positions from the absolute beat; their state is limited to the last-evaluated beat and beat-domain pending note-offs; **no free-running phase**. New `onTransportJump()` beside `onTransportStopped()`. Tempo changes between blocks stay phase-continuous. | **specified: [design.md 2.1.5](design.md#215-c4-the-time-model-and-rendercontext)** (2026-10-15; amends "beat-domain pending note-offs" and finds that the Toni runner needs no clock rework, see there; round 2 adds host-block continuity with a sticky `jumpPending` R5, `wrapBeat` and negative beats R6, the tail's MIDI sink R7, explicit padding); struct E1; first customer E2 |
| C5 | The shared wavetable bank is published as an **immutable atomic array** or each layer keeps a per-layer fallback; a try-lock miss is **never silence**. | E2 (Toni), E4 (Tant bank lock); T1 for the shared bank |
| C6 | No reference-counted last release on the audio thread (no `shared_ptr` final release in `render` or the bin). | E1 review, `check-rt-safety.ps1` |
| C7 | **Scene commit is pointer publication at a block start**, new state fully prebuilt at stage time (the stager's flag is atomic, but `commit()` runs on the message thread at about 100 Hz, so layers would land up to about 10 ms apart). The test measures the first-event audio position per layer. | T8a (mu-Tronic) |
| C8 | The base **chunks oversize blocks** (in the block driver around `renderLayers` and the mixer, design 2.1.1) (`GrooveVoices::renderAll` truncates silently today, a defect); pinned first (P7), fixed by the shared path; the oversize golden segment is re-recorded in a goldens-only commit the `dsp-controller` signs and stays in the tolerance-0 gate (design 2.1.6, point 10). | E1 (chunking), E3 test |
| C9 | `bindParams` pointers are **rebound on `onMoved`**, and the Rumble source index is remapped generically; layers cache atomics and **never call `getRawParameterValue` in `render`**. | E1 base, E2 to E5 |
| C10 | The `peer()` tap point is **post-insert, pre-strip**. | E1 doc and test, E3 |
| C11 | `ScopedNoDenormals` is owned by the base's render path (by `LayerBlock`, so the mixer and FX are covered too; design 2.1.1), **and** each product keeps its own at the top of `processBlock` (N7: mu-Tant's Note-mode gate runs outside `LayerBlock`). | E1 |
| C12 | **Hold-last-value on a `modLock` miss** (a missed matrix lock never snaps a parameter to default). | E2 to E5, per-product test |
| C13 | **Sidechain tap defined**: pre-duck, pre-fader snapshot, or written index order (mutual ducking is order-dependent today); the chosen rule is pinned by E0.8's mixer test before any change. | E0.8 pin; **E1 kept today's written index order unchanged (2026-10-16); decided before any change to the mixer's channel loop, at the latest T2** |
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
| `mu-core/Sequencer/Layer.h` | New virtuals, all with do-nothing defaults (exact list and threads: design 2.1): `typeId`, `prepare`, `bindParams(apvts, slot)`, `render(const RenderContext&, AudioBuffer<float>&, int)`, `renderPriority`, `loopBeats`, `preloadStaged`, `onTransportStopped(ctx)`, `onTransportJump(ctx)` (C4); `Layer` also derives from `mu_core::RetireTail` (`retirePolicy`, `beginRetire`, `onRetireStart`, `renderRetiring`; C2, C3), which replaces the earlier `tailBlocks`. `Layer` stays a copyable value; **engines are held in `SlotBound` / `unique_ptr` members that a copy leaves alone**, as `Arp::insert` and `Pattern::insert` already are. |
| `mu-core/Sequencer/RenderContext.h` (new) | The POD of [design 2.1.5](design.md#215-c4-the-time-model-and-rendercontext) (struct layout there): `structSize`/`version`, `sampleRate`, actual `blockSize` (host blocks of 0 are not rendered, longer ones are chunked), `slot`, `playing`, `bpm`, `startBeat` (never wrapped by the contract; `beatWrap` says whether the host bounds it), `beatsPerSample`, `callBeats`, `justStarted`, `justStopped`, `transportJumped`, `retiring`, `frozen`, a `MidiView` (POD events decoded once per block, channel mask, no copy per layer), a `MidiSink`, `peer(slot)` over a `PeerTap` table (post-insert, pre-strip, C10). **Settled before E2;** design 2.1.5 finds that Toni's runner clock needs no rework at all (its loop mode is already beat-derived and its frame counts are note-local). Also an E1 requirement (A14, section 9.6): the struct is exactly POD and mirrors a C struct (the MIDI view is pointer + count, no JUCE type), layer state in and out is text, and `render` makes no host callback. |
| `mu-core/Plugin/ProcessorBase.{h,cpp}`, `Plugin/ProcessorBase_Render.cpp`, `Plugin/LayerBlock.h`, `Plugin/AudioEpoch.h`, `Plugin/RetireBin.h`, `Sequencer/RetireTail.h` (new) | As [design 2.1.1 to 2.1.3](design.md#211-the-shape-of-one-block-base-driver): the block driver `renderAndMix` (a `LayerBlock` owning `ScopedNoDenormals`, C11; one MIDI decode; chunking of oversize blocks around `renderLayers` **and** the mixer, C8); `renderLayers` (edges and transport hooks, priority-0 then peers then priority-1 into per-slot scratch allocated in `prepareToPlay`, then the bin, then the loop-length cache), the mixer's `RenderChannelFn` a copy from scratch. Layer access through **one accessor**, `LayerBlock`, in `TryLock` mode (today's `layersLock` try-lock and `layerEditInFlight` check, unchanged) for the four products; the `Published` mode (immutable view, `AudioEpoch` grace period) is specified now and built with `setLayerType` (C1). `AudioEpoch` is coded in E1 as design 2.1.2 (read-modify-write `enter`/`leave` with the parity check and the sticky reclaim-off flag, lock-free `static_assert`, `bool waitFor(s, timeoutMs)`, Debug self-wait assert; N2, N4); `renderAndMix` is `noexcept`; the host-block continuity check with the sticky `jumpPending` and the tolerance with its tempo term (R5); `mu_core::wrapBeat` replaces the resolver's lambda (R6). `RetireBin` as the C2 state machine: per-slot rows of 4 (mu-Clid's depth), Empty/Parked/Draining/Done, seq_cst state, free after Done plus the grace period, activation serial for the same-block hand-over through **one packed count-and-serial word** in `ProcessorCore` that every count store uses, `applyComposedState` included, advanced by a compare-exchange loop with `nextSerial()` and its store inside one writer exclusion (`layoutWriteLock` in `TryLock` mode; R4, risk R16), lock order `layoutWriteLock`, `layersLock`, `binMutex`, phase 4 rendering rows below `mixCount` only, owned and in-place entries with the in-place rules (R1), `binMutex` serialising every message-side operation in both modes (R2), the `CountPolicy` (R3), entry-0 eviction that drops the incoming tail on a timeout, hard lifetime, flushed at prepare/release; `beginRetire`/`onRetireStart` snapshots (C3); tail contexts with an empty MIDI view and a sink from the note-off reserve (R7). A layer whose `retirePolicy()` is `None` is **not** parked, which keeps today's behaviour for products that drop the voice at once (mu-Toni, mu-On). |
| `tests/scripts/check-layer-boundary.ps1` (new) + `test-check-layer-boundary.ps1` fixture | Rules of 4.5; wired into `/build` beside `check-core-boundary.ps1`. |
| `mu-core/Tests/LayerRenderTests.cpp`, `RetireBinTests.cpp` | A fake heavy layer type: the C-list gates (allocation counter, stress thread, N-deep retire, `onMoved` rebinding, service lifetime, denormal cost, chunking), priority order, empty skipped, lock miss renders silence but transport and boundary detection still advance, retire fade then free off the audio thread, no allocation under the lock (counting allocator), Rumble-style `peer` read of a priority-0 layer; and from the round-1 review: no `transportJumped` on the block after a missed lock or a `layerEditInFlight` block in continuous play, a locate inside an unrendered block still delivered once, the continuity reduction at `beatWrap = 64` for deltas +-1e-4, +-1e-4 +- 64 and +-64 (no jump, F1), and the three stop sequences of design 2.1.5 (R5); the fake layer at `startBeat` -4 with and without `beatWrap = 64` (R6); a tail emitting note-offs through its sink (R7); the R1 to R4 bin cases of design 2.1.3; the `AudioEpoch` parity, timeout and self-wait cases (N2, N4); `RenderContext` `offsetof` freeze with no implicit padding. |

What E1 deliberately does **not** do: no change to `MixerEngine` (no `Phase 3b`, `MaxChannels` untouched), no `setLayerType`, no `LayerServices`, no
registry list, no change to any product source. The only product-visible change is that `ProcessorBase` has new unused members.

**As built (2026-10-16).** E1 is on branch `extraction`; the `architecture-steward`'s verdicts on where it differs from the text (eleven points, all accepted, one of them as a deferral)
and the two code changes owed before E2 (`RetireBin.h` from `Sequencer/` to `Plugin/`; the beat bound carried in `BlockTransport` instead of a second
`renderAndMix` argument) are in [design 2.1.7](design.md#217-e1-as-built-the-architecture-stewards-rulings-2026-10-16). The one `MixerEngine` change is the
test-only channel tap approved at E0.9. The conversion items each product owes at adoption are listed there and in the stage rows below.

**Conversion note: structural edits can silence the audio in an edit storm.** The layer-edit protocol (`layerEditInFlight`) that `TryLock` mode keeps from today
makes structural edits (add, remove, swap, reset) silence the whole audio for up to hundreds of blocks in a back-to-back edit storm. This is today's protocol,
accepted for E1 to E5; revisit it at T1 (non-muting publication, C1).

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
| E2.1 | Parameter table out of the processor: `Plugin/LayerParamIds.h` to `Persistence/LayerParamIds.h`; `addLayerParams(layout, slot)` and the global part split from `createParameterLayout` into `Persistence/ArpParams.{h,cpp}`. Pure move. The constructor's `numLayers.store` becomes `storeLayerCount` (design 2.1.7). | `PluginProcessor.cpp`, `LayerParamIds.h`, `ApvtsLayoutTests` | manifest diff empty |
| E2.2 | `ArpHost` interface in `UI/` (`getArp(i)`, `getModLive`, bank access, the few calls `ArpContent` makes today); `ArpContent` takes `ProcessorBase&` + `ArpHost&`; `PluginProcessor` implements `ArpHost`. | `UI/ArpContent.{h,cpp}`, `PluginProcessor.h` | editor builds; UI tests |
| E2.3 | **Held notes and MIDI channel become layer state** (D6): `ArpContext`, held-note stack, noteOnEdge into `Arp`; the MIDI-in channel read through the shared per-layer setting. Defaults (Omni) reproduce today. The E0.8 pins must pass unchanged. | `Arp.h`, `ArpVoiceRunner.h`, `PluginProcessor.cpp` | pins + golden (MIDI-script cases) |
| E2.4 | `Arp::bindParams` (the `vp` pointer table and `modDestAtoms`), `Arp::render` (= `readVoice` + runner update + `runner.render` + insert; `modLive` lives in `Arp`). `renderChannelCb` becomes a trivial adapter calling the layer; the processor still builds `ArpContext` once per block. Both old callers and the new method exist for **this commit only**; the `useLayerRender` equivalence test (4.1, section 2.7a) proves them equal, then the old code is deleted in the next commit. | `Arp.h`, `Sequencer/Arp.cpp` (new), `PluginProcessor.cpp` | equivalence test, golden |
| E2.5 | Delete the processor-side copies. `processBlock` = transport, `RenderContext`, `renderLayers`, `processCoreBlock`, `flagSwapBoundaries(loopBeats)`. `Arp::loopBeats()` replaces the bar-line assumption. The P7 oversize segment is re-recorded in a signed goldens-only commit (design 2.1.7). | `PluginProcessor.cpp/.h` | goldens, `TONI_swap`, `TONI_slot_swap`, `TONI_host_lock` listening tests |
| E2.6 | Library: rename `Audio/Scales.h` to `ToniScales.h` (mu-Toni is pre-release, so the rename is free now); `mu-toni-layer` INTERFACE target in `mu-toni/CMakeLists.txt`; `mu-toni` and `mu-toni-tests` link it; add the library-only test target. `check-layer-boundary.ps1` passes with Toni registered. | CMake, `Audio/ToniScales.h` | checker; all suites |

**Resting points:** safe after E2.1, E2.2, E2.3, E2.5 and E2.6. Not safe to leave in E2.4 (two code paths).

**As built (2026-10-16).** E2.1 to E2.6 are on branch `e2-toni` (with a contract commit before E2.3, a C12 hold-last-value fix and the P7 oversize goldens). The `architecture-steward`'s verdicts are in [design 2.1.8](design.md#218-e2-as-built-mu-toni-on-the-shared-layer-contract-2026-10-16). Where the build differs from the rows above:

- `ArpHost` forward-declares `ProcessorBase` in `ArpContent.h` (the checker forbids a `Plugin/` include in a library file); the layer's UI source is the CMake list `MU_TONI_LAYER_UI_SOURCES`, not part of the `mu-toni-layer` target (a console target cannot link UI sources). Family convention for E3 to E5.
- E2.3 needed four additions to `mu-core` that the rows did not foresee: `setCarrySkippedMidi` and `MidiView::carried`, `Layer::onParkedBlock`, and `resolveLane` returning `bool` (C12). `ParamText.h` also moved from `mu-core/Plugin/` to `Persistence/`; the forwarding header was deleted at E5.1 (mu-Tant's include moved at E4.1, mu-Clid's at E5.1).
- `HeldNotes.h` is `mu-toni/Source/Sequencer/` (one consumer). The checker now also finds a library declared in a product's `CMakeLists.txt`, so E3 to E5 register their libraries by declaring them, with no fixture change.
- The library-only test target is `mu-toni-layer-tests`; the processor-side comparison fixture is `Tests/ArpLayerFixture.h`.
- **Pathfinder list (what the contract got wrong), folded back:** (1) a layer added while keys are held does not see them, so the base must own an input-state snapshot (owed before T8a); (2) the base sends `channelMask` 0xFFFF for every layer (per-layer channel owed at T8a); (3) skipped-block MIDI was dropped (fixed by the carry, default off). None blocks E3.

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
| E3.1 | `Plugin/TrackParams.h` (`EngineType`, ids, `layerParamPrefix`) to `Persistence/TrackParams.h`; `addLayerParams` split out. The engines, `MuOnModDest.h`, `StepPattern.h`, `GrooveVoices.h` stop including `Plugin/`. The constructor's `numLayers.store` becomes `storeLayerCount` (design 2.1.7). | manifest diff empty |
| E3.2 | `TrackHost` interface; `TrackContent`, `StepRows` onto `ProcessorBase&` + `TrackHost&`. | UI builds |
| E3.3 | **Per-layer step row first, still behind the global grid** (D6): `StepRow` (16 steps, length, swing, accent) owned by `Track`; `StepPattern` becomes a view over the tracks' rows; saved state unchanged (the global `seq_swing`/`seq_accent` stay saved and seed every track's value on load, a unit test pins that). The E0.8 pin (a 5-lane preset with swing and accent) must pass unchanged. | pins, golden, `SequencerTests` |
| E3.4 | `GrooveSequencer` clocks one row per track; the shared clock is `RenderContext::startBeat`; swing/accent applied per row. Same timing arithmetic, so goldens at several block sizes decide this step. | goldens, `ON_roundtrip`, `ON_swap`, `ON_midi_clock*` |
| E3.5 | **Per-layer engine allocation.** `Track` holds exactly one engine (`unique_ptr`) for its selected `EngineType`; an engine change on the message thread builds the new engine outside the lock and swaps it (the existing hot-swap-style rule); `EngineMigrationTests` keeps passing. `GrooveVoices` splits: engine ownership into `Track`, resolve into `Track::bindParams`, trigger into `Track::render`. Memory: 5 engines x 8 slots drops to 8 engines **for an `engineLocked` track only; the standalone keeps all five per track (see As built).** | goldens (all engine types), `EngineTests`, `GrooveVoicesTests` rewritten to the new shape |
| E3.6 | `Track::render` in priority order: `renderPriority()` 0 for Kick/Bass/Hat/Snare, 1 for Rumble; Rumble reads its source through `RenderContext::peer(slot)` (still restricted to the mu-On types in the standalone). `remapLayerReferences` stays the hook for the Rumble source (it is already the generic one; `layersReordered` runs under `layersLock` and must not write parameters), its body moves to a library function (as built, below). Delete `GrooveVoices`. The P7 oversize segment is re-recorded in a signed goldens-only commit (design 2.1.7). | goldens including a Rumble-sidechained-to-Kick preset; `SlotStateTests`, structural-edit script |
| E3.7 | `engineLocked` flag (mu-Tronic wants it; add the field and the hidden selector now, default false, so the standalone is unchanged); `typeFromPreset` helper; the five `LayerTypeInfo` constants over the one class. | tests of the five types |
| E3.8 | Library: `mu-on-layer` target, checker, library-only test, Scales (none), `Track.h` loses `OwnerLink<PluginProcessor>`. | checker, all suites |

**Resting points:** after E3.1, E3.2, E3.3 (grid per layer behind the old clock), E3.4, E3.8. **Do not rest in E3.5/E3.6**: engine ownership and
Rumble priority change together; finish both in one session block.

dsp-controller review: **yes**, specifically E3.5 (engine swap while rendering, no allocation) and E3.6 (priority-1 reading a priority-0 buffer; lifetime of the scratch buffer).

**As built (2026-10-16).** E3.1 to E3.8 are on branch `e3-on` (with a contract commit before E3.6, the P7 oversize goldens as their own commits and a C12 hold-last-value fix). The `architecture-steward`'s verdicts are in [design 2.1.9](design.md#219-e3-as-built-mu-on-on-the-shared-layer-contract-2026-10-16). Where the build differs from the rows above:

- **Order and shape.** The per-layer step row and clock (E3.3, E3.4) landed as `Track::row` and `Track::clock` (`StepClock`); `StepPattern` is the standalone's view over the rows. `TrackEngine` (`Audio/TrackEngine.h`) is one engine behind a `std::variant`; **a `Track` holds all five unless `engineLocked`, then one** (the `dsp-controller`'s F1: the one-engine design dropped the step at a host-automated engine change, so the standalone is back to 5 x 8 and only mu-Tronic's locked tracks get the 8-engine saving; the E3.5 memory claim applies to those; golden `engine-automation-midplay`). `GrooveVoices` is deleted and its tests rewritten as `TrackRenderTests`. The five `LayerTypeInfo` constants and `typeFromPreset` are in `Sequencer/TrackTypes.h`.
- **Two `mu-core` additions the rows did not foresee:** `RenderContext::stoppedUnrendered` (a stop hidden in unrendered blocks; P5) and `renderPriority()` read once per call per live layer.
- **`remapLayerReferences`, not `layersReordered`.** The E3.6 row was wrong about which hook is generic. Done: the body is `mu_on::remapRumbleSources` in `Persistence/TrackParams.{h,cpp}` so mu-Tronic calls it rather than copies it.
- **Library.** `mu-on-layer` is declared in `mu-on/CMakeLists.txt`; UI sources are `MU_ON_LAYER_UI_SOURCES`; the library-only test is `mu-on-layer-tests`; the processor-side comparison fixture is `Tests/TrackLayerFixture.h`. `Track` names no processor.
- **Review code changes (done):** `getTrackEngine` / `selectTrackEngine` host names; `StepRow.h` and `StepClock.h` split out, `GrooveSequencer` moved to `Tests/`; one asserted `LayerTypeId` mapping in `Persistence/TrackParams.h`; `Layer::renderPriority`'s comment; `holdsEngine(e)` replaces `heldEngineIndex`.
- **Owed for mu-Tronic** (not E3 blockers, design 2.1.9 point 8): per-layer groove parameters (no global `seq_swing` / `seq_accent`), a one-row step component, `LayerTypeInfo::initParams`, and the engine build for locked tracks (`syncEngine` after `engineLocked` is set).

**Move rule (what stays in `mu-on` and what moves, with the trigger).** The test is the one design 2.5 states: a second consumer moves it to `mu-core` **before** the second copy is written; one consumer stays product-side under its namespace.

| Item | Where it lives now | Moves when |
|---|---|---|
| `StepClock` (one row's step cursor, swing, accent velocity) | `mu-on` `Sequencer/StepClock.h` | E5.5 writes `SampleRhythm`'s own step cursor: if it uses `StepClock` or the same arithmetic, `StepClock` moves to `mu-core/Sequencer/` first, and E5.5 uses it (the E5.5 row says "shared arithmetic"; this is the shared class) |
| `HeldNotes` (E2) | `mu-toni` | mu-Tronic or another product needs it for a second type |
| Rumble source remap (`remapRumbleSources`) | `mu-on` `Persistence/TrackParams` | never: it is mu-On-type logic; mu-Tronic calls it from its processor. A second type with a layer-valued parameter moves the *mechanism* (already `remapLayerReferences` in `mu-core`) not this body |
| `TrackEngine` (variant of five engines) | `mu-on` `Audio/` | never: it is the Track type's engine |
| `ProcessorBase` seams used (`stoppedUnrendered`, once-per-call priority) | `mu-core` | done |

### 2.4 E4 — mu-Tant  **size L, 6-9 sessions**  (first live product)

Sizing. `PluginProcessor.cpp` is 835 lines of which about 300 are the render chain (`renderVoice`, `applyModulation`, `applyFilterEnvelope`,
`applyPitchEnvelope`, `readConfig`, `renderRetiringVoice`, `renderVoiceCb`); `PluginProcessor_APVTS.cpp` 207, `_Preset.cpp` 320, `PluginProcessor.h` 383,
`PatternContent` 817+249, `GatingDesigner` 927+190 (UI, mostly independent of the processor), `GatePattern` 258+260, `SynthVoice` 121+145. 9,178 lines
in total. Less glue than On, but a **live product** and the Note-mode global (mixed into the final mix, not a layer).

| # | Step | Notes | Leaves green |
|---|---|---|---|
| E4.0 | **Gate: pre-conditions of 3.1** (tag, goldens recorded, E0.6 Note-mode pin, release decision taken). | |
| E4.1 | Parameter table: `createParameterLayout` per-voice part to `Persistence/PatternParams.{h,cpp}` as `addLayerParams`; `SwapBoundary.h` from `Plugin/` to `Sequencer/`. Pure moves. The constructor's `numLayers.store` becomes `storeLayerCount` (design 2.1.7). | manifest diff empty |
| E4.2 | `PatternHost` interface; `PatternContent` (23 `proc.` uses of APVTS and the `PluginProcessor::` id helpers) onto `ProcessorBase&` + `PatternHost&`; the id helpers move to `Persistence/`. `Pattern.h` loses `OwnerLink<PluginProcessor>`. | editor builds |
| E4.3 | **Note mode per layer** (D6). `midiNoteMode`, the held-note stack and the final-mix gate ramp (`applyNoteModeGate`) become `Pattern` state; the ramp applies to the layer's own output; the setting is saved per layer. **Behaviour risk:** today the gate is applied to the *master mix* after all voices and before... the mixer already ran (so after FX); per layer, the ramp acts before the strip, a different point in the chain. The pin from E0.8 decides: either the per-layer ramp is arithmetically equal for the single-layer case (then ship), or the master-mix gate is kept as a mu-Tant shell post-stage (**decision A3, Andy 2026-10-15: option (a), the old final-mix gate stays as a mu-Tant shell function so the live product is bit-identical; the library layer carries a per-layer gate that the standalone leaves off**;  **the old gate also gated the FX return and any second layer, so per-layer changes the final mix with FX tails**). | pins; golden with a MIDI script in Note mode |
| E4.4 | Cached atomic pointers (`voicePtrs`, `modDestAtoms`) to `Pattern::bindParams`; `voiceSnap` and the `MuTantModSnap` into `Pattern`; `readConfig`, `applyModulation`, `applyFilterEnvelope`, `applyPitchEnvelope` into `Pattern::render` (+ private helpers). The processor keeps `renderVoiceCb` as an adapter for this commit only (`useLayerRender` equivalence test). Beat wrap: the resolver's 64-beat wrap (`kMaxPatBeats`) becomes the layer wrapping its own `startBeat`; the arithmetic must yield the same value, golden over a long (more than 64 beats) render and a `--host-start-beat` case. | equivalence, golden |
| E4.5 | Engine array and `bankLock` handling: the engine moves into `Pattern`; the wavetable bank stays processor-owned and is handed through `PatternHost` (not yet a `LayerServices`); the layer's try-lock on the bank is kept with the same acquire order as today (layersLock, then bankLock). Delete the processor-side render chain. | golden, `SynthDSPTests`, `GateStageTests` |
| E4.6 | **Retire fade replaced by `RetireBin`**: today only a count-reducing swap fades a voice; the dropped slots' spare `Pattern`s are parked **in place** (`parkInPlace` with the next serial before the packed count store, `Fade` 25 ms, `RenderPastCount`, the `retiring[]` ramp and snapshot exactly; design 2.1.3). The in-place rules (R1): one active entry per row, skipped while the slot is live, regrow publishes the count then cuts, `layersMoved` calls `cutSlot` under `layersLock` before `addLayer` releases a spare, swaps and removes cut. Check: golden render of a count-reducing hot-swap (`TANT_preset`, `HotSwapBoundaryTests`) byte-identical; **except** the shrink-then-regrow cases of pin P1, where the cut fixes today's latent stale-tail bug (design 2.1.6, point 3): a signed goldens-only update after P1, before this commit. | those tests, P1 |
| E4.7 | Library target `mu-tant-layer`, `Audio/Scales.h` to `TantScales.h`, checker, library-only test, adopt `renderLayers`/`flagSwapBoundaries(loopBeats)`; `swapLoopBeats`/`blkPatternBeats` removed. The P7 oversize segment is re-recorded in a signed goldens-only commit (design 2.1.7). | checker, pluginval |

**Resting points:** after E4.1, E4.2, E4.4 (with the adapter), E4.7. The Note-mode step (E4.3) is a single commit; never leave it half.

**As built (2026-10-16).** E4.1 to E4.7 are on branch `e4-tant` (with a contract commit before the render steps, the P7 oversize goldens as their own commits, a C12 hold-last-value fix and the C3 retire-isolation twin test). The `architecture-steward`'s verdicts are in [design 2.1.10](design.md#2110-e4-as-built-mu-tant-on-the-shared-layer-contract-2026-10-16). Where the build differs from the rows above:

- **Commits.** E4.3 and E4.4 are one commit (Note mode and the render move, both chains behind `useLayerRender`), E4.5 and E4.6 one commit (engine and bank-lock hand-over, `RetireBin` replacing `retiring[]`, old chain deleted). Accepted: the equivalence test spans the merged first commit and no state lay between the second pair.
- **E4.1.** `SwapBoundary.h` did **not** move to `Sequencer/`: `swapLoopBeats` is the host's policy and includes `Plugin/HotSwap.h` (checker rule 1), so it stays in `Plugin/` (design 2.1.10 point 4). `ParamText.h`'s include moved to `Persistence/`; the forwarding header remained for mu-Clid until E5.1 (deleted then).
- **E4.3 (A3, D7).** Note mode stays an app setting pushed to every Pattern; each Pattern holds its own keys and pitch track; the 8 ms ramp stays on the final mix as a shell post-stage, and the per-layer gate exists as `Pattern::gatesOwnOutput` (off in the standalone). "Saved per layer" is withdrawn for the standalone and owed to mu-Tronic (design 2.1.10 point 5). A layer added while keys are held is seeded by a mu-Tant-side `seedInputState`, which the base's input-state snapshot (design 2.1.8 gap 1) replaces.
- **E4.5.** The engine stays **per slot** and processor-owned, reached through `PatternHost::getPatternEngine(slot)` at `bindParams`, because `VoiceEngine::reset` keeps the oscillator phase and the structural-edit golden pins it (design 2.1.10 point 3). The bank stays processor-owned and its lock is `getPatternBankLock()`; the acquire order is unchanged.
- **E4.6.** `RenderPastCount`, `Fade`, and a new constructor opt-out `setRegrowCutsInPlaceTails(false)`: mu-Tant keeps the P1 stale tail, so the stage changes no mix; the golden update and the removal of the opt-out are a separate signed commit (design 2.1.10 point 6, temporary). `setCarrySkippedMidi(true)`.
- **E4.7.** `mu-tant-layer` is declared in `mu-tant/CMakeLists.txt`; UI sources are `MU_TANT_LAYER_UI_SOURCES`; the library-only test is `mu-tant-layer-tests`; the processor-side comparison fixture is `Tests/PatternLayerFixture.h`. `TantScales.h` done. Checker rule 5 skips the shell hook `renderPlaysByDefault`. `renderLayers` caches every non-Empty slot's `loopBeats()` and `layerLoopBeats(slot)` reads it.
- **Owed (not E4 blockers):** one shared `HeldNoteStack` in `mu-core/Sequencer/` for mu-Toni's and mu-Tant's copies (before T1); the generic `checkRetireIsolation` helper (before E5.4d); `kPatternType` and `kArpType` with the registry hook (T1); removal of the regrow opt-out after the P1 golden update; the base's input-state snapshot (before T8a); small code changes from the review (design 2.1.10 points 2b, 6).

**Move rule (E4).**

| Item | Where it lives now | Moves when |
|---|---|---|
| `HeldNoteStack` (mu-Tant) beside mu-Toni's `HeldNotes` | `mu-tant` `Sequencer/` | **Now owed:** two consumers exist. One `mu_core` class with capacity and overflow policy as parameters, before T1 |
| `swapLoopBeats` / `SwapBoundary.h` | `mu-tant` `Plugin/` | E5 writes mu-Clid's equivalent: if the arithmetic matches, it moves to `mu-core/Plugin/HotSwap.h` first |
| The slot engines (`voices[]`) | `mu-tant` `PluginProcessor` | never moves into the Pattern in the standalone (pinned); mu-Tronic's adapter supplies its own (design 2.1.10 point 3) |
| Note-mode final-mix gate | `mu-tant` shell | never: A3, the standalone's own post-stage; mu-Tronic uses `gatesOwnOutput` |
| `ProcessorBase` seams used (`setRegrowCutsInPlaceTails`, `layerLoopBeats`) | `mu-core` | done; the opt-out is deleted with the P1 golden update |

### 2.5 E5 — mu-Clid  **size XL, 9-14 sessions**  (riskiest; last)

Sizing. This reverses the Stage 7 ruling (engines stay processor-side). The affected code: `PluginProcessor.cpp` 546 + `.h` 478 + `_Modulation.cpp`
**444** + `_APVTS.cpp` 395 + `_Layers.cpp` 213, `HotSwapStager` **119 + 265**, `PresetIO.cpp` **1,048** + `_HostState.cpp` 245 + `.h` 139, `SampleLibrary`
60+72, `RhythmContent` 137+703, and tests of about 7,000 lines (`ClipLaunchTests` 868, `SwapCommitTimingTests` 807, `ProcessorSeamTests` 665,
`ClidLayerStructureTests` 464, `ClidEditContinuityTests` 353, `ClidScriptedEditRenderTests` 236). The glue is not one chain but several lifecycles
(trigger, MIDI note-off ownership, retire tails with drain flags, sample paths, previous-state modulation flags, a stager that prepares a `Rhythm` +
`VoiceEngine` including file I/O at stage time). `mu_core::Rhythm` is also used by **mu-Clid MIDI**, which must keep compiling and sounding unchanged.

| # | Step | Notes | Leaves green |
|---|---|---|---|
| E5.0 | **Gate** as E4.0, plus a fresh `mu-clid-tests` build (the target is not in ALL) and the Stage 7 listening baselines, and `VoiceEngine::bufferLock` (a `juce::ReadWriteLock`, which can allocate on the audio thread) replaced in `mu-core` by a mechanism the `dsp-controller` signs, before any mu-Clid render runs through the contract (design 2.1.7). | | |
| E5.1 | `SampleLibrary` stops taking `PluginProcessor&` (takes a path provider + the ProcessorBase storage root); `RhythmHost` interface; `RhythmContent`, `VoiceSection`, `MidiOutSection`, `SampleBrowser`, `RhythmMiniVisual` onto `ProcessorBase&` + `RhythmHost&`. | UI builds |
| E5.1b | **Parameter table** (partner of E5.2, done first or in the same session): `createParameterLayout`'s per-rhythm part of `Plugin/PluginProcessor_APVTS.cpp` to `Persistence/RhythmParams.{h,cpp}` as `mu_clid::addLayerParams(layout, slot)`, like E4.1 (`PatternParams`) and E2 (`ArpParams`). Pure move; the constructor's `numLayers.store` stays for E5.2's `storeLayerCount` pass. | parameter manifest diff empty |
| E5.2 | `PresetIO` off `PluginProcessor&`: the rhythm's tree code moves to a processor-free `Persistence/` file with a small `RhythmPresetContext` for the sample folder (`writeExtras/applyExtras` follow with `SampleRhythm`, see the E5.2 ruling below); `PreparedFullPreset` payload kept. (The `storeLayerCount` pass under `layoutWriteLock`, design 2.1.7, was already done on `main` before E5.) This is the largest single file (1,048 lines) but a *pure refactor with a byte-compare of saved files*, so it is cheap to verify and early to do. | state bytes (E0.6), `PresetXMLRoundTripTests`, `PresetMigrationTests` |
| E5.3 | `mu_clid::SampleRhythm : mu_core::Rhythm` (not copyable; `Rhythm` itself and mu-Clid MIDI untouched) holding the `VoiceEngine`, `MidiOutputEngine`, play state, modulation snapshot, sample path, previous-state flags and the retire tail. The engines still *also* exist in the processor arrays for this commit only; the new type is constructed but not rendered. | all suites |
| E5.4 | **Per-block glue into `SampleRhythm`, as built:** (a) modulation resolve and write-back; (b) hit dispatch and play-state publish. Done 2026-10-19. The remaining halves of the old E5.4 (MIDI engine and owed note-offs; retire tails) are E5.5c and E5.5e below, re-ordered after the cursor and the render adoption (ruling 2026-10-19, below). | goldens |
| E5.5 | **Cursor, render adoption, then the MIDI engine and tails** (re-opened 2026-10-15, re-cut 2026-10-19 into sessions of one agent each; the order is a ruling, not a preference: E5.5a, b, c, d, e). The `RenderContext` carries no product trigger field, so `SampleRhythm` clocks its own step cursor. Every sub-step leaves goldens bit-identical (tolerance 0) unless it names an accepted change, and each is one commit.<br>**E5.5a. Cursor.** A processor-free step-cursor class in `mu-clid/Source/Sequencer/` (or `mu-core/Sequencer/` `StepClock` if the arithmetic is the same as mu-On's: decided by reading both, recorded here; **decided 2026-10-19: mu-Clid-local**, `Sequencer/RhythmCursor.{h,cpp}`, a host half `RhythmStepClock` (step from beat, master-loop modulo and wrap, restart / realign, run id) and a per-rhythm `RhythmCursor` (pattern hand-over, phase, step walk). mu-On's `StepClock` fires each 16th at its sample offset with swing and a resync window; `RhythmSequencer` resolves one step per block start with phase origins and a back-walk, so the arithmetic is not shared. `RhythmSequencer` stays for mu-Clid MIDI, so its arithmetic now exists twice; folding `RhythmSequencer` onto the two halves is a later option. Host contract for E5.5b on, from the gate: skip the clock and every cursor in a block with no live rhythm, and pair each `advance` with exactly one `endBlock` after every cursor ran, on every path. `dsp-controller` signed the bit-for-bit equality 2026-10-19) using `RhythmSequencer`'s arithmetic verbatim, with the wrap and swap-start rules; `loopBeats()` for the swap boundary; the master-loop wrap stays host-side. Not wired. A table test drives it and `RhythmSequencer` through the same beat sequences (wrap, swap-start, tempo change, loop restart) and asserts equal step/offset output. **Gate: `dsp-controller` signs bit-for-bit equality before E5.5b.** | `RhythmSequencer*Tests`, the new equality test |
| | **E5.5b. `SampleRhythm::render` fires its own hits** (sample-accurate offsets inside the block) through the cursor, behind `useLayerRender`, with the voice engine and play state still processor-side and passed in (as E5.4a/b). Both chains run in the equivalence harness (`LayerEquivalence`) and the processor chain stays the default. **Gate: `dsp-controller` signs trigger-to-render ordering and that the Euclid override and swap-start match.** | `LayerEquivalenceTests` for mu-Clid, `SwapCommitTimingTests`, goldens |
| | **E5.5c. Owed note-offs and the MIDI engine.** New `mu-core` hook (ruling below). `MidiOutputEngine` moves into `SampleRhythm`; the removed layer's owed note-offs are handed to a processor-side fixed-size `orphanOffs` list in `layerRemoved`, drained by the existing block path; the host `MidiBuffer` merge is reviewed here. `onTransportStopped` stays a no-op on the layer (note-offs count down every block). | `MidiOutputModeTests`, `ClidEditContinuityTests`, structural-edit golden, new `mu-core` hook test |
| | **E5.5d. Voice engine into the layer and mu-Clid on `renderAndMix`.** `VoiceEngine` becomes layer-owned, `useLayerRender` flipped on for mu-Clid, the processor-side render chain and `processCoreBlock` use deleted in the same commit (one path after the commit). `RhythmPlayState` stays a processor-side array (the UI holds raw pointers to it; a remove destroys a layer) and is passed to the layer until E5.7. Pre-merge: the bin is still empty of engine tails, so the old `RetiredVoices` descriptor keeps working only if `renderAndMix` is not yet used for it: **if both cannot coexist for one commit, merge E5.5d and E5.5e into one session**. **Gate: `dsp-controller`** (the contended-block envelope fix from E5.0 Minor 2 goes in here as an accepted goldens-only change if its pin differs). | goldens, `LayerEquivalenceTests`, listening baselines |
| | **E5.5e. Retire tails into `RetireBin`:** `mu_clid::EngineTail : RetireTail` (`Natural` policy, block-counted drain inside the tail, `markRetired` as `onRetireStart`, entry-0 eviction kept, parked under `layersLock` with the current serial, `service()` before the install in each deferred-work pass; count policy after P3; design 2.1.3, 2.1.6 points 4, 7, 8); the `retiredVoiceEngines`/`retiredReadyForCleanup` arrays disappear. The generic `checkRetireIsolation` helper (owed from E4) is written first in this commit. Accepted changes, each a signed goldens-only update before the commit if its pin differs: stop at Done (P2), the 30 s cap (P4), the flush at prepare, the count policy (P3). **Gate: `dsp-controller`** (drain/cleanup order, no free on the audio thread). | goldens (retire-tail, structural-edit), `ClidEditContinuityTests`, `RetireBin*Tests`, P2 to P4 |
| E5.6 | **Stager (one agent session).** `HotSwapStager` stays **for the standalone** (the Stage 7 step 5 decision stands) but its payload preparation uses `SampleRhythm::preloadStaged` so the sample I/O is one function. The swap timing is not touched. Replacing the stager is explicitly **not** in this programme (a later option once `SwapCommitTimingTests` has run green for a release). | `SwapCommitTimingTests`, `HotSwapCancelTests`, swap listening tests |
| E5.7 | **Cut as two sessions, one commit each: (a) delete the processor-side arrays and glue** (the `RhythmPlayState` array moves into the layer with the UI pointer fix, the hook glue and `RhythmSequencer` bookkeeping go, deferred items 6a/7 above, `Plugin/PresetIO` folded into `PluginProcessor_Preset.cpp`), **then (b) library target, checker, library-only test and the P7 goldens re-record** (signed goldens-only commit). Delete the processor-side arrays and glue; `processBlock` becomes transport, context, `renderLayers`, mixer. Library target, checker, library-only test. The P7 oversize segment is re-recorded in a signed goldens-only commit (design 2.1.7). | everything below |

**E5.1 as built (2026-10-19, `architecture-steward` Pass).** `mu-clid/Source/UI/RhythmHost.h` (beside `ArpHost.h`, `TrackHost.h`, `PatternHost.h`), methods named `getRhythm*`/`…Rhythm…` per the E2 rule. "Path provider" is read as `SampleSlotHost`, declared beside `SampleLibrary` in `Audio/SampleLibrary.h` (slot engine, live slot count, app settings file as the storage root); it is wider than a path provider because the library loads into the slot engines. `ProcessorBase&` is passed only to the shared `EuclideanPanel` and `InsertSubsection`. `RhythmHost`/`SampleSlotHost` are product-side (`mu_clid::`, they name `Rhythm`, `VoiceEngine`, `SampleLibrary`), so neither moves to `mu-core`. `SampleLibrary` stays a mu-Clid class until E5.3 decides whether `SampleRhythm` owns the paths. Owed to E5.7: the layer library's UI list (`MU_CLID_LAYER_UI_SOURCES`) takes the files that now include only `RhythmHost.h`.

**E5.2 ruling (2026-10-19, `architecture-steward`).** `PresetIO` splits by what it needs. (1) The rhythm-type persistence (a rhythm's tree from the APVTS: kinded params, name, colour, relative sample path, embedded sample, modulators, clip bank; `prepareRhythm`; the sample resolve/embed helpers; the `GlobalState` rows) goes to `mu-clid/Source/Persistence/RhythmPresetTree.{h,cpp}`, free functions in `mu_clid::` taking the APVTS, a `Rhythm`, the sample path and `mu_clid::RhythmPresetContext {samplesDir, sampleRate, blockSize, onLoadError}`; `PreparedRhythm` moves with it as `mu_clid::PreparedRhythm`. It includes nothing under `Plugin/` or `License/`, mu-core's included (checker rules 1 and 2). Saved bytes unchanged. (2) The orchestration (`commitStagedFullPreset`, `restoreStateFromTree`, stage/apply, get/set state, live state, clip masks) is shell glue and moves to `Plugin/PresetIO.{h,cpp}` and `Plugin/PresetIO_HostState.cpp`, keeping `PluginProcessor&`, beside `HotSwapStager`; `PreparedFullPreset` stays in `Plugin/HotSwapStager.h` and holds `PreparedRhythm`. This is mu-Tant/mu-Toni/mu-On's shape (orchestration in `Plugin/PluginProcessor_Preset.cpp`, layer persistence below `Plugin/`). No `RhythmPresetHost` interface: it would wrap processor internals that E5.4 to E5.7 rewrite. (3) `SampleRhythm::writeExtras/applyExtras` arrive with E5.3/E5.4 and call `RhythmPresetTree`, never a copy of it; `mu_core::Rhythm` and mu-Clid MIDI stay untouched. Owed to E5.7: when the remaining `Plugin/PresetIO` is small, fold it into `Plugin/PluginProcessor_Preset.cpp` like the other three products.

**E5.0, E5.3, E5.4a, E5.4b as built (2026-10-19, coder notes for the `architecture-steward`).** E5.0: `VoiceEngine::bufferLock` is a `juce::SpinLock` (writers lock, `process()` try-locks; `dsp-controller` Pass). Deferred from that review (Minor 2, pre-existing): when `process()` misses the try-lock it returns before advancing the three envelopes and `retireDrainBlocks`, so a contended block loses one block of envelope time; the fix (advance them by the block length on that path) changes output only on a contended block and is owed with the retire-tail work. E5.3: `SampleRhythm` members exist but stay empty (no second engine built); `typeId()` stays Empty until mu-Clid renders through the contract. E5.4a/b: the modulation pass, the hit dispatch and the play-state publish are `SampleRhythm` methods the processor calls per rhythm, verbatim moves proved by the tolerance-0 goldens, so no `useLayerRender` switch (one path at each commit). The voice and MIDI engines and the `RhythmPlayState` array stay processor-side and are passed in: the UI's rhythm circles keep raw pointers to the play state, and a remove destroys a layer object. **Open for a ruling before E5.4c/d:** (c) a layer-owned `MidiOutputEngine` needs the removed layer's owed note-off, but `layersReordered` runs after `unpublishLayerLocked` and never sees the removed layer (a `mu-core` hook such as `layerRemoved(Layer&, int)` under the lock, or keep the MIDI engines processor-side until E5.7 as the voice engines are); `onTransportStopped` has nothing to do on the processor path (note-offs count down in every block, playing or not). (d) `RetireBin` tails are rendered only inside `ProcessorBase::renderAndMix`; mu-Clid still renders through `processCoreBlock` with the `RetiredVoices` descriptor, so moving the tails into the bin first needs either mu-Clid on `renderAndMix` (which needs `SampleRhythm::render` firing its own hits, that is E5.5's per-layer cursor) or a `mu-core` hook that renders the bin inside `processCoreBlock`'s channel loop. Suggested order: E5.5 (cursor, own `dsp-controller` gate), then (c) and (d) together with the render adoption behind `useLayerRender`, as E4.3 to E4.6 did for mu-Tant.

**E5.4c/d ruling (2026-10-19, `architecture-steward`).** (1) Order: E5.5a (cursor), E5.5b (own hits), then the MIDI engine (c), the render adoption (d), the tails (e). Tails cannot enter the bin while mu-Clid renders through `processCoreBlock`, and the cursor is what lets `SampleRhythm::render` stand alone. (2) **No second render hook:** rendering the bin inside `processCoreBlock` is refused; it would be a second path to delete. mu-Clid adopts `renderAndMix`, as mu-Tant did at E4.3 to E4.6. (3) **A `mu-core` removal hook is accepted:** `virtual void layerRemoved(Layer& removed, int index)` on `ProcessorBase`, beside `layersReordered`, called from `removeLayer` inside the same `layoutWriteLock`/`layersLock` section, after `unpublishLayerLocked` and before `layersReordered(Remove, ...)`, while `retired` is still alive. Same contract as `layersReordered`: it must only move and reset, never allocate or wait. Neutral wording (a layer, an index, no MIDI or rhythm terms); the default does nothing, so the other three products and mu-Clid MIDI are untouched. A product with owed per-layer state (mu-Clid's note-offs) moves it out of `removed` into storage it owns. Not offered for swap (a swap moves layers, it does not destroy them). The alternative, keeping MIDI engines processor-side to E5.7, is refused: it leaves the owed-note-off ownership as the one piece of Clid glue outside the layer. (4) Gates: the `dsp-controller` signs E5.5a (bit-for-bit cursor), E5.5b (trigger-to-render ordering), E5.5d (render adoption, contended-block envelope fix) and E5.5e (drain/cleanup order); E5.5c is reviewed for the hook's lock discipline and that the orphan list is fixed-size. (5) `RhythmPlayState` stays processor-side to E5.7.

**E5.5d and E5.5e as built (2026-10-19, merged `aca1b94`; rulings by the `architecture-steward`).** mu-Clid now renders only through `renderAndMix`; the voice engine is layer-owned and the retired engines sound out as `mu_clid::EngineTail` in the shared `RetireBin`. Rulings on the deviations:
1. **Per-block product hooks `layersBlockStarting` / `layersBlockRendered`: accepted** as `ProcessorBase` virtuals (default no-op, neutral wording: a view of the block's layout, the transport, the host buffers). Contract: called only in a block that renders, once each, `layersBlockStarting` after the MIDI decode and before any layer or tail renders, `layersBlockRendered` after the last chunk's mixer, both inside that block's `LayerBlock` so the layout cannot change under them; the same discipline as the render hooks (no allocation, no waiting). Use: per-block glue that spans layers and must not race an edit (mu-Clid's MIDI-mode note-on/off countdown, clock and play-state publish). **Other products adopt them only when they have such glue outside the `LayerBlock`**; no product is to move code into them for symmetry. Any new product reaches for them before adding a processor-side pre/post pass.
2. **`copyToMixer` skips a slot nothing wrote: accepted** as a `mu-core` behaviour (a no-write slot is not a zero-copy), pinned by a `mu-core` test; the other products' goldens were unchanged.
3. **`RetirePolicy::closeWithDeclick` (Natural only, 10 ms declick when the tail reports Done): accepted.** It is an opt-in field on the policy and the bin stays product-agnostic. It is part of the signed P2/P4/flush/`CutAtCount` set (design 2.1.6), not a precedent: a further policy field needs its own ruling.
4. **MIDI-mode note-on and note-off countdown stay processor-driven in the hooks, and `RhythmSequencer` keeps its pattern bookkeeping to E5.7: accepted.** This is the sanctioned residue, because the hooks are the only place the owed-note-off ordering against the host `MidiBuffer` is safe; E5.7 moves it into `SampleRhythm` or deletes it with the processor-side arrays (owed list below).
5. **An engine with no sample ends at once** (instead of holding its bin entry 30 s): accepted, part of the P2 stop-at-Done change.
6. **Deferred, both owed to E5.7 (not blockers):** (a) a contended-block sample voice does not advance (E5.5d Minor 2; the envelope fix is in, the voice playhead is not); (b) the level-based drain criterion (tails drain by block count inside the tail, not by measured level). Neither changes a non-contended render.
7. **The legacy host-state path frees engines under `layersLock`: accepted** (never on the audio thread; it is the message-thread restore). Revisit only if the legacy path is deleted in E5.7.
8. **`mu-core/Tests/RetireIsolation.h` `checkRetireIsolation`** is the shared check the plan owed from E4 and is now used by `mu-core`, mu-Tant and mu-Clid. Do not copy it into a product test.

**Resting points:** after E5.1, E5.1b and E5.2 (all are pure and independently valuable: the UI and the persistence stop depending on the processor);
after E5.4a, b, E5.5a, b, c (each is a self-contained move behind the golden). **Do not stop between E5.5c and E5.5e** (engines live in two places until E5.5d, and the tails are not in the bin until E5.5e) or between E5.6
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

**Decided (Andy, 2026-10-15, A1): no release is needed before E4/E5 start; the v1.1 tag is the rollback.** So:
(a) tag `pre-extract-all` now; (b) a user-visible fix pending for a live product may still be shipped from the baseline first, at Andy's discretion, so the
next release carries the extraction on its own (one change type per release is easier to attribute); (c) the first release that
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
registered type (a coarse grep that keeps D1 honest; the test-steward owns its fixture); (6) **layer reads outside `LayerBlock`** (N5, design 2.1.1): in a
product whose processor selects `LayerAccess::Published`, a `layers[` or `layerAs<` read in `processBlock`, a shell post-stage or `flagLayerSwapBoundaries`
(those use values cached by `renderLayers`). Rule 6 is coarse too (it fires on the file, the reviewer clears audited lines with `layer-ok`); it is live from T1
and for each product only when it switches mode.

### 4.6 What the reviewers do at each stage

| Stage | dsp-controller | test-steward adds / confirms | architecture-steward |
|---|---|---|---|
| E0 | signs the seed fix (E0.2); reviews design fixes C1 to C4 before E1 is coded (round 1 2026-10-15; round 2 requested); signs the goldens-only updates that follow pins P1, P2, P4, P7 | owns the work in tests.md section 7 items 1 to 9, 11 to 13 and the harness self-test | confirms D6 pin list against design 3.5 |
| E1 | **full review**: `renderLayers` try-lock behaviour, no allocation (scratch sized in `prepareToPlay`), lock-miss silence and transport advance, `RetireBin` ordering (the design 2.1.3 transition table, free on the message thread after the grace period, no free under `layersLock`), `retirePolicy()` `None` not parked, denormal handling in fades | `LayerRenderTests`, `RetireBinTests`, a counting-allocator test, `mu-core-tests-16` | contract vs design 2.1; scope guard. **Ruled 2026-10-16** (design 2.1.7): all deviations accepted, two code changes owed before E2 |
| E2 | held-note state crossing threads (UI/MIDI vs audio), `Arp::render` allocation-free, no param-by-string in `render` | ArpHost UI test, A/B test, library-only test, Toni pins, Toni structural-edit script | boundary, naming, the "contract got wrong" list |
| E3 | engine swap while rendering (build off-lock, swap under lock, free off-thread), Rumble `peer` lifetime, priority-1 reading scratch, per-layer sequencer vs block-boundary triggering sample offsets | per-engine golden, `GrooveVoices` tests rewritten, Rumble silence cases (empty / priority-1 / removed source) | per-layer StepRow shape, `engineLocked` |
| E4 | gate: M2 done (pluginval with the parity assert, offline bounces); `Pattern::render` allocation, the layersLock/bankLock acquire order, Note-mode ramp, 64-beat wrap equivalence through `wrapBeat`, `RetireBin` replacing `retiring[]` with the in-place rules and the P1 golden update | Note-mode pin and golden, count-reducing swap golden, structural-edit script | PatternHost, `SwapBoundary.h` placement |
| E5 | the E5.5 cursor gate (bit-for-bit `RhythmSequencer` arithmetic); the count policy after P3; trigger-to-render ordering within a block (sample-accurate trigger offsets), `orphanOffs` ownership on remove, retire tail drain/cleanup order, sample I/O never on the audio thread (`preloadStaged`), `SwapCommitTimingTests` unchanged | the whole Stage 7 proof list, `mu-clid-midi-tests` | `SampleRhythm` vs `Rhythm`, stager decision, `mu-clid-layer` contents |
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
| R9 | **Include collisions** when four `Source/` roots share one include path (`Audio/Scales.h`; relative paths unique) | M / M | rename `ToniScales.h` (E2.6, done 2026-10-16), `TantScales.h` (E4.7, done 2026-10-16); checker rule (3); the combined link test (E6) |
| R10 | **`RetireBin` changes audible behaviour** (a product that today cuts a removed voice would start fading it, or the reverse) | M / M | `retirePolicy()` `None` means not parked; per product the policy reproduces today's (design 2.1.3 mapping table) (Tant fade, Clid engine tail, Toni/On none); structural-edit script goldens |
| R11 | **Floating-point differences** from re-ordered operations (rendering all layers first then mixing) | L / M | per-layer arithmetic is unchanged (the mixer already treats channels independently; copying to scratch adds no arithmetic); 0-difference pass criterion; the `useLayerRender` equivalence test catches it at the commit that introduced it |
| R12 | **Determinism of the harness itself** (timers, thread timing, uninitialised memory) | M / M | harness self-repeat test (E0.5) and the seed fix (E0.2); deterministic scripted blocks; a flaky case is removed, not tolerated |
| R13 | **Schedule risk on one developer**: the programme blocks mu-Tronic and other work for weeks | H / M | resting points (section 2); each product independent after E1/E2; mu-Tronic starts as soon as Toni and On are done (section 6) |
| R14 | **Build-number and backlog churn on `main`** during a branch (Clid) | L / L | merge `main` into the branch weekly; the build number is a generated file, never edited by hand |
| R15 | **Test-tool blind spots**: golden and equivalence tests prove equality to the baseline, not correctness of the baseline | L / M | listening-test expectations (musical assertions) still run; baseline recorded only after the unmodified tree passes everything |
| R16 | **Lost or misattached layout serial** (open until E1 implements it): a host-thread `setStateInformation` running `applyComposedState` racing a message-thread edit loses a serial or attaches a park to the wrong count store, so one block renders a layer both live and as a tail | M / H | design 2.1.3: the serial advances by a compare-exchange loop on the packed `layout` word, and `nextSerial()` plus the store that carries it run inside one writer exclusion (`layoutWriteLock` in `TryLock` mode, the writers' mutex in `Published` mode); the `applyComposedState` race test from a second thread in `RetireBinTests` (E1); dsp-controller at E1 |

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
| E2 Toni | M | 4-6 (5) | 0.9 week (design 2.1.5: the runner clock needs no rework; only its context source and the held notes move) |
| E3 On | L | 6-10 (8) | 1.4 weeks |
| E4 Tant | L | 6-9 (7.5) | 1.25 weeks |
| E5 Clid | XL | 10-16 (12) | 2 weeks |
| E6 Close-out | S | 1-2 (1.5) | 0.25 week |
| **Total** | | **39-61 (about 49)** | **7 to 11 weeks serial, likely 8 to 9; about 1 week less where test work overlaps coding** |

**Honest uncertainty.** The first estimate (29 to 46 sessions) is superseded: the real-time review added contract requirements (C1 to C18) and the test
review added about 12 to 13 working days of safety-net work that the first draft treated as 1 to 2 sessions. The upper bound is "plan for it" (a session
that ends in a non-zero golden diff costs the next session). **Models (Andy, 2026-10-15, A13):** the contract design (E0d and E1) and the mu-Clid extraction (E5) run as **Opus-grade agents**, and the strongest
available reviewer reviews them; the main conversation coordinates; the middle stages use the normal setup. A reviewer's missed finding in the contract is repaid four times, once per product.

Reviewer rounds are inside these numbers. What is **a day or less**: E0.1, E0.2, E0.3, E1's limits and ids, E2.1/E2.2/E2.6, E3.1/E3.2, E4.1/E4.2, E5.1.
What is **a week or more**: E5 as a whole, and E3.5/E3.6 and E5.4 as blocks (each is the equivalent of two to three sessions of tightly coupled changes that
cannot be split).

### 6.3 Milestones and what they unblock

| Milestone | After | Meaning | Unblocks (mu-Tronic stages) |
|---|---|---|---|
| **M0** | E0 | Safety net exists (harness, goldens, corpus, manifests, probes), design fixes C1 to C4 signed; no code moved. | nothing visible |
| **M1** | E1 | Contract proven on a fake type; checker live. | **T1 (empty unit) may start**, see 6.4 |
| **M2** | E2 | Toni is a library; contract validated by a real type. | T1 can register `Arp`; the first real layer type exists |
| **M3** | E3 | The five mu-On types exist. | **Internal checkpoint only** (Arp + Kick/Bass/Hat/Snare/Rumble make mu-Tronic usable for Andy's own testing; **no release**, A12); T8a can start; T1 + T2 usable |
| **M4** | E4 | Pattern exists. | Pattern registered; T8b Tant importer |
| **M5** | E5 | Rhythm exists. | all nine types; T8b Clid importer; **the earliest point at which mu-Tronic may launch** (with T1/T2/T8a done): one release with all four engines |
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
| T9 release | M5 + T1/T2/T8a. Andy (2026-10-15): launch only with all four engines ready; no early release. | |

### 6.5 Suggested cadence

Weeks 1-2: E0, E1, E2 (M2). Weeks 3-4: E3 (M3), T1 interleaved. Weeks 4-5: E4 (M4). Weeks 5-7: E5 (M5). Week 7-8: E6 (M6), T2. mu-Tronic
is usable internally at M3 (week ~4) with 6 layer types (a checkpoint, not a release); it can launch at M5 with all 9. If the schedule slips the order does not need to change: stop at any milestone and
the product set is consistent.

---

## 7. Decisions (Andy answered 2026-10-15)

**Answered 2026-10-15:** A1 (a): no release is needed before E4/E5; the v1.1 tag is the rollback. A2 to A10 accepted as recommended, with A3 (a) chosen
explicitly. A11: noise seeding is **per slot** (a fixed seed per layer slot, repeatable and different per layer). Also decided: the first mu-Tronic release
waits for all four engines (A12), Rumble reads any layer type (design.md still-open item 3, closed), and the review grade (A13). **A14 (licence state / engine loading) decided
2026-10-15: "Go with the recommendation. Updates will be suite wide so mu-tronic will rebuild if the engine of an app does."** Analysis in sections 8 and 9. The rows below keep the options for the record, each marked with the answer.

| # | Decision | Options | Recommendation |
|---|---|---|---|
| **A1** | **Decided: (a), no release first.** Release before extracting the live apps? | (a) none, the shipped v1.1 tag is the rollback; (b) ship any pending fix for mu-Clid/mu-Tant first from baseline; (c) cut a mu-Clid and mu-Tant release purely as a clean baseline before E4/E5. | (b): ship pending fixes before E4.0 / E5.0 if there are any; otherwise (a). The first release containing extracted live code is a named patch release with "nothing audible changed" notes after Andy's own soak. |
| **A2** | **Accepted as recommended.** Pass criterion | (a) 0 differing samples at 24-bit; (b) <= 1 LSB (the existing roundtrip tolerance). | (a). A non-zero diff goes to Andy with the cause; the programme does not absorb it silently. |
| **A3** | **Decided: (a), keep the old final-mix gate as a mu-Tant shell function; the live product stays bit-identical.** **Tant Note mode (live product).** Today the Note-mode gate multiplies the **final mix**, so it also gates the FX return (delay and reverb tails stop with the note) and the sum of any second layer. Moving it per layer (design 3.5) gates only that layer's own signal before its strip: **with FX tails or a second layer the final mix changes**, so "nothing audible changed" (D7) would no longer hold for Note-mode sessions. | (a) **Preserve the old behaviour exactly**: the final-mix gate stays as a mu-Tant shell function (one documented exception to D3 for one function, the library layer has a per-layer gate that the standalone leaves off), final mix bit-identical, the live product unchanged; (b) **accept the change**: per-layer gate everywhere, a named behaviour change in release notes, golden updated by `update-golden.ps1` with Andy's sign-off, which affects existing Note-mode sessions (FX tails now ring on after the note ends). | (a) for mu-Tant's own app (a paying product, "nothing audible changed" is the promise of this programme), with (b) available later as a deliberate change. The E0.6 Note-mode pin (stem and with-FX final mix) decides how large the difference is before this is final. |
| **A4** | **Accepted as recommended.** Branch strategy for Clid | `main` incremental vs short-lived branch with fast-forward merge | branch for E5 only (3.2). |
| **A5** | **Accepted as recommended.** Is the "release freeze" rule acceptable? | release only from a gate-tagged commit while E4/E5 are in progress | yes. |
| **A6** | **Accepted as recommended.** T1 timing | after M1 vs after M2 vs after M3 | after M2, interleaved (6.4). |
| **A7** | **Accepted as recommended (after M5).** T2 (buses) slot | after M3 vs after M5 | after M5 (keeps E4/E5 diagnosis clean); after M3 if Andy wants buses in early mu-Tronic use. |
| **A8** | **Accepted as recommended (keep).** Replace mu-Clid's `HotSwapStager`? | keep for the standalone (Stage 7 step 5 stands) vs fold into `layerSwapper` | keep; revisit only after a release of the extracted Clid. |
| **A9** | **Accepted as recommended (services at T1).** Scope of "completely self contained": do the wavetable bank and `SampleLibrary` become `LayerServices` now, or are they handed through the host interface and unified at T1? | now vs at T1 | at T1 (keeps E2 to E5 about the layer, not about services; both standalones keep their own). |
| **A11** | **Decided: per-slot seed (a fixed seed per layer slot).** **Seed fix (E0.2).** Fixing the seed of mu-Tant `NoiseGen` and mu-Toni `ToniVoice::rng` makes noise identical on every note and run instead of different each time. Approved by the main conversation as Andy's delegate so goldens can be repeatable. | confirm; or seed per layer slot (differs per layer, still repeatable) | per-slot seed if the pin shows no problem, otherwise one fixed seed; dsp-controller signs. |
| **A10** | **Accepted as recommended.** Variation: do the pure steps for both live products early (E4.1, E4.2, E5.1, E5.2 right after M3) | yes / no | yes, optional, it shortens the audio steps and has no audible risk. |
| **A12** | **Decided (Andy): "No, all four engines for launch. There's no rush, so we get each one ready."** First mu-Tronic release. | (a) ship early with Arp + mu-On types; (b) wait for all four products' layers. | (b). mu-Tronic launches only when all four layer libraries (including mu-Tant and mu-Clid) are ready, i.e. after M5 and T1/T2/T8a. The "useful after E3" point (M3) is an **internal checkpoint only**. The "ship before T5/T7" option is removed. |
| **A13** | **Decided (Andy): review grade.** Who runs the contract and the Clid extraction. | | The contract design (E0d and E1) and the mu-Clid extraction (E5) run as **Opus-grade agents**; the main conversation coordinates. |
| **A14** | **Decided (Andy, 2026-10-15): the recommendation, runtime engine modules NOT adopted.** Licence state in mu-Tronic: Andy, "I think mu-tronic will carry the license state of the engines it uses, to be confirmed." | Andy later: engines loaded at runtime from the installed apps, then "not committed, if not worth it then fine". Sections 8 and 9. | Static layer libraries compiled into mu-Tronic; engines need not update independently (suite-wide releases; mu-Tronic is rebuilt and released whenever an app's engine changes). The three zero-cost E1 constraints of 9.3(1) are adopted. Licence follows the install through a small per-app manifest; mu-Tronic has no key code. Whether it shows a Support prompt for an installed-but-unlicensed engine is a later T1 / `ux-controller` decision (9.6). |

Also answered: **Rumble reads any layer type: yes** (design.md still-open item 3 closed).

---

## 8. A14 — licence state in mu-Tronic (analysis, 2026-10-15; **decided 2026-10-15, see the ruling below**)

**Ruling (Andy, 2026-10-15).** Licence state follows the install: each app writes a small manifest (supporter flag, checkout URL, content dir) at install and at
every launch; mu-Tronic reads it and has **no key code, no `LicenseKey.h`, no `ActivationOverlay` and no Lemon Squeezy product of its own** (design.md 3.1,
freeware, stands). Options 1 to 5 below are kept for the facts in 8.1 and 8.3 only; options 2 to 5 are **not adopted** (no `initLicensing` generalisation, no
`LayerTypeInfo::licenceProductId`, no family key, no cross-reading of activation files). The manifest write is one small change per app, made when mu-Tronic T1
starts (not an E-stage item). Whether mu-Tronic shows a generic Support prompt for an installed-but-unlicensed engine in use is a later T1 / `ux-controller` decision.

**Earlier note (superseded).** Andy had clarified that mu-Tronic would load the engines at runtime from the installed apps (section 9); that model is not adopted,
but the licence answer of 9.5 holds unchanged with static libraries.

**Conflict.** design.md 3.1 assumes mu-Tronic is freeware: no `LicenseKey.h`, no Support prompt, no `ActivationOverlay`. Andy now thinks it will carry the licence
state of the engines it uses. The two cannot both hold, so this is ruled before E1: it decides whether the contract has a licence hook in it.

### 8.1 What the licence system is today (read from the code)

- **Keys remove nothing but the Support prompt.** There are no feature limits ([ProcessorBase.h](../../mu-core/Plugin/ProcessorBase.h) "an unlicensed copy is fully
  functional"). Pricing is Pay What You Want (2026-10-14), so a key means "supporter". `isSupporter()` is a pure function in `License/SupportPromptPolicy.h`.
- **Licence state is per `ProcessorBase`, one bit.** `initLicensing(config)` (defined in `License/ProductLicensing.h`, included only in a licensed product's
  processor) takes a `LicensingConfig` (product id, licence file, public key, activation file, checkout URL) and sets `licensingEnabled`, `offlineLicensed`,
  `onlineActivated`. `isLicensed()` is `!licensingEnabled || offlineLicensed || online`. A processor holds **one** config.
- **Offline `.lic`**: signed with Ed25519 per product; the product id is a field in the file and the public key is embedded per product, so a mu-Tant key
  fails on mu-Toni offline. **Online activation** (Lemon Squeezy, `OnlineActivation`) is passed a content dir and a file name and **does not check the product id**:
  any valid Lemon Squeezy key activates whichever product asks, and the result is stored in that product's own content folder. So today a purchase of one
  product does not carry to another only because each stores its activation separately and the offline path checks the product.
- **Boundary.** `License/` is in mu-core, and `check-layer-boundary.ps1` (D2) forbids a layer-library file including `Plugin/` or `License/`. A layer library
  therefore cannot ask "am I licensed"; licence state lives in the host (the processor). That is already how it is: no engine, sequencer or `Layer` class
  reads it. The conclusion holds for any option below: **the layer libraries stay licence-free and need no change**; the licence is a host concern and the
  contract (E1) needs only an optional, generic, host-side lookup (8.3).

### 8.2 Options

| Option | What it means | Pros | Cons |
|---|---|---|---|
| **1. Freeware, no licence** (design.md today) | No licence code; no Support prompt. | Simplest; no `License/` in the product; matches mu-Link and mu-Clid MIDI. | Gives the PWYW engines away with no supporter path (design.md risk 10); contradicts Andy's thought. |
| **2. mu-Tronic free, but one Support prompt, any product key counts** | `initLicensing` with a mu-Tronic config; a supporter is anyone with a valid key for **any** of the four products (checks each product's public key / activation file). | One prompt, one entry box; supporter of any engine is a supporter; no per-type UI. | A mu-Toni-only supporter silences the prompt for mu-Tant's engine too (acceptable: keys unlock nothing). Needs a "licensed by any of N configs" host feature. |
| **3. Licence state per layer type (Andy's thought, literal)** | Each layer type shows/respects the licence of the product it comes from; the Support prompt names the unsupported engines in use. | Faithful to PWYW (support the engine you use). | Most work: N configs, per-type state, a prompt policy for "some engines supported". Because keys unlock nothing, per-type state has no function other than prompt text. UX is heavy for a free product. |
| **4. One family key** | A single key (a new product id, e.g. `family`) supports everything; mu-Tronic and the apps accept it. | Cleanest model for "supporter". | New key type, new Lemon Squeezy product, changes the four live licensing paths and `OnlineActivation`'s lack of product check becomes a design issue; touches live products. Owner decision (pricing, store). |
| **5. mu-Tronic free; each *product* unchanged; mu-Tronic reads the other apps' stored licences** | mu-Tronic checks the content folders of the installed products for their `.lic` / activation files. | No new keys. | Cross-product file coupling; breaks the "mu-Tronic is a separate install" idea; fragile. Not recommended. |

### 8.3 What each needs in the shared code and how the boundary is kept

- **Layer libraries: nothing, for every option.** They never include `License/`. The licence-per-type information is **data the registry carries** (option 3):
  `LayerTypeInfo` (already in `mu-core/Sequencer/`, describe-only) would gain `const char* licenceProductId` (a string, no `License/` include), and the host
  (mu-Tronic `Plugin/`, which may include `License/`) maps it to a `LicensingConfig`. That keeps `Sequencer/` and the libraries free of `License/`, and keeps
  `mu-core` plugin-agnostic (the field is an opaque id, not a product symbol).
- **`ProcessorBase`:** options 2 and 3 need `initLicensing` generalised from one config to a list (or a second entry point, `addLicensingConfig`), with
  `isSupporter()` computed over the list; today it holds single `licensingEnabled / offlineLicensed / onlineActivated` booleans. That is a small `mu-core` change
  but it touches the live products' licensing path, so it needs the same golden/behaviour pin as any live code and `SupportPromptPolicyTests`.
- **Public keys:** per-product public keys live in each product's `License/LicenseKey.h` (product code). mu-Tronic would need all four in its own `License/`
  folder (options 2, 3): a product-side copy of four keys, the one place the "unlicensed engine" fact is wired; no duplication of logic, only of constants.
  Option 4 needs one new key pair (outside the repo, under the existing secrets rule).
- **`OnlineActivation`:** no product id is checked, so option 2 needs only "activation exists in any of N stored files". For option 3 or 4 the missing
  product check becomes a correctness issue (a mu-Tant key would mark mu-Toni's engine supported); add a product check in `OnlineActivation` first.
- **Freeware today is also true in the code:** the product that never calls `initLicensing` always counts as licensed (`hasLicensing() == false`); option 1
  needs nothing.

### 8.4 UX impact (the `ux-controller`'s to design if chosen)

- Option 1: none. Option 2: one Support prompt and one key entry (a settings section) in the multi-engine host, wording "any mu key"; the prompt policy
  (`SupportPromptPolicy`) is unchanged. Option 3: the prompt and the key entry must say *which* engine, and the chooser may mark engines the user has not
  supported; a per-engine key list in settings; the highest effort and the most confusing for a free product. Option 4: one entry box; needs pricing messaging.
- Common risk to avoid: a Support prompt on a free product that names engines the user cannot "unlock" invites the question "what does the key do?"; the
  existing answer (nothing; it thanks the author) has to be explicit.

### 8.5 Recommendation and the minimal question for Andy

**Recommendation: option 2 now, with the door kept open for 3.** Reasoning: keys unlock nothing, so a per-type licence state has no function except the
prompt wording; a single "supporter of any mu product" state delivers what Andy wants (supporters are recognised, the prompt appears for people who have supported
none of the engines), costs one generalisation of `initLicensing` and four embedded public keys, and does not touch any layer library or the contract. Keep
`LayerTypeInfo` able to carry an optional `licenceProductId` string (cheap, added with E1 only if Andy chooses 3) so per-engine state can be added later without
changing a layer. Do not choose option 4 without Andy's pricing/store decision; do not choose 5.

**Effect on the plan.** Under option 1 or 2 nothing in E0 to E5 changes (the programme touches no licensing code); the licence work is a mu-Tronic T1
item. Under option 3 E1 adds the optional `licenceProductId` field to `LayerTypeInfo` and `initLicensing` becomes list-based in a mu-core change that must be
pinned on the four live products. So the answer is needed before E1 only to decide whether that one field is added.

**Minimal question for Andy:** "Should mu-Tronic show the Support prompt, and if so should a key from *any* mu product silence it (option 2), or should it
track each engine separately (option 3)?" A "free, no prompt at all" answer is option 1 and is also valid.

---

## 9. Runtime engine modules (Andy, 2026-10-15 clarification; **ruled 2026-10-15: NOT adopted**; Andy: "I'm not committed to that approach, so if it's not worth the effort then fine")

**Decision (Andy, 2026-10-15): "Go with the recommendation. Updates will be suite wide so mu-tronic will rebuild if the engine of an app does."** Runtime loading is
not adopted; the hybrid of 9.4 is the plan. This section is kept as the record of the analysis and as the starting point if a reason for modules ever appears
(a third-party engine, a store requirement); the module model would then be stage M on top of the finished libraries.
Everything from "What this supersedes" to the end of 9.3 describes the module model that was **not chosen**; its supersessions of design.md do **not** take effect.
design.md 2.5, 3.2 and the stage list stand. What **is** adopted from this section: 9.3(1) (the three E1 constraints), 9.4 items 1 to 5, and 9.5 (licence follows the install, via the manifest).

**What Andy first said.** mu-Tronic does **not** contain the engines. It requires the standalone app of any synth whose engine it uses (installed mu-Clid, mu-Tant,
mu-Toni, mu-On) and **loads the engine at runtime from the installed apps**. Licence state follows from the app being installed (and activated).

**What this supersedes.** design.md 2.5 ("one CMake `INTERFACE` library per product, linked into mu-Tronic"), 3.2 (CMake links the layer libraries), the T4 to T7
"mu-Tronic registers the type" wording, the equivalence test as "a mu-Tronic slot built from the same library", and design.md risk 10 / still-open items 1 and 2.
It also changes D2 and D3 of section 1.2 in meaning (see 9.3). It does **not** change what E0 to E5 do to each product's own tree.

### 9.1 The hard problems

| Problem | Finding | Rule that follows |
|---|---|---|
| **ABI between independently released builds** | A module and mu-Tronic are built at different times from different commits. C++ across a DLL boundary (vtables, `std::` layouts, `juce::` classes, name mangling, MSVC vs GCC/Linux) breaks silently. | **A C ABI only**: `extern "C"` entry points, POD structs, a version number and a size field at the head of every struct, additive-only minor changes. No C++ class, virtual, exception, `std::` type or `juce::` type in the interface. |
| **Separate heaps (`CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded`)** | Root `CMakeLists.txt` embeds the CRT statically, so host and each module have their own heap. Anything allocated in one and freed in the other, or held by value across (`std::string`, `std::vector`, `juce::String`, `juce::MidiBuffer`, `juce::AudioBuffer` that owns memory, `ValueTree`), is a crash. | The module that allocates frees: every object is an **opaque handle with a paired `destroy`**. Strings are UTF-8 `char*` into a **caller-provided buffer** (or a module-owned `const char*` valid until the next call). Audio is `float* const*` plus a frame count, host-owned memory the module only writes. MIDI is a POD event array (frame offset, 3 bytes). State is a UTF-8 XML string through a caller buffer (it is the ValueTree already). |
| **Audio-thread contract** | `RenderContext` is already a POD (C4); the filtered MIDI view becomes a POD array pointer plus count; `peer(slot)` becomes `const float* const* peer` pointers (Rumble). The module must catch every exception and return a code; `render` is `noexcept` in effect. Denormal state is per thread, so `ScopedNoDenormals` in the host covers the module. | `render(handle, const MuRenderCtx*, float* const* out, int n)` and nothing else on the audio thread; no host callback during render. The `AllocationProbe` (E0.7) cannot see a module's own CRT heap, so each module carries a build-time probe hook and the module test asserts zero allocations itself. |
| **What mu-core is** | `mu-core` is source, an `INTERFACE` library: every module compiles **its own copy** (`Layer`, `ModulationMatrix`, `InsertProcessor`, FX, wavetable code). Nothing needs to match between copies because only the C ABI crosses. The module owns the whole layer: engine, sequencer, modulators and the matrix (still the single reader, inside the module), the per-layer insert, clips, extras. The host keeps what is not the layer: mixer strip (`ch{N}_`), sends, buses, FX returns, master, transport, scenes. | **Common = the ABI header only** (`mu-core/Module/MuEngineApi.h`, pure C, no JUCE, no `std`). mu-core version skew between host and module is irrelevant. |
| **Parameters and APVTS (everything in APVTS)** | The host cannot compile in a module's parameter table. The module exports **parameter descriptors** (id suffix, name, range, step, default, group) per slot; the host builds the 16-slot APVTS layout **after scanning the modules** (standalone only, so an app installed after launch needs a restart) and gives the module `const float*` to the live values (read relaxed-atomic, written by the host). Saved state stays host APVTS, so "if it isn't in the ValueTree it won't save" still holds. | `addLayerParams` becomes descriptor export. Parameter ids stay `r/v/a/t{N}_` (unchanged data). The module never calls `getRawParameterValue`; it has pointers. |
| **UI content (JUCE components)** | A module's `RhythmContent`/`PatternContent`/`ArpContent`/`TrackContent` are JUCE `Component`s using the module's own JUCE copy, its own `MuLookAndFeel`, `MessageManager` singleton, typeface cache. Handing a module-created `Component*` to the host's component tree crosses two JUCE copies and two heaps: not viable. Three ways: **(i) native child window**: the module creates its own component in its own JUCE and attaches it to a host-supplied native parent handle (`HWND`/X11), exactly the VST3/CLAP GUI pattern; (ii) **host-drawn generic UI** from a parameter + control description (no module JUCE); (iii) one shared JUCE DLL (unsupported by JUCE, version-locked, contradicts static CRT). | Recommend (i). Costs: DPI scaling, focus and keyboard, overlay z-order (a native child window always paints over host overlays: the host's modals and drag-and-drop cannot cover it), resizing, and the module needs a working JUCE `MessageManager` hook in a loaded DLL (plugins already do this). (ii) loses `GatingDesigner`, the step grid, the sample browser and breaks the one-UX rule; it is the fallback only for a headless/simplified layer. |
| **The module needs a processor** | After E1 to E5 the layer UI takes `ProcessorBase&` + `<Type>Host&` (D4) and the layer's `bindParams(apvts, slot)`. Across an ABI the module has no host `ProcessorBase`. | The module wrapper contains a **small headless processor** (`ModuleProcessor : ProcessorBase`, one slot, own APVTS mirror fed from the host's values, no mixer output used) so the unchanged content classes have a `ProcessorBase&`. This is the single largest cost of the model and it is not removed by E1 to E5; it is built on top of them. |
| **Presets and clips** | Layer presets are files (`.muArp` etc.), already format-stable and shared folders (design 3.4). Clip = the type's layer preset (a string). | They cross as XML text. Unchanged. |
| **Services (wavetable bank, `SampleLibrary`)** | Per-module and private (a heap and a JUCE copy each). No sharing between modules. | A9 ("services at T1") is moot; `LayerServices` does not cross the ABI. Memory duplication (two wavetable banks, Tant and Toni, as today) is accepted. |
| **Lifetime, unload and the retire bin** | A module's code must outlive every object it created, including a retiree still draining in the `RetireBin`. Unloading while a retiring layer exists is a use-after-free in the audio thread. | **Never unload a module before process exit** (the simple, safe rule: modules stay loaded for the life of the process). The `RetireBin` holds `{handle, module->destroy}` and the module's load count is raised for every live handle; shutdown order: stop audio, flush the bin, destroy all handles, then unload. Hot reload of a module is explicitly not supported. |
| **Discovery** | Apps are installed to different places (Documents\TDP\<app> is only the content folder, and `contentDir` is a user setting). | Each app's installer (and the app's first run) writes a small manifest in a fixed per-user location (`engines/<app>.json`: module path, engine id, API version, app version, content dir, supporter flag). mu-Tronic reads manifests at startup; it never scans the disk blindly. Modules are loaded only from a path an installer wrote and, on Windows, must be Authenticode-signed once signing exists (a user-writable folder + `LoadLibrary` is an injection vector; macOS signing is on hold). |
| **Versioning and the compat range** | Apps ship on their own schedule. | `mu_engine_api_version()` (major.minor), `engine_version`, and `state_format_version`. mu-Tronic supports **its own major, any module minor >= the minimum it needs**. A major break is a new function name set (`..._v2`), the host keeps loading v1 for at least two releases. A module whose major is unknown is shown as "update <app>" and not offered. State newer than a module understands is refused with a message (existing "missing means default" covers older). An ABI snapshot test freezes struct sizes and offsets. |
| **Debug vs Release, Linux** | Separate heaps make Debug/Release mixing harmless with a C ABI. Linux: `dlopen` with hidden visibility and only the exported C symbols; libstdc++ is shared, so the C ABI still applies. macOS on hold. | Same rule. |

### 9.2 Alternatives compared

| | (A) Engine modules, purpose-built C ABI | (B) Host the installed apps' VST3/CLAP plugins inside mu-Tronic | (C) Static libraries in mu-Tronic + licence check (not chosen; reference) |
|---|---|---|---|
| Idea | CLAP-like: version query, create layer, render, state, GUI. | `AudioPluginFormatManager`; each layer = a plugin instance of an installed product. | The plan up to this clarification. |
| Fits "layer" (engine + sequencer + one mixer channel) | Yes: the module is exactly the layer; the mixer, buses, scenes, FX stay host. | **No.** Each instance is a full processor: its own mixer, FX sends/returns, master, transport, up to 8 layers, own editor. 16 instances mean 16 mixers and FX chains; mu-Tronic's strips, buses, scenes and clip alignment would sit on top of duplicates; per-layer sidebar/header integration is lost; a "layer" would be a whole app. | Yes. |
| UI | Needs child-window embedding and a module processor (9.1). Large. | Free (the plugin editor in a window). | Free (same code). |
| State, presets, params | Descriptor export + XML state, moderate. | Free (plugin state), but opaque: no cross-layer modulation, no `peer()` for Rumble, no per-layer clip/scene control except through automation. | Free. |
| Licence | Module wrapper reports supporter flag (9.5). | Automatic (the plugin shows its own Support prompt in its own window). | Needs licence code in the host (section 8). |
| Needs the extraction programme (E0 to E5) | **Yes**, then a module stage on top. | **No.** | **Yes.** |
| Formats | Standalone-only mu-Tronic loads modules built from the app's tree; no VST3 needed. | Needs VST3 of every product present (mu-Toni/mu-On are VST3 + CLAP; fine) but couples to plugin-hosting limits (threading, one instance per process issues, host-in-host). | Standalone only. |
| Effort | Highest of the three (below). | Lowest and available soonest, **but it is a different product** (a plugin rack), not "the voices of the other apps". | Medium (current plan). |
| Independent release | Needs a stable C ABI discipline forever. | Free (VST3 is the ABI). | Not independent: mu-Tronic must be rebuilt for any engine fix. |

### 9.3 Consequences for the extraction programme

**E0 to E5 still hold, and are still first.** Extracting the layer into a self-contained library inside each product is a prerequisite for (A) and (C) and is
valuable alone; nothing in this clarification argues for building an ABI before the layers are self-contained. Reasons not to put the ABI into E1:
the ABI is the interface of the *finished* layer (parameters, state, render, UI), which only exists after a product is extracted; E2 (Toni) is the right place
to find out what the C boundary really needs; building an ABI first would freeze guesses.

What changes:

1. **E1 stays the in-process contract** (`Layer::render`, `RenderContext`, `RetireBin`), with three added constraints at no cost: (a) `RenderContext` is an exactly-POD,
   pointer-and-count struct with no JUCE type (C4 already says POD; make `const juce::MidiBuffer&` a view struct that a C struct can mirror); (b) layer state
   in and out is text (the existing ValueTree XML) and layer-created objects never escape; (c) nothing in `render` calls the host. All three are good design anyway.
2. **Stage M (module wrapper) exists only if Andy chooses the module model (9.4: not recommended)**; then it runs after E6 and before any mu-Tronic product work, one module per product, in this order: **M0 spike (Toni)**: C ABI header,
   loader, one Arp layer in a mu-Tronic window with the **embedded native editor**, DPI/focus/overlay behaviour judged by Andy and the `ux-controller`, and a
   golden equivalence against the app. **The spike is the gate for the whole model**: if the child-window editor is not acceptable, the answer is (C) or a
   host-drawn UI, decided then. M1 On, M2 Tant, M3 Clid, then the shared pieces.
3. **Gates added:** (a) `check-layer-boundary.ps1` rule: library files still include no `Plugin/`/`License/`; the **module wrapper** (new folder `Module/` is **not** allowed, since
   the eight-folder rule stands: it lives in `Plugin/` as `Plugin/EngineModule.{h,cpp}` and the CMake target `mu-<product>-engine` (SHARED), which may include
   `License/`); (b) `check-module-abi.ps1`: the exported symbols of each built module are exactly the ABI list (`dumpbin /exports`), no C++ in `MuEngineApi.h`;
   (c) an ABI snapshot test; (d) a **version-skew test** with a frozen old module binary kept under `tests/` loaded by the current host; (e) golden equivalence
   *through the module loader* (the same goldens, run through the C ABI, tolerance 0, with a per-module allocation probe); (f) a lifetime test (retiree outlives a
   type change; shutdown order); (g) the loader fuzz case (missing, wrong version, truncated manifest).
4. **D2/D3 amended:** D2 stays (the library is licence-free and processor-free); D3 becomes "the app runs the library **in-process, statically**; the module is a second
   build of the same sources behind the C ABI". An app does **not** `dlopen` its own module (it would add a failure path to a live product for no gain); the golden
   equivalence between the static and the module build is the proof they are the same code.
5. **Contract shape (`mu-core/Module/MuEngineApi.h`, pure C).** Per module: `get_api_version`, `get_info` (id, display name, group, prefix letter, preset extension,
   colour token, `engine_version`, `state_format_version`, supporter flag + checkout URL), `get_params(slot-independent descriptors)`, `create(slot, sr, block)` /
   `destroy`, `bind_params(handle, const float* const* values)`, `render`, `loop_beats`, `preload_staged(handle, xml)`, `on_transport_stopped/jumped`,
   `get_state/set_state` (XML into caller buffer), `begin_retire` / `tail_blocks`, `render_priority`, `gui_create(handle, native_parent, w, h)`, `gui_resize`, `gui_destroy`, `mod_live`
   (the 30 Hz UI tables as plain float arrays). Everything has a `size` field and is additive.
6. **Per-product build, installer, CI, Linux** (each of five products): CMake `mu-<product>-engine` SHARED target from the `mu-<product>-layer` sources + the wrapper;
   the installer and the release zip add `engine/` plus the manifest write; `release.yml` builds and packages it for Windows and Linux (Linux: hidden visibility, `.so` in the
   tarball, same manifest); `check-module-abi.ps1` and the loader tests in CI; mu-Tronic's installer **declares dependencies** (it works with zero apps installed: it
   opens with an empty chooser and a "install an app to get its layers" message).
7. **Every live app needs a release that contains its module before mu-Tronic can launch.** The earlier A1 answer (no release before E4/E5) still stands, but the first
   mu-Tronic release is now blocked on a release of each of the four apps (a normal release; the module is additive). Pay-What-You-Want apps that do not yet have
   the module simply do not appear in the chooser.

**Revised estimate.** The 39 to 61 session range (about 49) covers E0 to E6 and is unchanged. The module model adds: ABI header, loader, manifest, versioning, its tests
(6 to 10); the headless `ModuleProcessor` and descriptor export (5 to 8); embedded-editor spike (M0, Toni) including DPI, focus and message-loop work (6 to 10);
three more module wrappers (3 to 6 each, 9 to 18); installer, `release.yml` and Linux packaging for five products (5 to 8); module golden/skew/lifetime tests and
the allocation probe per module (5 to 9). **Added: about 36 to 63 sessions. New total about 75 to 124 (central about 95)**, more than double. Honest reading: this is
a different, larger programme than "extract the layers", and the upper bound is dominated by the embedded editor.

**New risks (added to the register as R16 to R22).** R16 child-window UI (overlay z-order, DPI, focus, drag-and-drop) may not meet the family look and feel
(High/High; the spike exists to find out). R17 independent releases across a frozen ABI force a compatibility burden forever (Medium/High). R18 module + host each with
their own JUCE message handling in one process (Medium/High). R19 four modules each carry full mu-core and a JUCE copy: memory and load time of 16 layers x private
services (Medium/Medium). R20 DLL loading from user-writable folders is a security risk until signing exists (Medium/Medium). R21 a live app's release now carries
a module build as well (Medium/Medium). R22 `AllocationProbe` and the lock probe cannot see across module heaps (Medium/Medium, mitigated per module).

### 9.4 Cost/benefit and recommendation (a call, not an exercise in making loading work)

**What Andy would gain from runtime loading, and how real each gain is:**

| Gain | How much it is worth |
|---|---|
| Engine fixes reach mu-Tronic when the user updates the app, without a mu-Tronic release | Real but small. mu-Tronic is freeware with one developer and `release.yml` already builds and packages all products; with static libraries, an engine fix is one extra mu-Tronic build and release, and the code is the same library (no new test). The module model buys this by freezing a C ABI forever. |
| Independent updates of engines | Same item. Also a liability: mu-Tronic must then run against engines it was never tested with (a skew matrix for ever). |
| Licence follows the install | Achievable without loading anything: detect the installed apps' manifests/activation state (see 9.5, hybrid). Loading is not needed for it. |
| Smaller mu-Tronic | Irrelevant for a desktop standalone; four statically linked engines are a few tens of MB at most. |
| mu-Tronic does not redistribute the engines | A policy point, not an engineering one; the engines are Pay What You Want and the Support prompt is the only thing a key affects. |

**What it costs:** the C ABI and a versioning/compat policy that must hold for every release of five products; the CRT/heap boundary (no `std::`/`juce::` types
across; opaque handles everywhere); the UI problem, which is the real cliff (a module's JUCE components cannot join the host's tree, so an embedded native child
window per layer, with overlay z-order, DPI, focus and a second `MessageManager`, or a host-drawn generic UI that abandons the family look); a headless
`ModuleProcessor` in each module so the unchanged content classes have a `ProcessorBase&`; installer, `release.yml` and Linux packaging for five products; a signing
question for DLLs loaded from user folders; and a release of every live app before mu-Tronic can launch. Revised estimate: **+36 to +63 sessions on top of the
39 to 61, total about 75 to 124 (central about 95)**, with the embedded editor dominating the upper bound; new risks R16 to R22 (9.3). That is more than the whole
extraction programme again, to save one extra mu-Tronic release per engine fix.

**Is (B) better?** No. It is cheap and independent, but each hosted instance is a whole app with its own mixer and FX; it is a plugin rack, not "the voices of the
other apps", and gives up scenes, buses, `peer()` and clip alignment.

**Recommendation (adopted by Andy, 2026-10-15): do not adopt runtime loading. The hybrid:**

1. **Keep E0 to E6 exactly as agreed.** They are needed by every option and valuable alone.
2. **Static libraries compiled into mu-Tronic (C) behind the layer contract**, as design.md 2.5 and 3.2 say today. Nothing in design.md 2.5, 3.2 or the stage list is superseded.
3. **Keep a module ABI possible later at zero cost now**: the three constraints of 9.3(1) go into E1 (`RenderContext` mirrors a C struct; layer state in and out is text; no host
   callback in `render`), plus: library code never returns or stores `juce::` types across its public layer interface where a plain pointer/string would do. No ABI
   header, no loader, no `ModuleProcessor` is built. If a reason appears (a third-party engine, a store requirement), stage M of 9.3 can be done on top of the finished
   libraries without redoing the extraction, because the library boundary (D2) is the module boundary.
4. **Licence follows the install, without loading** (9.5): mu-Tronic detects which apps are installed and supporters from a small manifest each app writes at install and launch, and shows no
   key entry of its own. This delivers "licence follows the install" and the Support-prompt answer for the cost of a manifest file per app, which is a T1/T9 item,
   not an E-stage item.
5. The A12 timing stands (launch when all four engines are ready). The hybrid adds no release dependency on the four apps beyond the ones they would have anyway; the
   manifest write is one small change per app, made when mu-Tronic T1 starts.

### 9.5 Licence: "follows the install" (answers A14; applies to the hybrid as well as to modules)

Written for the module model; with the recommended hybrid the same holds except that the supporter flag is read from the manifest the **app** writes (the wrapper
`Plugin/EngineModule.cpp` below becomes the app's own `Plugin/` manifest writer; the engines are linked into mu-Tronic and the layer libraries still contain no licence code).

**Licence state is "the app is installed and activated", and mu-Tronic needs no licence code.** Reasons: keys unlock nothing (no feature limits), the engine is loaded
from an installed app that Andy's own PWYW/Support flow governs, and a licence check in a host cannot be stronger than that. The licence code (`initLicensing`,
`SupportPromptPolicy`, the product public key) stays **in the app and, for the supporter flag, in the module wrapper** (`Plugin/EngineModule.cpp` may include `License/`; the
layer library may not, D2). Detection: the app writes its manifest at install and at every launch, including `supporter` (the result of `isSupporter()`), its
checkout URL and its content dir (the content dir is user-overridable in app settings, so mu-Tronic must not guess `Documents/TDP/<app>`); mu-Tronic reads the
manifest. Activation files (`mutant.activation`, `mutant.lic`) are read by the module (inside the app's code, in the app's content dir) so the host never parses them
and `OnlineActivation`'s missing product-id check stays an app-internal matter (it does not become a cross-product trust issue).

If the app is installed but unlicensed: the engine works fully (no limits). The Support prompt is a **UI element of the layer's app**; mu-Tronic shows one generic,
low-key prompt per unsupported engine **that is in use** (text and URL from the manifest, policy from `SupportPromptPolicy`, which is pure and lives in mu-core), so the
prompt does not depend on the user opening the app itself; this is a `ux-controller` decision. If Andy prefers that the prompt appears only inside the apps, mu-Tronic
shows nothing and the users who never open the apps are never asked. Not installed: the type is not offered. **Freeware mu-Tronic (design 3.1) stays true**: it
has no key, and design risk 10 (free product carries PWYW engines) is resolved, because the engines are not in it.

### 9.6 The question and its answer

**Must the engines update independently of mu-Tronic?** **Answered by Andy, 2026-10-15: no.** Releases are suite-wide; mu-Tronic is rebuilt and released whenever an
app's engine changes. Static libraries stand, about 36 to 63 sessions are saved, and nothing else is lost. Consequences recorded:

- The three zero-cost E1 constraints of 9.3(1) are **adopted as E1 requirements**: `RenderContext` mirrors a C struct (POD, pointer-and-count, no JUCE type; the MIDI view is a
  struct a C struct can mirror); layer state in and out is text (the existing ValueTree XML) and layer-created objects never escape; `render` makes no host callback.
- No ABI header, loader, `ModuleProcessor`, `Plugin/EngineModule.*`, `check-module-abi.ps1` or stage M is built; R16 to R22 do not apply.
- Release process: a release of any app's engine implies a mu-Tronic rebuild and release in the same suite release (a `/release` step to be added when mu-Tronic T9 is planned).
- Licence: the per-app manifest (supporter flag, checkout URL, content dir) is written at install and at every launch; mu-Tronic has no key code (9.5). The small follow-up,
  whether mu-Tronic shows a generic Support prompt for an installed-but-unlicensed engine in use or none at all, is **left to T1 and the `ux-controller`**; it is not needed before E1.

---

## Review notes merged

Both reviews are folded into the body (2026-10-15); this is the index.

- **dsp-controller (design: pass with notes).** Four items fixed before contract code: C1 (no suspend / no muting for type changes and scenes, atomic
  publication), C2 and C3 (`RetireBin` state machine, retiree snapshot, services outlive retirees), C7 (scene commit as block-start publication), C4 (time
  model in `RenderContext`, `onTransportJump`, no free-running phase). Secondary items C5, C6, C8 to C18 are in section 2.1a with the stage that tests them.
  Per-stage RT gates are in section 2.1a and 4.6; the CPU plan is C18. design.md section 2.1 points to section 2.1a here. **E0d (2026-10-15):** C1 to C4 are
  specified in design.md 2.1.1 to 2.1.5 with twelve open points in 2.1.6. Round 1 (2026-10-15): Blocked for E1, document edits only, structure stays (C1, C3
  Pass with notes; C2, C4 Not yet); R1 to R7, N1 to N7, the amendments and the open-point rulings are folded into design.md 2.1 and the rows here, pins P1
  to P7 and measurements M1, M2 into E0.6. Round 2 (2026-10-15): F1 and the R5 rule line folded in, with the binding notes (packed-word compare-exchange
  under one writer exclusion, risk R16; lock order; eviction under `layersLock` without a wait; phase 4 below `mixCount`). **C1-C3 signed (round 2, with
  binding notes); C4 accepted once F1 and the R5 rule line are in the doc (dsp-controller: no further round).**
- **test-steward.** The safety net is tests.md "Layer extraction safety net"; this plan sequences it as E0.1 to E0.9, adopts `useLayerRender` (2.7a),
  the approved test-only channel tap, and the seed fix; dropped the earlier WAV/SHA matrix. About 12 to 13 working days of test work, about 6 before any
  code moves. Open: mu-Tant gate-probability generator location.
- **architecture-steward re-check:** pending reviewers' sign-off on the merged text (Pass or Not yet). (A14 ruling of 2026-10-15 recorded in sections 7 to 9; the footer stays pending.)
