# mu-Tronic — design (architecture ruling, 2026-10-15)

**Status:** design only. Nothing is built. Written by the Architecture Steward for Andy's brief of 2026-10-15 and
revised the same day after his answers (section 9 records them); the look and feel of every screen below is the
`ux-controller`'s, and real-time review of the audio-thread parts is the `dsp-controller`'s before anything is
pushed. Only the short "still open" list in section 9 needs Andy; everything else is ruled.

**Settled by Andy, 2026-10-15 (the shape of the product):** a fifth product; **Standalone only** (no VST3, no CLAP); **freeware** (no licence key, no
Support prompt); name mu-Tronic, plugin code `MTrn`; three buses with two inserts each; every mu-On engine is its own layer type; scenes; mu-link
left as it is; the product refactors of mu-Clid and mu-Tant wait until after their next releases. **Superseded later the same day:** Andy re-prioritised, so all four extractions come before any mu-Tronic product work; see [extraction-plan.md](extraction-plan.md), which replaces the ordering in section 6.

**Decisions of 2026-10-15 (later), recorded in [extraction-plan.md](extraction-plan.md) section 7:** the first mu-Tronic release waits until **all four engines** are
ready (Andy: "No, all four engines for launch. There's no rush, so we get each one ready"), so the earlier "useful after E3" point is an internal checkpoint only and the
"ship before T5/T7" option is removed (decision 5 and still-open item 1 below are closed that way); Rumble may read any layer type (still-open item 3 closed); mu-Tant Note
mode keeps its final-mix gate as a mu-Tant shell function (live product bit-identical); noise is seeded per layer slot. **Decided (A14, Andy, 2026-10-15: "Go with the recommendation. Updates will be suite wide so mu-tronic will rebuild if the engine of an app does."):**
runtime engine loading is **not adopted**. Sections 2.5, 3.1 and 3.2 stand: static layer libraries compiled into mu-Tronic; engines do not update independently of mu-Tronic
(suite-wide releases; mu-Tronic is rebuilt and released whenever an app's engine changes). Licence follows the install through a small per-app manifest (supporter flag,
checkout URL, content dir) written at install and at launch; mu-Tronic has no key code of its own and stays freeware. Whether it shows a generic Support prompt for an
installed-but-unlicensed engine is a later T1 / `ux-controller` decision. Three zero-cost constraints go into E1 (`RenderContext` mirrors a C struct, layer state is text, `render`
makes no host callback) so a module ABI stays possible. Analysis: extraction-plan sections 8 and 9.

**Andy's brief.** "An empty unit with the same mod, effects and mixer as all the other apps. The voices are empty slots, into which we can add a voice
from any of mu-clid, mu-tant, mu-toni and mu-on. mu-tronic will hold up to 16 voices, with paging on the UI from 1-8 to 9-16. The voices of the other
apps will therefore need to be completely self contained in the code so they can be used in mu-tronic. We will have buses in the mixer so channels can
be assigned to a bus instead of the master out. Buses will have the same insert effects as a layer, same side chain options. It may end up replacing
mu-link as a performance tool."

**What this is.** It is the "One framework, one instance" idea of [design-future.md](../design-future.md), scheduled as a fifth product. That
entry's open questions are answered: it is a **fifth product** (not a mode of an existing one), capacity is **16 layers**, it is **freeware with no
licence key**, and it is **Standalone only** because Andy sees it as a new performance framework, not a DAW instrument. The groundwork it asked for is mostly built (layer container, `Layer` virtuals, composed slot state, clip bank, shared
sidebar and panel, performance state); what is missing is `Layer::render`, the layer-type registry, product code that does not depend on its own
`PluginProcessor`, 16-layer capacity, and buses.

Related: [design-plugin-family.md](../design-plugin-family.md) (platform contract, layer ownership, Stage 8), [design-naming.md](../design-naming.md),
[mu-link/design-mulink.md](../mu-link/design-mulink.md), [design-launchpad.md](../design-launchpad.md).

---

## 0. Standing check (design-future)

Easier: this is the direction. Every product's engine becomes reachable from one container, the per-product glue that sits in
`PluginProcessor` is forced into the layer type (so it can no longer be copied), and the mixer gains one feature that every product can then switch on.
Harder: nothing, provided the rules in section 2 hold. Two things are made *deliberately* harder to do wrong:
a product that reaches for its own `PluginProcessor` from a layer class, and a new per-product copy of per-block glue. Both are caught by a new checker
(section 2.5). The standard UI sizing idea is helped: mu-Tronic adds no layout constants of its own except the page size (`kLayersPerPage`, default 8), which becomes a shared constant. Standalone-only also removes work: no DAW parameter list, no plugin-format
validation, no host state restore, no licence flow.

---

## 1. What "a voice" is

### 1.1 Vocabulary (design-naming §1 stands)

Andy's word "voice" means what the family calls a **layer**: one engine + one sequencer + one mixer channel. The code keeps **Layer**; "voice" stays
reserved for a polyphonic voice inside an engine (`SynthVoice`, `ToniVoice`). User-facing wording in mu-Tronic is the `ux-controller`'s call; the docs
and code say layer. New terms recorded in design-naming §1:

| Term | Means |
|---|---|
| **Empty layer** | A slot with no layer type chosen. Type `Empty`; silent; costs nothing per block. |
| **Page** | `kLayersPerPage` layers (default 8): page 1 is layers 1-8, page 2 is 9-16. The page count is derived (`ceil(kMaxLayers / kLayersPerPage)`); no code may assume "8" or "2" literally. UI and controller concept only; the processor sees all layers. |
| **Bus** | An internal group strip that channels can be routed to instead of the master (section 5). Not the DAW multi-out. |
| **Direct out** | The existing DAW output routing of a channel (`ChannelState::outputBus`, parameter `outBus`). Renamed in comments and UI to "Out" so "bus" has one meaning. Not offered in mu-Tronic (Standalone only, stereo master). |
| **Scene** | Clip n launched on every layer at once (section 3.6). An action across layers; the clip bank is per layer, the scene is only an index. |

### 1.2 Definition and recommendation

Options considered: (A) the engine alone; (B) engine + sequencer; (C) engine + sequencer + modulation + insert + parameters + presets + clips + UI content.

**Recommendation: C. A voice (layer) is everything the product's `Layer` subclass already stands for, minus the mixer strip and minus the transport.**

| Part | In the voice? | Today it lives in | Notes |
|---|---|---|---|
| Engine (DSP) | yes | `Audio/` (Tant, Toni, On); `mu-core` + processor arrays (Clid) | |
| Sequencer / trigger model | yes | `Sequencer/`; Tant's gate in `Pattern`; On's `StepPattern` is global | A voice may not depend on another voice's sequencer (On's single pattern becomes per layer). |
| Modulators + matrix + mod-target table | yes | `Layer` (core) + `Modulation/` | Already per layer; `isValidDest` virtual. |
| Per-layer insert | yes, when the type has one | `Layer` subclass (Tant, Toni); inside `VoiceEngine` (Clid); none in On | `Layer::insertGainReduction()` already abstracts it. |
| Parameters (`r/v/a/t{N}_`) | yes | declared in the processors (`createParameterLayout`) | The table moves with the type (section 2.3). |
| Layer preset + clip bank + extras | yes | `Layer::writeExtras/applyExtras`, `ClipBank`, per-product preset I/O | A clip is the type's layer preset; it carries the type (section 3.4). |
| UI content (`<Type>Content`, `<Type>MiniVisual`) | yes | product `UI/` | Must stop reaching for the product `PluginProcessor` (section 2.2). |
| Mixer strip (`ch{N}_`), FX sends, sidechain | **no** | `MixerEngine` / `MixerChannel` (core) | Already shared. A layer feeds one channel. |
| Transport, tempo, play state | **no** | `ProcessorBase` / `resolveTransport` | Handed in per block (section 2.1). |
| Hot-swap staging, structural edits | **no** | `ProcessorBase` / `LayerSwapper` | Shared. The voice only supplies `preloadStaged`, `loopBeats` (section 2.1). |
| Product shell (settings overlay, licence, editor frame, standalone app) | **no** | `Plugin/`, `License/`, `UI/SettingsOverlay.*` | mu-Tronic has its own. |

Why not A or B: an engine alone cannot be saved, automated, modulated or shown, which is everything a user means by "add a voice from mu-Tant". Why not
"engine + sequencer + params but no UI": the brief wants to *use* the other apps' voices, and a layer with no panel cannot be played. The cost of C is the UI
decoupling in section 2.2; the benefit is that a layer in mu-Tronic and the same layer in its own app are the same code, so there is nothing to keep in step.

### 1.3 The self-containment boundary

A voice is self-contained when **its source files include nothing from `Plugin/`, `License/` or another product, take no `PluginProcessor&`, and
reach shared state only through `ProcessorBase`/`Layer` interfaces and the per-block `RenderContext`.** What each product's voice depends on today:

| Product | Voice code today | Depends on its own processor for | Must move or be wrapped |
|---|---|---|---|
| **mu-Tant** (`Pattern`) | `Pattern : Layer` (3 gates, user wavetables, insert, ring tap); `mu_tant::VoiceEngine` (`Audio/SynthVoice`); `GatePattern`; `PatternContent`, `GatingDesigner`, `PatternMiniVisual` | The whole render chain: `renderVoice`, `applyModulation`, `applyFilterEnvelope`, `applyPitchEnvelope`, `readConfig`, `renderRetiringVoice` (about 300 lines in `PluginProcessor.cpp`); the cached `v{N}_` atomic pointers (`voicePtrs`, `modDestAtoms`); the snapshot table `voiceSnap`; the engine array `voices`; `retiring[]`; the wavetable bank and `bankLock`; the **global Note mode** (`midiNoteMode`, `heldNotes`, `noteGateGain`, and `applyNoteModeGate` on the final mix); `createParameterLayout` (`PluginProcessor_APVTS.cpp`); `swapLoopBeats`/`SwapBoundary.h` (in `Plugin/`) | Render chain into `Pattern::render`; engine, retire fade and cached pointers into `Pattern`; Note mode becomes per layer state (held-note stack and gate ramp in `Pattern`, MIDI channel per layer); the param table into `Persistence/PatternParams.{h,cpp}`; `SwapBoundary.h` into `Sequencer/`; the bank into a shared service (section 2.4); `PatternContent` takes a `PatternHost&` interface instead of `mu_tant::PluginProcessor&` |
| **mu-Toni** (`Arp`) | `Arp : Layer` already holds `ArpVoiceRunner` (arp clock + `ToniVoice`) and the insert. Cleanest of the four. | `readVoice` (modulation resolve into `ArpParams`/`ToniVoiceParams`), the `vp` pointer table, `modDestAtoms`, `modLive`, `renderChannelCb`; the **global** held-note stack and `ArpContext` (root-by-MIDI, MIDI-trigger mode), `midiInChParam`; `createParameterLayout`; `Plugin/LayerParamIds.h`; `ArpContent` includes the processor | `readVoice` + pointers + `modLive` into `Arp`; held notes and MIDI channel per layer; the param table and `LayerParamIds.h` into `Persistence/`; `ArpContent` onto an `ArpHost&` |
| **mu-On** (`Track`) | `Track : Layer` (step row, Rumble envelope, trigger counter); engines `KickEngine`, `BassEngine`, `SampleChannel`, `RumbleEngine` in `Audio/` | `GrooveVoices` holds **every engine of every slot** (5 per slot, large) and does the modulation resolve; `StepPattern` + `GrooveSequencer` are **one global grid and clock for all tracks**, with global swing/accent params; the Rumble **reads another layer's audio** (render order, `remapLayerReferences`); `Plugin/TrackParams.h` (`EngineType`, param ids) is included by the engines, `MuOnModDest.h`, `StepPattern.h`, `GrooveVoices.h`; `TrackContent`/`StepRows` include the processor | Per-layer engine instance (only the selected engine allocated; changing engine is a layer-internal swap on the message thread), per-layer step grid and swing/accent (`StepPattern` becomes a per-layer `StepRow` driver, `GrooveSequencer` clocks one row), `TrackParams.h` into `Persistence/`, the Rumble source resolved through the render context (section 2.1), `TrackContent` onto a `TrackHost&`. No per-layer insert today (hook returns `nullptr`); buses give On groups an insert. **Each engine is also its own mu-Tronic layer type: section 1.4.** |
| **mu-Clid** (`Rhythm`) | Engine, sequencer, MIDI engine and play state are **already in `mu-core`** (`mu_core::Rhythm`, `RhythmSequencer`, `::VoiceEngine`, `MidiOutputEngine`, `RhythmPlayState`, shared with the MIDI product); `RhythmContent`, `VoiceSection`, `MidiOutSection`, `SampleBrowser`, `EuclideanPanel` | The per-block glue: `PluginProcessor_Modulation.cpp` (444 lines: mod seed and write-back, Euclid overrides), trigger firing, `voiceEngines[]`, `midiEngines[]`, retired tails and drain flags, `rhythmPlayState[]`, `modSnapshot`, `rawParams`/`pendingDirty` drain, `SampleLibrary`/sample paths, `orphanOffs`, `HotSwapStager` (prepares a `Rhythm` + `VoiceEngine` at stage time, sample I/O included), `PresetIO` (includes the processor), the master loop in `RhythmSequencer` | New `mu_clid::SampleRhythm : mu_core::Rhythm` that carries the engine, MIDI engine, play state, snapshot, sample path and retire tail, with `render`; the glue becomes its methods; `PresetIO` shapes onto `Layer::writeExtras/applyExtras` + `captureLayerPresetTree`; sample I/O at stage time via `preloadStaged`; `SampleLibrary` becomes a shared service |

Everything above is "voice code in the processor" that the docs already say belongs elsewhere (design-naming §3: `Plugin/` holds only the processor,
editor and standalone; engine and preset code belong in `Audio/` and `Persistence/`). So this is also a correction, not only new work.

### 1.4 mu-On's engines are individual layer types (Andy, 2026-10-15)

Andy: every mu-On voice engine can be added as a mu-Tronic layer. So the registry lists **Kick, Bass, Hat, Snare and Rumble as five layer types**, not one
"mu-On" type. They are five registry entries over **one class**, `mu_on::Track`, differing only in the engine the entry fixes (`mu_on::EngineType`):

| Aspect | Ruling |
|---|---|
| Ids | `LayerTypeId` gains `Kick, Bass, Hat, Snare, Rumble` (section 2.5), saved by name. The enum order of `mu_on::EngineType` is not saved or relied on. |
| One class | `Track` holds exactly one engine. In mu-On the engine choice on a track stays (a layer-internal engine swap on the message thread, staged and retired like any engine change). In mu-Tronic the entry creates the `Track` with the engine **locked**: the engine selector is hidden and changing the sound family is a type change (chooser, confirm, clip bank cleared). One code path, one flag (`engineLocked`). |
| Parameters, prefix, preset | Unchanged: prefix `t{N}_`, extension `.muTrack`, `Persistence/TrackParams` (`addLayerParams`). The engine id stays a saved parameter, so a `.muTrack` saved by mu-On loads into a mu-Tronic slot of the right type: **type inference reads the extension for the family, then the saved engine for the member** (`LayerTypeInfo::typeFromPreset(tree)`). A mu-Tronic Kick preset loads in mu-On. |
| Mod targets, content, mini-visual | Per entry: the mod-target table is the engine's own subset (mu-On already filters by engine), `TrackContent` shows only the locked engine's panel, the mini-visual is the step row. |
| Self-containment (section 1.3) | The per-layer engine allocation and per-layer step grid are now **required**, not optional: a 16-slot instance cannot afford five engines per slot. This settles the T6 decision: mu-On goes per-layer in the standalone too (one code path). `GrooveVoices` as a five-engine pool disappears. |
| Global grid and clock | Gone from the layer: each `Track` owns its step row, length, swing and accent and reports `loopBeats()` (a 16-step row is one bar). The standalone's grid view is a view over its tracks; the shared clock is `RenderContext::startBeat`, as for every type. |
| Rumble reading another layer | Stays the only cross-layer audio read. Priority 1; the source is a slot index resolved through `RenderContext::peer(slot)`. Because the types are now independent, the source may be **any priority-0 layer of any type** in the instance (a Kick, an Arp, a Pattern), which costs nothing extra and is the useful case; a priority-1 source (another Rumble) or an Empty slot processes silence. The source slot index is a layer parameter and is remapped on swap/remove by the existing `layersReordered` hook (`remapLayerReferences` becomes generic, not On-specific). |
| Chooser | Types are grouped by product in the picker: `LayerTypeInfo::group` ("mu-Clid", "mu-Tant", "mu-Toni", "mu-On") and `displayName` ("Rhythm", "Pattern", "Arp", "Kick" ...). Nine types plus Empty; the layout of the chooser (nine cards or a grouped list) is the `ux-controller`'s. A type is offered only if registered in the build, so a mu-Tronic that has not yet got Pattern and Rhythm simply does not list them. |
| Default names | `<displayName> <slot>` ("Kick 3"), editable and saved (section 4). |

---

## 2. Where voice code lives

### 2.1 The contract: `Layer::render` and the per-block context (Stage 8, option C, now scheduled)

Stage 8 of design-plugin-family waited for exactly this decision. Ruling: **dispatch stays virtual on `Layer`; a registry only creates and describes.**
New hooks (all `mu-core/Sequencer/Layer.h`, defaults do nothing, all called once per layer per block or per edit, never per sample):

| Hook | Thread | Purpose |
|---|---|---|
| `int typeId() const` | any | The type's id (a `mu_core::LayerTypeId`, section 3.3). |
| `void prepare(double sr, int blockSize)` | message, processing suspended | Allocate everything (engine, buffers, insert). |
| `void render(const RenderContext&, juce::AudioBuffer<float>& out, int n)` | audio | Write the layer's stereo block (the former `renderVoice` / `renderChannelCb` / `GrooveVoices::renderAll`). Must not allocate, lock beyond the existing spin locks, or touch the APVTS by string. |
| `double loopBeats() const` | audio | Loop length in beats for the hot-swap boundary (replaces `swapLoopBeats`, `blkPatternBeats`). |
| `void preloadStaged(const ValueTree&)` | message | Do file I/O for a staged tree (sample, user wavetable) so the boundary commit does none. |
| `void onTransportStopped()` | audio | Silence held notes and tails on the play to stop edge (Tant gates, On Rumble tails, Toni note-off). |
| `int renderPriority() const` | any | 0 normal, 1 after all priority-0 layers (mu-On Rumble processes another layer's output). |
| `const std::atomic<float>* insertGainReduction() const` | exists | Unchanged. |

`mu_core::RenderContext` (a plain struct, built once per block by `ProcessorBase`, no allocation): `sampleRate`, `blockSize`, `playing`, `bpm`,
`startBeat` (the **unwrapped** song beat at the block's first sample, a `double`; each layer wraps it to its own loop, replacing Tant's 64-beat wrap in the
resolver), `beatsPerSample`, `const juce::MidiBuffer& midi` (the block's MIDI after the router's claims), `slot`, and `peer(slot)` (read-only view of an
already-rendered priority-0 layer's output, for Rumble). Parameters are read through `Layer`-owned cached atomic pointers bound once by a new
`Layer::bindParams(apvts, slot)` (message thread); the products' `voicePtrs`/`vp`/`modDestAtoms` tables move into the layer type.

**Render order and the mixer.** `ProcessorBase` gains one helper, `renderLayers(ctx)`: under the one `layersLock` try-lock it renders priority-0 layers then
priority-1 layers into per-slot scratch buffers (allocated in `prepareToPlay`), and the `MixerEngine::RenderChannelFn` becomes a copy from scratch. This
is the mu-On precedent (`renderChannelCb` copies what `GrooveVoices::renderAll` already rendered) lifted to the base; Tant, Toni and Clid adopt it, so
there is one render path. Empty layers are skipped. A lock miss renders silence but still advances transport and boundary detection (contract item 5 of
Layer ownership, unchanged).

**Where the engine lives (amends Stage 7 ruling 1 for hosts that render through `Layer::render`).** Stage 7 kept engines processor-side so this question
stayed open. It is now answered: **the engine lives in the layer object** (a `unique_ptr` member for the heavy ones), and travels with the layer on a
pointer move. What stays processor-side is only state that must outlive or sit still apart from the layer: the mixer strip (sidechain followers), the UI
snapshot tables read by index at 30 Hz (moved into the layer as atomics, address-stable because the layer is heap-allocated), and the **retire bin**:
when a slot's layer is replaced (type change, remove) the old `unique_ptr` is parked in a per-slot `RetireBin` owned by `ProcessorBase`; the audio thread
renders it under a falling gain for its `tailBlocks()` (25 ms default, longer for a reverb or comb insert) and sets a cleanup flag; the message thread
frees it off the audio thread (the mu-Tant `retiring` and mu-Clid `retiredReadyForCleanup` pattern, generalised once). `Rhythm` stays a copyable value for mu-Clid MIDI;
mu-Clid's engine-carrying type is the derived `mu_clid::SampleRhythm` (section 1.3), which is not copyable and which only `Layer::render` hosts use.

**Real-time contract requirements (dsp-controller review, 2026-10-15).** Requirements C1 to C18 (atomic publication for type changes and scenes with no suspend
and no muting, the `RetireBin` state machine and retiree snapshot, the `RenderContext` time model with `onTransportJump`, oversize-block chunking, `onMoved`
rebinding, peer tap point, hold-last on a modulation-lock miss, sidechain tap, latency assertion, CPU measurement plan) are in
[extraction-plan.md section 2.1a](extraction-plan.md#21a-contract-requirements-from-the-real-time-review-c1-to-c18) and bind this section; they are signed before any contract code (stage E0d).

### 2.2 The product UI stops depending on the product processor

Each `<Type>Content` takes `ProcessorBase&` plus one narrow host interface declared in the product's own `UI/` folder next to the content
(`mu_tant::PatternHost`, `mu_toni::ArpHost`, `mu_on::TrackHost`, `mu_clid::RhythmHost`): the few things it calls today on the product
processor (`getPattern(i)`, `loadUserWavetable`, `getTantSnap`, `getModLive`, `selectEngine`, `samples`...). The product's `PluginProcessor` implements its host
interface for the standalone; in mu-Tronic one adapter per type implements it over the type's shared service (section 2.4). `SettingsOverlay` is *not* a
voice file; mu-Tronic has its own, built from `StandardSettingsOverlay` sections.

### 2.3 Parameter tables travel with the type

`PluginProcessor::createParameterLayout` in each product is split into a global part (stays) and **`addLayerParams(layout, slot)` in
`Persistence/<Type>Params.{h,cpp}`** (the type's table for one slot, using its prefix helper `layerParamPrefix`). The standalone calls it for slots
`0..kMaxLayers-1`; mu-Tronic calls all four for all 16 slots. This is what design-plugin-family already says `Persistence/` holds ("Product-specific
param tables, preset I/O"). Moves: Tant `PluginProcessor_APVTS.cpp` (`createParameterLayout` per-voice part), Toni `LayerParamIds.h` and the layout in
`PluginProcessor.cpp`, On `Plugin/TrackParams.h` and its layout, Clid `PluginProcessor_APVTS.cpp` (it already builds from `RhythmParamTable.h`, which is core).

### 2.4 Shared services (one instance per processor, created by the type registry)

Some resources belong to a type, not a layer: mu-Tant's and mu-Toni's `WavetableBank` (two banks today; **one shared `mu_wavetable::WavetableBank` in
the processor**, with its own `bankLock`, since both already use the mu-core class), mu-Clid's `SampleLibrary` (primary sample folder, preview). Each
`LayerTypeInfo` may declare a `createServices()` returning a `mu_core::LayerServices` (an opaque base owned by `ProcessorBase`, created when the first layer of
that type is added, message thread). The type's host adapter (2.2) reads it. This keeps the per-type shared state out of `Layer` and out of the
processor class.

### 2.5 Library structure: per-product layer libraries, no moves into mu-core

Options: (a) move all voice code into `mu-core`; (b) one CMake `INTERFACE` library per product, `mu-clid-layer`, `mu-tant-layer`, `mu-toni-layer`,
`mu-on-layer`, made of that product's existing folders; (c) copy.

**Ruling: (b).** (a) breaks "mu-core is plugin-agnostic" and the boundary checker, and makes mu-core grow by four engines it cannot test without
product names; the one exception already made (`Rhythm`) is justified because **two products** use it, and that remains the test for moving anything to core.
(c) is forbidden by the family consistency rule. With (b):

- **No folder moves** and no new folder. The eight `Source/` subfolders stay. A product's `<product>-layer` target is `Audio/ Sequencer/ Modulation/
  Persistence/ UI/` minus `UI/SettingsOverlay.*`; `Plugin/`, `License/` and `Tests/` are the app. The product's own `CMakeLists.txt` defines both
  (`mu-tant` links `mu-tant-layer` + `mu-core`).
- `INTERFACE`, not `STATIC`, for the reason already recorded in Stage 33 (the JUCE module init macros).
- **Include collisions.** All trees use includes relative to their own `Source/` (`"Audio/..."`, `"Sequencer/..."`), and mu-Tronic puts four `Source/` roots on
  one include path. A survey (2026-10-15) found exactly one voice-side clash, `Audio/Scales.h` in mu-Tant and mu-Toni (different content); every other clash
  is in `Plugin/`, `License/` or `UI/SettingsOverlay.*`, which are not in the library. Rules: (1) rename them `Audio/ToniScales.h` (mu-Toni, in Stage T4, when
  mu-Toni is still pre-release) and `Audio/TantScales.h` (mu-Tant, in Stage T5, after its next release; until then Tant's `Audio/Scales.h` is the only one of that path in
  mu-Tronic's include path, because mu-Tant is not yet in mu-Tronic, so there is no clash and the live app is untouched); (2) a layer-library file **never includes anything under `Plugin/` or `License/`**, which section 1.3 and 2.2 already require;
  (3) **every relative path in a layer library is unique across mu-core and all four libraries** (new rule in design-naming §3, files). Both rules are
  enforced by a new `tests/scripts/check-layer-boundary.ps1` (run by `/build` beside `check-core-boundary.ps1`): fails on a `Plugin/`/`License/` include from a
  library file, on a duplicate relative path, and on a library file that includes another product's header.
- Namespaces are already per product (`mu_clid` ... `mu_on`, design-naming §3), so four engines coexist in one binary. Global-namespace names in a library
  are not allowed (the checker greps for them): the only global voice-side class today is mu-core's `::VoiceEngine`, which mu-Tant's `mu_tant::VoiceEngine`
  already coexists with.
- mu-Clid MIDI is unaffected (it uses core `Rhythm`); mu-Tronic's Rhythm layer in "MIDI output" mode is the same capability, so the MIDI product's
  voice is available for free.

**Naming.** Library target `mu-<product>-layer` (a library of that product's layer type: the Layer vocabulary, not "voice"). Layer classes keep the
design-naming derived names (`Rhythm`, `Pattern`, `Arp`, `Track`); mu-Clid's engine-carrying subclass is `SampleRhythm`. Host interfaces `<Type>Host`.
Registry entry `LayerTypeInfo` in `mu-core/Sequencer/LayerTypeInfo.h`; ids `enum class LayerTypeId : int { Empty = 0, Rhythm, Pattern, Arp, Kick, Bass, Hat, Snare, Rumble }` in
`mu-core` (neutral: the ids are family vocabulary, as `kMaxLayers` is), values saved by name (`"Arp"`, `"Kick"`), never by number. The four products' prefixes and
extensions are unchanged, so five ids (the mu-On engines) share the prefix `t` and the extension `.muTrack` (section 1.4); the id, not the prefix, identifies the type.

`LayerTypeInfo` (all message-thread data): `id`, `name`, `group` (the product it belongs to, for the chooser), `paramPrefixLetter` (`r v a t`), `presetExtension`,
`typeFromPreset(tree)`, `contentFolderName`, `modTargetTable`, `addLayerParams(layout, slot)` (called once per prefix, not once per id), `create(slot)`,
`createServices()`, `createContent(...)`, `createMiniVisual(...)`, and the type's colour token (named by the `ux-controller`). It constructs and describes; it never
dispatches (design-plugin-family "Why virtual and not a table" stands).

---

## 3. mu-Tronic product layout and state

### 3.1 Identity (saved data, change only with a migration)

**Ruled with Andy's answer, 2026-10-15** (Andy typed `mu-tronic` and `mtrn`; the differences below are only capitalisation, forced by two existing rules):

| Item | Value | Why it differs from "mu-tronic" / "mtrn" |
|---|---|---|
| Display name (prose, window title, installer, About) | **mu-Tronic** (the logo and UI use the family's μ form, "μ-Tronic", per design-ui-family §9) | design-naming "Product naming pattern" is `mu-<Name>` with a capital (`mu-Clid`, `mu-Tant`, `mu-Toni`, `mu-On`). Lower case is the slug. |
| Slug (folder, CMake targets, executable base, docs, site path) | `mu-tronic` (targets `mu-tronic`, `mu-tronic-tests`) | as typed |
| Namespace | `mu_tronic` | pattern |
| JUCE `PLUGIN_CODE` | **`MTrn`** | JUCE requires exactly four characters **with at least one upper-case letter**, so `mtrn` is rejected; `MTrn` follows the family (`MCld MTnt MTni`) and collides with none of them (`MuOn`, `MCLt`) |
| `PLUGIN_MANUFACTURER_CODE` | `TDP1` (as the family) | |
| Bundle id | `com.transwarp.mu-tronic` | |
| State identifier / full-preset tag / extension | `MuTronicState` / `MuTronicPreset` / `.muTronic` | data names follow `Mu<Product>State` |
| Formats | **`FORMATS Standalone` only.** No VST3, no CLAP, no AU. | Andy: a new performance framework that may make mu-link redundant. `juce_add_plugin` still needs the plugin and manufacturer codes (the standalone's settings file and bundle name derive from the product), so they are fixed now: they are data and may not change later (design-naming "Product renames are display-only"). |
| Licence | **Freeware.** No `LicenseKey.h`, no key check, no Support prompt, no `ActivationOverlay`, no Lemon Squeezy product. The site lists it as freeware, like mu-Link and mu-Clid MIDI. | |

Consequences of Standalone only, all removals: no pluginval run; no host automation list, so the
parameter-count and parameter-pool work of the earlier draft is dropped; no DAW-hosted state restore, bus layout or `producesMidi` declaration; no DAW direct
out (`outBus` is not offered; the master is the standalone's stereo output); no DAW sidechain input (an external sidechain source exists only if the standalone
audio device supplies input channels, as the other standalones do today). The macOS hold is unaffected (nothing here is Windows-specific).

### 3.2 The eight Source folders

```
mu-tronic/
  CMakeLists.txt            juce_add_plugin (FORMATS Standalone); links mu-core + the layer libraries that are registered (mu-toni-layer and mu-on-layer first; mu-tant-layer and mu-clid-layer later)
  resources/ installer/
  Source/
    Audio/                  empty (.gitkeep): the engines are in the layer libraries
    License/                empty (.gitkeep): freeware, no key (mirrors mu-Clid MIDI)
    Modulation/             empty (.gitkeep): mod-target tables stay with each type (LayerTypeInfo)
    Persistence/            TronicState (type attribute, import of product presets), the registry list (which types this build offers)
    Plugin/                 PluginProcessor(+_APVTS/_Preset/_Layers/_Content), PluginEditor, StandaloneApp
    Sequencer/              EmptyLayer.h (the Empty type); scene launch glue if it is not generic enough for mu-core (it should be: section 3.6)
    Tests/                  the tests of section 7
    UI/                     TronicSettingsOverlay (no licence section), the empty-slot "add layer" chooser, the page switch glue
```

No mu-Tronic file names an engine or a sequencer: it names layer types through the registry. A fifth type later is a new `mu-xxx-layer` and one line in
`Persistence/`.

### 3.3 `ProcessorBase` subclass sketch

```cpp
namespace mu_tronic {
class PluginProcessor : public ProcessorBase
{
public:
    // Always 16 live layers (numLayers == kMaxLayers == 16); an unused slot holds an EmptyLayer.
    bool canEditLayers() const override    { return true; }
    bool canReorderLayers() const override { return true; }   // swap two slots, empty or not
    int  getNumActiveLayers() const override;                 // non-empty layers (CPU meter, controller)

    // Base API added by this work (message thread, same protocol as resetLayer):
    //   bool setLayerType(int slot, mu_core::LayerTypeId t, const juce::ValueTree* initial = nullptr);
    // builds the new layer and prepares it OUTSIDE the lock, parks the old one in the RetireBin, swaps the pointer under
    // layersLock, resets the type's parameters of that slot to defaults (or applies `initial`), keeps the mixer strip.
    // "Delete a layer" in mu-Tronic is setLayerType(slot, Empty); removeLayer (shifting) is not offered.
    //   void launchScene(int clipIndex);   // generic in ProcessorBase (section 3.6), not mu-Tronic-specific

    std::unique_ptr<Layer> createLayer(int slot) override;    // EmptyLayer
    // Per-type pieces come from the registry, not from overrides:
    juce::File   getLayerPresetDir()       const override;    // of the selected slot's type
    juce::String getLayerPresetExtension() const override;
    juce::Array<juce::File> slotPresetFiles(int slot) const override;   // lists the slot type's folder
    const char*  getFullPresetTag() const override { return "MuTronicPreset"; }
    juce::String getFullPresetExtension() const override { return "muTronic"; }
    void prepareToPlay(double, int) override;                 // prepares every layer + scratch buffers
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
        // resolveTransport -> RenderContext -> renderLayers (try-lock) -> processCoreBlock (mixer + buses + FX)
        // -> flagSwapBoundaries(loopBeats per slot)
};}
```

`ProcessorBase` additions: `setLayerType`, `renderLayers`, `RetireBin`, `LayerServices` ownership, the registry lookup, `launchScene`. All generic; none names a product.
`Layer` gains the hooks of 2.1. `ProcessorCore::getMaxLayers()` returns `mu_limits::kMaxLayers`.

### 3.4 Capacity, parameters, and what is saved

**Capacity of 16 without changing the other products.** `mu_limits::kMaxLayers` is used as an array size in about 46 files; raising it to 16 for everyone
would double every product's parameters, memory and automation list for a feature they do not have. Ruling: `kMaxLayers` becomes a per-target compile-time
constant, `inline constexpr int kMaxLayers = MU_MAX_LAYERS;` with `MU_MAX_LAYERS` defaulting to 8 in `MuLimits.h`; `mu-tronic/CMakeLists.txt` defines 16 for its
target (mu-core is an `INTERFACE` library, so each product compiles its own copy; the layer libraries compile inside mu-Tronic with 16, which is what they
should: they size their per-slot arrays by `kMaxLayers`). The four products and `mu-core-tests` keep 8 and are byte-for-byte unchanged (Stage T0 proof). A
second core test target `mu-core-tests-16` (same sources, `MU_MAX_LAYERS=16`) covers the 16 build. Do not raise the default, and do not add a second
constant for "framework max": one constant, one value per binary.

Two saved-data consequences, both safe because mu-Tronic is new: (1) `MixerEngine::kExtSidechainSrc = MaxChannels` moves to 16 in this binary; the strip
sidechain source encoding is therefore *per product*, and the import rule below remaps it; (2) the colour palette has 8 entries and `colourIndex` repeats
past 8 (the base's "first unused" rule falls back to modulo); `ux-controller` to confirm.

**Parameters.** Every slot declares every type's parameters with its own prefix (`r{N}_ v{N}_ a{N}_ t{N}_`, N = 0..15) plus the shared strip (`ch{N}_`):
the schemes are already unique (design-naming §3), so nothing collides and a slot's type selects which prefix is live (the five mu-On engines share `t{N}_`, so
that prefix is declared once per slot). Estimated size is **about 4,000 to 6,000 parameters** (mu-Toni 63 per layer, mu-Tant and mu-Clid of similar order, mu-On's
table once). **Resolved by Standalone only (Andy, 2026-10-15): there is no DAW parameter list, so the count is not a product constraint and the mitigations of the
earlier draft (hidden inactive prefixes, the 64-slot parameter pool) are dropped.** What remains is an internal cost check in Stage T0: build the real 16-slot
layout once and record APVTS construction time and memory against a budget (target: editor opens in under a second on the development machine; the number is
recorded in this section). If it fails, the answer is lazy per-type declaration inside the product (a design change then, not now). The parameter-name scheme
stays unchanged, so nothing that is built now closes that door.

**What is saved.** Slot state is the family standard ([Slot state](../design-plugin-family.md#slot-state-presets--sessions--family-standard)),
with one addition, the `type` attribute, which is Stage 8 option B:

```
<MuTronicState format="2" numVoices="16" voiceColours="..."> <!-- numVoices/voiceColours: saved names kept (design-naming §3) -->
  <Globals> mixer, buses, FX, master, transport rows </Globals>
  <Slots>
    <Slot idx="0"  type="Arp">   rows (ids without prefix) + extras </Slot>
    <Slot idx="1"  type="Pattern"> ... </Slot>
    <!-- an absent <Slot> is an Empty layer; "missing means default" is unchanged -->
  </Slots>
</MuTronicState>
```

- **Loading** a slot: read `type`, `setLayerType` (default layer, prepared), then apply the rows with that type's prefix. The existing single apply path
  (stopped load, hot-swap commit, standalone state restore) is unchanged apart from this first step; a type change inside a staged commit parks the old layer in the
  RetireBin.
- **Layer presets** keep the product's extension and shape (`.muArp`, `.muPattern`, `.muTrack`, `.muRhythm`); the type is inferred from the extension and, for
  `.muTrack`, from the saved engine (section 1.4), so **a preset saved by mu-Toni loads into a mu-Tronic slot and the reverse**, which is what makes the apps and
  mu-Tronic one library. **Ruled by Andy, 2026-10-15: the layer-preset folder is the product's own** (`<TDP>/muToni/Arps`, ...) via `contentFolderName`, shared,
  not copied; mu-Tronic has no separate layer-preset tree. A slot of type Kick lists the mu-On track presets whose engine is Kick. Only the full presets
  (`.muTronic`) and performances get a mu-Tronic folder.
  Loading a preset whose type differs from the slot's is a type change (confirm, then stage like any swap).
- **Clips** (`Layer::clips`): a clip is the slot's type's layer preset and carries its type. A clip whose type differs from the slot's current type is not
  launchable; changing a slot's type clears its clip bank after a confirm. This keeps the clip bank "a launch is a staged layer swap, never a type change".
- **Full presets** (`.muTronic`) are the state above wrapped as `<MuTronicPreset name description category>`; performance files (`.muPerformance`)
  point at them unchanged (`product` is `MuTronicState`).
- **Import (Stage T8b).** "Import product preset" reads a `.muToni`/`.muOn`/`.muTant`/`.muClid` full preset and lays its layers into free slots (type from the
  file's tag and, for mu-On, each track's engine; strips and sidechain sources remapped with `mu_mix::sidechain*`, the external-sidechain value 8 mapped to this
  binary's `kExtSidechainSrc`). Global FX/master of the imported preset are ignored by default. Each product's importer is added when that product's layer is.
- Hot-swap: `LayerSwapper<ValueTree, 16>` as for the other products, boundary per slot from `Layer::loopBeats()`; `Layer::preloadStaged` is called at stage
  time. mu-Clid's own `HotSwapStager` stays in the standalone mu-Clid until Stage T7 decides (section 6).

### 3.5 Product-global state that becomes per layer

A voice that is self-contained cannot read a processor-wide setting. Moves (each is a visible-behaviour check for the owning product's roundtrip test):

| Today (global) | Product | Becomes |
|---|---|---|
| Note mode (`midiNoteMode`, held-note stack, final-mix gate ramp) | Tant | layer state; ramp applied to the layer's own output; setting saved per layer (a layer extra) |
| Held-note stack, `ArpContext` root-by-MIDI, MIDI-trigger, `midiInChParam` | Toni | layer state |
| One `StepPattern` + `GrooveSequencer`, global swing/accent, five-engine pool | On | one `StepRow` driver and one engine per `Track`; swing/accent per layer (`t{N}_swing`, `t{N}_accent`; the standalone's global value seeds all tracks on load; a unit test pins that); section 1.4 |
| Master loop (`mstrLoop`), swap timing (`mstr_swap`) | Clid, all | stays a global (it is already a shared global parameter) |
| MIDI input channel | Toni, Tant | one per-layer **MIDI in** setting in the shared strip params (`ch{N}_midiIn`, 0 Omni, 1-16) for every type; types that take notes read the filtered buffer in `RenderContext`; defaults reproduce today (Omni) |
| MIDI output (Clid MIDI-out mode) | Clid | a layer appends to the block's MIDI output through the context; the standalone sends it to its MIDI output device (the path mu-Clid's standalone uses; no plugin `producesMidi` declaration is involved). Lands with the Clid layer. |

### 3.6 Scenes: clip n on every layer (Andy, 2026-10-15: yes)

A scene is **an action across layers**, designed into the base now so no product re-invents it.

- **Definition.** Scene n = launch clip n of every non-empty layer. The clip bank stays per layer (`Layer::clips`, 8 clips); a scene is only the index, so scenes are
  as many as clips (`ClipBank::kClips`, 8) and nothing new is saved. A layer with no clip n is left playing as it is (a scene never stops a layer; a pad on an empty
  clip does nothing, as now). A layer whose clip n carries a different type from the slot cannot occur (section 3.4: the bank is cleared on a type change).
- **Placement.** `ProcessorBase::launchScene(int clipIndex)` (message thread, generic, no product name), built on the existing per-layer clip launch. A new
  `ControlAction::LaunchScene(n)` in `mu-core/Control/` (device-independent; the Perf shield rules of the existing clip actions apply), so any controller or MIDI
  mapping can fire it, as `PerformanceItem(n)` can.
- **Timing and atomicity.** Each layer's launch is the existing staged swap and commits at the boundary chosen by the global swap-timing (Master loop, Layer loop,
  Bar). With Master loop or Bar all layers commit on the same line. To stop a boundary falling between the first and last staging call, the scene stages all layers
  inside one `layersLock`-guarded batch (`LayerSwapper::stageBatch`, or an equivalent single "staging open" gate the audio thread honours): the audio thread sees
  either none or all of a scene. **S-kind work for the `dsp-controller`** (Stage T8a); the proof is a test that stages a scene while blocks run and asserts all
  commits share one boundary under Bar timing.
- **Controller feedback.** A queued scene lights the clip column; surface and screen design is the `ux-controller`'s ([design-launchpad.md](../design-launchpad.md) lists the
  Launchpad mapping as still open).

---

## 4. UI structure (structure only; the look is the `ux-controller`'s)

- **Sidebar and panel.** `LayerSidebar` and `LayerPanel` are unchanged in kind. `LayerSidebar` gains a **page** (`kLayersPerPage` in `MuLimits.h`, default 8, used by the sidebar,
  the mixer overlay and `mu-control`; the page count is derived and nothing hard-codes 8 or 2) and an empty-slot item ("add a layer"); `LayerPanel::setContent` is already callable again so the content swaps with the
  slot's type (the 2026-10-12 ruling anticipated this). The page shown is editor state, not saved; the page of the selected layer is shown on selection.
  `kLayersPerPage` may not exceed 8 while a Launchpad (8 by 8 grid) is a target; a larger page would need the surface to page inside it.
- **Layer names are editable and saved** (Andy). They use the existing `LayerHeaderBar` name field and the slot's saved name exactly as the four products do (mirror
  them; no new storage). The default is `<displayName> <slot>`; a changed type resets a name that was still the default and keeps one the user typed.
- **Layer-type chooser.** Choosing a type for an empty slot (or changing it) is a shell dialog listing the registry grouped by product (section 1.4); the type's
  preset dropdown appears in the `LayerHeaderBar` as today, filled from the slot's type. The main area is painted by the **type's colour** (Andy, UI decision;
  token named by the `ux-controller`): `LayerTypeInfo` carries the token, so a type added later brings its own colour with it.
- **Mixer overlay.** 16 strips do not fit one row beside returns, buses and master: strips page 1-8 / 9-16 with the buses, returns and master always visible
  (`MixerOverlay` gains the page, shared with the sidebar). Bus strips are `MixerChannel` of a new `Type::Bus` (section 5).
- **Controller pages.** `mu-control` `SurfaceModel` reads `kLayersPerPage` and a page index from the `LiveView`; the pad column that is absolute for 8 layers
  becomes absolute within the page. New `ControlAction::PagePrev` / `PageNext` in `mu-core/Control/` (device-independent; they change the page, never the processor
  state). On the Launchpad, top-row buttons 5 and 6 are PagePrev / PageNext in mu-Tronic ([design-launchpad.md](../design-launchpad.md) §3.6). Andy: "the Launchpad will
  be important for this", so the controller stage (T8a) is not an afterthought. Not built before T8a.
- **Accent.** Ice white (Andy, UI decision) is the `ux-controller`'s; structurally it is one `appPrimaries` entry for the product, nothing in the layer libraries.

---

## 5. Buses in the shared mixer (all `mu-core`)

### 5.1 Shape

- **Count and inserts (Andy, 2026-10-15: three buses, two inserts each, not four buses with one).** Two compile-time constants in `MuLimits.h`:
  `mu_limits::kMaxBuses = MU_MAX_BUSES`, default **0** (existing products unchanged, no new parameters, no UI), **3 in mu-Tronic**; and
  `mu_limits::kBusInserts = MU_BUS_INSERTS`, default **2**, used only where `kMaxBuses > 0`. Both are one-line changes (`MU_MAX_BUSES=4`, `MU_BUS_INSERTS=1`
  restore the earlier shape), and nothing, UI included, may hard-code 3 or 2: loops, parameter declarations, the overlay and the sidechain range all run to
  the constants. Any product can later opt in with one define and one `addBusParams` call (mu-Clid could group its rhythms).
- **State.** `MixerEngine::BusState` = a `ChannelState` (level, pan, mute, solo, three sends, sidechain source / amount / attack / release; `outputBus` unused)
  **plus an insert chain of `kBusInserts` inserts, each the layer's insert**: `insertAlgo`, `insertParam[VoiceParams::kInsertSlotCount]` atomics and an
  `InsertProcessor`, held as `BusInsert insert[kBusInserts]`. So "same sidechain options" and "same insert effects as a layer" are literally the same fields
  and the same class, not copies. The chain runs insert 0 then insert 1; an unused insert is the layer's default (off) algorithm and costs nothing. Peaks
  (`busPeaks*`) and `busSidechainGR` arrays beside the channel ones (one GR meter per insert that has gain reduction); the follower `scBusEnv[]`.
- **Assignment.** A channel strip gets one new field, `ChannelState::busAssign` (0 = master, 1..kMaxBuses), parameter `ch{N}_bus` (int), declared by
  `mu_mixfx::addChannelStripParams` only when `kMaxBuses > 0`. It is part of the strip (like level and pan): it travels with the strip on swap/remove/reset
  (`copyFrom`, `reset`, `swapChannelState` extended), resets to master, and is **not** in a layer preset or clip (strips never are). The existing
  `outputBus` stays the DAW direct out; it wins if non-zero (a channel with a direct out feeds neither a bus nor the master, as today).
- **Bus parameters.** `bus{B}_*` with the strip's 12 suffixes (B = 0..`kMaxBuses`-1) through `addChannelStripParams(layout, "bus{B}_", ...)`, and each insert with the
  layer-insert suffixes through a shared `mu_mixfx::addInsertParams(layout, prefix)` called once per insert with prefix `bus{B}_i{I}_` (I = 0..`kBusInserts`-1; the coder
  confirms the suffix list against the layer's, which is the reference); `InsertSubsection` is already prefix-bound, so each bus insert uses it with its prefix. `ProcessorBase::syncGlobalFxParam` gets one more prefix route (`bus{B}_`). Buses have **no modulators**: modulation is per layer
  ("channels are self-contained"), so the single-reader rule is untouched.
- **Sends.** A bus has the same three FX sends as a channel (post-bus-fader). A channel's sends are taken from the channel, post-fader, **whatever its
  routing** (a channel sent to a bus still feeds the returns; its dry goes to the bus). Returns have no bus assignment; they go to the master. Direct
  out channels skip buses and sends as they skip them today.

### 5.2 Routing order and feedback prevention

The graph is a fixed three-layer DAG, **channels -> buses -> returns -> master**, so a loop cannot be expressed:

1. Phase 1-2 unchanged (render, trim, per-channel sidechain duck), with `MaxChannels` = 16.
2. Phase 3: per channel, fader/pan, peak; the dry signal is added to `busBuf[busAssign-1]` (or `output` for master); sends go to the send buffers.
3. **Phase 3b (new): for each bus in index order**, sidechain duck (if configured), the insert chain in order (each the layer `InsertProcessor`, `VoiceParams` built
   from the bus atomics as the master insert does), then bus fader/pan, peak and sends, then add to `output`. Mute/solo as below. The sidechain rules are unchanged by
   the second insert: duck first, then the chain, as for a layer.
4. Returns, master inserts and master fader as today.

Rules that keep it acyclic: **a bus cannot be assigned to another bus** in this version (one level; a bus-to-bus option, if ever wanted, allows only a higher
index, the same order rule the modulation matrix uses); **a channel's sidechain source is a channel or the external input, never a bus** (a bus is built
after the channel duck, so a bus source would be a loop); **a bus's sidechain source may be a channel, a lower-index bus, or the external input** (a lower-index bus is already
complete); **a return's source may be a channel, a bus or the external input** (returns come after buses). Encoding of the source integer:
`-1` off, `0..N-1` channel, `N` external (`kExtSidechainSrc`), `N+1+b` bus b; the UI lists only legal sources for each strip type, and
`mu_mix::sidechainAfterRemove/Swap` (SidechainRemap.h) learn the bus range (buses do not shift on a layer edit, so only the range check changes).

Mute / solo: a muted bus silences the dry path of every channel routed to it; the channels' sends belong to the channel and are unaffected by the bus mute
(say so in the manual). Solo is evaluated over audible paths: a soloed channel makes its bus audible; a soloed bus solos all members; with any solo active, strips not on a soloed
path are cut, as the channel solo works today. `isAudible()` takes the bus into account. (Unit-tested in section 7.)

**Latency.** The only built-in inserts with latency would need delay compensation across buses (now two inserts deep); none of the current insert algorithms report latency
(the `dsp-controller` confirms at review). If one is added later, buses require the compensation rule (all channel paths to the master delayed to the
longest bus path); note it as a standing constraint on `InsertProcessor`.

### 5.3 What is mu-core

All of it: `MixerEngine` (`BusState`, Phase 3b, arrays), `MixerFxParams` (`addBusParams`, `addInsertParams`, `busAssign` strip param),
`ProcessorBase::syncGlobalFxParam` route, `MixerChannel` (`Type::Bus`, an Out dropdown listing Master + buses), `MixerOverlay` (bus strips, page), `MuLimits.h`
(`kMaxBuses`, `kBusInserts`, `kLayersPerPage`). mu-Tronic supplies nothing but the defines. No product-specific symbol is involved, so the boundary checker is
unaffected. The unit tests are core tests (section 7).

---

## 6. Staged build plan (smallest first)

Each stage ends with a Debug build, its suites, the `*_roundtrip` listening tests of every product it touched (byte-identical where stated), and the static
checks. `dsp-controller` reviews every stage that changes the audio thread (T2, T4 to T8). A failed proof is reverted, not carried. Stage kinds as in Layer
ownership: M mechanical, S supervised (audio thread or lock), O needs Andy.

| Stage | Kind | What | Touches | Proof |
|---|---|---|---|---|
| **T0** | M | Groundwork with no behaviour change: `MU_MAX_LAYERS` / `MU_MAX_BUSES` / `MU_BUS_INSERTS` / `kLayersPerPage` in `MuLimits.h` (defaults 8 / 0 / 2 / 8); add `LayerTypeId`, `LayerTypeInfo`, `Layer::typeId`; `check-layer-boundary.ps1`; build a throw-away 16-slot APVTS layout from the param tables to **record construction time and memory** (the section 3.4 cost check). **No product source is touched** (the `Scales.h` renames moved to T4 and T5). | `mu-core`, `tests/scripts` | full Debug build of all five; every suite and `*_roundtrip` byte-identical; `mu-core-tests-16` builds and passes; measurement recorded in this doc |
| **T1** | M | **The empty unit.** `mu-tronic/` (Standalone only, freeware) with the eight folders, 16 `EmptyLayer`s, `setLayerType` (only `Empty` registered), the shared mixer/FX/master/modulator panel/preset flow/transport, sidebar and mixer **paging**, a `.muTronic` round trip. No audio sources, no licence code. This is the brief's "empty unit". | new product; `LayerSidebar`/`MixerOverlay` page (core, ux-controller) | builds; opens; the standalone smoke (no pluginval: no plugin formats); 16 strips page; state round trip; the other four unchanged (byte-identical) |
| **T2** | S | **Buses** in the shared mixer (section 5), opt-in `MU_MAX_BUSES` / `MU_BUS_INSERTS`, then tests with a test-tone layer in the core tests; mu-Tronic turns them on (**3 buses, 2 inserts each**). Rename the `outputBus` UI label to Out. | `mu-core` mixer + overlay + params | `BusRoutingTests`, `BusSidechainTests`, `BusInsertTests`, `BusStateResetTests` (section 7); all four products byte-identical (buses 0); a scripted mu-Tronic render with test-tone layers through a two-insert bus chain |
| **T3** | S | `Layer::render`, `RenderContext`, `renderLayers`, `RetireBin`, `setLayerType` with real types, `LayerServices`, `bindParams`, `preloadStaged`, `loopBeats` (section 2.1), defaults only; mu-core tests drive a fake layer type. No product changes yet. | `mu-core` | `LayerRenderTests` (order, priority, empty skipped, lock miss silent and still advancing, retire fade then free off thread, no allocation under the lock), `LayerTypeSwapTests`; products byte-identical |
| **T4** | S | **mu-Toni layer** (cleanest; mu-Toni is pre-release, so this may go ahead now): `readVoice` + pointers + held notes into `Arp::render`; param table and `LayerParamIds.h` into `Persistence/`; `ArpHost`; rename `Audio/Scales.h` to `ToniScales.h`; `mu-toni-layer` target; **mu-Toni standalone moves onto `renderLayers`** (so the glue exists once); mu-Tronic registers `Arp`. | mu-toni, mu-tronic | `TONI_roundtrip` byte-identical; mu-Toni suites; a mu-Tronic render with two Arp layers equals two mu-Toni layers' renders (the equivalence test, section 7) |
| **T5** | S | **mu-Tant layer. WAITS until after mu-Tant's next release (Andy).** Rename `Audio/Scales.h` to `TantScales.h`; render chain, engine, retire fade (replaced by the shared bin), per-layer Note mode, shared bank, param table, `PatternHost`; standalone adopts `renderLayers`. | mu-tant, mu-tronic | `TANT_roundtrip` byte-identical (Note-mode behaviour pinned by a new test first); mu-Tant suites; equivalence test |
| **T6** | S | **mu-On layers (mu-On is pre-release, so this may go ahead now).** One `Track` class, **five registered types Kick, Bass, Hat, Snare, Rumble** (section 1.4): per-layer step grid, swing/accent, per-layer engine allocation with the `engineLocked` flag, Rumble through `RenderContext::peer` (any priority-0 source), `TrackParams.h` to `Persistence/`, `TrackHost`, `typeFromPreset`; the standalone adopts and the five-engine pool goes (decided: per-layer in both). | mu-on, mu-tronic | `ON_roundtrip` byte-identical; the 5-lane preset still loads as 5 tracks; equivalence test per engine type |
| **T7** | S, O | **mu-Clid layer. WAITS until after mu-Clid's next release (Andy).** `SampleRhythm` (engine, MIDI engine, play state, snapshot, retire tail, sample path), glue out of `PluginProcessor_Modulation.cpp`, `PresetIO` shapes onto the `Layer` hooks, `SampleLibrary` as a service, sample I/O in `preloadStaged`, `RhythmHost`. **The standalone mu-Clid adopts it** (the glue must not exist twice); its `HotSwapStager` is either kept for the standalone only (recommended until the swap timing tests prove equal timing) or replaced. Highest risk of the plan (Stage 7 of Layer ownership is the precedent and its proof list applies in full). | mu-clid, mu-tronic | `CLID_roundtrip`, `swap`, `slot swap`, retire-tail renders byte-identical; `SwapCommitTimingTests`; `mu-clid-midi-tests` (shared `Rhythm`); equivalence test |
| **T8a** | S | Cross-type features that need only the types already extracted (**can go ahead after T4 and T6**): type-carrying clips, **scenes** (`launchScene`, `LaunchScene`, section 3.6), performance files for `MuTronicState`, per-layer MIDI in, **controller pages** (`PagePrev` / `PageNext`, Launchpad top-row 5 and 6) in `mu-control`, mu-link bridge for the standalone (the hook is one line), CPU meter on the transport bar. | mu-core, mu-control, mu-tronic | tests of section 7 |
| **T8b** | M | Importers for the products extracted later: `.muTant` and `.muClid` full presets (each lands with T5 / T7; `.muToni` and `.muOn` importers land with T8a). | mu-tronic | `TronicStateTests` import cases |
| **T9** | O | Release stages: installer, manual, site page (listed as freeware), no licence work. **Launch only when all four engines are ready** (Andy, 2026-10-15); the earlier "ship before T5 and T7" option is removed. | | |

**Superseded 2026-10-15 (later): [extraction-plan.md](extraction-plan.md) now sets the order (E0 safety net, E1 contract, E2 Toni, E3 On, E4 Tant, E5 Clid, then T1, T2, T8a); Tant and Clid are extracted before mu-Tronic product work, not after their next releases. The paragraph below is kept for its reasoning.**

**Order after Andy's timing answer (2026-10-15).** Run now: **T0, T1, T2, T3, T4, T6, T8a**, then T9 if Andy wants an early release. **Wait until after the next
mu-Clid and mu-Tant releases: T5 and T7** (and T8b for those two). Concretely:

- T4 (mu-Toni) and T6 (mu-On) can go ahead **earlier than T5/T7 and independently of them**, because both products are pre-release (`version-pre.txt`): no shipped
  build, saved session or release number is at risk. They do not depend on T5/T7; T3 is the only prerequisite.
- The live apps still link `mu-core`, so T0 to T3 are not literally "no change" for them. Every T0 to T3 change is additive or compile-time gated (defaults
  8 layers / 0 buses), touches no product source, and is proven by every suite plus byte-identical `*_roundtrip` of mu-Clid and mu-Tant. If Andy wants the next
  mu-Clid / mu-Tant release cut from a tree whose `mu-core` is exactly the released one, the release must be cut **before T2** (the first stage that edits the
  mixer's audio code) or the proof above must be re-run on the release candidate. T0 and T1 carry no such risk.
- Dropped from the early stages: the `Scales.h` rename in mu-Tant (now T5), so no live-product file is touched before its release.
- Until T5 and T7 land, mu-Tronic's chooser lists Arp, Kick, Bass, Hat, Snare and Rumble only (section 1.4: unregistered types are not offered).

Order rationale: T1 is the brief's literal first request and is small and safe. T2 before any voice because buses are pure `mu-core` and are testable
with synthetic sources, and because they affect the mixer UI every later stage shows. T3 before any product so the contract is proven on a fake type, not
guessed from one product (Stage 8's objection: "built now would have one type per product and no way to be tested against a second"). Then products by
ascending glue: Toni (already composed), On (global sequencer and pooled engines; pre-release, so it goes before the live products although it is larger than Tant),
then, after their releases, Tant (a long render chain in the processor) and Clid last (engine-in-layer reverses a Stage 7 ruling, the stager, samples, 1,580
references). The equivalence test in each of T4 to T7 is the proof that "completely self contained" holds. T4 to T7 are numbered by glue size, not run order; the run order is the paragraph above.

### What each product must change (summary)

- **mu-Toni** (now): `Arp::render`, held notes per layer, `Persistence/Toni*Params`, `ArpHost`, `ToniScales.h`, `mu-toni-layer`, adopt `renderLayers`.
- **mu-On** (now): per-layer step grid and swing/accent, one engine allocation per Track (`engineLocked` in mu-Tronic), five registered engine types, Rumble via the context, `TrackParams.h` to `Persistence/`, `TrackHost`, `mu-on-layer`, adopt `renderLayers`.
- **mu-Tant** (after its next release): `Pattern::render` + engine + cached pointers, Note mode per layer, shared wavetable bank service, `Persistence/PatternParams`, `SwapBoundary.h` to `Sequencer/`,
  `PatternHost`, `TantScales.h`, `mu-tant-layer`, adopt `renderLayers`.
- **mu-Clid** (after its next release): `SampleRhythm`, glue out of the processor, `PresetIO` onto the hooks, `SampleLibrary` service, `RhythmHost`, `mu-clid-layer`, adopt `renderLayers`.
- **mu-Clid MIDI**: nothing (core `Rhythm`).
- **All**: no `Plugin/`/`License/` include from library files; unique relative paths; param table as `addLayerParams`.

---

## 7. Tests (for the `test-steward`; listed so the stages are provable)

`BusRoutingTests` (assign, reassign mid-play, master default, bus and channel sums, a muted bus, solo-through-bus, direct out wins), `BusSidechainTests`
(legal sources per strip type, no loop, bus ducked by a channel, return ducked by a bus, source renumbering on swap/remove), `BusInsertTests` (the bus insert is the
layer insert algorithm for algorithm; the two-insert chain runs in order, insert 1 sees insert 0's output, an empty insert is a bit-exact pass-through; GR meter; the
tests run at `MU_BUS_INSERTS` 1 and 2 so the constant is real), `BusStateResetTests` (`copyFrom`/`reset`/`swapChannelState` carry `busAssign`), `SceneTests` (a scene
stages every non-empty layer with a clip n, skips layers without one, never stops a layer, all commits share one boundary under Bar timing, no allocation, the
`ControlAction` round trip), `LayerRenderTests`,
`LayerTypeSwapTests` (type change under a staged commit; old layer fades and frees off-thread; clip bank cleared), `TronicStateTests` (empty slots, mixed types round trip,
older file without `type`; each mu-On engine type round trips and `typeFromPreset` picks it from a `.muTrack`; Rumble reads a non-`Track` source and gets silence from a
priority-1 or empty one), the **equivalence test per product** (per mu-On engine type for mu-On) (a mu-Tronic slot of type X, same parameters and same MIDI, renders sample-identically to the
product's own layer; listening-test option added per product beside `roundtrip`), a two-thread stress test (type changes while `processBlock` runs: no
allocation, no crash), a CPU test (16 layers of the heaviest types within a block-time budget), the layout construction-time and memory measurement of T0. `check-layer-boundary.ps1`
has a fixture of its own. `mu-core-tests-16` runs the core suites at capacity 16.

---

## 8. Relation to mu-link

mu-link today is the **performance hub**: it owns the hardware output, sums the standalones' audio rings, is the master clock, offers Scenes, MIDI-clock out and
(planned) a Link peer. mu-Tronic gives the same rig in one process: one transport (already sample-accurate in-process), a mixer with buses and the shared
FX, a performance file and scenes (section 3.6; the `Performance` level exists), hardware output owned by the standalone, and the controller work in `mu-control`.
Andy's view (2026-10-15): "a new performance framework that might make mu-link redundant".

**Ruling (Andy, 2026-10-15: "leave it as is for now"): coexist, revisit later.** mu-link is **not frozen and not retired**; its stages continue as
planned. Not "replace" because mu-link still does what mu-Tronic cannot: sum *separate processes* (another mu product instance) into one output with one clock.
Not "absorb" because that would copy mu-link's server, IPC and registry into a product that does not need them.

- mu-Tronic **is a mu-link client like the other standalones** (the bridge is one line in `StandaloneApp.cpp`), so the two can run together; when attached it
  slaves to the mu-link clock via `resolveTransport`.
- **No duplicated clock/MIDI code.** MIDI clock out, the Link peer, the tempo PLL and the transport sources already live or are planned in `mu-core`
  (`TimedMidiOut`, `MidiClockTempo`, `TransportResolver`); mu-Tronic and mu-link both use them. Anything mu-link would still need to build for performance
  use (Scenes, a Link peer) is built in `mu-core` or the shell where it is generic, so the second product that wants it does not copy it. This is the
  standing one-framework rule, not a freeze: **no feature freeze on mu-link**.
- **Revisit later.** When mu-Tronic's T8a is done, Andy may judge whether mu-link is redundant (the earlier draft's test: hardware output ownership, MIDI-clock out,
  a Link peer if licensed, scenes, controller workflow). Nothing is scheduled and no decision is needed before then.

---

## 9. Risks and open decisions

### Risks (and the answer to each)

1. **Parameter count** (about 4,000 to 6,000). No longer a DAW concern (Standalone only); an internal construction-time and memory check at T0 (3.4), lazy per-type declaration as the fallback.
2. **Stage 7 reversal for mu-Clid** (engine into the layer, 1,580 references). Mitigated by making it last, by `SampleRhythm` being a derived type
   (mu-Clid MIDI's `Rhythm` is untouched), and by the Stage 7 proof list applying in full. If T7 stalls, mu-Tronic ships without the Rhythm type and
   nothing else is blocked.
3. **Self-containment drift.** Without a checker a product will re-add a `Plugin/` include. `check-layer-boundary.ps1` plus the equivalence tests are the guard;
   `architecture-steward` reviews every T4 to T7 diff.
4. **CPU.** 16 layers of mixed engines (mu-On Rumble, mu-Tant filters, mu-Clid insert tails) can exceed a laptop budget. Empty layers cost nothing; a CPU
   meter lands in T8; a per-layer "freeze/mute stops processing" rule (a muted layer skips `render` but not note tracking) is a candidate saving, decided
   with the `dsp-controller` at T3. The honest limit is "16 slots", not "16 of the heaviest".
5. **Cross-layer audio (Rumble).** The only layer that reads another layer's output. Contained by `renderPriority` + `peer`, in the same instance; the source may be
   any priority-0 layer of any type (section 1.4); an empty, priority-1 or removed source processes silence (as today). The source index is remapped on swap/remove.
6. **Same-name collisions in one binary** (global names, include paths). Handled in 2.5; the checker greps.
7. **Hot-swap of a type change** replaces an engine, not just data. Uses the shared `RetireBin` and `preloadStaged`; staged at the boundary like any swap;
   `LayerTypeSwapTests`.
8. **Colour palette** has 8 colours for 16 layers (section 3.4). `ux-controller`.
9. **Live-product impact.** mu-Clid and mu-Tant are shipping (1.1). **Superseded 2026-10-15: Andy ordered their extraction (T5, T7) before mu-Tronic product work**; safety is the golden / compat / equivalence net and the release rules of extraction-plan.md sections 3 and 4. (Earlier answer: after their next releases.)
   Before that only additive `mu-core` stages (T0 to T3) reach them, byte-identical or reverted; the release-cut rule is in section 6. None of the stages
   changes saved data or parameter ids (prefixes and extensions stay).
10. **Free product carries the engines of the Pay What You Want apps.** A free mu-Tronic gives away mu-Toni's and mu-On's engines (and later mu-Tant's and mu-Clid's)
    without a Support prompt. This follows from Andy's answers (freeware, every engine a layer) and the layer libraries contain no licence code; recorded so it is a
    known consequence, not a surprise. Resolved by A14 (2026-10-15): licence follows the install via a per-app manifest, no key code in mu-Tronic; whether a Support prompt is shown for installed-but-unlicensed engines is a T1 / `ux-controller` call (extraction-plan sections 8 and 9).
11. **Layer count in the existing `getLayerName`/`getLayerColourIndex` overrides** (each product returns `"Voice N"`-style names): mu-Tronic names a layer from
    its type and slot (`Layer::name`), a default the base already provides.

### Decisions: resolved by Andy, 2026-10-15

| # | Question | Answer | Where it landed |
|---|---|---|---|
| 1 | Fifth product | Yes; the four apps continue | header, 0 |
| 2 | Plugin formats | **Standalone only** ("a new performance framework that might make mu-link redundant"); no VST3/CLAP; parameter-count/pool work dropped | 3.1, 3.4 |
| 3 | Licence / price | **Free**, freeware like mu-Link and mu-Clid MIDI; no key, no Lemon Squeezy; site lists it as freeware. A14 (2026-10-15): licence state of the engines follows the install via a per-app manifest, no key code in mu-Tronic; a Support prompt for unlicensed installed engines is a later T1 / `ux-controller` decision | 3.1, 3.2 |
| 3a | Engine loading (A14) | **Static layer libraries**, not runtime modules; engines update with mu-Tronic (suite-wide releases) | 2.5, 3.2 |
| 4 | Name | mu-Tronic (display), `mu-tronic` (slug), code `MTrn` (JUCE needs an upper-case letter) | 3.1 |
| 5 | Timing | **Revised 2026-10-15: all four extractions first (E0 to E5 of extraction-plan.md), mu-Tronic product work after; the first release waits for all four engines (no early release).** Earlier: T5, T7 after next releases | extraction-plan.md, risk 9 |
| 6 | mu-link | Leave as is; coexist; no freeze; revisit later | 8 |
| 7 | Buses | 3 buses, 2 inserts each, both compile-time (`MU_MAX_BUSES`, `MU_BUS_INSERTS`) | 5 |
| 8-11 | UI | Ice-white accent; main area painted by layer-type colour; layer names editable and saved; Launchpad top-row 5 and 6 change page | 4, `design-ui.md` (ux-controller), design-launchpad §3.6 |
| 12 | Overview paging | Pages of 8 by default, page size and count constants, not hard-wired ("2 pages of 8 perhaps, keep it flexible") | 1.1, 4 |
| 13 | Scenes | Yes; an action across layers | 3.6 |
| 14 | mu-On engines | Each of Kick, Bass, Hat, Snare, Rumble is its own layer type | 1.4, 2.5 |
| 15 | Layer-preset folders | Shared with each product's existing folders | 3.4 |

### Still open (Andy)

1. **Closed (Andy, 2026-10-15): no early release.** mu-Tronic launches only when all four engines (all nine types) are ready.
2. **Closed (A14, Andy, 2026-10-15): static libraries, no runtime engine loading.** "Go with the recommendation. Updates will be suite wide so mu-tronic will rebuild if the engine of an app does."
   Licence follows the install via a per-app manifest (supporter flag, checkout URL, content dir); mu-Tronic has no key code. Analysis: [extraction-plan.md](extraction-plan.md) sections 8 and 9.
   Remaining small point, for T1 and the `ux-controller` (not Andy yet): does mu-Tronic show a generic Support prompt for an installed-but-unlicensed engine in use, or none.
3. **Closed (Andy, 2026-10-15): Rumble reads any layer type.**
4. **Launchpad scenes.** How a scene is fired on the pad (a modifier, a spare button, a column). Not needed until T8a; `ux-controller` proposes.
5. **Later revisit of mu-link** (no date).

---

## 10. Owner-visible effects of this plan on existing products

**Revised 2026-10-15:** the shipping products are extracted before mu-Tronic product work, under the "nothing audible or saved changes" rule and the safety net of extraction-plan.md (one exception under decision: mu-Tant Note mode). Earlier text follows. None to the shipping products before their next releases. T0 to T3 add compile-time defaults and additive `mu-core` code only (no product source changes, no file
renames). mu-Toni and mu-On (pre-release) are rebuilt on the shared render path from T4 and T6 and must be sample-identical; mu-Tant and mu-Clid follow after their
next releases (T5, T7; the `Scales.h` renames happen then). No preset, parameter id, extension, plugin code or folder changes. mu-On's and mu-Toni's global swing/accent and held-note settings become per layer inside, with
the same defaults and the same saved results (pinned by tests before the move).
