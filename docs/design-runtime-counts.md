# Runtime counts: one mu-core library, counts decided at construction

Status: ruling, 2026-11-14 (architecture-steward, Andy's go given). Replaces the per-binary macros `MU_MAX_LAYERS`, `MU_MAX_BUSES`, `MU_BUS_INSERTS`, `MU_EXT_SIDECHAIN`, `MU_VERSION_BASE` and the `mu_core_variant()` library-per-settings scheme. Standing check: this makes the one-framework / one-instance direction in [design-future.md](design-future.md) easier (one binary of mu-core, a host's size becomes data) and a new product needs no CMake variant.

Priorities, in order: correct and real-time safe; fast on the audio path; readable; small binaries; best practice; open to new products.

## 1. Today (what the code does)

- [mu-core/MuLimits.h](../mu-core/MuLimits.h): `kMaxLayers`, `kMaxBuses`, `kBusInserts`, `kExternalSidechain` come from four macros, one value per binary. About 500 uses in about 108 files, about 65 of them array dimensions.
- [mu-core/CMakeLists.txt](../mu-core/CMakeLists.txt) `mu_core_variant()` builds one static library per settings set: default (8 layers) for mu-Clid, mu-Tant, mu-Toni MIDI, mu-Clid MIDI; mu-Tronic 16/3/2/ext 0; mu-Host 1/0/ext 0; mu-On, mu-Toni, mu-Tilate default layers with the pre-release `MU_VERSION_BASE`. Test targets set the macros themselves (`mu-core-tests`, `-16`, `-bus1`, `mu-control-tests`, `mu-tronic-tests`, `mu-host-tests`).
- The settings change the layout of shared types (`MixerEngine`, `ProcessorCore::layers`, `LayerBlock`, `ProcessorBase` members), so [ProcessorBase.h](../mu-core/Plugin/ProcessorBase.h) ends with a layout guard that compares `sizeof(ProcessorBase)` against the library and `_Exit(70)`s.
- Parameters are declared at construction: `MixerFxParams.h` (`kScSrcMax`, per-channel `bus`, per-bus insert params) is evaluated while the product builds its `ParameterLayout`, before `ProcessorBase` exists. So the counts must be known at construction. They cannot be discovered in `prepareToPlay`.
- `MU_VERSION_BASE` is read only by non-real-time UI (`AboutOverlay.cpp`, `UpdateNotice.h`, `ShellChrome.cpp`).

## 2. Ruling

### 2.1 The config type and where it lives

In `mu-core/MuLimits.h` (kept; it is still the one home of capacity invariants), namespace `mu_limits`:

```cpp
inline constexpr int kAbsoluteMaxLayers = 32;   // the 32-bit masks (layer view frozen mask, RetireBin slot loops)
inline constexpr int kAbsoluteMaxBuses  = 9;    // bus parameter ids carry one-digit bus and insert indices
inline constexpr int kAbsoluteBusInserts = 9;

struct ProcessorLimits
{
    int  layers = 8;            // 1..kAbsoluteMaxLayers
    int  buses = 0;             // 0..kAbsoluteMaxBuses; 0 declares no bus parameters and shows no bus UI
    int  busInserts = 2;        // 1..kAbsoluteBusInserts
    bool externalSidechain = true;

    constexpr bool valid() const noexcept;   // range checks above; the one place they live
};
```

The product owns its value as a `static constexpr mu_limits::ProcessorLimits kLimits { ... }` next to its `PluginProcessor` (mu-Tronic `{16, 3, 2, false}`, mu-Host `{1, 0, 2, false}`, everyone else the defaults). It is passed twice, from the same constant, so the two cannot disagree: to the product's `createParameterLayout(kLimits)` (which hands it to the `MixerFxParams.h` helpers) and to the `ProcessorBase` constructor as a new first argument. `ProcessorBase` stores `const mu_limits::ProcessorLimits limits` and exposes `getLimits()`. A processor with `!valid()` fails in every build with a logged message, never clamps silently (same stance as the old guard).

Products keep their own domain alias (`RhythmSequencer::MaxRhythms`) but initialise it from `kLimits.layers`, no longer from a global.

Everything that has no processor in reach (static helpers in `mu_mix`, `LiveState`, `SceneState`) takes the count as an argument or reads it from the object it belongs to. No global `limits()` accessor: a global would make a second instance with other counts (a test, a future multi-instance host) silently wrong.

Constants that stay compile-time and global because they are not per-product: `kLayersPerPage`, `kClipsPerLayer`, `kMaxControlSequences`, `kMaxModulationAssignments`, `kMaxSamplerVoices`, `kRetireDepth`, the MIDI block sizes, `kLayerViewPool`. `kMaxLiveRetirees` becomes `limits.layers * kRetireDepth`, computed once at construction.

### 2.2 What becomes runtime-sized and what stays fixed

The rule: **a fixed array at `kAbsolute*` is allowed only where the element is small plain data (pointer, int, bool, float, `std::atomic<float>`), the array is indexed on the audio thread, and a fixed array is measurably faster or simpler than one base pointer. Everything heavy is runtime-sized.** Both kinds are bounded by the runtime `limits.layers` in every loop.

| Group | Examples | Decision | Reason |
|---|---|---|---|
| Heavy per-layer or per-bus objects | `MixerEngine::channels` (`ChannelState`, with inserts), `buses`, `channelBufs`, `busBufs/busTap/busScSource`, `ProcessorCore::layers` (`unique_ptr<Layer>`), `ProcessorBase` render scratch (`scratch`, `view`, `peers`), `pendingPresetTree`, `clipWatch`, `presetLabels`, `LiveState::clipState`, `SceneState::perLayer` | **Runtime**: one `mu_core::FlatArray<T>` (owns `unique_ptr<T[]>` + count, `begin()/end()/size()/operator[]`, `std::span` view), allocated once in the constructor or `prepareToPlay` | 32 slots of a big type in an 8-layer product is wasted memory and binary data; one contiguous block, one base pointer, no per-element indirection |
| Small hot POD per-layer state | `chBus/chOut/chAudible` locals in `MixerEngine::processBlock`, `channelLastBus`, `scEnv`, `muteParams`, `midiInRaw`, `live`, `loopBeats`, the `directOuts` pointer table | **Fixed at `kAbsoluteMaxLayers`** (stack or member), loops bound by `limits.layers` | A few hundred bytes at most; stays on the stack and in cache; zero allocation and no pointer chase; the compiler loses only the compile-time trip count |
| Atomics read by the UI (`channelPeaks*`, `sidechainGR`, `scEnvResetRequest`, `busPeaks*`) | `MixerEngine` | **Fixed at `kAbsoluteMax*`** | `std::atomic` is neither copyable nor movable; a fixed array is the simplest correct form; 32 x 4 bytes each |
| Bus state when `buses == 0` | the `kNumBuses > 0 ? kNumBuses : 1` idiom | **Runtime** `FlatArray` of size `buses` (empty at 0) | Removes the "size 1 dummy" idiom; every bus loop is a plain range loop that does not run at 0 |
| Compile-time branches on buses/ext sidechain | `if constexpr (kMaxBuses > 0)` in `MixerFxParams.h`, `static_assert(kNumBuses <= 9)` | **Runtime `if`**, once per block (audio) or once per build of the layout (parameters); the static_asserts become `ProcessorLimits::valid()` | One predictable branch per block, no per-sample cost |
| `std::array<...*, MaxChannels>* directOuts` in the `processBlock` signature | `MixerEngine`, `ProcessorBase`, `ProcessorBase_Render.cpp` | **Pointer + count** (`std::span<juce::AudioBuffer<float>*>`) | Signature no longer carries the count in its type |

Per-sample code never indexes through a runtime lookup: loops hoist `n = limits.layers` and the `FlatArray` base pointer once per block. `MixerEngine::MaxChannels` and `kNumBuses` become `const int` members (`numChannels`, `numBuses`) set once; the sidechain source encoding (`kExtSidechainSrc = MaxChannels`) becomes a member computed from them.

The one deliberate loss is compile-time unrolling and dead-code removal of the bus path in products with 0 buses (the whole bus pass is currently compiled out). It becomes one `if (numBuses > 0)` per block. The benchmark (section 5) decides whether that is acceptable; if the full mixer pass measures slower than noise, a fixed `kAbsolute*` array for that member plus the same runtime bound is the fallback, not a return to macros.

### 2.3 `MU_VERSION_BASE` moves product-side

Only non-real-time UI reads it. `ProcessorBase` gets the string with the limits: constructor takes `mu_core::ProcessorConfig { ProcessorLimits limits; const char* versionBase; }` (the config type lives beside `ProcessorLimits`), exposed through `ShellBackend::getVersionString()`. `AboutOverlay`, `ShellChrome` and `UpdateNotice` read it from the backend instead of `MU_VERSION_STRING`. `BuildNumber.h` keeps only `BUILD_NUMBER`; the base is a product constant (`version.txt` or `version-pre.txt` is read in the product's CMake and defined once for that product's own sources, never for mu-core). `cmake/IncrementBuildNumber.cmake` stops emitting `MU_VERSION_BASE`/`MU_VERSION_STRING` into the shared header.

### 2.4 What replaces the layout guard

With the counts out of the types' dimensions and out of the macros, the only remaining cause of a size mismatch between the library and a product is the CLAP base class (`MU_CORE_HAS_CLAP`). Ruling:

1. `MU_CORE_HAS_CLAP` is decided once in `mu-core/CMakeLists.txt` and defined PUBLIC on the one library, so every product and the library see the same value (the library already links `clap_juce_extensions` PUBLIC). `mu-clid-midi`, `mu-toni-midi` and the tests link the same library; there is no INTERFACE-source path left to disagree.
2. `ProcessorLimits::valid()` is checked in the `ProcessorBase` constructor in every build (log, then stop), covering the runtime equivalent of a wrong setting.
3. The `_Exit(70)` sizeof guard, `verifyProcessorBaseLayout`, and the `MU_CORE_STATIC` machinery are removed in the last phase, once only one library exists. Until then the guard stays and protects every intermediate phase.

### 2.5 Library and tests

`mu_core_variant()` and the per-settings static libraries are deleted; there is one `mu-core-static` (mu-clid etc. link it, plus `mu-core-assets`). The test targets lose their macro definitions: `mu-core-tests` runs the same executable over several `ProcessorLimits` values (parameterised fixtures), replacing `-16` and `-bus1` as separate builds. Smaller total build time and one binary of mu-core objects are a side effect, not the goal.

## 3. Phased migration (each phase builds and passes its tests on its own)

Ordered lowest risk first. A phase never changes behaviour; the macros keep working until phase 5.

**Phase 0: benchmark (before anything else; section 5).** Files: new `mu-core/Tests/MixerBlockBench.cpp` target `mu-core-bench` (on demand, not in ALL), `tests/scripts/run-bench.ps1`. Guard: the recorded baseline JSON in `tests/`.

**Phase 1: introduce the types, no behaviour change.** `ProcessorLimits`, `ProcessorConfig`, `kAbsolute*`, `FlatArray<T>` in `MuLimits.h` and a new `mu-core/Audio/FlatArray.h`. `ProcessorBase` gains the new constructor argument, stores and validates it, `getLimits()`; every product passes a `kLimits` built from the old macros (`{kMaxLayers, kMaxBuses, ...}`), and `MixerFxParams.h` helpers take a `limits` parameter and no longer read `kMaxBuses`. Version base moves to the backend (2.3). Files: `MuLimits.h`, `FlatArray.h`, `ProcessorBase.h/.cpp`, `MixerFxParams.h`, each product's `PluginProcessor.h/.cpp` and `createParameterLayout`, `AboutOverlay.cpp`, `ShellChrome.cpp`, `UpdateNotice.h`, `BuildNumber.h`, `IncrementBuildNumber.cmake`. Tests: new `FlatArrayTests`, `ProcessorLimitsTests` (valid/invalid ranges), parameter-set equality (the declared parameter IDs of every product are byte-identical before and after: golden list), all product suites.

**Phase 2: the mixer engine.** `MixerEngine` members move from `MaxChannels` / `kNumBuses` to runtime counts per 2.2; `directOuts` becomes a span; `mu_mix` helpers already take the count. Files: `Audio/MixerEngine.h/.cpp`, `Plugin/ProcessorBase.cpp`, `ProcessorBase_Render.cpp`, `MixerOverlay`, `MixerChannel*`, `MasterInsertBinding.cpp`. Tests: `Bus*Tests`, `MixerBlockValueTests`, `ChannelStateResetTests`, `LayerEquivalence*`, the benchmark (no regression), the frozen baseline harness.

**Phase 3: layer storage.** `ProcessorCore::layers`, `LayerBlock::layer`, `ProcessorBase` scratch/peers/preset/clip members, `RetireBin::kRows`, `LiveState`, `SceneState` to `FlatArray` or bounded fixed arrays per 2.2; `kMaxLiveRetirees` computed. Files: `Plugin/ProcessorCore.h`, `LayerBlock.h`, `ProcessorBase*.cpp`, `RetireBin.h`, `Control/LiveState.*`, `Control/SceneState.h`, `Sequencer/RhythmSequencer.h`, `Sequencer/RenderContext.h`, `LayerTypeInfo.h`, `ProcessorBase_Clips/ClipAutoSave.cpp`. Tests: `LayerRenderTests`, `LayerOwnershipTests`, `LayerSwapPublishedTests`, `RetireBinTests`, `LayerPagingTests`, `LiveStateTests`, `ClipLaunchTests`, soak tests, the benchmark.

**Phase 4: products and the remaining readers.** The 261 product uses (HotSwapStager, `PluginProcessor_*`, mu-On `StepRows/TrackContent`, mu-Tronic state and import, `mu-control` `SurfaceModel`, `LayerSidebar`, `MixerOverlay`) read `getLimits()` or their own `kLimits`; remaining array dimensions in products follow 2.2. mu-Clid's `HotSwapStager` rows come from `kLimits.layers`. Tests: each product's `ProcessorSeamTests`, `NoDemoCapsTests`, `Tronic*Tests`, `HostTests`, `LayerCountPersistTests`, `mu-control-tests`.

**Phase 5: one library.** Delete `mu_core_variant()`, the macros in `MuLimits.h` and `kMaxLayers/kMaxBuses/kBusInserts/kExternalSidechain`, the macro definitions in every CMake file, the `-16` / `-bus1` test builds (replaced by parameterised fixtures), and the `MU_VERSION_BASE` override. `MU_CORE_HAS_CLAP` as 2.4. Gate: `rg "MU_MAX_LAYERS|MU_MAX_BUSES|MU_BUS_INSERTS|MU_EXT_SIDECHAIN|mu_core_variant"` returns nothing outside docs; add it to `check-core-boundary.ps1`.

**Phase 6: retire the guard and tidy.** Remove `verifyProcessorBaseLayout` and the `MU_CORE_STATIC` guard block; update this doc, `design-plugin-family.md` (build and variant sections), `design-naming.md` if names change, the per-product CLAUDE.md lines that mention the macros. Full Debug build of all products, one test sweep, benchmark rerun.

## 4. Real-time rules during the migration

- All `FlatArray`s are allocated in the constructor or `prepareToPlay` and never resized afterwards. A layer count above `limits.layers` is a refused request (as `kMaxLayers` is today), never a growth.
- No `std::vector` growth, no `new` on the audio path; `check-rt-safety.ps1` runs per phase.
- Layer/bus counts stay `int` and are read once per block into a local; a layer added at runtime (`ProcessorCore::numLayers`) is still bounded by `limits.layers`.
- A processor's count is immutable after construction (`const`); no instance changes it, so there is no cross-thread visibility question.

## 5. Benchmark that must exist before phase 1

A headless `mu-core-bench` target (not in ALL; Release or RelWithDebInfo for the numbers, Debug only for a sanity run) built on the existing test rigs (`LayerContractFixtures.h`, `BusTestRig.h`, the frozen baseline harness):

| Case | Setup | Measured |
|---|---|---|
| mu-Tronic shape | 16 layers all active (a real layer type with an insert each), 3 buses with 2 inserts, ext sidechain off, 48 kHz, 256 and 64 sample blocks | wall time per `processBlock`, median and p99 over about 5000 blocks after 500 warm-up blocks |
| mu-Clid shape | 8 layers all active, no buses, ext sidechain on | same |
| mu-Host shape | 1 layer active, no buses | same |
| Mixer only | the three shapes with a fixed test source per layer, so the mixer is not hidden by voice cost | same |

Method: pin the thread, run the three cases interleaved three times, take the best of the three medians; record the machine, compiler and flags in the result file. Output: JSON in `tests/benchmarks/` (baseline recorded from the pre-change tree on this PC). Pass criterion for every phase that touches the audio path (2, 3 and 5): **median and p99 within 2 percent of baseline, or within the baseline's own run-to-run spread if larger.** A phase that misses it is fixed (bound the loop, hoist the pointer, fall back to a fixed array per 2.2) before it is committed, not accepted. Also record binary size of each product's VST3 at baseline and after phase 5; the target is no growth.

## 6. Risks and review

- **Parameter set drift (highest risk).** Parameter IDs and counts must be identical to today for every product, or saved projects and presets break. Guarded by the golden parameter-ID list in phase 1; any change is an owner decision.
- **Silent loss of unrolling or inlining** in `MixerEngine::processBlock` and the bus pass. Guarded by the benchmark; fallback in 2.2.
- **Fixed vs runtime mix confusion**: a loop bounded by `kAbsolute*` instead of `limits.layers` would process dead strips. Rule: no loop in a `.cpp` may name `kAbsolute*`; only declarations do. Add a grep to `check-core-boundary.ps1` in phase 5.
- **Layer-index encodings** that embed the count (sidechain source `kExtSidechainSrc`, `kScSrcMax`, scene/clip arrays, the `ProductImport` layer-count constant in mu-Tronic) must all come from the same `limits`; covered by `BusSidechainTests` and `ProductImport` tests.
- **Persistence**: any stored array sized by count (scenes, clip states) keeps its on-disk shape; the file's own count, not `limits.layers`, bounds a read.
- **Ordering/ownership**: moving storage from members to heap changes construction order of `ProcessorBase` members (apvts must stay the first member). Keep `limits` declared before every member that is sized from it.

Needs `dsp-controller` review (a Blocked verdict stops the change): phases 2 and 3 (every audio-thread array, the render scratch lifetime, the `directOuts` span, the bus-count branch, no allocation in `processBlock`), and the phase 5 benchmark result. Phases 1, 4 and 6 are structural and need only the static checks plus the test suites.
