# mu-Tronic — design (architecture ruling, 2026-10-15)

**Status:** design only. Nothing is built. Written by the Architecture Steward for Andy's brief of 2026-10-15 and
revised the same day after his answers (section 9 records them); the look and feel of every screen below is the
`ux-controller`'s, and real-time review of the audio-thread parts is the `dsp-controller`'s before anything is
pushed. Only the short "still open" list in section 9 needs Andy; everything else is ruled.

**Settled by Andy, 2026-10-15 (the shape of the product):** a fifth product; **Standalone only** (no VST3, no CLAP); **freeware** (no licence key, no
Support prompt); name mu-Tronic, plugin code `MTrn`; three buses with two inserts each; every mu-On engine is its own layer type; scenes; mu-link
left as it is; the product refactors of mu-Clid and mu-Tant wait until after their next releases (historical). **Superseded later the same day:** Andy re-prioritised, so all four extractions come before any mu-Tronic product work; see [extraction-plan.md](extraction-plan.md), which replaces the ordering in section 6.

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
| **mu-On** (`Track`) | `Track : Layer` (step row, Rumble envelope, trigger counter); engines `KickEngine`, `BassEngine`, `SampleChannel`, `RumbleEngine` in `Audio/` | (as it was before E3; built at E3, 2.1.9) `GrooveVoices` held **every engine of every slot** (5 per slot, large) and does the modulation resolve; `StepPattern` + `GrooveSequencer` are **one global grid and clock for all tracks**, with global swing/accent params; the Rumble **reads another layer's audio** (render order, `remapLayerReferences`); `Plugin/TrackParams.h` (`EngineType`, param ids) is included by the engines, `MuOnModDest.h`, `StepPattern.h`, `GrooveVoices.h`; `TrackContent`/`StepRows` include the processor | Per-layer engine instance (only the selected engine allocated; changing engine is a layer-internal swap on the message thread), per-layer step grid and swing/accent (`StepPattern` becomes a per-layer `StepRow` driver, `GrooveSequencer` clocks one row), `TrackParams.h` into `Persistence/`, the Rumble source resolved through the render context (section 2.1), `TrackContent` onto a `TrackHost&`. No per-layer insert today (hook returns `nullptr`); buses give On groups an insert. **Each engine is also its own mu-Tronic layer type: section 1.4.** |
| **mu-Clid** (`Rhythm`) | Engine, sequencer, MIDI engine and play state are **already in `mu-core`** (`mu_core::Rhythm`, `RhythmSequencer`, `::VoiceEngine`, `MidiOutputEngine`, `RhythmPlayState`, shared with the MIDI product); `RhythmContent`, `VoiceSection`, `MidiOutSection`, `SampleBrowser`, `EuclideanPanel` | The per-block glue: `PluginProcessor_Modulation.cpp` (444 lines: mod seed and write-back, Euclid overrides), trigger firing, `voiceEngines[]`, `midiEngines[]`, retired tails and drain flags, `rhythmPlayState[]`, `modSnapshot`, `rawParams`/`pendingDirty` drain, `SampleLibrary`/sample paths, `orphanOffs`, `HotSwapStager` (prepares a `Rhythm` + `VoiceEngine` at stage time, sample I/O included), `PresetIO` (includes the processor), the master loop in `RhythmSequencer` | New `mu_clid::SampleRhythm : mu_core::Rhythm` that carries the engine, MIDI engine, play state, snapshot, sample path and retire tail, with `render`; the glue becomes its methods; `PresetIO` shapes onto `Layer::writeExtras/applyExtras` + `captureLayerPresetTree`; sample I/O at stage time via `preloadStaged`; `SampleLibrary` becomes a shared service |

Everything above is "voice code in the processor" that the docs already say belongs elsewhere (design-naming §3: `Plugin/` holds only the processor,
editor and standalone; engine and preset code belong in `Audio/` and `Persistence/`). So this is also a correction, not only new work.

### 1.4 mu-On's engines are individual layer types (Andy, 2026-10-15)

Andy: every mu-On voice engine can be added as a mu-Tronic layer. So the registry lists **Kick, Bass, Hat, Snare and Rumble as five layer types**, not one
"mu-On" type. They are five registry entries over **one class**, `mu_on::Track`, differing only in the engine the entry fixes (`mu_on::EngineType`):

| Aspect | Ruling |
|---|---|
| Ids | `LayerTypeId` gains `Kick, Bass, Hat, Snare, Rumble` (section 2.5), saved by name. The enum order of `mu_on::EngineType` is not saved or relied on. |
| One class | In mu-On the engine choice on a track stays and is changeable at any time (also by host automation, in the same block), so an unlocked `Track` holds all five prepared engines (as built at E3, 2.1.9). In mu-Tronic the entry creates the `Track` with the engine **locked**: the engine selector is hidden, the track holds exactly one engine, and changing the sound family is a type change (chooser, confirm, clip bank cleared). One code path, one flag (`engineLocked`) choosing the allocation policy. |
| Parameters, prefix, preset | Unchanged: prefix `t{N}_`, extension `.muTrack`, `Persistence/TrackParams` (`addLayerParams`). The engine id stays a saved parameter, so a `.muTrack` saved by mu-On loads into a mu-Tronic slot of the right type: **type inference reads the extension for the family, then the saved engine for the member** (`LayerTypeInfo::typeFromPreset(tree)`). A mu-Tronic Kick preset loads in mu-On. |
| Mod targets, content, mini-visual | Per entry: the mod-target table is the engine's own subset (mu-On already filters by engine), `TrackContent` shows only the locked engine's panel, the mini-visual is the step row. |
| Self-containment (section 1.3) | The engine belongs to the layer and the step grid is per layer; a 16-slot instance cannot afford five engines per slot, so a locked `Track` holds one. As built, the standalone keeps all five per track (an unlocked track must switch engine in the same block); the per-slot pool `GrooveVoices` is gone. |
| Global grid and clock | Gone from the layer: each `Track` owns its step row, length, swing and accent and reports `loopBeats()` (a 16-step row is one bar). The standalone's grid view is a view over its tracks; the shared clock is `RenderContext::startBeat`, as for every type. |
| Rumble reading another layer | Stays the only cross-layer audio read. Priority 1; the source is a slot index resolved through `RenderContext::peer(slot)`. Because the types are now independent, the source may be **any priority-0 layer of any type** in the instance (a Kick, an Arp, a Pattern), which costs nothing extra and is the useful case; a priority-1 source (another Rumble) or an Empty slot processes silence. The source slot index is a layer parameter and is remapped on swap/remove by the base's `remapLayerReferences` hook (generic since Stage 6, called in phase 3 where parameters may be written); `layersReordered` is **not** the hook for it (ruled at E3, 2.1.9 point 4). The Track-side rewrite is one library function, so mu-Tronic's processor calls it rather than copying it. |
| Chooser | Types are grouped by product in the picker: `LayerTypeInfo::group` ("mu-Clid", "mu-Tant", "mu-Toni", "mu-On") and `displayName` ("Rhythm", "Pattern", "Arp", "Kick" ...). Nine types plus Empty; the layout of the chooser (nine cards or a grouped list) is the `ux-controller`'s. A type is offered only if registered in the build, so a mu-Tronic that has not yet got Pattern and Rhythm simply does not list them. |
| Default names | `<displayName> <slot>` ("Kick 3"), editable and saved (section 4). |

---

## 2. Where voice code lives

### 2.1 The contract: `Layer::render` and the per-block context (Stage 8, option C, now scheduled)

Stage 8 of design-plugin-family waited for exactly this decision. Ruling: **dispatch stays virtual on `Layer`; a registry only creates and describes.**
This section is the specification of record for E1. Subsections 2.1.2 to 2.1.5 are the four design fixes C1 to C4 of the real-time review
([extraction-plan.md 2.1a](extraction-plan.md#21a-contract-requirements-from-the-real-time-review-c1-to-c18), written 2026-10-15 as stage E0d); C5 to C18
stay in that table and bind this section too. **Status: specified 2026-10-15; `dsp-controller` round 1 (2026-10-15): C1 and C3 Pass with notes, C2 and
C4 Not yet (R1 to R7), structure kept; this revision folds in R1 to R7, the binding notes N1 to N7, the accepted amendments and the open-point rulings (2.1.6).
Round 2 (2026-10-15): C1 and C2 Pass with notes, C3 Pass, C4 Not yet (F1, folded in with the R5 rule line and the round-2 binding notes on the packed word,
lock order, eviction under `layersLock` and phase 4). `dsp-controller` sign-off: C1-C3 signed (round 2, with binding notes); C4 accepted once F1 and the R5
rule line are in the doc (dsp-controller: no further round).** E1 may start on C1 to C3. **E1 built 2026-10-16** (branch `extraction`); the
`architecture-steward`'s rulings on where the build differs from this text, and what each product must convert when it adopts the contract, are in 2.1.7. The
subsections below already show the accepted as-built shape.

New hooks (defaults do nothing; all called once per layer per block or per edit, never per sample). `Layer` gains a second base, the interface
`mu_core::RetireTail` (2.1.3), so the bin can hold either a whole layer or a product's engine tail:

| Hook | Thread | Purpose |
|---|---|---|
| `int typeId() const` | any | The type's id (a `mu_core::LayerTypeId`, section 3.3). |
| `void prepare(double sr, int blockSize)` | message; the layer is not reachable by the audio thread (not yet published, or the host is not processing) | Allocate everything (engine, buffers, insert). |
| `void bindParams(juce::AudioProcessorValueTreeState&, int slot)` | message, before publication and again on `onMoved` (C9) | Cache the slot's parameter atomics (the products' `voicePtrs` / `vp` / `modDestAtoms` tables move here). `render` never calls `getRawParameterValue`. |
| `void render(const RenderContext&, juce::AudioBuffer<float>& out, int n)` | audio | **Overwrite** frames `[0, n)` of `out` (two channels) with the layer's block, insert included (the former `renderVoice` / `renderChannelCb` / `GrooveVoices::renderAll`). No allocation, no lock beyond the existing in-layer spin locks, no APVTS by string, no host call (2.1.5). |
| `int renderPriority() const` | any | 0 normal, 1 after every priority-0 layer (mu-On Rumble reads a peer). It may follow a parameter (mu-On's engine); the base reads it **once per call per live layer** and uses that reading for both passes (2.1.9 point 5). |
| `double loopBeats() const` | audio | Loop length in beats for the hot-swap boundary (replaces `swapLoopBeats`, `blkPatternBeats`); cached per slot by the base each rendered block. |
| `void preloadStaged(const juce::ValueTree&)` | message | File I/O for a staged tree (sample, user wavetable) so the boundary commit does none. |
| `void onTransportStopped(const RenderContext&)` | audio | The play-to-stop edge (2.1.5): silence held notes and tails (Tant gates, On voices, Toni note-off). |
| `void onTransportJump(const RenderContext&)` | audio | A discontinuity while playing (locate, host loop, clock snap): release or re-anchor pending notes (2.1.5). |
| `void onParkedBlock(const RenderContext&)` | audio | Once per call for a layer parked above the live count (a kept spare): no audio, no output, no parameter; it may follow the call's MIDI and flags so input state it keeps (held keys) is current when the slot goes live (2.1.8). |
| `RetirePolicy retirePolicy() const` | any | How this layer leaves: `None` (dropped after the grace period; no family layer type uses it from T1b, 2.1.11 point 3), `Fade`, `Natural` (2.1.3). Replaces the earlier `tailBlocks()`: block counts depend on the block size. |
| `void beginRetire()` | message | Freeze what the tail needs (2.1.4). |
| `void onRetireStart(const RenderContext&)` | audio, once | Audio-side freeze at the first tail block (2.1.4). |
| `bool renderRetiring(const RenderContext&, juce::AudioBuffer<float>& out, int n, RetireState&)` | audio | **Add** the tail into `out` (or write it when `RetireState::exclusive`); return false once silent for good (2.1.3). |
| `const std::atomic<float>* insertGainReduction() const` | exists | Unchanged. |

**Where the engine lives (amends Stage 7 ruling 1 for hosts that render through `Layer::render`).** Stage 7 kept engines processor-side so this question
stayed open. It is now answered: **the engine lives in the layer object** (a `unique_ptr` member for the heavy ones), and travels with the layer on a
pointer move. What stays processor-side is only state that must outlive or sit still apart from the layer: the mixer strip (sidechain followers), the UI
snapshot tables read by index at 30 Hz (moved into the layer as atomics, address-stable because the layer is heap-allocated), and the **retire bin**
(2.1.3). `Rhythm` stays a copyable value for mu-Clid MIDI; mu-Clid's engine-carrying type is the derived `mu_clid::SampleRhythm` (section 1.3), which is not
copyable and which only `Layer::render` hosts use.

#### 2.1.1 The shape of one block (base driver)

A product's `processBlock`, after its E stage, is the transport, one base call and its own shell post-stages; everything between is `ProcessorBase`:

```cpp
void mu_xxx::PluginProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    // no captureSidechainAndClear here: renderAndMix does the sidechain copy and the clear per chunk (below)
    queueMidiProgramChanges(midi);                         // router claims leave `midi` here
    const auto t = resolveBlockTransport(midi, buffer.getNumSamples(), kBeatWrap);   // base wrapper of resolveTransport (2.1.5); t carries kBeatWrap; mu-Clid keeps its Note-mode source
    renderAndMix(buffer, midi, t, directOuts, fxReturns);  // base: LayerBlock, MIDI decode, chunk loop { renderLayers, processCoreBlock }; both outs optional (mu-Clid's direct outs)
    mergeLayerMidiOut(midi, byteBudget);                   // only a product whose layer types write ctx.midiOut (mu-Clid MIDI mode, mu-Tronic)
    // product shell post-stage, if any: mu-Tant's Note-mode final-mix gate (decision A3), mu-Clid's sample preview
    flagLayerSwapBoundaries(t, fullPresetLoopBeats);       // per-slot loopBeats cached by renderLayers; the full preset's length is the product's rule (default 4); runs on a miss too
}
```

**The beat bound is given once.** `kBeatWrap` goes to `resolveBlockTransport` only and travels in the `BlockTransport` it returns; `renderAndMix` reads it from
there for the continuity reduction and `RenderContext::beatWrap`. `renderAndMix` has no `beatWrap` argument (as built, 2026-10-16): the resolver sets
`BlockTransport::beatWrap`, because two copies of one value can differ, and a mismatch makes every wrap of mu-Tant's 64-beat space a false jump.

**Sidechain capture and the clear are inside `renderAndMix`** (as built). It takes the host buffer as received and, per chunk, copies that chunk's DAW-sidechain
frames, clears the chunk's output region, then renders and mixes; a skipped block (failed `LayerBlock`) clears the whole buffer. A product adopting the driver
**must not** call `captureSidechainAndClear` before `renderAndMix`: doing so on an oversize host block captured the wrong sidechain frames for later chunks
(wrong ducking).

**MIDI out is merged by the product, not by `renderAndMix`** (as built, accepted 2026-10-16). The layers' and tails' MIDI stays in the block's event storage;
the product calls `mergeLayerMidiOut(midi, byteBudget)` once per rendered block, after `renderAndMix` and before any shell stage that reads `midi`. The budget
is the room the product knows the host buffer already has, so the merge never grows it on the audio thread (2.1.5, R7); tails' owed note-offs go first. Only the
product knows that room (mu-Clid's `ProcessorSeamTests` rule), which is why the base does not merge by itself; the budget mechanism is settled at E5.4c. A
product none of whose types writes `midiOut` (mu-Tant, mu-Toni, mu-On) does not call it.

Each product keeps its own `juce::ScopedNoDenormals` at the top of `processBlock` (N7): mu-Tant's Note-mode gate and any other shell post-stage run outside
`LayerBlock`. **Shell code reads no layer (N5):** in `Published` mode nothing on the audio thread outside `LayerBlock` may read `layers[]` or `layerAs<T>`;
shell post-stages and `flagLayerSwapBoundaries` use values cached by `renderLayers` only (rule 6 of `check-layer-boundary.ps1`, extraction-plan 4.5).

`renderAndMix` (base, audio thread, `noexcept`): (1) feeds the host-block continuity check of 2.1.5 (every host block, rendered or not), then constructs a
`LayerBlock` (2.1.2), which owns `juce::ScopedNoDenormals` for the whole render, bin and mixer (C11: held by the block object rather than by `renderLayers`
alone, so the mixer and FX are covered too); a failed `LayerBlock` renders nothing and **touches no bin entry** (R4), exactly as a missed try-lock today, while
the transport (already resolved) and the boundary flagging (cached loop lengths) still advance. (2) Decodes `midi` once into the block's `MidiEvent` array
(2.1.5). (3) **Chunks** a block longer than the prepared size into pieces of at most the prepared size and runs `renderLayers` then `processCoreBlock` per piece
on a sub-buffer of `buffer` (C8; the chunking sits here and not inside `renderLayers` because `MixerEngine`'s channel buffers are sized at prepare too; today's
truncation of an oversize block is a defect, open point 10). (4) A host block of 0 frames renders nothing and calls no hook; its play/stop edge and any jump are
delivered on the next rendered block.

`renderLayers(const LayerBlock&, const BlockTransport&, chunk)` (base, audio thread), in this order:

| Phase | What | Result |
|---|---|---|
| 0 (first chunk) | Play/stop edges against the last *rendered* block, `transportJumped` from the sticky `jumpPending` (2.1.5); `onTransportStopped` / `onTransportJump` on every live layer in slot order. `mixCount` by the product's count policy (2.1.3, R3): `RenderPastCount` gives `max(view.numLive, bin.highestActiveSlot(view.serial) + 1)` (mu-Tant's `renderCount` rule), `CutAtCount` gives `view.numLive`; taken **before** any tail renders. | base `RenderContext` template |
| 1 | Priority-0 live layers in slot order: `ctx.slot = s`, `layer->render(ctx, scratch[s], n)`. An Empty layer is not called; its scratch is cleared. **Every slot below `mixCount` with no live layer gets a cleared scratch here** (R4), so an entry cut between phase 0 and phase 4 renders silence, not last block's samples. | `scratch[s]` |
| 2 | Peer table: `peers[s] = { scratch[s] pointers, valid = 1 }` for each slot rendered in phase 1; every other entry `valid = 0`. | `PeerTap[kMaxLayers]` |
| 3 | Priority-1 live layers, with `ctx.peers` set (C10: post-insert, pre-strip, and before any tail is added). | `scratch[s]` |
| 4 | The bin (2.1.3), rows `s < mixCount` only, slot order then entry order: tails add into `scratch[s]` after the live layer, as mu-Clid's retired engines add after the live engine today. | `scratch[s]` |
| 5 (last chunk) | Cache `loopBeats()` per live slot; latch the rendered play/stop state and clear `jumpPending` (the continuity end beat is kept per host block, not here); publish nothing else. | caches |

`processCoreBlock` gets a `RenderChannelFn` that copies `scratch[r]` into the mixer's channel buffer (an exact copy, so R11 holds) for `r < mixCount`. Scratch
buffers (`kMaxLayers` x 2 channels x prepared size) and the decoded MIDI array are allocated in `prepareToPlay` / at construction, never grown.
The driver's own state (scratch, peers, latches, MIDI storage) and the `RetireBin` are `ProcessorBase` members held by a `unique_ptr` created in the member
initialiser (as built, accepted): allocated once on the constructing thread, never reset or reseated while the processor lives, and reached through a
reference member, so the audio thread sees a fixed address and `ProcessorBase` stays small for stack-built test hosts. The driver is in
`mu-core/Plugin/ProcessorBase_Render.cpp` (design-naming §3, `ProcessorBase_<Aspect>.cpp`).

#### 2.1.2 C1: layer access and non-muting publication

**Ruling.** The audio thread reaches layers through exactly one object, `LayerBlock`, and through nothing else. `LayerBlock` has two implementations behind
one interface, selected once per processor (`ProcessorBase::setLayerAccess`, constructor only):

- **`LayerAccess::TryLock`** (E1 to E6, and the four products until each switches deliberately): today's protocol, byte for byte. `ScopedTryLock(layersLock)`
  plus the `layerEditInFlight` check; on success it fills a block-local `LayerView` from `layers[]` and the packed layout word; the lock is held until the
  block object is destroyed (the mixer still reads strip state guarded by it). Nothing in any product changes behaviour.
- **`LayerAccess::Published`** (mu-Tronic from T1; a product only in its own later commit, after E6, with the stress tests): the audio thread never takes
  `layersLock`. It checks `layerEditInFlight`, then loads one immutable published `LayerView`.

```cpp
// mu-core/Plugin/LayerBlock.h
struct LayerView                                    // immutable once published; never written by the audio thread
{
    uint32_t serial  = 0;                           // layout serial: changes on every publication and structural edit
    int32_t  numLive = 0;                           // slots 0..numLive-1 render
    uint32_t frozenMask = 0;                        // C7 provision: bit s = slot s renders from its last resolved values (no APVTS, no matrix)
    std::array<Layer*, mu_limits::kMaxLayers> layer {};   // every slot, spares included (in-place tails, peers); null = no object
};

class LayerBlock                                    // audio thread, stack only, one per processBlock
{
public:
    explicit LayerBlock(ProcessorBase& owner) noexcept;    // epoch.enter(); then TryLock or Published acquisition
    ~LayerBlock() noexcept;                                // releases the try-lock (TryLock mode); epoch.leave()
    explicit operator bool() const noexcept;               // false: render nothing this block, touch no bin entry
    const LayerView& view() const noexcept;
private:
    juce::ScopedNoDenormals noDenormals;                   // C11
    // TryLock: juce::ScopedTryLock + a local LayerView;
    // Published: after enter(), layerEditInFlight.load(seq_cst) (N4), then const LayerView* loaded once (seq_cst)
};

// mu-core/Plugin/AudioEpoch.h: the grace period, single audio-thread writer (processBlock is never re-entered)
class AudioEpoch
{
public:
    void     enter() noexcept;    // audio: r = e.fetch_add(1, seq_cst); r must be even (N4); Debug: records the thread id
    void     leave() noexcept;    // audio: r = e.fetch_add(1, seq_cst); r must be odd; Debug: clears the thread id
    uint64_t stamp() const noexcept;                 // any: e, seq_cst
    bool     passed(uint64_t s) const noexcept;      // (s even) || (e != s): no block that could hold an old pointer is still running
    bool     waitFor(uint64_t s, int timeoutMs) const noexcept;   // non-audio thread: yield until passed(s) or timeout; false = timed out
                                                     // Debug: jassert the caller is not the thread inside the block
    bool     reclaimDisabled() const noexcept;       // sticky: an enter/leave saw the wrong parity (Release); true until destruction
private:
    std::atomic<uint64_t> e { 0 };                   // static_assert(std::atomic<uint64_t>::is_always_lock_free)
    std::atomic<bool>     broken { false };
};
```

**Parity and a moved audio thread (N4).** `enter()` / `leave()` are `fetch_add(1, seq_cst)`, not a load and a store: the same cost as a seq_cst store on x64, and
correct if the host moves `processBlock` to another thread between blocks. A wrong returned parity (two threads inside at once, outside the JUCE contract) is a
`jassert` in Debug; in Release it sets the sticky `broken` flag, which **turns off all reclamation** (views, layers, tails are kept, not freed, until
destruction): a leak, never a use-after-free. Before E4, M2 confirms with the assert in Debug pluginval runs of VST3, CLAP and Standalone plus an offline bounce
while playing in Reaper and Bitwig (open point 2).

**Waiting, and what each caller does on a timeout (N2).** No caller waits unboundedly; `waitFor` returns false after `timeoutMs` (proposed 50 ms), and each
caller has its own fallback:

| Caller | Waits? | On timeout |
|---|---|---|
| Bin frees (`service`) | never: it tests `passed` and defers to the next pass | n/a |
| `setLayerType` step 6 | yes | proceed: worst case one block in which the old live layer reads a parameter mix; no memory is at risk (nothing is freed by step 7) |
| Quiesce (`Published` mode structural edits) | yes | give up, lower the flag, retry on the next deferred-work pass; **never move strip state** without the grace |
| Bin eviction (2.1.3) | yes | drop the **incoming** tail (freed later by `service` after its own grace); the evicted entry stays Done |
| Quiesce in `TryLock` mode | **skipped**: the blocking `layersLock` already excludes the audio thread, and the four products' timing stays as today | n/a |

The `LayerView` storage is a pool of `kLayerViewPool` (4) views owned by `ProcessorBase`; `published` is a `std::atomic<const LayerView*>`. A view is reused only after
`epoch.passed(stamp)` for the stamp taken when it was replaced. **No operation publishes twice without a grace wait in between** (asserted, N3); `setLayerType`
and the scene commit wait between their two publications, so at most two views are in grace at once. If the pool is still exhausted, the writer allocates an
**overflow view on the message thread** instead of waiting (it joins the reclaim list and is freed after its grace); the T1 stress test counts overflow
allocations and expects none. No reference counting anywhere (C6): views, layers and tails are owned by `unique_ptr`s on the message thread and reached by raw
pointer on the audio thread.

**What the audio thread loads and never retains.** It loads the view once, in the `LayerBlock` constructor. `Layer*`, `LayerView*`, `RetireTail*` and scratch
pointers are used only inside that block and never stored in a member that outlives it. State that survives blocks (edge latch, loop-length cache, peer
validity) holds values, never addresses.

**Memory orders and the proof.** Writer (message thread): `published.store(next, seq_cst)`, then `s = epoch.stamp()` (seq_cst); `prev` is reclaimable once
`epoch.passed(s)`. Reader: `epoch.enter()` (seq_cst read-modify-write), then `layerEditInFlight.load(seq_cst)`, then `published.load(seq_cst)`, use,
`epoch.leave()`. In the single total order of seq_cst operations, if `s` is even no block was running at the stamp, so every later block's load comes after the
store and sees `next`; if `s` is odd the running block may hold `prev`, and `e != s` proves it ended. This is the Dekker (write, then load of the other variable)
pattern, which is why these variables are seq_cst rather than release/acquire (the flag load is seq_cst for the same reason against quiesce's flag store and
stamp); the cost is two seq_cst read-modify-writes per block. The view's fields are plain data published by the pointer store (a seq_cst store is also a release).

**Who builds, prepares, binds and defaults a new layer, in what order** (`setLayerType`, mu-Tronic, T1; message thread throughout; nothing suspended, nothing
muted):

| # | Step | Why here |
|---|---|---|
| 1 | Cancel the slot's staged swaps (as every edit does). | A staged payload of the old type must not land on the new one. |
| 2 | `g = prepareGeneration` (read under the writers' mutex); `fresh = info.create(slot)`; `fresh->prepare(sr, bs)` with that generation's rate and size. | All allocation before anything is visible. `prepareToPlay` may run on a host thread between here and step 8 (N1). |
| 3 | Tear down the slot's `<Type>Content` and mini-visual (C16). | No UI pointer into the old layer survives into the bin. |
| 4 | `old->beginRetire()` when `old->retirePolicy().kind != None` (2.1.4). | The last moment the old layer may read its parameters: none has changed yet. |
| 5 | Publish view **V1** with the slot set to null (Empty) and `bin.park(slot, std::move(old), V1.serial)` (or `None`: the old layer goes to the reclaim list). The bin entry is written before V1 is published (2.1.3, activation serial: a park outside `layersLock` uses the next serial, R4). | From V1 on, the old layer renders only as a tail. |
| 6 | `epoch.waitFor(stampAfterV1, timeout)`; on timeout proceed (N2). | No block that still renders the old layer **live** (it reads the slot's parameters) can overlap step 7; a timeout costs at most one block of a parameter mix, no memory. |
| 7 | Reset the new type's prefix of this slot to defaults, or apply `initial` (`applyLayerNode`), through the APVTS as any edit does; `fresh->bindParams(apvts, slot)`; the mixer strip `ch{N}_` is kept **and its sidechain follower is left alone** (ruled 2026-10-20, 2.1.11 point 2: C17 as first written, a follower reset here, would pump; only `resetLayer` and a strip-defaults restore request one); then `fresh->completeSetup(apvts, slot)` (2.1.11 point 5). | Nothing live reads these parameters now: the old layer is a tail reading its snapshot, the new one is not published. Same-prefix types (Kick to Bass, both `t{N}_`) are the reason. |
| 8 | Under the writers' mutex: if `prepareGeneration != g`, `fresh->prepare(sr, bs)` again with the current values (N1); then publish view **V2** with `fresh` in the slot. | One atomic pointer store; the new layer's first block is the first block that loads V2, and it is prepared for that block's rate and size (a grown block size would otherwise overrun). Parameter values are never moved after it. |
| 9 | V0 and V1 return to the pool after their grace; the old layer is freed by the bin (2.1.3) or the reclaim list, on the message thread. | |

Two publications are always made, also when the prefixes differ, so there is one code path; the new layer starts one block later than the old one stops,
which is inaudible (it starts at the beat of its first block, 2.1.5). A scene commit (C7, T8a) uses the same pieces: publish a view with `frozenMask` set for the
scene's slots, wait one grace period, apply every layer's tree, publish with the mask clear, so every layer of the scene switches in the same block and none is
muted. The boundary-exact timing of that switch (today's commit lands up to one poll interval after the boundary) is T8a's to design on top of this.

**What remains under `layersLock`.** In `TryLock` mode, everything it guards today, with today's hold (pointer moves, strip moves, microseconds; the lock
probe of E0.8 measures it). In `Published` mode it becomes the **writers' mutex only**: structural edits, `setLayerType`, `prepareToPlay` /
`releaseResources` (which may run on a host thread other than the message thread; they increment `prepareGeneration` under it, N1) take it; the audio
thread never does. Its hold is the writer's own section; no writer holds it across a grace wait, **with one exception**: a park under the writers' mutex
that finds its row full evicts entry 0 and waits one bounded grace for it (2.1.3, Depth and overflow). The bin's message-side operations take the separate
**bin mutex** in both modes (2.1.3, R2), nested inside `layersLock` when the caller already holds it (lock order `layoutWriteLock`, `layersLock`, `binMutex`;
`prepareToPlay` / `releaseResources` call `cutAll` and `service` nested inside the writers' mutex, or release it before taking `binMutex`; nobody takes
`layersLock` while holding `binMutex`); a free never needs `layersLock`, so it can never cause an audio try-lock miss.

**Coexistence with `ScopedSuspend` structural edits** (`addLayer`, `removeLayer`, `swapLayers`, `resetLayer`, unchanged in what they do and in what they sound
like). They keep `ScopedSuspend` (`layerEditInFlight` raised, processing suspended) because they move parameter values and edit layers in place
(`adoptDefaults`), and a block between those moves must play nothing, as today. In `Published` mode their exclusion against the audio thread becomes
**quiesce**: raise the flag (seq_cst), `epoch.waitFor(stamp, timeout)` (the block that may have started before the flag ends), then do every move; on a
timeout the edit lowers the flag and retries on the next pass, never moving strip state (N2). Until the last product has switched mode, every edit runs one
code path: quiesce (whose wait is **skipped in `TryLock` mode**, where the blocking `layersLock` already gives the exclusion, so the four products' edit timing is
unchanged), then `ScopedLock(layersLock)` around the moves, then publish a view, then the parameter phase, then lower the flag. Both mechanisms are then safe in
one binary, whichever mode a processor runs.

**Why it stays bit-identical for the four products.** They never call `setLayerType` and stay in `TryLock` mode through the programme, so their audio thread runs
today's lock protocol. The golden harness renders single-threaded (edits between blocks), where both modes give the same blocks. A later switch to `Published`
changes only which blocks are silent under contention with a non-edit holder of `layersLock` (fewer), which no golden contains.

**Invariants a test can assert.** (1) A block renders the old layer live, or as a tail, never both and never neither, across V0 to V1 to V2 (a recording fake layer
asserts the sequence per block, also when V1 and V2 are published inside one block). (2) A type change in one slot leaves every other slot's output bit-identical
to a run without it (the "does not mute" test). (3) No allocation or free on the audio thread during a publication (`AllocationProbe`). (4) A view, a layer or
a tail is freed only after its grace period (ASan two-thread stress: the message thread hammering `setLayerType` and structural edits while blocks run).
(5) In `Published` mode the audio thread never takes `layersLock` (lock probe). (6) No block runs while `layerEditInFlight` is up and strip state moves
(stress, ThreadSanitizer). (7) Every golden of the four products unchanged at each of these commits. (8) A `prepareToPlay` with a larger block size between
steps 2 and 8 leaves `fresh` prepared for the new size (generation test). (9) No operation publishes twice without a grace wait (assert); overflow views
counted, zero expected in the T1 stress test. (10) `waitFor` timeouts take each caller's fallback (fake epoch that never advances).

**Failure modes considered.** Pool reuse while a block still reads a view (ABA): solved by the grace period before reuse. The audio thread keeping a pointer
across blocks: forbidden by construction (stack object), caught by ASan in the stress test. Writer starvation: there is always a gap between blocks, and
`passed` also succeeds on any change of the counter, so a busy audio thread cannot starve it; a block that stalls (a debugger break, a starved
thread) is bounded by the timeouts. Offline render on the message thread: the writer is never inside a block, the stamp is even,
reclamation is immediate; a writer waiting on its own thread's block is a Debug assert. A host that calls `processBlock` from two threads at once: outside the
JUCE contract; the parity check asserts it in Debug and turns reclamation off in Release. A host that moves `processBlock` to another thread: correct
(read-modify-write counter). `prepareToPlay` on a host thread during an edit: the writers' mutex plus the prepare generation. Destruction:
`prepareForDestruction` empties the bin and the reclaim list before any product member goes.

**Rejected.** (a) Keeping the try-lock and holding `layersLock` briefly for a type change: a miss silences every layer for a block, which C1 forbids.
(b) `std::shared_ptr` views (or `std::atomic<std::shared_ptr>`): the last release can land on the audio thread (C6) and the atomic form may lock. (c) Hazard
pointers or general RCU: one reader makes a counter enough. (d) One atomic `Layer*` per slot: several slots (a scene, a reorder) could land in different blocks and
the count and the bin could not be coupled to the same block. (e) An audio-to-message acknowledgement per publication: the epoch is the same proof with no
per-publication message. (f) Running type changes through `ScopedSuspend`: it mutes everything.

#### 2.1.3 C2: the `RetireBin`

**Ruling: per-slot rows of `kRetireDepth` (4) entries, owned by `ProcessorBase`; each entry runs the four-state machine below; the message thread frees only
after it saw Done and the grace period passed.** Per-slot rows rather than one global ring because a tail renders into its own slot's strip (level, sends,
sidechain), a structural edit moves owned tails with their slot (mu-Clid's `layersReordered` does today; in-place entries are cut instead, R1), and mu-Clid's eviction is per slot. Depth 4 is mu-Clid's
`kMaxRetiredVoiceEngines`, so two retargets inside 25 ms never drop, free early or block. A global budget, `kMaxLiveRetirees`, defaults to `kMaxLayers *
kRetireDepth` (never binding in the four products); mu-Tronic sets it from the C18 measurements, and when it binds the oldest draining entry in the instance is
evicted.

```cpp
// mu-core/Sequencer/RetireTail.h  (namespace mu_core): what a layer type implements and includes (Layer.h includes it).
// Also holds RetireSnapshot, ParamBinding and applyLinearFade (2.1.4). No Plugin/ include.
struct RetirePolicy
{
    enum class Kind : uint8_t { None, Fade, Natural };
    Kind  kind       = Kind::None;   // None: not parked; freed after the grace period (the default; Arp and Track move to Fade at T1b)
    float fadeMs     = 25.0f;        // Fade: linear ramp (mu-Tant), converted at park time with the prepared rate
    float maxSeconds = 2.0f;         // hard lifetime, clamped to kRetireHardCapSeconds (reverb and comb tails)
};

struct RetireState                   // per entry: the message thread fills it before Parked; the audio thread owns it after
{
    int32_t samplesLeft;             // Fade: ramp frames left; gain at frame i = max(0, (samplesLeft - i) * gainStep)
    float   gainStep;                // 1 / rampFrames, rampFrames = max(1, (int) (sampleRate * (fadeMs / 1000.0))), mu-Tant's exact arithmetic
    int64_t samplesLived;            // frames rendered since activation
    int64_t maxSamples;              // hard lifetime in frames
    uint8_t exclusive;               // 1: `out` holds nothing yet this block (no live layer, no earlier tail): the tail may write
    uint8_t started;                 // onRetireStart has run
};

class RetireTail                     // a whole layer (type change, remove) or a product engine tail (mu-Clid hot-swap)
{
public:
    virtual ~RetireTail() = default;
    virtual RetirePolicy retirePolicy() const { return {}; }
    virtual void beginRetire() {}
    virtual void onRetireStart(const RenderContext&) {}
    virtual bool renderRetiring(const RenderContext&, juce::AudioBuffer<float>&, int, RetireState&) { return false; }
};

// mu-core/Plugin/RetireBin.h  (namespace mu_core): processor-side machinery, owned by ProcessorBase beside LayerBlock.h and
// AudioEpoch.h; no layer type includes it (ruled 2026-10-16, 2.1.7; E1 built it as Sequencer/RetireBin.h, to be moved).
class RetireBin
{
public:
    enum State : uint8_t { Empty = 0, Parked, Draining, Done };
    enum class CountPolicy : uint8_t { RenderPastCount, CutAtCount };   // R3: per product, set once at construction
    enum class ParkResult : uint8_t { Parked, NotParked, AlreadyActive, Dropped, Invalid };

    explicit RetireBin(const AudioEpoch&);   // the processor's epoch; service() needs no argument

    // Configuration, constructor or before processing.
    void setCountPolicy(CountPolicy);  void setGlobalBudget(int maxLive);  void setEvictTimeoutMs(int ms);   // default 50 ms

    // Message or host thread; every one takes binMutex (both modes, R2). binMutex is never taken by the audio thread and is
    // not layersLock. Lock order: layoutWriteLock, layersLock, binMutex (mu-Tant's layersMoved calls cutSlot under layersLock);
    // never the reverse. A RetireTail destructor (run under binMutex by service) takes no lock.
    void setPrepared(double sampleRate, int blockSize);   // prepareToPlay: the rate park converts with, and the declick scratch
    // audioExcluded: the caller holds layersLock in TryLock mode, so the audio thread cannot be in the bin (eviction without a wait).
    ParkResult park       (int slot, std::unique_ptr<RetireTail> owned, uint32_t activationSerial, bool audioExcluded);
    ParkResult parkInPlace(int slot, RetireTail& spare,                 uint32_t activationSerial, bool audioExcluded);
                                             // borrowed spare (mu-Tant); AlreadyActive if the row holds an active in-place entry (R1)
    void cutSlot (int slot);                 // every entry of the slot to Done: a remove, a regrow into an in-place slot, before a spare dies
    void cutAtOrAbove(int count);            // CutAtCount products: a count-reducing store cuts the rows it leaves
    void swapRows(int a, int b);             // structural edits, quiesced: owned tails follow their slot; in-place entries are CUT (R1)
    void removeRow(int index, int liveCount);   // mirrors unpublishLayerLocked: the removed row is cut, rows index+1 .. liveCount-1
                                                // move down one with their layers, the cut row lands at liveCount-1; spares above stay
    void cutAll  ();                         // prepareToPlay, releaseResources, prepareForDestruction
    int  service ();                         // free every Done entry and reclaim-list tail whose grace passed; never waits
    bool hasPendingWork() const noexcept;    // any thread: something waits for a service pass (the 100 Hz poll checks it)
    static bool isFreeingOnThisThread() noexcept;   // true while this thread runs a tail destructor inside the bin

    // Audio thread, inside a LayerBlock that succeeded (a failed one calls nothing here, R4).
    int  highestActiveSlot(uint32_t viewSerial) const noexcept;
    bool render(int slot, RenderContext ctx, juce::AudioBuffer<float>& slotOut, int n,
                uint32_t viewSerial, bool slotHasLive) noexcept;   // ctx.retiring = 1, empty midi, valid midiOut (R7), no peers;
                                                                   // skips in-place entries when slotHasLive (R1);
                                                                   // true: an entry went Done (the caller requests deferred work)

private:
    struct Entry
    {
        std::atomic<uint8_t> state { Empty };
        uint8_t      inPlace = 0;            // written while Empty, published by the Parked store
        uint32_t     activation = 0;         // ditto; compared wrap-safely: (int32_t) (viewSerial - activation) >= 0
        RetireTail*  tail = nullptr;         // ditto
        RetireState  rs {};                  // ditto
        uint64_t     doneStamp = 0;          // message thread: epoch stamp taken when it first saw Done (0 = not yet)
        uint64_t     parkSeq = 0;            // message thread: park order (global-budget eviction)
        std::unique_ptr<RetireTail> owned;   // message thread only; null for an in-place entry
    };
    std::array<std::array<Entry, mu_limits::kRetireDepth>, mu_limits::kMaxLayers> rows;
    std::mutex binMutex;                     // message and host threads only
    double     preparedRate = 0.0;           // guarded by binMutex
};
```

**Serialising the message side (R2).** Every message-side bin operation (`setPrepared`, `park`, `parkInPlace`, the cuts, `swapRows`, `removeRow`, `cutAll`,
`service`) takes `binMutex`, in **both** modes: the 100 Hz service, a deferred-work pass and a host thread running `prepareToPlay` / `releaseResources` (which
call `cutAll` then `service`) can otherwise overlap. It is not `layersLock`, so a free never makes the audio thread miss its try-lock; the audio thread never
takes it. `park` reads the prepared rate under it, so a park racing a prepare converts with one consistent rate. In each deferred-work pass `service()` runs
**before** the product's swap commit (mu-Clid's order today: free first, then install, `HotSwapStager.cpp:160`), so an install never finds a row full of Done
entries it could have reused.

**State machine (exact transitions).** Every operation on `state` is seq_cst (a store is also a release, a load also an acquire), for the grace proof of
2.1.2; the payload fields are plain and are published by the store that leaves Empty.

| From | To | Thread | Operation | When |
|---|---|---|---|---|
| Empty | Parked | message | write `tail`, `rs`, `activation`, `owned`; then `state.store(Parked)` | `park`, `parkInPlace` |
| Parked | Draining | audio | `compare_exchange(Parked, Draining)`; on success `onRetireStart(ctx)`, `rs.started = 1` | first rendered block whose `(int32_t) (view.serial - activation) >= 0` and, for an in-place entry, whose view does not have the slot live |
| Parked | Done | message | `compare_exchange(Parked, Done)` | cut before the audio thread started it |
| Draining | Done | audio | `compare_exchange(Draining, Done)`, then `requestDeferredWork()` | `renderRetiring` returned false; `Fade` ramp spent (`samplesLeft == 0`); `samplesLived >= maxSamples` |
| Draining | Done | message | `compare_exchange(Draining, Done)` | eviction, `cutSlot`, `cutAtOrAbove`, `swapRows` / `removeRow` (in-place entries), `cutAll` |
| Done | Empty | message | first sight: `doneStamp = epoch.stamp()`; when `epoch.passed(doneStamp)`: `owned.reset()`, `tail = nullptr`, `state.store(Empty)` | `service` |

The audio thread never touches an Empty or Done entry and never writes `tail` or `owned`; the message thread never writes `rs` or `tail` of a non-Empty entry. A
failed audio compare-exchange means the message thread cut the entry: the audio thread skips it. **The epoch rule proves the audio thread has left the object:**
every audio access to `tail` happens inside a block, after a load that read Parked or Draining in that block; the free waits for a grace period stamped after
Done was observed, so the block that may still be inside `renderRetiring` has ended, and every later block reads Done (seq_cst order: the Done write precedes the
stamp, the stamp precedes the next block's `enter()`, which precedes its load).

**Same-block hand-over (activation serial, R4).** An entry renders only in blocks whose view serial is at least its `activation` (wrap-safe compare), and it is
parked before that view is published. So the block that still sees the old view renders the old layer live and skips the entry; the first block that sees the
new view renders it as a tail; there is no block with both and none with neither (C1 invariant 1). The rule that makes this hold:

- **One packed word.** In `TryLock` mode the serial lives in a single `std::atomic<uint64_t> layout` in `ProcessorCore` (count in the low 32 bits, serial in
  the high 32; `static_assert` lock-free), and **every** count store goes through it as one store that also advances the serial: the structural edits,
  `publishLayerLocked` / `unpublishLayerLocked`, and `applyComposedState`, which today stores the count with no lock (`ProcessorBase.cpp:759`). `numLayers`
  stays as a mirror for existing readers and is written from the same value. `LayerBlock` reads count and serial in one load. In `Published` mode the serial is
  the view's. **The serial advances by a read-modify-write** (a compare-exchange loop on `layout`, never a load then a store), and `nextSerial()` plus the
  store that carries it run inside **one writer exclusion**, the **layout writer lock**: in `Published` mode the writers' mutex; in `TryLock` mode a separate
  message/host-thread mutex `layoutWriteLock` (not `layersLock`, which the audio thread try-locks and which must stay short while a preset is applied between
  the park and the store). Otherwise a host-thread `setStateInformation` running `applyComposedState` racing a message-thread edit can lose a serial or attach a
  park to the wrong store, which breaks the same-block hand-over (a block with both live and tail). **Risk until implemented**; tested by the
  `applyComposedState` race test run from a second thread. Lock order: `layoutWriteLock`, then `layersLock`, then `binMutex`.
- **A park outside `layersLock`** (mu-Tant's count-reducing full preset, `setLayerType` step 5) uses `nextSerial()`, the serial the following count store or
  publication will carry, under the layout writer lock held until that store; until the store lands the entry does not render and the old layer still
  renders live.
- **A park under `layersLock`** (mu-Clid's engine install) may use the current serial: the audio thread cannot start a block until the lock is released, and
  the first block after it renders the new engine live and the old one as a tail in the same slot, as today.
- **A failed `LayerBlock`** never touches the bin: no transition, no `onRetireStart`, no ramp step.
- **A cut between phase 0 and phase 4** (`mixCount` was taken with the entry active) renders silence: every slot below `mixCount` with no live layer has its
  scratch cleared in phase 1 (2.1.1).

**In-place entries (R1, mu-Tant).** An in-place entry borrows a spare that stays in `layers[slot]`, so its lifetime is tied to the slot, not to the bin:

1. A row holds **at most one active** in-place entry; `parkInPlace` on a slot that already has one is a no-op (the running fade continues).
2. An in-place entry **never renders in a block whose view has its slot live** (`render(..., slotHasLive)` skips it): the spare is then the live layer.
3. **Regrow order:** publish the count first, then `cutSlot` the entry. Cutting first leaves one block with neither live nor tail; with the count first, the
   block that sees the new count renders live and skips the entry (rule 2).
4. A spare may be destroyed only **after** `cutSlot`, done under `layersLock` (`TryLock` mode) or followed by a grace period (`Published` mode).
   `addLayer` destroys the spare it replaces (`ProcessorCore.h:129`, `publishLayerLocked` returns it), so mu-Tant's `layersMoved` must call `cutSlot` under the lock
   before that spare is released.
5. `swapRows` and `removeRow` **cut** in-place entries; they never move them (today mu-Tant zeroes the tails of moved slots, `PluginProcessor.cpp:692`). Owned
   entries move with their slot.

This also fixes a latent bug in today's mu-Tant (open point 3): a shrink then regrow leaves the stale tail armed, the next shrink skips the arm, and the old
snapshot resumes. The cut is accepted as the fix, with a signed golden update at E4.6 after pin P1.

**Tails past the live count (R3).** Per product, a `CountPolicy` set at construction:

| Policy | `mixCount` | On a count-reducing store | Product |
|---|---|---|---|
| `RenderPastCount` | `max(view.numLive, highestActiveSlot + 1)` | tails above the count keep rendering into their strips | mu-Tant (today's `renderCount` rule) |
| `CutAtCount` | `view.numLive` | `cutAtOrAbove(newCount)` under `binMutex`, after the count store | mu-Clid today renders retired engines only for slots below the active count (`MixerEngine.cpp:166-194`) |

mu-Clid's choice is made at E5 after pin P3 shows what today does when the count drops while tails ring and then regrows (whether the unrendered engines
resume); if `CutAtCount` differs from it, the difference is a signed golden update at E5.4d. mu-Toni and mu-On park nothing, so the policy is moot;
mu-Tronic's is chosen at T1 with its remove path. **Phase 4 renders bin rows only for `s < mixCount`** under either policy: under `CutAtCount` a Draining
entry above the count must not render in the window between the count store and `cutAtOrAbove`.

**Depth and overflow.** Placement is into the first Empty entry of the row. A full row evicts **entry 0** (mu-Clid's force-cut rule, kept for bit identity),
under `binMutex`, in one of two ways:

- **`TryLock` mode, park under `layersLock`** (mu-Clid's install): the audio thread cannot be inside the bin while the caller holds `layersLock`, so the
  entry is cut and emptied **at once, without a wait** (today's force-cut: bit-identical, and nothing is added to the lock hold). Its tail is **not deleted
  there**: it goes to the bin's reclaim list and the next `service()` deletes it, outside `layersLock` (as built in E1, accepted 2026-10-16: "only `service`
  frees" and "no free under `layersLock`" then hold without exception, and the entry is reusable at once, so the render is unchanged).
- **Otherwise** (a park outside `layersLock`, or under the writers' mutex in `Published` mode): the message thread cuts it, then `waitFor(stamp, timeout)` its
  grace (one block at most; `binMutex`, and in `Published` mode the writers' mutex, are held across this one bounded wait, the exception named in 2.1.2;
  `layersLock` in `TryLock` mode never is). On success it frees the entry and reuses it, on a timeout it **drops the incoming tail** (an owned tail goes to the reclaim list and is
freed by `service` after its own grace; an in-place spare is simply not parked) and the evicted entry stays Done for `service` (N2). The evicted tail stops
without a fade, as today; a fast declick on eviction is not adopted for the four products (it would change mu-Clid's spam-swap render) and is to be considered
for mu-Tronic's own types at T1.

**Fade and lifetime.** The bin's ramp is a service the tail applies itself, at the point in its chain where today's product applies it (mu-Tant fades the engine
*before* its insert): `mu_core::applyLinearFade(buf, n, rs)` uses exactly mu-Tant's expression, and the bin decrements `samplesLeft` by `n` after the call.
Lengths are in seconds and are converted with the rate prepared when the entry is parked; a bin never crosses a prepare (below), so nothing needs rescaling.
`maxSeconds` is clamped to `kRetireHardCapSeconds` (proposed 30 s); in an entry's last 10 ms of life the bin renders it into its own prepared scratch and applies a
10 ms linear declick before adding it, then sets Done. The 30 s cap is accepted provided pin P4 shows no golden tail exceeds it (mu-Tant 25 ms; mu-Clid's
amp release plus the 2 s drain, and `ampRelToEnd` samples); **recorded change:** a retired `ampRelToEnd` sample longer than 30 s, which plays to its end
today, now fades out at 30 s. **Seconds, with one kept exception (amendment b):** policies are in seconds, but mu-Clid's drain stays block-counted inside
`EngineTail` for bit identity; it is already block-size dependent today, which is recorded, not changed.

**Who frees, on which thread.** Only `service()` frees, on the message thread (the 100 Hz poll, `runDeferredWork`, `flushPendingAsyncUpdates` for headless
renders), under `binMutex` and outside `layersLock`; it never waits (an entry whose grace has not passed waits for the next pass). Because frees run under
`binMutex`, **a `RetireTail` destructor takes no lock** (and so cannot invert the order). C++ cannot detect "took a lock" generically, so the rule is
enforced per tail type: review, plus a destructor probe in the type's tests that asserts on `RetireBin::isFreeingOnThisThread()` around any lock the type's
members own (mu-Clid's `EngineTail` at E5.4d is the first; the E1 fake tail proves the probe). The audio thread's Done requests deferred work so the free is
prompt. **Flush:** `prepareToPlay` and `releaseResources` call `setPrepared` (prepare only), `cutAll()` then `service()` under `binMutex`, nested inside the
writers' mutex they already hold (or after releasing it; 2.1.2 lock order), so they serialise with a concurrent 100 Hz service on another thread (the host is not processing, the stamp is even, everything is freed at
once); `prepareForDestruction()` does the same before any product member is destroyed. mu-Clid's flush at prepare (today it re-prepares retired engines and
keeps them) is **accepted as a deliberate change** (open point 8). **Natural Done for mu-Tronic:** whether a `Natural` tail must also render one silent block
before Done is to be considered for mu-Tronic's types at T1; mu-Clid stops at Done (open point 4).

**Service lifetime.** The wavetable bank and the sample library outlive every layer and every tail: in the four products they are product members, and every
product destructor calls `prepareForDestruction()` first, which empties the bin; mu-Tronic's `LayerServices` (T1) are `ProcessorBase` members declared before
the bin, so they are destroyed after it. A tail reads a service only as its live layer does (until C5, under the same lock discipline). No tail, view or
layer is reference counted (C6).

**How each product maps onto it (bit identity).**

| Product | Today | In the bin |
|---|---|---|
| mu-Tant (E4.6) | count-reducing full preset: `retiring[v]` fades the slot's own engine and insert for 25 ms with a message-thread snapshot, into channel `v` past the live count | `RenderPastCount`; `parkInPlace(v, pattern, nextSerial())` outside `layersLock`, before the packed count store: the spare `Pattern` stays in `layers[v]` and keeps its engine; `Fade`, 25 ms; `renderRetiring` writes (`exclusive`) as `renderRetiringVoice` does; `mixCount` covers the slot. Regrow: count first, then `cutSlot` (R1 rule 3); `layersMoved` cuts under the lock before `addLayer` releases a spare (rule 4); swaps and removes cut (rule 5). |
| mu-Clid (E5.4d) | hot-swap retires the slot's `VoiceEngine` (not the `Rhythm`) into 4 per-slot entries; `markRetired` under suspend; drained by `isFullyDrained`; overflow force-cuts entry 0; retired engines render only below the active count | `park(r, std::make_unique<EngineTail>(std::move(oldEngine)), currentSerial)` under `layersLock` in the install, after `service()` in the same pass; `mu_clid::EngineTail : RetireTail` in the mu-Clid library; `Natural` (drain block-counted, amendment b); `markRetired` becomes `onRetireStart`; tail adds after the live engine; owned rows move in `layersReordered`; count policy chosen at E5 after P3; stop at Done accepted after P2 (open point 4). |
| mu-Toni, mu-On | a removed or replaced layer is reset or cut | `None`: not parked, freed after the grace period. |
| mu-Tronic (T1) | | type change and remove park the whole layer (`park(slot, std::move(layer))`). |

**Invariants a test can assert** (`RetireBinTests`, E1): the transition table above checked exhaustively by a deterministic two-actor scheduler (every
interleaving of park, cut, service against block start, render, finish); N-deep: four tails in one slot, a fifth evicts entry 0; two retargets inside 25 ms
both sound to their end; nothing is freed before Done plus grace (fake epoch); `cutAll` at prepare and release frees everything; the hard lifetime ends a tail
that never reports silence, with the declick; the ramp length at 44.1, 48, 96 and 192 kHz equals mu-Tant's `retireRampSamples()`; no allocation or free on the
audio thread; the activation serial gives the same-block hand-over, including across a serial wrap; destruction order (a fake service asserts no tail is
alive when it dies). **R1 to R4 cases:** a second `parkInPlace` on an active in-place row is a no-op; an in-place entry is skipped in every block whose view has
its slot live, and on regrow (count first, then cut) no block has neither; a spare is never destroyed before its `cutSlot` (destructor probe, `addLayer` over a
ringing spare); `swapRows` / `removeRow` cut in-place entries and move owned ones; `service`, a park and `cutAll` + `service` from a second "host" thread never
overlap (`binMutex`, two-thread stress); `service` runs before the install in a deferred-work pass; both count policies (`RenderPastCount` renders above the count,
`CutAtCount` cuts at the store); a failed `LayerBlock` changes no entry; an entry cut after phase 0 renders silence in its slot; every count store goes through the
packed word and no serial is lost or misattached (the `applyComposedState` race test, run from a second thread against message-thread edits); under
`CutAtCount` a Draining entry above a just-stored count does not render before `cutAtOrAbove`; a full-row eviction under `layersLock` in `TryLock` mode frees
without a wait (the tail is deleted by the next `service`); a tail destructor runs only with `isFreeingOnThisThread()` true (the destructor probe).

**Failure modes considered.** Free while the audio thread is inside `renderRetiring` (the grace period); a tail started after it was cut (the audio
compare-exchange); a tail rendered both live and as a tail (activation serial; for in-place entries also the `slotHasLive` skip); a block with neither (count
before cut on regrow; cleared scratch after a late cut); a spare destroyed under a ringing in-place entry (cut first, R1 rule 4); a stale in-place tail
resuming after shrink, regrow, shrink (one active entry, cut on regrow); two message-side operations racing (`binMutex`); a count stored without the serial
(the packed word); eviction of an entry the audio thread is reading (cut, grace, then reuse; drop the incoming tail on timeout); a prepare changing the rate
under a ramp (flush, and the rate read under `binMutex`); a tail outliving the bank (destruction order); an endless comb or reverb tail (hard lifetime); 16 slots of tails
(global budget, C18 numbers); `-0.0` versus `+0.0` in stems (tails add straight into the slot scratch, as today; the private scratch is used only in the declick window).

**Rejected.** A global ring of 16 to 32 (slot numbers would need renumbering on every edit, and mu-Clid's per-slot eviction could not be kept); a fixed gain
ramp applied by the bin after the tail (it would move mu-Tant's fade after its insert and change the sound); fade lengths in blocks (block-size dependent);
owning only whole layers (mu-Clid retires an engine, not its `Rhythm`; mu-Tant keeps the spare's engine); freeing on the audio thread or under `layersLock`.

#### 2.1.4 C3: frozen retire snapshots

**Ruling.** A tail reads only (a) its own object's DSP state, (b) its declared snapshot, (c) the transport fields of the `RenderContext` it is given, and
(d) a service through the same discipline as its live layer. It never reads: any APVTS atomic (bound or by id), its slot's or its own `ModulationMatrix`,
`ControlSequence`s, `modLock`- or `voiceParamsLock`-guarded data, a peer (`ctx.peer()` returns null), the MIDI input (`ctx.midi` is empty), the `LayerView`, another layer, or
`ProcessorBase`. It may **write** MIDI: the tail context's `midiOut` is a valid sink drawn from the note-off reserve (2.1.5, R7), which is where
`onRetireStart` emits owed note-offs. Same-prefix types are the case that makes this necessary: a retired Kick and a new Bass in one slot share `t{N}_`.

**`beginRetire` contract (message thread).** The host calls it **before** any parameter of the slot changes and **before** the layer leaves the view (step 4 of
2.1.2; mu-Tant's `applyFullPresetTree` already arms its tail before `applyComposedState`). It is the last time the layer may read its bound parameters. It copies
into the layer's snapshot everything `renderRetiring` needs that another thread may change afterwards: base parameter values (mu-Tant: `readConfig(v)`, insert
algorithm and its four parameters, exactly today's `RetiringVoice` fields), sample paths, mode switches. It must not allocate if it can be avoided (the snapshot is a
member sized at construction). Its peer on the audio thread, **`onRetireStart(ctx)`**, runs once at the first tail block: it freezes audio-owned state
(mu-Clid's `markRetired`: envelope note-offs and the drain budget), copies last-resolved values when the type's tail wants modulated values rather than base
ones, and emits owed MIDI note-offs into `ctx.midiOut` so a tail never holds a MIDI note. From then on the layer runs on what it froze.

**How a layer declares its snapshot so the test can prove it.**

```cpp
// mu-core/Sequencer/RetireTail.h
template <class Snapshot>
struct RetireSnapshot                    // a layer type with a tail holds one of these as a member
{
    static_assert(std::is_trivially_copyable_v<Snapshot>, "a retire snapshot is plain data, with no pointer into the APVTS");
    Snapshot value {};
};

// e.g. mu-Tant:  struct PatternRetire { VoiceConfig config; int insAlgo; std::array<float, 4> insP; };
//                RetireSnapshot<PatternRetire> retireSnap;   // written only by beginRetire, read only by renderRetiring
```

The proof is behavioural and generic, so it needs nothing from the type but its registration: **`mu_contract_test::checkRetireIsolation(info)`**
(`mu-core/Tests/RetireIsolation.h`, the test-helper namespace rule of design-naming §3; E1 proved the twin test on its fake type inside `RetireBinTests.cpp`
with a negative control, and the generic helper is written, from that test, with the first product type that has a tail, mu-Tant's `Pattern` at E4.6, then
run for every registered type at T1) builds two
identical instances of the type in slot 0 of a test processor, renders the same blocks, calls `beginRetire` on both and parks both, then on **one** of them
scrambles **every APVTS parameter of the processor** (legal random values; not only the slot's prefix, because a snapshot may hold globals: mu-Tant's includes
the global root and scale through `globalPtrs`), replaces its modulation matrix and control sequences, and feeds garbage peers; the two tails must be
bit-identical for their whole life, compared on the **tail's own output before the strip** (N6), so mixer and FX parameters scrambled on one side cannot mask
or fake a difference. A second case puts a same-prefix type in the slot while the first rings (the E1 fake pair; mu-On Kick to Bass
in E3). In Debug and test builds the `bindParams` cache is a `mu_core::ParamBinding` whose `load()` asserts that the read is **not made from inside a tail call**
(`onRetireStart` or `renderRetiring` of its own layer; the bin sets the flag around both), so an accidental read is a jassert in the first test that hits it,
not a silent mix of two layers. (As built, accepted 2026-10-16: "has `onRetireStart` run" would stay true for a mu-Tant in-place spare that becomes the live
layer again on regrow, and would then fire on legal live reads.)

**Invariants.** The isolation twin test per registered type; the same-prefix case; a tail emits every owed note-off in its first block and none later; no
allocation in `onRetireStart` or `renderRetiring`.

**Failure modes considered.** A read through a helper outside the binding (`getRawParameterValue`, the shared `resolveLane`): the twin test. A snapshot that holds a
pointer into the APVTS (trivially copyable but live): the twin test. A parameter write between steps 4 and 5 (host automation in a plugin, for
mu-Tant's and mu-Clid's tails): the tail keeps the value frozen at step 4, which is the rule, exactly as mu-Tant's `RetiringVoice` does today. A UI still writing to the old layer: C16 teardown in step 3.

**Rejected.** Freezing by copying the whole layer (allocates, and copies engines); letting tails read the APVTS with a "retiring" flag that the host promises not to
change (unenforceable, and false for same-prefix types); a snapshot taken only on the audio thread (mu-Tant's tail uses unmodulated base values read before the new
preset lands, which the audio thread cannot see any more once the new values are in).

#### 2.1.5 C4: the time model and `RenderContext`

**Ruling: one plain struct, mirrored by a C struct, built once per block by the base and copied per layer with `slot` set; every transport-anchored position a
layer computes is a function of the absolute beat in it.**

```cpp
// mu-core/Sequencer/RenderContext.h  (namespace mu_core)
// Standard layout, trivially copyable, fixed-width fields, no JUCE or std type, no reference, no callback (A14 constraints).
inline constexpr uint32_t kRenderContextVersion = 1;    // fields are only ever appended

struct MidiEvent                 // 8 bytes: one short MIDI message (sysex and longer messages are not carried)
{
    int32_t frame;               // offset from the start of the host block
    uint8_t size;                // 1..3
    uint8_t bytes[3];
};

struct MidiView                  // the block's MIDI after the control router's claims: decoded once per block, no copy per layer
{
    const MidiEvent* events;     // time-ordered, valid for this block only
    int32_t  count;
    int32_t  firstFrame;         // this call covers host frames [firstFrame, firstFrame + blockSize)
    uint16_t channelMask;        // bit c = channel c+1 is this layer's (0xFFFF = omni; per-layer MIDI-in arrives at T8a)
    uint16_t dropped;            // events the decoder had no room for; 0 expected, asserted by tests
    uint16_t carried;            // the first `carried` events are earlier host blocks' channel messages, at frame 0 (2.1.8); 0 unless the processor carries them
    uint16_t reserved;           // explicit: no implicit padding (24 bytes)
};

struct MidiSink                  // the layer's MIDI output (mu-Clid MIDI mode, E5; tails: owed note-offs); events == nullptr: none
{
    MidiEvent* events;
    int32_t    capacity;
    int32_t    reserved;         // explicit: no implicit padding before `count`
    int32_t*   count;            // appended by the layer, frames relative to the host block
};

struct PeerTap                   // a priority-0 layer's output this block: post-insert, pre-strip (C10)
{
    const float* channel[2];     // channel[1] == channel[0] when mono
    int32_t      valid;          // 0: Empty, priority-1, not rendered: read as silence
    int32_t      reserved;
};

struct RenderContext
{
    uint32_t structSize;         // sizeof (RenderContext)
    uint32_t version;            // kRenderContextVersion
    double   sampleRate;
    int32_t  blockSize;          // frames in THIS call, 1..preparedBlockSize (host blocks of 0 are not rendered; longer ones are chunked)
    int32_t  preparedBlockSize;
    int32_t  frameOffset;        // where this call starts inside the host block (0 unless chunked)
    int32_t  slot;
    double   bpm;
    double   startBeat;          // song beat at this call's first frame; never wrapped by the contract; may be NEGATIVE (pre-roll, count-in)
    double   beatsPerSample;     // the resolver's (under MIDI clock: span / frames); > 0 also while stopped
    double   callBeats;          // beats this call spans; 0 while stopped
    double   beatWrap;           // 0 = unbounded; > 0 = the HOST bounds its beat space to [0, beatWrap)
    uint8_t  playing;
    uint8_t  justStarted;        // playing, and not playing in the last RENDERED block
    uint8_t  justStopped;        // not playing, and playing in the last rendered block
    uint8_t  transportJumped;    // playing in both, and a host-block discontinuity since the last rendered block (sticky jumpPending)
    uint8_t  retiring;           // 1 in renderRetiring (midi empty, midiOut valid from the note-off reserve, no peers)
    uint8_t  frozen;             // 1: render from the last resolved values, read no APVTS or matrix (C7 provision, C12 rule)
    uint8_t  stoppedUnrendered;  // 1 with transportJumped: the transport stopped in a host block that rendered nothing between the last rendered block and this one (below)
    uint8_t  reserved;
    MidiView midi;
    MidiSink midiOut;
    const PeerTap* peers;        // kMaxLayers entries in the priority-1 pass, else nullptr
    int32_t  numPeers;
    int32_t  reserved2;

    // Helpers, not part of the C mirror (non-virtual members do not change the layout).
    const PeerTap* peer(int s) const noexcept { return peers != nullptr && s >= 0 && s < numPeers && peers[s].valid ? &peers[s] : nullptr; }
    double endBeat() const noexcept { return startBeat + callBeats; }
};
static_assert(std::is_standard_layout_v<RenderContext> && std::is_trivially_copyable_v<RenderContext>);
// forEachMidi(view, fn(const MidiEvent&, int frameInCall)) applies channelMask and the call's frame range (inline, not a callback).
// The base asserts ctx.blockSize == n and ctx.frameOffset == ctx.midi.firstFrame on every call (the view stays self-contained for forEachMidi).

// mu-core/Sequencer/BeatWrap.h: the one floor-wrap (R6), moved out of resolveTransport's lambda (mu-core/Plugin/TransportResolver.h:126-130)
inline double wrapBeat(double b, double wrap) noexcept
{
    if (wrap <= 0.0) return b;                       // unbounded
    b = std::fmod(b, wrap);
    if (b < 0.0) b += wrap;                          // negative positions (pre-roll) wrap upward
    return b < wrap ? b : std::nextafter(wrap, 0.0); // -1e-17 + 64 rounds to exactly 64.0: keep the result below wrap
}
```

On non-negative input `wrapBeat` is exactly today's lambda (`fmod` is exact), so every golden is unchanged; on a tiny negative input it returns the largest
double below `wrap` where today's lambda returns `wrap` itself (an out-of-range gate index in an unbounded host). The resolver, every layer that wraps
(`Pattern::render`) use it; no layer calls bare `fmod` on a beat. The jump check does **not** use it (it reduces a delta, below).

**Edges (computed by the base; R5).** Two pieces of audio-thread state, both reset by `prepareToPlay`, with different update rules:

- **Host continuity, every host block** (rendered, missed try-lock, `layerEditInFlight`, 0 frames), in step (1) of `renderAndMix` before `LayerBlock`:
  `host = { bool valid; bool playing; double endBeat; double beatsPerSecond; double blockSeconds; }`. If the previous host block and this one are both playing,
  `d = startBeat - host.endBeat`, reduced to the nearest multiple of `beatWrap` when the host bounds its beat space,
  `if (beatWrap > 0) d -= beatWrap * std::round(d / beatWrap);` (so mu-Tant's own 64-beat wrap is not a jump; **not** `wrapBeat`, which maps into
  `[0, wrap)` and would turn a delta of -0.0001 into 63.9999); `|d| > tol` sets the sticky **`jumpPending`**. **The stop rule, exactly:** any non-rendered
  host block with `!playing` while the rendered latch `last.playing` is true sets `jumpPending` (a stop hidden in unrendered blocks; the layers would otherwise
  see neither edge if play resumes). Then `host` is overwritten with this block's values.
- **Play/stop latch, rendered blocks only** (phase 5, as now): `last = { bool playing; }`. `justStarted = playing && !last.playing`;
  `justStopped = !playing && last.playing`; `transportJumped = playing && last.playing && jumpPending`, and phase 5 clears `jumpPending`.

**`stoppedUnrendered` (added at E3, ruled 2026-10-16).** A layer whose stop edge does real work (mu-On's `Track` silences a sustaining Bass, which has no note-off)
cannot see a stop that happened only in unrendered blocks: `justStopped` needs a rendered stopped block, and a jump says only that the position moved. The base
keeps a third sticky bit, `stopPending`, set exactly where the stop rule above sets `jumpPending` (a non-rendered host block, 0 frames included, with `!playing`
while the rendered latch is playing) and cleared with it in phase 5. In the first chunk of a block that carries `transportJumped`, `stoppedUnrendered` is 1 when
`stopPending` is set. It is a **qualifier of `transportJumped`**: never set without it, 0 when `justStopped` fires (the next rendered block saw the stop itself) and
0 for a locate that hid no stop. A layer that stops its sound on a stop reacts to `justStopped || stoppedUnrendered`; a layer that only re-aligns on a
discontinuity ignores the field. It uses a former `reserved` byte, so the struct keeps its size and `kRenderContextVersion` stays 1 (the `MidiView::carried`
precedent, 2.1.8); the `offsetof` freeze test pins it at byte 78.

What the three stop sequences give: a stop in an unrendered block, then a restart on the next rendered block: `transportJumped` once (and `stoppedUnrendered`); a stop in an unrendered
block, then the next rendered block stopped: `justStopped` fires and phase 5 clears the flag (no jump later, no `stoppedUnrendered`); a pause and resume at the same position inside
unrendered blocks: `transportJumped` once (the layers saw no stop, so the gap reaches them as a jump; `stoppedUnrendered` is 1, since the pause was a stop).

**Why (the defect R5 fixes):** with the end beat latched only on rendered blocks, the block after an unrendered one (every structural edit in mu-Tant, mu-On and
mu-Clid raises `layerEditInFlight`, and any try-lock miss) saw a delta of one block of beats and fired `transportJumped` during continuous play. Comparing
against the previous **host** block removes the false jump; making the result sticky still delivers a real locate that happened inside an unrendered block.
Pin P6 records today's note state across a structural edit mid-play ("no note release"). A locate while stopped is reported by `justStarted` alone. The play/stop
latch matches mu-On's `wasPlaying` and mu-Toni's `wasRunning`, which are written only inside the lock.

**Tolerance (open point 1, ruled).** E1 codes `tol = 1/256 + |beatsPerSecond_now - beatsPerSecond_last| * lastBlockSeconds` (beats; the second term
absorbs a tempo change between blocks under automation). It needs no measurement before E1; M1 (block-to-block host position deltas under fast tempo
automation in Bitwig and Reaper, and under MIDI clock with the sync-offset ramp) is measured before the first consumer, at T1, and may only widen it.

`onTransportStopped` and `onTransportJump` are called in phase 0 of the block that carries the flag, before any render; the same flags are in the context so a
layer whose order needs it can act inside `render` instead (a type uses one or the other, not both). Hooks are not called on tails; tails see the flags.

**State a layer may keep (the rule behind "no free-running phase").**

| Kind | Rule | Examples |
|---|---|---|
| Transport-anchored position | A function of `startBeat` (and `beatsPerSample` inside the call) only. A cursor holding the **last evaluated beat or step** is allowed, to avoid re-firing and to detect a discontinuity with the layer's own tolerance. Never `beat += frames * tempo` carried across blocks. | step onsets, gate and envelope position in a pattern, modulator playheads |
| Event-anchored duration | May count frames from its own event (a note-on, a key press), per frame, so it is block-size invariant. It belongs to the note, not to the song position. | note gate length, MIDI-trigger step clock, envelopes, owed note-off countdown, retire ramp |
| DSP state | Unrestricted. | oscillator phase, filter and delay memory |

**Pending note-offs.** A layer that owes note-offs (gated notes, MIDI out) must: release them on `justStopped`; on `transportJumped` release them or re-anchor
them (its choice, documented per type); emit them on `onRetireStart`; and never let them depend on the block size. They may be held as an event-anchored frame
countdown (mu-Toni's `gateCounter`, mu-Clid's `noteOffCountdown`) or as an end beat. **This amends C4's "beat-domain pending note-offs":** converting today's
frame countdowns to beats changes the frame a note-off lands on by rounding, which breaks D7 for mu-Toni and mu-Clid; a frame countdown that starts at the
note-on is not a transport phase, and the three release rules give the jump and stop behaviour C4 asks for (accepted, amendment a). New layer types (mu-Tronic)
may use either form, **with one caveat:** mu-Clid's `MidiOutputEngine` counts its countdown from the block start, not from the note-on frame, so no mu-Tronic
type may reuse it for triggers inside a block; a new type counts from the event's own frame.

**MIDI from a tail (R7).** The tail context has an **empty** `midi` view and a **valid** `midiOut` sink drawn from the note-off reserve (the last 256 entries of
the block's event storage), so `onRetireStart` can emit its owed note-offs into `ctx.midiOut` even when live layers filled the rest. **Merging sinks into the
host `MidiBuffer` never grows it on the audio thread:** the merge stays within the room the host buffer already has (today's rule, pinned by mu-Clid's
`ProcessorSeamTests`, "the host owns this buffer's room; the processor must not need more"), note-offs first; the AllocationProbe covers it. The merge is the
product's call, `mergeLayerMidiOut(midi, byteBudget)` (2.1.1); how mu-Clid derives the budget is reviewed with E5.4c.

**Tempo changes between blocks stay phase-continuous** by construction: the resolver makes each block start where the last one ended (internal and MIDI
clock; a host supplies its own position), positions are functions of the beat, so a new tempo changes only the next block's `beatsPerSample`; event-anchored
durations started before the change keep their frame length (today's behaviour). Tempo is constant inside a call (as today; host ramps inside a block are not
modelled).

**How each product's clock maps onto it** (and what changes at its stage):

| Product | Clock today | Kind | In the contract |
|---|---|---|---|
| mu-Toni loop mode (`ArpVoiceRunner::renderOnGrid`) | `nextStep = ceil(startBeat / stepBeats)`, cursor plus `expectedBeat`, re-find when off by more than half a step | transport-anchored cursor | Unchanged code; reads `ctx.startBeat`, `ctx.beatsPerSample`, `ctx.bpm`, `ctx.playing`. Keeps its own half-step tolerance. **No clock rework in E2** (accepted, amendment c); only `ArpContext`'s source changes. |
| mu-Toni MIDI-trigger mode | `stepCounter` frames from the key press; `samplesPerStep` from `bpm` per block | event-anchored | Unchanged. `noteOnEdge`, `anyNoteHeld` and the root override come from the layer's own held-note scan of `ctx.midi` (E2.3). |
| mu-Toni gate | `gateCounter` frames from the note-on; stop: `run == false` notes off inside `render` | event-anchored | Unchanged; stop handled inside `render` (flags form). `wasRunning` stays in the runner (it includes the MIDI-trigger gate). |
| mu-On (`GrooveSequencer`) | one global `nextGlobalStep` cursor, resync beyond plus or minus one step; frame offset `round((t - start) / bps)`; stop edge `grooveVoices.reset()`; Rumble envelope at the beat | transport-anchored cursor | One cursor per track row with the same arithmetic (E3.4); stop edge as `onTransportStopped`; Rumble envelope at `ctx.startBeat`; tempo-synced taps from `ctx.bpm`. Order of `reset` against the parameter refresh: pin P5 first; if the hook order differs, mu-On uses the flags form inside `render` (open point 9). |
| mu-Tant (`gateAt(beat)`, `resolveLane` at the beat) | absolute beat, wrapped to 64 beats by the resolver | transport-anchored, no cursor | The mu-Tant standalone keeps `wrapBeats = 64` as its host policy (`beatWrap = 64`), because an unwrapped counter reaches a different double after 64 beats of internal play; `Pattern::render` applies `wrapBeat(startBeat, 64)` itself (never bare `fmod`, which is negative on a pre-roll beat), which is exact on already-wrapped input, so mu-Tant stays bit-identical and the layer is correct in an unbounded host (mu-Tronic), including at negative beats. |
| mu-Clid (`RhythmSequencer`) | step from the beat (`stepAt`), `lastEffectiveStep` wrap detector, dip-hold `resolveBlockBeat`, block-granular triggers at frame 0; plugin Note mode runs its own beat counter | transport-anchored; the dip-hold and Note-mode counter are host transport policy | Dip-hold and the Note-mode counter stay host-side (they produce `startBeat`). The context carries **no** product trigger field (accepted, amendment d); `SampleRhythm` runs its own cursor, which must reproduce `RhythmSequencer`'s wrap and swap-start arithmetic bit for bit (E5.5, own `dsp-controller` gate; open point 6). MIDI note-off countdown: event-anchored, counted from the block start (caveat above). |

**What the three E1 constraints imply.** (1) *C-struct mirror:* fixed-width scalars, `uint8_t` flags, no reference, no JUCE or `std` type, `structSize` and
`version` at the head, fields only appended; the MIDI view is a POD array the base decodes once per block into storage sized at construction
(`kMaxBlockMidiEvents`, proposed 2048, the last 256 reserved for note-offs so an overflow can never strand a note); peers and MIDI out are data, not calls. A
static-assert test freezes `sizeof` and every `offsetof` (the ABI snapshot that a later module would need), without writing a C header now. (2) *Text state:*
nothing in the context is state; a layer's state is its APVTS rows plus `writeExtras` / `applyExtras` (and `preloadStaged` takes the same tree); no saved state
holds a pointer or an index into the host. (3) *No host callback:* nothing reachable from `render` or `renderRetiring` calls `ProcessorBase` or a function
pointer; a layer's only outputs are its buffer, `ctx.midiOut`, its own atomics (UI snapshots, counters) and its return value. Deferred-work requests are the
base's (the bin's Done), never a layer's.

**Invariants a test can assert.** Layout: standard layout, trivially copyable, frozen size and offsets. Block-size invariance: a beat-driven fake layer fires
its events on the same absolute frames under a fixed 512 schedule and the ragged schedule (1, 7, 63, ..., oversize). Tempo ramp: event beats exact to 1e-9,
no double fire, no missed step. `startBeat` 1e6: positions within one frame. Edges: `justStarted` / `justStopped` once per edge; latched across a missed
lock and a 0-frame block; `onTransportJump` exactly once per host locate, loop-back and clock snap, never on a contiguous block, **never on the block after a
missed lock or a `layerEditInFlight` block during continuous play**, still once for a locate that happened inside an unrendered block, and never on mu-Tant's
64-beat wrap. Continuity unit test at `beatWrap = 64`: deltas of +-1e-4, +-1e-4 +- 64 and +-64 all give no jump. The three stop sequences above, each
asserting its stated result. Negative beats: a fake layer started at `startBeat` -4 (pre-roll) fires on the same frames as its unwrapped reference and, with `beatWrap = 64`,
never sees a wrapped beat outside `[0, 64)` (E1). MIDI: `dropped == 0` in all goldens; note-offs survive an overflow; a tail's note-offs reach the host buffer
when live layers filled the non-reserved storage; the merge allocates nothing. Retire contexts carry no MIDI input and no peers, and a valid sink. Layout: no
implicit padding (`offsetof` test, the reserved fields).

**Failure modes considered.** A layer accumulating beats across blocks (drifts under tempo change, breaks on locate: the block-size and ramp tests); edges lost on a
missed lock (latch, sticky `jumpPending`); a false jump after an unrendered block (host-block continuity, R5); a jump tolerance too tight for host jitter under
tempo automation (false jumps) or too loose (missed small locates; layers keep their own detectors, so the flag is a hint): the tempo term and M1; the wrap of a
bounded host read as a jump (the delta reduced to the nearest multiple of `beatWrap` with `std::round`, not with `wrapBeat`); a negative pre-roll beat indexing out of range (`wrapBeat`, R6); a tail with an owed note and
no sink (the reserve, R7); MIDI overflow stranding notes (reserve); an oversize block overrunning prepared buffers (chunking).

**Rejected.** Passing `const juce::MidiBuffer&` (a JUCE type, against A14; a per-layer filtered copy would allocate); a callback for `peer(slot)` (a host
callback); a per-layer beat wrap inside the resolver (mu-Toni's dotted rates do not divide 64); unwrapping mu-Tant's transport now (breaks D7 after 64 beats);
beat-domain note-offs for the existing products (above); a product trigger mask in the context (product vocabulary in core, and a field that mu-Tronic could
never fill).

#### 2.1.6 Open points: the `dsp-controller`'s rulings (round 1, 2026-10-15)

None of the twelve blocks E1. The pins (P) and measurements (M) named here are listed in [extraction-plan.md](extraction-plan.md) step E0.6.

1. **Jump tolerance:** not needed before E1. E1 codes `tol = 1/256 + |beatsPerSecond_now - beatsPerSecond_last| * lastBlockSeconds` (2.1.5); M1 measures
   block-to-block host position deltas under fast tempo automation (Bitwig, Reaper) and under MIDI clock with the sync-offset ramp before the first consumer (T1).
2. **Single reader:** E1 codes N4 (read-modify-write parity, sticky reclaim-off, 2.1.2). Before E4, M2: Debug pluginval for VST3, CLAP and Standalone with the
   assert, plus an offline bounce while playing in Reaper and Bitwig.
3. **mu-Tant shrink then regrow:** today has a latent bug (the stale tail stays armed; on the next shrink the arm is skipped, so the old snapshot resumes). The
   bin's cut (2.1.3, R1) is accepted as the fix, with a signed golden update at E4.6 after pin P1. **As built (2.1.10 point 6):** E4 kept the stale tail behind `setRegrowCutsInPlaceTails(false)`; the golden update and the removal of the opt-out are a later signed commit.
4. **mu-Clid output after drain:** Karplus inserts can ring for minutes. "Stop at Done" is accepted, with a signed golden update at E5.4d if pin P2 shows
   non-zero samples after `isFullyDrained()`.
5. **C11 placement:** accepted with N7 (each product keeps its own top-of-`processBlock` `ScopedNoDenormals`, 2.1.1).
6. **mu-Clid trigger source:** accepted as amendment (d): no trigger field; `SampleRhythm` runs its own cursor. E5.5 is re-opened with its own `dsp-controller`
   gate: the per-layer cursor must reproduce `RhythmSequencer`'s wrap and swap-start arithmetic bit for bit.
7. **Hard cap 30 s:** accepted if pin P4 shows no golden tail exceeds it; recorded that a retired `ampRelToEnd` sample longer than 30 s now fades at 30 s (2.1.3).
8. **mu-Clid flush at prepare:** accepted as a deliberate change.
9. **mu-On stop order:** pin first (P5: the stop edge with `applyParams` before `reset`, and a 0-frame stop block); if the hook order differs, mu-On uses the
   flags form inside `render`.
10. **Oversize blocks:** today's truncation is a defect. The oversize segment is re-recorded in a goldens-only commit the `dsp-controller` signs, then kept in the
    tolerance-0 gate; the chunking test is the main evidence (pin P7 records today's segment per product first).
11. **mu-Tant bank-lock miss:** accepted (only contention changes: a miss silences one layer, not all); a tail takes the same bank try-lock as its live layer.
12. **View pool:** accepted with N3 (pool of 4, the publish-twice assert, an overflow view instead of a wait, counted in the T1 stress test).

**Amendments accepted with caveats:** (a) frame countdowns for note-offs, but no mu-Tronic type reuses `MidiOutputEngine` for in-block triggers (2.1.5); (b)
`retirePolicy()` in seconds, with mu-Clid's drain kept block-counted inside its tail (2.1.3); (c) no mu-Toni clock rework; (d) no trigger field, E5.5 re-opened.

#### 2.1.7 E1 as built: the architecture-steward's rulings (2026-10-16)

E1 (branch `extraction`, six commits) is all `mu-core`, additive, and touches no product source; `check-core-boundary.ps1` and
`check-layer-boundary.ps1` pass on it. Files and names follow 2.1 and design-naming §3 (`Plugin/AudioEpoch.h`, `Plugin/LayerBlock.h`,
`Plugin/ProcessorBase_Render.cpp`, `Sequencer/RenderContext.h`, `Sequencer/BeatWrap.h`, `Sequencer/LayerTypeInfo.h`, `Sequencer/RetireTail.h`; the tests and
`LayerEquivalence.h` in `mu-core/Tests/`, as tests.md section 3 places it). `LayerTypeId` naming the family's type names in `mu-core` is the 2.5 ruling, not a
boundary breach. Where the build differs from the text, the subsections above now show the accepted shape; the verdicts:

| # | As built | Verdict |
|---|---|---|
| 1 | MIDI out not merged by `renderAndMix`; the product calls `mergeLayerMidiOut(midi, byteBudget)` | **Accept** (2.1.1): only the product knows the host buffer's room |
| 2 | Eviction under `layersLock` empties the entry at once and defers the delete to `service()` | **Accept** (2.1.3): no free under `layersLock` without exception, render unchanged |
| 3 | `removeRow(index, liveCount)` | **Accept** (2.1.3): it must mirror `unpublishLayerLocked`, which moves only the live rows |
| 4 | `ParamBinding` asserts "not inside a tail call" | **Accept** (2.1.4): survives an in-place spare going live again |
| 5 | No generic lock assert in tail destructors; `RetireBin::isFreeingOnThisThread()` for per-type probes | **Accept** (2.1.3): not detectable generically; per-type destructor probe |
| 6 | No `checkRetireIsolation` helper; the twin test runs on the fake type | **Accept as a deferral** (2.1.4): the generic helper is owed at E4.6, not dropped |
| 7 | `RetireTail` in its own `Sequencer/RetireTail.h` | **Accept, and change the code:** `RetireBin.h` moves to `mu-core/Plugin/` (below) |
| 8 | Extra parameters: `setPrepared(rate, blockSize)`, `flagLayerSwapBoundaries(t, fullPresetLoopBeats)`, `resolveBlockTransport`, `park(..., audioExcluded)` returning `ParkResult`, `setGlobalBudget`, `setEvictTimeoutMs`, `hasPendingWork`, `render` returning bool | **Accept** (2.1.1, 2.1.3); `resolveBlockTransport` was already in the 2.1.1 sketch. **Change the code** for one: `renderAndMix`'s separate `beatWrap` argument goes; it travels in `BlockTransport` (2.1.1) |
| 9 | `RetireBin` and the driver state heap-held in `ProcessorBase` (`unique_ptr` + reference member) | **Accept** (2.1.1): allocated once at construction, never reseated |
| 10 | `mu-core-tests-16`: the same source list through one CMake function with `MU_MAX_LAYERS=16`; two fixtures stop assuming 8 slots | **Accept** (3.4): one list, so the two cannot drift |
| 11 | `MixerEngine::ChannelTap` added although E1 "does not change `MixerEngine`" | **Accept**: it is the test-only channel tap approved at E0.9 |

**Code changes owed on the E1 item (backlog #1609) before E2 starts**, both now done: (a) `RetireBin.h` lives in `mu-core/Plugin/` (not `Sequencer/`): it is
processor-side machinery that only `ProcessorBase` owns, and layer-side folders (`Audio/ Modulation/ Persistence/ Sequencer/`) never include `Plugin/`; a layer
type reaches only `RetireTail.h` (design-naming §3). (b) `BlockTransport::beatWrap` is set by the resolver; `renderAndMix` has no `beatWrap` argument.

**Further as-built points from the E1 review (2026-10-16):**

- **Nothing is freed or waited under `binMutex`.** Frees go to a stack list that is destroyed after the lock is released; a waiting eviction releases the lock,
  waits, re-locks and revalidates the entry.
- **`applyComposedState` cuts the in-place slots `[old, new)` on a count increase**, so a regrown slot never inherits a parked tail.
- **A host reporting playing with a frozen ppq** (paused-but-playing) produces no transport jump during the freeze and exactly one on resume.
- **`mergeLayerMidiOut` clamps frames** to the block, and the caller reserves room in the host buffer first (the `midiEventBytes` helper gives the size of one
  event for the budget).
- **`check-layer-boundary.ps1` rules 7 and 8:** (7) no `numLayers.store` outside `ProcessorCore.h` (`Tests/` exempt), so every count store goes through
  `storeLayerCount`; (8) no `Plugin/` include from `mu-core` `Audio/`, `Modulation/`, `Persistence/` or `Sequencer/`.

**What each product converts when it adopts the contract** (stage rows in [extraction-plan.md](extraction-plan.md) section 2):

- **Every live-count store goes through `storeLayerCount`.** The four product constructors store `numLayers` directly today (mu-Clid
  `PluginProcessor.cpp`, mu-Tant `PluginProcessor.cpp`, mu-Toni `PluginProcessor.cpp`, mu-On `PluginProcessor.cpp`), and so do mu-Clid's
  `Persistence/PresetIO.cpp` (shrink and grow in the full-preset apply) and `Persistence/PresetIO_HostState.cpp` (both stores). Each is converted in the
  product's first code-moving commit (E2.1, E3.1, E4.1, E5.2), under `layoutWriteLock` where it can race an edit. After E5 no `numLayers.store` remains outside
  `ProcessorCore::storeLayerCount`; checker rule 7 enforces it.
- **Each product sets its `RetireBin` count policy in its constructor** (`setRetireCountPolicy`): mu-Tant `RenderPastCount` (E4.6); mu-Clid decided at E5 after
  pin P3 (E5.4d); mu-Toni and mu-On park nothing, so they leave the default.
- **mu-Clid's `VoiceEngine::bufferLock`** (`mu-core/Audio/VoiceEngine.cpp`, `tryEnterRead` in `process`) is a `juce::ReadWriteLock`, whose reader bookkeeping
  can allocate on the audio thread. It is replaced, in `mu-core` (it is the shared engine) and by a mechanism the `dsp-controller` signs, **before mu-Clid renders
  through the contract**: a gate item of E5.0, so that `SampleRhythm::render` and the tails pass the AllocationProbe.
- **Oversize-block goldens (pin P7)** are re-recorded when each product adopts `renderAndMix` (E2.5, E3.6, E4.7, E5.7), in a signed goldens-only commit
  (2.1.6 point 10), never in the code-moving commit.
- **Sidechain duck order (C13) stays a later decision.** E1 keeps today's written index order unchanged (pinned by E0.8). It is decided before any change to
  the mixer's channel loop, at the latest with the buses (T2).

#### 2.1.8 E2 as built: mu-Toni on the shared layer contract (2026-10-16)

Branch `e2-toni` (E2.1 to E2.6, a C12 fix and the P7 goldens). Rulings of the `architecture-steward`:

| # | As built | Ruling |
|---|---|---|
| 1 | `UI/ArpHost.h` (`mu_toni::ArpHost`: parameters, `Arp&`, `getModLive`, colour index, insert meter, transport, wavetable names); `ArpContent(ProcessorBase&, ArpHost&)`, `ProcessorBase` forward-declared | **Accept** (2.2). The method names carry the type (`getArpParameters`, `getArpColourIndex`) so one adapter class can implement several `<Type>Host`s without clashes |
| 2 | `MU_TONI_LAYER_UI_SOURCES` CMake list instead of a second target | **Accept** as the family convention (2.5) |
| 3 | `check-layer-boundary.ps1` and its fixtures find a library declared in a product's `CMakeLists.txt` | **Accept.** 2.5 always said the product's own `CMakeLists.txt` defines the target and that no folder moves; the checker's root-level `mu-*-layer/` scan was the stale part. Both forms stay supported (the fixtures cover each) |
| 4a | `HeldNotes.h` in `mu-toni/Source/Sequencer/`, not `mu-core` | **Accept.** One consumer; the second-product rule says it moves to `mu-core` (`Sequencer/`) when mu-Tronic or another product needs it for a second type, and mu-Toni's design says the same. Namespace `mu_toni` |
| 4b | `ParamText.h` moved `Plugin/` to `Persistence/` in `mu-core` | **Accept:** a type's parameter table uses it and a library file may not include `Plugin/`. **Forwarding header: keep, transitionally.** `mu-core/Plugin/ParamText.h` forwards for mu-Clid's and mu-Tant's `PluginProcessor_APVTS.cpp` (live products, not touched before their release) and is deleted in E4.1 / E5.1, when those files move to `Persistence/` and change their include (mu-Tant's did at E4.1; mu-Clid's remains, so the header goes at E5.1). `MixerFxParams.h` (mu-core) includes `Persistence/ParamText.h` directly now (code change owed on this branch) |
| 4c | `Persistence/ArpParams.{h,cpp}`, `Persistence/LayerParamIds.h` | **Accept** (2.3). Minor: the struct `ArpParams` in `Sequencer/ArpSequencer.h` shares the name; rename it `ArpPoolParams` at the next Toni touch |
| 4d | `Audio/Scales.h` to `Audio/ToniScales.h` | **Accept**, done (2.5 rule 1) |
| 5 | Four `mu-core` additions, below | **Accept** with the contract text below |
| 6 | Gaps the author found, below | **Accept as owed work**, not E2 blockers |

**The four additions (contract text).**

- `ProcessorBase::setCarrySkippedMidi(bool)` (constructor only, default off; same shape as `setRetireCountPolicy`). On: the channel messages (not system messages) of a host block that renders nothing are kept, up to `kMaxCarriedMidiEvents` (512, in `MuLimits.h`), and delivered at frame 0 ahead of the next rendered block's events. Overflow is counted into `MidiView::dropped`. For a product whose layers keep input state: a key released during an edit must not stay held.
- `MidiView::carried` (uses the former `reserved` bytes; the struct stays 24 bytes and the `offsetof` freeze is updated): the first `carried` events are carried. A layer applies their key state but does not count a carried note-on as this block's press (`HeldNotes::apply(..., countsAsPress, ...)`).
- `Layer::onParkedBlock(const RenderContext&)`: audio thread, once per call for a layer parked in a slot at or above the live count. It renders nothing; it may follow MIDI and flags only. A layer that finds it was parked in the last rendered block knows it has just gone live. No allocation. Default does nothing, so only a layer that keeps input state overrides it.
- `mu_mod::resolveLane` returns `bool` (false: the slot's `modLock` was missed, `out` holds the unmodulated values). This is how C12 (hold last value on a miss) is implemented generically: the caller keeps its last resolved values and ignores `out` on false.

**Contract gaps the pathfinder found (the "what the contract got wrong" list).**

1. *A layer added or swapped in while keys are held does not know about them.* Today's shared held stack let a new layer see them; per-layer state starts empty. The fix is generic and belongs to `mu-core`: `ProcessorBase` keeps the processor's current input state (a held-key bitmap per MIDI channel, updated in `decodeBlockMidi`, audio thread) and hands it to a layer once when it goes live, through `onParkedBlock`'s counterpart `Layer::onLive(const InputSnapshot&)` or an `InputSnapshot*` in `RenderContext` (decide by struct-freeze cost when built). Owed before T8a (per-layer MIDI-in), which makes it visible; for mu-Toni (pre-release) the gap is accepted until then and recorded here.
2. *The base always sends `channelMask` 0xFFFF.* The field is per-layer in the contract; the base must fill it per slot from one shared per-layer setting (`LayerSettings::midiChannel`, state saved with the layer) at T8a. Until then mu-Toni filters by its own `midiInCh` parameter, which becomes that shared setting's source at T8a; **from then on a layer must not filter a second time** (mask in the context only). The Omni default reproduces today.
3. *Skipped-block MIDI was dropped by the base.* Resolved by `setCarrySkippedMidi`; the default stays off so mu-Clid, mu-Tant and mu-On keep their pinned behaviour.

**For the next products:** E3 (mu-On) needs none of 1 and 2; E4 (mu-Tant) and E5 (mu-Clid) keep `setCarrySkippedMidi` off unless a layer keeps input state (mu-Tant's Pattern keeps its held keys, so mu-Tant turned it on, 2.1.10 point 5).

#### 2.1.9 E3 as built: mu-On on the shared layer contract (2026-10-16)

Branch `e3-on` (E3.1 to E3.8, the contract commit, a C12 fix and the P7 goldens). mu-On is now a `Track : Layer` that clocks its own step row, owns its one engine
and renders only through `renderAndMix`; `GrooveVoices` is deleted. `check-layer-boundary.ps1` passes with two layer libraries (Toni, On). Rulings of the
`architecture-steward`, applying the E2 standards (2.1.8):

| # | As built | Ruling |
|---|---|---|
| 1 | `UI/TrackHost.h` (`mu_on::TrackHost`); `TrackContent(ProcessorBase&, TrackHost&)` with `ProcessorBase` forward-declared and unused; `StepRows(TrackHost&)` | **Accept.** The unused `ProcessorBase&` stays: the content factory signature is `(ProcessorBase&, <Type>Host&)` for every type (2.2), and the shared sections that take a processor arrive with mu-Tronic's chrome. **Done (review commit):** `engineOf` / `selectEngine` are now `getTrackEngine` / `selectTrackEngine`, because host methods carry the type name (2.1.8 row 1) so one adapter can implement several hosts. `getModLive` stays as ArpHost's does (the overloads differ by arity). `StepRows` takes only the host because it reads no `ProcessorBase` service; it is the standalone's all-tracks grid and mu-Tronic does not show it as is (point 8) |
| 2 | `mu-on-layer` INTERFACE target in `mu-on/CMakeLists.txt` (`Persistence/TrackParams.cpp`, `Sequencer/Track.cpp`); `MU_ON_LAYER_UI_SOURCES` (`StepRows.cpp`, `TrackContent.cpp`); `mu-on-layer-tests` (console target: library + mu-core layer-side sources + one test, no `Plugin/` source), with the stream hashed through the real processor and through hand-built contexts equal; the checker finds the library by its `add_library` | **Accept** as the 2.5 / 2.1.8 family shape; the target name `mu-<product>-layer-tests` matches Toni. `GrooveSequencer` is out of the library (row 3b, done) |
| 3 | `Audio/TrackEngine.h`: `TrackEngine` holds a `std::variant` of the five engines behind one interface (`prepare`, `reset`, `resetToPrepared`, `setParams`, `trigger`, `render`); built and prepared on the message thread, installed and removed under the track's `engineLock`, freed on the message thread. **Final shape (after the `dsp-controller`'s F1):** a track that is not `engineLocked` (every mu-On track) holds **all five** prepared engines (`engines[5]`, `heldMask`, `holdsEngine(e)`) and the audio thread plays the one `t{N}_eng` names *in the same block*, whichever thread changed the parameter; an `engineLocked` track (mu-Tronic) holds only its own engine, built by `syncEngine()` on the message thread (silence until built, nothing built on the audio thread) | **Accept the placement and the name.** It is the engine side of the layer (`Audio/`, role `Engine` in design-naming); the `Track` prefix says whose engine it is, like `ToniVoice` in a `Toni` library. `std::variant` and `std::visit` do not allocate. **Ruling on F1:** the first E3 shape (one engine per track, rebuilt on a parameter change, silence until the 100 Hz poll) dropped the step at a host-automated engine change; the fix is right and mu-On is back to 5 engines x 8 tracks. The "8 engines, not 40" saving applies **only to `engineLocked` tracks**, which is what mu-Tronic's 16 slots need (one engine per layer, design 1.4); the plan's E3.5 claim for the standalone is withdrawn. One class and one flag (`engineLocked`) select between the two policies. The variant is as large as its largest engine, so the locked policy's saving is what `sizeof` says; the `dsp-controller` owns that measurement. The golden `engine-automation-midplay` covers the automation case |
| 3a | `Persistence/TrackParams.{h,cpp}`: `EngineType` and its helpers, the `t{N}_` id helpers, the legacy lane prefixes, `addLayerParams` (one table per prefix: every engine's rows on every track), `addTrackGlobalParams` (`seq_swing`, `seq_accent`) | **Accept** (2.3, the `ArpParams` precedent). `addTrackGlobalParams` is the standalone's global rows and mu-Tronic does not call it: see point 8 |
| 3b | `GrooveSequencer.h` held two classes and `StepPattern.h` held `StepRow` and `StepPattern` | **Done (review commits):** `Sequencer/StepRow.h` and `Sequencer/StepClock.h` (the two things `Track` needs; `StepClock::currentStep` is the playhead helper `StepRows` calls); `GrooveSequencer.{h,cpp}` moved to `Tests/` (test support, not in the library); `StepPattern` stays in `Sequencer/` as the standalone's view over the rows |
| 3c | `Sequencer/TrackTypes.h`: `layerTypeOf(EngineType)`, `isTrackType`, `engineOfType`, `typeFromPreset(tree)`, `createTrack(slot)`, `makeTrackTypeInfo(engine)` and the five `LayerTypeInfo` constants (`kKickType` ... `kRumbleType`) | **Accept the file and the five constants** (the constants are the registered types, 1.4; a header of free functions and constants is not a class, so the file-equals-class rule does not apply, and Toni's single constant sits in `Arp.h` only because it has one). `typeFromPreset` is a free function, not a `LayerTypeInfo` field, until the registry exists (T1 adds the field and points the five constants at it). **Done (review commit):** the mapping (`layerTypeOf`, `isTrackType`, `engineOfType`) lives once beside `EngineType` in `Persistence/TrackParams.h` with `static_assert`s of the order (`LayerTypeId` is saved by name, so its order could change), and `Track::typeId` uses it |
| 3d | `Track::engineLocked`, a plain `bool`, default false, not saved; `TrackContent` hides the Engine selector when set | **Accept; not saving it is correct (point 6).** The Engine selector hides, nothing else changes |
| 4 | `remapLayerReferences` kept for the Rumble source | **Accept the hook, amend the plan; the body is now `mu_on::remapRumbleSources` (done).** See below |
| 5 | `RenderContext::stoppedUnrendered`; `renderPriority()` read once per call | **Accept**, contract text in 2.1.5 and the 2.1 table; see below |
| 6 | `engineLocked` not saved | **Accept** (architecture-steward's decision, below) |
| 7 | Engine history belongs to the track, not the slot | **Accept** (below) |

**Point 4: the Rumble source remap.** `layersReordered` runs under `layersLock` with processing suspended and is documented as "only move and reset, never allocate or
wait"; a parameter write there (`setValueNotifyingHost`) notifies the host from inside the lock, and the parameter values themselves only shift later, in phase 3
(`copyLayerParams`), so a rewrite has to happen **before that shift, at the old slot**, which is exactly where `remapLayerReferences` is called. That hook is already
the generic one: its contract (design-plugin-family, "Layer references") is "a parameter whose value names another layer follows its layer", it is not mu-On-named, and
mu-Tronic needs the same thing for the same parameter. So the extraction plan's "becomes the generic `layersReordered` use" was a wrong guess about which hook was
generic, and is corrected there. What is left to fix is the placement of the body: it is in `PluginProcessor.cpp` (`Plugin/`), and mu-Tronic's processor would be a second
copy, which the move rule forbids. **Done (review commit):** the body is `mu_on::remapRumbleSources(apvts, liveCount, engineOfSlot, oldToNew)` in
`Persistence/TrackParams.{h,cpp}` (it reads and writes only `t{N}_eng` and `t{N}_src`, and the "gone source becomes the first Kick" fallback is the same rule over
the type), and mu-On's `remapLayerReferences` override is one call. **mu-Tronic needs:** its processor overrides `remapLayerReferences` with the same one call, passing
how it finds a slot's engine (the layer's type, for a Track). Nothing in `mu-core` changes. The rewrite applies to slots whose layer is a Rumble Track; other types'
parameters are never touched.

**Point 5: the two `mu-core` additions.**

- `RenderContext::stoppedUnrendered` (2.1.5): a qualifier of `transportJumped`; name and placement accepted (a former `reserved` byte, size and version unchanged, the
  `offsetof` freeze updated; the internal names `stopPending` and `edgeHiddenStop` are private to `ProcessorBase`). It closes a gap the E1 stop rule left open: P5
  (the stop edge with a 0-frame stop block) now reaches a layer that silences on stop. Tests in `LayerRenderTests.cpp` cover a stop in an unrendered block, a 0-frame
  stop block, a pause and resume, and the cases where it must stay 0.
- `renderPriority()` read once per call (2.1 table): the base reads it for each live layer in pass 1 and keeps the result for pass 3, so a priority that follows a
  parameter (mu-On's engine) cannot make a layer render twice or not at all when the parameter moves between the passes. **The contract text is
  in `Layer::renderPriority`'s comment (done):** it may depend on state that changes on any thread; the base reads it once per live layer per call. Residual, accepted: a track whose
  engine becomes a Rumble between the base's reading and its own `render` renders one call as a Rumble in pass 1 with no peers (it processes silence) and publishes that
  output as a peer for the call; the next call is consistent. One block, never a hang or a double render.

**Point 6: `engineLocked` is not saved.** The layer's type is the saved thing: mu-Tronic saves a Track layer by its type name (`"Kick"`), so what the host needs to restore is
derived, not stored. A flag in the layer's saved node would be a second copy of the type that could disagree with it (and with `t{N}_eng`, which is saved too). Rule: **the
host that creates the layer sets it, from the type, in the same function that sets `t{N}_eng`, on create and on restore.** Today's `createTrack(slot)` produces an
unlocked track with the engine its parameter names, which is right for mu-On. **Gap, owed at T1:** `LayerTypeInfo::create` is `(slot) -> Layer` and cannot say which of
the five types it is making, so all five constants point at the same `createTrack`. The registry gets a message-thread `initParams(apvts, slot)` hook on `LayerTypeInfo`
(called after `create`, before `bindParams`), and mu-On's five write `t{N}_eng` and set `engineLocked`; this is also where a restored session's type name and `t{N}_eng`
are reconciled (the type wins and the parameter is rewritten). Not an E3 blocker: mu-On has one host and sets what it needs. **Superseded 2026-10-20 (2.1.11 points 5 and 6): no `initParams` on `LayerTypeInfo`.** `create` receives the type id and
locks the Track to its engine; a locked Track reads its engine from that lock, not from `t{N}_eng`, so the type cannot drift; and a generic `Layer::completeSetup` writes the
parameter (so saved rows and `.muTrack` presets carry the right engine) and builds the engine.

**Point 7: engine history belongs to the track.** Consistent with the layer-state ownership design (1.3, 3.5): state that belongs to a layer lives in the layer and moves with it.
The old arrangement, an engine per slot and per engine type, tied sound history to a slot number. Now the engines are the track's: a track keeps the history of each of its
engines as it moves (Kick, then Bass, then Kick again resumes that track's own Kick, not the slot's), and when a structural edit moves a track to another slot every held
engine is reset to its prepared state (`resetToPrepared`: voices silenced, the Bass filter's drive smoothing restarted as a fresh engine's is). A Rumble that merely moves slot keeps its
reverb's settled parameters (only a newly built reverb restarts its glide). The difference from the slot-owned arrangement is therefore small and is the history that used to be an accident of the
slot; a golden that depends on it is re-recorded in a signed goldens-only commit, never in the code-moving one. The engine installation of an `engineLocked` track (message thread builds
and prepares outside every lock, pointer moves under `engineLock`, old engine freed on the message thread, a try-lock miss renders that track silent for one call) is the R1 rule applied
to a layer-internal engine.

**Point 8: what mu-Tronic still needs from this library (owed, not E3 blockers; (a) to (d) built in T1b step 2b, 2026-10-20, per 2.1.11).** (a) `TrackContent` and `Track::bindParams` bind to the global rows `seq_swing` and `seq_accent`;
mu-Tronic has no global groove, so before it links the library the groove parameters become per-layer rows (appended to the `t{N}_` table, pre-release) or the host names the
ids; the plan's E3.3 seeding (global values seed each row on load) stays for the standalone. (b) `StepRows` draws every live layer; mu-Tronic's Track content shows only its own
row, so a one-row component is split out of it (T1, `ux-controller` for the look). (c) `initParams` (point 6). (d) `Track::prepare` builds no engine: the host calls `syncEngine()`
after `engineLocked` and the engine parameter are set, and (for a locked track the engine never changes, so only on a type change) builds on the message thread; mu-On needs the call at create, and
its `engineParamChanged` listener is now only a safety net for the all-five policy. A generic message-thread service hook on `Layer` is the shape
if mu-Tronic would otherwise copy that listener; decided at T1. **All four decided 2026-10-20: 2.1.11 points 4 to 6.**

#### 2.1.10 E4 as built: mu-Tant on the shared layer contract (2026-10-16)

Branch `e4-tant` (E4.1 to E4.7 as nine commits, a contract commit, a C12 fix, the C3 twin test and the P7 goldens). mu-Tant is now a `Pattern : Layer` that renders only
through `renderAndMix`; the processor-side render chain, `retiring[]` and the per-slot config tables are gone. `check-layer-boundary.ps1` passes with three layer
libraries (Toni, On, Tant). Rulings of the `architecture-steward`, applying the E2 and E3 standards (2.1.8, 2.1.9):

| # | As built | Ruling |
|---|---|---|
| 1 | `UI/PatternHost.h` (`mu_tant::PatternHost`); `PatternContent(ProcessorBase&, PatternHost&)`, `ProcessorBase` forward-declared; methods `getPatternParameters`, `getPatternLayer`, `getPatternModLive`, `getPatternInsertGainReduction`, `isPatternTransportPlaying`, `getPatternTransportBeat`, the five wavetable calls, `resolvePatternWavetable`, `getPatternEngine`, `getPatternBankLock` | **Accept.** The type name is in every method (2.1.8 row 1). The interface carries two bind-time resources (engine, bank lock) beside the UI reads; that is right, because a host that implements `PatternHost` is the one place that supplies everything a Pattern needs from outside (2.2). `Pattern::host` is an `OwnerLink<PatternHost>`. Contract on the two bind-time calls: below (row 3) |
| 2 | `mu-tant-layer` INTERFACE target in `mu-tant/CMakeLists.txt` (`Persistence/PatternParams.cpp`, `Sequencer/Pattern.cpp`, `Sequencer/GatePattern.cpp`, `Audio/SynthVoice.cpp`); `MU_TANT_LAYER_UI_SOURCES` (`PatternContent.cpp`, `GatingDesigner.cpp`); `mu-tant-layer-tests` (library, mu-core layer-side sources and one test, no `Plugin/` source; the stream hashed through the real processor and through hand-built contexts equal); `Audio/Scales.h` to `Audio/TantScales.h`; `Persistence/PatternParams.{h,cpp}` (`addLayerParams`, `addPatternGlobalParams`, the `v{N}_` helpers, `kRootParamId`, `kScaleParamId`); registered in the checker by declaring the library | **Accept** as the 2.5 family shape. The `v` letter and ids are unchanged (saved data, design-naming section 3). `PatternParams.h` holds the id helpers as mu-On's `TrackParams.h` does (Toni keeps them in `LayerParamIds.h`; both stand). `ParamText.h` moved to `Persistence/` for mu-Tant at E4.1 as 2.1.8 row 4b said; the forwarding header in `mu-core/Plugin/` now has one user left (mu-Clid's `PluginProcessor_APVTS.cpp`) and is deleted at E5.1 |
| 2b | `check-layer-boundary.ps1` rule 5 skips a function named `renderPlaysByDefault` | **Accept.** It is the `StandaloneShell` virtual (`mu-core/Plugin/StandaloneShell.h`) that tells `--render` whether to start the transport: app policy in the shell, not per-layer render glue, and mu-Tant's `StandaloneApp.cpp` is the first product to override it. An exact-name list (`$shellRenderHooks`) is the right shape: a new shell hook is one added name, never a pattern. **Code change owed (test-steward and the coder):** the rule 5 paragraph in the script header names the list; `test-check-layer-boundary.ps1` gets one fixture where the hook passes and one where a real `renderVoice` definition still fails |
| 3 | The engine stays per **slot** and processor-owned (`voices[slot]`); a Pattern reaches it through `PatternHost::getPatternEngine(slot)` at `bindParams` | **Accept, and it does not conflict with layer-owned state.** See "Point 3" below |
| 4 | `swapLoopBeats` and `SwapBoundary.h` stay in `mu-tant/Source/Plugin/`; the Pattern's part is `Pattern::loopBeats()` (its gate length) | **Accept; amends the plan's E4.1 row ("`SwapBoundary.h` from `Plugin/` to `Sequencer/`") and E4.7.** See "Point 4" below |
| 5 | Note mode: `midiNoteMode` stays an app setting, pushed to every Pattern by `setMidiNoteMode`; each Pattern keeps its own held keys for its pitch track (`HeldNoteStack`); the 8 ms final-mix gate stays in the shell (`scanNoteMode`, `applyNoteModeGate`, decision A3); nothing new is saved (D7) | **Accept; the plan's E4.3 text "the setting is saved per layer" is withdrawn for the standalone.** What mu-Tronic needs: "Point 5" below |
| 6 | `ProcessorBase::setRegrowCutsInPlaceTails(bool)`; `renderLayers` phase 5 caches `loopBeats()` for every non-Empty slot; `ProcessorBase::layerLoopBeats(slot)` | **Accept** with the contract text and two small code changes: "Point 6" below |
| 7 | E4.3 + E4.4 one commit (both chains behind `useLayerRender`), E4.5 + E4.6 one commit (engine array, bank lock and the retire bin) | **Accept** as a deviation. The plan split them so no commit holds two render paths and a failure points at one change. The merged E4.3/E4.4 commit still holds both chains behind the switch with the equivalence test, then E4.5/4.6 deletes the old one, so the property the split protected is kept; the engine move is a pointer hand-over and the bin replaces `retiring[]` arithmetic-for-arithmetic, so there was no state to bisect between them. E4.3 stayed a single commit with E4.4 |
| 8 | No generic `checkRetireIsolation` helper; no `kPatternType` constant | **Accept both as deferrals, tracked** ("Point 8" below) |
| 9 | `HeldNoteStack` (mu-Tant, 16 keys, drop-oldest) written beside mu-Toni's `HeldNotes` (32 keys, ignore-when-full, MIDI decode) | **Change the code, tracked, not an E4 blocker.** The second-product rule (2.5) says it moves to `mu-core/Sequencer/` before the second copy is written, and 2.1.8 row 4a named the trigger: this is it. Two different overflow rules are pinned behaviour, so one `mu_core` class takes the capacity and the overflow policy as template parameters, Toni's channel-filtering `apply(MidiEvent, ...)` stays Toni's thin wrapper, and both products keep their numbers. No behaviour change; goldens must be byte-identical. Owed before T1 (mu-Tronic puts both libraries on one include path) |

**Point 3: the engine is slot-bound, like the parameter rows.** The `v{N}_` rows are slot-keyed and the base rebinds a layer on every move (`bindParams(apvts, newSlot)`
in the structural-edit paths of `ProcessorBase.cpp`), so a Pattern's engine pointer is a *binding* of the same kind as its `ParamBinding`s: re-fetched for the slot it is in, never owned,
never outliving the host. What survives in the slot is only what `VoiceEngine::reset` keeps, the oscillator phase (the filter and insert memory is reset in `layersMoved`), plus the
slot's noise-sequence identity (`setSlotIndex(slot, salt)`). Moving the engine with the Pattern would change the sound after a structural edit and break the pinned goldens
(`note-mode-structural-edit`), on a live product whose rule is "nothing audible changes". That is a bounded, documented exception to 1.3 and 3.5 (state that belongs to a layer
moves with it), and it is the opposite choice to mu-On's (2.1.9 point 7, where the engine history is the track's) for a reason that is mu-Tant's pin, not a principle. The interface keeps both
options open for mu-Tronic: the Pattern never constructs, owns or frees an engine, so a different adapter changes only what `getPatternEngine` returns.
Contract on `getPatternEngine` and `getPatternBankLock`: message thread; the base calls `bindParams` under `layersLock` on a move (a swap or a remove), so the call
**allocates nothing and takes no lock**; the engine already exists, prepared, before the layer is created, and outlives every layer bound to it. mu-Tronic's
adapter does the same (**recommended: one engine per slot, built when the slot's first Pattern is created, so the standalone and mu-Tronic share one path**; per-layer engines whose phase travels are
a later choice for Andy, to be made with the `dsp-controller`'s `sizeof` measurement of 16 engines, because it changes the sound after a move). Not an E4 or T5 blocker.

**Point 4: `swapLoopBeats` is the host's policy.** It chooses *which* loop ends a pending swap from the swap timing, the master loop, the live count and the slot; the type contributes only
the per-slot length (`Pattern::loopBeats()`), which the base now caches (point 6). It builds on `mu_hotswap` (`mu-core/Plugin/HotSwap.h`), which checker rule 1 forbids
a library file to include, and which loop a full preset waits for is each host's rule (mu-Tant: the master loop, else voice 0's). So it stays in `Plugin/` with a comment saying so (done) and the
plan's `Sequencer/` placement was wrong. **Move rule:** `HotSwap.h` already holds the predicates that are common (`layerBoundaryReached`, `fullPresetBoundaryReached`); if mu-Clid's
equivalent at E5 turns out to be the same arithmetic, `swapLoopBeats` moves to `HotSwap.h` as a function before E5 writes its copy, and mu-Tant's header is deleted. The `flagLayerSwapBoundaries(t, fullPresetLoopBeats)` convenience
covers hosts whose full-preset rule is one number; mu-Tant needs the callable form (`flagSwapBoundaries` with a lambda) because its per-slot rule depends on the swap timing.

**Point 5: Note mode, for mu-Tronic.** Recorded so T5 does not re-decide it:

- *The standalone.* Note mode is an app setting (`midiNoteMode`, stored in the app settings, not in the preset), pushed to every Pattern on change. The held-key stack of each Pattern drives its
  pitch track; the shell keeps a second stack and applies the 8 ms ramp to the **final mix** (so the FX returns are gated, as they always were; decision A3, saved bytes D7).
- *mu-Tronic.* `Pattern::gatesOwnOutput` (default false) makes the layer apply the ramp to its **own output**, before its strip: a different point in the chain, owner-visible only in mu-Tronic and
  not a regression, because mu-Tronic has no pinned mix. The Note-mode choice becomes a per-layer saved setting there. **It must not change mu-Tant's saved bytes (D7):** it is written by the host
  that sets `gatesOwnOutput` (in its layer node), and `Pattern::writeExtras` stays as it is. The setting then reaches the Pattern through `setNoteMode`/`initNoteMode`, as the standalone's does.
- *Input state is transitional.* `takeInputStateFrom`, `hasInputState` and `markInputSeeded`, with the processor's `seedInputState`, are mu-Tant's workaround for pathfinder gap 1 (2.1.8: a layer added while keys are held does not
  know them). When the base owns the held-key snapshot and hands it to a layer when it goes live, those four are deleted and `Pattern` takes the snapshot instead. Owed with the gap-1 fix, before T8a.
- `setCarrySkippedMidi(true)` is on for mu-Tant, as 2.1.8 prescribed for a product whose layers keep input state (the Pattern's held keys); it replaces the old shared stack, which heard every block.

**Point 6: the two `mu-core` additions.**

- `ProcessorBase::setRegrowCutsInPlaceTails(bool)` (protected, constructor only, default on; same shape as `setRetireCountPolicy` and `setCarrySkippedMidi`). Name accepted: it names what it turns off, the cut
  of the in-place tails of the slots a count increase regrows (R1.3, 2.1.3). On is the contract. Off is mu-Tant's pin P1 (a shrink, regrow, shrink resumes the stale tail), kept so the live
  product's mix is unchanged by this stage. **It is temporary:** it exists to separate the refactor from the bug fix. Remove the call, the member, the branch in `applyComposedState` and the opt-out wording
  when Andy signs the P1 goldens-only update (the shrink-regrow-shrink cases, 2.1.6 point 3); the plan's E4.6 row put that update before the stage, and the stage went ahead without it.
  Recommended timing: a separate signed commit after the first release that contains E4 and Andy's soak, and before mu-Tronic's Pattern work (T5). A new product never calls it.
- `renderLayers` phase 5 caches `loopBeats()` for every non-Empty slot, a parked spare included (it was live slots only); `ProcessorBase::layerLoopBeats(slot)` reads the cache (audio thread, or a test while not processing; -1 out of range).
  Accepted: mu-Tant's rule reads every slot's length, and a spare's length is the value the slot will have when it goes live. Placement (protected, next to the other render accessors) and the name (`layer` prefix as `layerAs`, `layerSwapper`) are right.
  **Contract text (add to `Layer::loopBeats`):** "audio thread; called for any non-Empty layer in any slot, including a parked spare; reads atomics only". **Two code changes:** (a) `ProcessorBase_Render.cpp` compares `typeId() != 0`; use `mu_core::LayerTypeId::Empty`.
  (b) `flagLayerSwapBoundaries` repeats the same cache read inline; it calls `layerLoopBeats(slot)` so there is one definition. The change alters what a staged swap for a slot at or above the live count sees (its spare's length, not the bar); Toni and On pass their goldens, and for mu-Clid it is the more correct value.

**Point 8: deferrals, where tracked.**

- *Generic `checkRetireIsolation`.* 2.1.7 row 6 said it is owed at E4.6. The twin test now exists for the fake type (`RetireBinTests`) and for `Pattern` (`RetireIsolationTests.cpp`), two instances, which is when a helper is
  worth writing. It is owed **before mu-Clid's tails (E5.4d)**, the third and largest instance, and written once in `mu-core/Tests/` from the two existing tests. Tracked on the E1/E4 item (#1609).
- *`kPatternType`.* Not done, and not done for `Arp` either: design-naming's statement that `kArpType` sits in `Arp.h` is wrong (mu-Toni has no `LayerTypeInfo` constant; mu-On has five, in `TrackTypes.h`). Fixed in design-naming. A `Pattern` constant cannot be complete today,
  because `LayerTypeInfo::create(slot)` returns a layer with no host: a Pattern needs `host.ptr` set (and mu-On's `initParams` gap, 2.1.9 point 6, is the same shape). So `kPatternType` and `kArpType` are written together
  with the registry hook at **T1** (`initParams` / host wiring), not now; the fields that exist already (`id`, `name`, `group`, `v`, `.muPattern`, `addLayerParams`) are known and the ids are in `LayerTypeId`. Tracked on #1609 for T1.

**Other as-built points.** `mu-core` carries the E4 contract commit with its tests (`LayerRenderTests`, `RetireBinTests`, `LayerContractFixtures.h` exposing the two new members). The hold-last-value fix (C12) is `Pattern`'s, built on the `resolveLane` boolean of 2.1.8.
`Pattern` names no processor; its `host` link is set by the creator (`createLayer`).

#### 2.1.11 T1b step 2: registering the first Track type (architecture-steward rulings, 2026-10-20)

Applies the E2 and E3 standards (2.1.8, 2.1.9) to the second registered type. Kick is the first; Bass, Hat and Snare differ from it by id only and may land in the
same commit; Rumble follows with its `remapLayerReferences` override (2.1.9 point 4).

1. **Hard gates before any Track type is registered** (each is a few lines; all are `mu-core` or build plumbing, so they land once for every later type).
   - *Selected slot.* `selectedSlot` (an `std::atomic<int>`), `setSelectedSlot(int)` and `getSelectedSlot()` move into `ProcessorBase` (they are generic: any slot host
     lists a typed folder). The one caller is **`LayerPanel::setLayer`** (`mu-core/UI/LayerPanel.cpp`), which every product's editor already routes the shown layer
     through, sidebar click, page change and Performance-mode follow alike; `EditorShellBase` has no selection point of its own. mu-Tronic's `PluginEditor::showSlot`
     drops its `setSelectedSlot` call. For the four list products it is a harmless store.
   - *Typed preset folder.* `presetDirOf(info)`, the extension spelling and the typed listing (`slotPresetFiles` over the registry) move into `ProcessorBase` as protected
     helpers over `getContentDir()` and `layerTypes`; mu-Tronic's three overrides become one-line calls. A type that shares an extension with others (the five Track types
     share `.muTrack`) lists only the files `LayerTypeInfo::typeFromPreset` maps to its id (the field 2.5 already names; the five constants point at `mu_on::typeFromPreset`).
     A Track type names `contentFolderName = "muOn"`, `presetFolder = "Tracks"`.
   - *`LayerLayout` private constructor.* The `int unused` separator becomes a private nested tag type (`struct SetsTag {}`); `withPrefixSets` is the only caller.
   - *`kPrefixLetters` test* (`TronicLayoutTests`): every registered type's `paramPrefixLetter` is in `kPrefixLetters`; for each slot the layout holds at least one parameter
     under that letter's prefix; and `LayerLayout::slotOf` puts every `<letter><slot>_` id in its slot, never in the globals (a letter missing from the list would save
     that family's rows in `<Globals>` and no test would notice).
   - *`PARENT_SCOPE`.* Every `mu-<product>-layer` CMake file ends its UI-list declaration with `set(MU_<P>_LAYER_UI_SOURCES "${MU_<P>_LAYER_UI_SOURCES}" PARENT_SCOPE)`, as
     mu-Toni's does. mu-On's was missing (mu-Tronic would have linked the library and silently compiled no Track UI); mu-Tant and mu-Clid get the line now, not when
     each is registered (family consistency). The root `CMakeLists.txt` already adds the products before `mu-tronic`.
   - *Services.* **Confirmed: the Track engines need no `LayerServices`.** `TrackEngine`'s five engines (Hat and Snare's `SampleChannel` is synthesised) read no wavetable bank
     and no sample library and load no file. `LayerServices` and the shared bank wait for Pattern, whose registration builds them and moves Arp's `sharedArpBank()` onto
     them in the same change (2.4). Do not build `LayerServices` for Kick.
   - *Where per-type host glue lives.* Rule 10 of `check-layer-boundary.ps1` stands (mu-Tronic names a type's header only under `Persistence/` and `Tests/`). A type's
     registration glue, its host adapter (`TrackHost`, `ArpHost`, over `ProcessorBase`, which is all they need), its `createContent` / `createMiniVisual` factories and its
     `LayerTypeInfo` additions, therefore live in **`Persistence/<Type>LayerType.{h,cpp}`** (`ArpLayerType`, `TrackLayerTypes`), and `LayerTypes.cpp` stays the one-line
     list. `LayerTypeInfo` gains `createContent(ProcessorBase&, int slot)` and `createMiniVisual(ProcessorBase&, int slot, pulse)` function pointers (null for a host that
     builds its own UI, as the four list products do) and `typeFromPreset`; `CreateFn` becomes `(int slot, LayerTypeId type)` so one function can make five types.
2. **The sidechain follower is not touched by `setLayerType`.** The follower is the *receiving strip's* smoother of its *source's* envelope; a type change replaces neither
   the source nor the strip (`ch{N}_` is kept, step 7), so zeroing it only unducks a strip that is still being ducked and re-ducks it at the attack: an audible pump, and
   the old layer's tail (also rendered through that strip) would be ducked differently from its live self. Rule: **`setLayerType` requests no reset.** `resetLayer`
   (the strip returns to defaults) keeps its request, because there the sidechain setup itself changes. **Amended 2026-10-20 (T1b 2a review):** `applyStateTree` and a
   stopped full-preset load never carried a follower request in code (only `resetLayer` does), and none is added: the follower is a millisecond smoother, a restore
   rewrites every parameter at a silent moment, and a new request there would be a behaviour change in four shipped products for no audible gain. Design row 7 above and extraction-plan C17 are amended; `LayerTypeSwapTests::followerRestarts` and the matching `TronicArpTests` case
   change to pin "unchanged by a type change, zeroed by reset" (test-steward). The `dsp-controller` signs the removal.
3. **Retire policy.** A type change parks the old layer, which renders as a tail reading no parameters. `None` cuts it, a click for any sounding voice, so **every
   registered layer type declares `Fade`**, with `fadeMs` and `maxSeconds` left at the `RetirePolicy` defaults (25 ms, 2 s: the one family value, `RetirePolicy`'s own
   defaults, which `Pattern` already uses without writing a literal). A new literal in a layer type is wrong.
   - *Track* (Kick to Bass is the same-prefix case, safe because the tail reads no parameter): `retirePolicy()` returns Fade; `beginRetire` copies nothing, because the
     held engine keeps the parameters it was last given; `renderRetiring` renders that engine with no trigger and no `setParams`, through `applyLinearFade`, returning false
     when the fade ends. A Rumble tail has no peer and processes the track's `silence` buffer (as a Rumble with no source does), and the fade cuts its reverb at 25 ms; a
     `Natural` Rumble tail is a later, separate ruling. The tail must not call a `ParamBinding` (it asserts).
   - *Arp* (the missing policy): the same, over the voice and its insert. This changes mu-Toni's own behaviour: a removed layer now fades 25 ms instead of cutting, so
     the bin's count policy stays at its default and the structural-edit goldens that remove a sounding layer are re-recorded in a signed goldens-only commit (`dsp-controller`
     reviews; presets and renders changing is accepted, Andy 2026-10-17).
4. **Groove rows are per layer, in both products.** `t{N}_swing` (0 to 1, default 0) and `t{N}_accent` (0 to 1, default 1) are appended to `addLayerParams`'s `t{N}_`
   table; `Track::bindParams` and `TrackContent` bind `layerParamId(slot, "swing")` / `"accent"` (the content rebinds its attachments when its layer changes, as it does for
   the engine rows). `addTrackGlobalParams`, `seq_swing`, `seq_accent` and the processor's `seqSwingParam` / `seqAccentParam` / `seedRowGroove` are deleted: one
   mechanism, not a host that names ids. This *is* a behaviour change in mu-On (the Swing and Accent knobs on a track's panel now move that track only, where they moved every
   track) and matches 3.5; mu-On is pre-release. **No migration**: an older state's `seq_swing` / `seq_accent` rows are ignored on load (rows with no parameter are skipped), so
   3.5's earlier "the global value seeds every track" is withdrawn (Andy: old presets sounding different does not matter). `ExtractionPinTests` swing/accent pins and the
   `groove-five-swing-accent` goldens are rewritten to set the five per-track rows (signed goldens-only commit); the behaviour pinned is the same maths.
5. **A generic `Layer::completeSetup(apvts, slot)`, not `LayerTypeInfo::initParams`.** A function pointer on the type cannot reach the layer it is about, and the work
   (build an engine, write the layer's own parameter) is the layer's own message-thread setup, which Pattern's bank binding and Rhythm's sample load will need too.
   Contract: virtual on `Layer`, default no-op; message thread; **called once, after the first `bindParams`, outside every lock, before the layer is published**; it may
   allocate, build and prepare, and read or write the layer's own parameters through the APVTS. Call sites: `setLayerType` step 7 (after `bindParams`, which is already
   outside `layersLock`) and each product's own create path (mu-On's `createLayer` replaces its `t->syncEngine()` with it). It is *not* folded into `bindParams`, which also
   runs under `layersLock` on a reorder. `Track::completeSetup` writes `t{N}_eng` to the locked engine when it differs (so the saved rows and a saved `.muTrack` name the
   right engine and mu-On loads it) and calls `syncEngine()` (2.1.9 point 8d). mu-Tronic installs **no** engine-parameter listener.
6. **A locked Track's type cannot drift.** `create(slot, type)` calls `Track::lockEngine(engineOfType(type))`, which sets `engineLocked` and the engine; for a locked
   track `engineSetting()` returns that engine and ignores `t{N}_eng`. The parameter is therefore only a record (point 5 keeps it right), a stray automation or a loaded row
   cannot change the layer's type, and the "type wins on restore" reconciliation of 2.1.9 point 6 holds by construction. mu-On's tracks are never locked, so the standalone is unchanged.
7. **UI additions of step 2a (ruled 2026-10-20).** (a) `mu-core/UI/LayerContent.h` is the content base: `setLayer`, `releaseModulationArcs`, `onStatusUpdate` and a virtual `getModDestProvider()` (null default). The provider stays on the content, not on `LayerTypeInfo`: `ModDestProvider` is a UI struct whose resolvers bind to the content's own matrix and widgets, and `LayerTypeInfo` is message-thread data that only forward-declares UI types. Every product's `<Type>Content` derives from it when its type is registered in mu-Tronic (`ArpContent` now; `TrackContent` at 2c; `PatternContent` and `RhythmContent` at their registration), not before. (b) `MuLookAndFeel::setAccentOverride(Component&, Colour)` / `clearAccentOverride` (a component property read up the parent chain by `appAccent`, the main-page knob colour and `lcdLitColour`) is the one mechanism for a host that paints the shown layer in its type's colour; `setAppAccent` stays one value per LookAndFeel. It covers knobs, lit LCDs and `appAccent` readers; segment and toggle selected-states keep the app colour (they read the theme's colour IDs), which is the `ux-controller`'s to confirm against design-ui-family section 11. (c) `LayerHeaderBar::setTypeTag` is accepted: off by default, no height, other products unaffected. (d) `MU_TRONIC_WITH_UI` is accepted as the headless split for T1b: defined on the app target only, and confined to the one `.cpp` per type (`Persistence/<Type>LayerType.cpp`), never a header or any other file, so the console test target registers the same engine description with null content and picture factories. If a second `#if` appears, split the factories into an app-only `<Type>LayerUi.cpp` instead. The factories are therefore unexercised by `mu-tronic-tests`; the preset helpers in `ProcessorBase` are pure and are tested (test-steward). (e) `ArpLayerType.{h,cpp}` under `Persistence/` is correct per point 1. (f) The `PARENT_SCOPE` lines in mu-On, mu-Tant and mu-Clid are correct and complete point 1.

**Sub-steps (each builds, passes its suites and commits alone):** 2a the gates and the Arp content (`mu-core`, `mu-tronic`, `mu-toni` for the `create` signature, no Track
code); 2b the mu-On library made ready (groove rows, `lockEngine` and `completeSetup`, the Track and Arp retire tails, the one-row step component split out of `StepRows`,
`PARENT_SCOPE`); 2c Kick registered (`Persistence/TrackLayerTypes.cpp`, `TrackHost` adapter, the content in mu-Tronic, the equivalence test, the cost test re-run). The
`ux-controller` rules the single-row STEPS layout inside a Track content for 2c; the split in 2b is pixel-identical in mu-On.

**Rulings from the 2c review (2026-10-20).**

8. **Layer presets in a slot are not built yet; they are T1b step 3, before Pattern is registered (not T8a).** Today `ProcessorBase::loadLayerPreset` / `saveLayerPreset` are base
   no-ops and mu-Tronic's layer-preset applier (`layerSwapper.setAppliers`, second lambda) is empty, so the typed folder, extension and listing (2.1.11 point 1) list files that
   do nothing when chosen. It is a gap since Arp (step 1), not a 2c regression. It is generic, so it lands once in `mu-core`, not per type and not in `Persistence/<Type>LayerType`:
   - *Load* (`ProcessorBase`, slot hosts): parse the file, take the type from `presetExtension` and, where set, `typeFromPreset`; stage `applyLayerNode(slot, node)` (the type's
     prefix rows, then `Layer::applyExtras`) through `layerSwapper`'s layer applier, so it lands on the layer's `loopBeats()` wrap exactly as mu-On's `stageLayerPreset` does.
     The same applier serves clips (T8a), which is why this precedes T8a.
   - *Save*: the slot's rows with the type's prefix plus `writeExtras`, into `typedLayerPresetDir(slot)` with `typedLayerPresetExtension(slot)`; the root tag is a new
     `LayerTypeInfo::presetTag` (each product's existing tag, so a file saved by the product loads in mu-Tronic and the reverse).
   - *Mismatch.* A locked slot takes only its own type: loading a file whose type is not the slot's (a `.muTrack` that names Snare into a Kick, a `.muArp` into a Kick) is
     **refused with no change**: `onLoadError("This is a Snare preset; this slot is a Kick")`. The typed listing never offers it, so it can only arrive by drop, MIDI preset or a
     clip, and a type is changed only through the chooser or the header menu (never as a side effect of a preset). An **Empty** slot takes the file's type (the listing already
     offers every type there): `setLayerType(slot, type, &node)`. This replaces the "type change (confirm, then stage)" wording of 3.4, which is withdrawn for T1b; a confirmed
     preset-driven type change can be added later if Andy wants it.
   - *Test* (test-steward): per registered type, a saved file round-trips through a slot; a mismatched file changes nothing; an Empty slot takes the file's type; `.muTrack`
     files of the four engines land only in their own slot.
9. **A fresh locked Track starts on its engine's default groove** (the family answer to "I picked Kick in an empty slot and it is silent"). mu-On gives a new track
   `loadDefaultGroove(engine)` in `layerAdded`; mu-Tronic has no such hook, and putting it in `completeSetup` would clobber a restored empty row (`completeSetup` runs after
   the saved rows are applied, step 7). The place is **`createTrack` in `mu-on/Source/Sequencer/TrackTypes.h`**: it runs before any saved row or `initial` node is applied, and
   `Track::applyExtras` (`StepRow::deserialise`) overwrites every step, empty or not, so a restore is unaffected. `createTrack` calls `row.loadDefaultGroove(engineOfType(type))`;
   Rumble gets nothing, as in mu-On. mu-On's unlocked tracks keep `layerAdded`. This is product behaviour only in that a new slot makes a sound at once; Andy may veto it
   (one line to remove), nothing else depends on it. Tests that need an empty row clear it (the equivalence tests already set the row explicitly).
10. **One disc picture in `mu-core`.** `mu_toni::ArpMiniVisual` (with 2c's text overload) and `mu_on::TrackMiniVisual` now draw the same disc. That is a second copy of shared
    behaviour; Pattern and Rhythm would be a third and fourth. At step 3 add `mu_core::LayerDiscVisual(Colour, String)` in `mu-core/UI/Components/`, delete both classes
    (mu-Toni's sidebar passes the number, mu-On's the engine initial, mu-Tronic the type's initial; pixel-identical in both apps), and have each type's picture factory use it.
    Until then 2c's overload is accepted as the smaller step. The glyph itself (the step row design-ui 4.5a names) is the `ux-controller`'s.
11. **A slot host adapter must not cast a foreign layer.** `ArpSlotHost::getArp` casts whatever the slot holds to `Arp`; with five types registered, slot 0 (which the
    content's constructor binds) can be a Kick, and the cast is undefined behaviour. `TrackSlotHost::getTrack` answers with an unbound stand-in; the Arp adapter takes the same
    pattern (a type check, else a stand-in `Arp`). Fix with step 3. Test layers use ids no product registers: see the reserved test id in `TronicProcessorTests` (use a
    value `layerTypeName` can spell, e.g. a `LayerTypeId::Test` entry reserved in the enum and never registered by a product), not a real product's id, which collides the day that
    product's type is registered.

**Rulings from the step 3 review (architecture-steward, 2026-10-20; commit 4f872e23, not yet merged).**

12. **Typed layer-preset helpers are in the right place.** `layerPresetType`, `saveTypedLayerPreset`, `loadTypedLayerPreset` and `applyStagedTypedLayer` in `ProcessorBase` follow point 8. The base `saveLayerPreset` / `loadLayerPreset` and the `stageLayerPresetTree` default being conditional on `layersAreSlots()` is accepted: the four list products override all three and are untouched. Rule: a product that overrides any of the three owns the whole path; do not mix.
13. **`presetTag` and `descriptor` on `LayerTypeInfo` are type description (library data), like `name`, `presetExtension`, `typeFromPreset`.** The look (`appColourId`, `typeTag`) stays host glue (`Persistence/<Type>LayerType.cpp`); mu-On's `TrackTypesTests` pins that. `kArpPresetTag` / `kTrackPresetTag` in `Persistence/ArpParams.h` / `TrackParams.h` are correct (a saved-format constant belongs with the param table), and both the standalone processor and the library's `LayerTypeInfo` use the one constant.
14. **`LayerDiscVisual` is `mu_ui::LayerDiscVisual`**, not `mu_core` (design-naming 3, Namespaces: UI components of `mu-core` use `mu_ui`; `mu_core` is for the layer model such as `LayerTypeInfo`). Rename in the namespace line and the two factory calls. Deleting `ArpMiniVisual` / `TrackMiniVisual` is correct.
15. **The chooser card draws the disc directly, accepted** (an Empty slot has no type or slot, so `createMiniVisual(host, slot, pulse)` cannot be called). Consequence for later: a type with a richer picture of its own needs a **slot-free card factory** on `LayerTypeInfo` (e.g. `createCardVisual(Colour)`), added with that type; until then every type's card is the disc and the sidebar picture may differ from the card.
16. **`LayerTypeId::Test` and `Test2` in the shared enum are accepted** (point 11 asked for it); they must never be registered by a product, and `layerTypeFromName` accepting them as saved names is harmless. Do not add `Test3`: a test needing a third type reuses one in a different slot.
17. **`descriptor` in `trackDescriptor` (`TrackTypes.h`) and `arpLayerTypeInfo` is consistent**: it is the type's own wording (engine data), the colour and tag plate are the host's look. The chooser card reads it from `LayerTypeInfo`.
18. **mu-On's own `.muTrack` carries an `engine` name property the generic save does not write: no problem.** The only thing that selects a type is `mu_on::typeFromPreset`, which reads the `eng` row (or the legacy `lane` property) and never `engine`; the generic save writes the `t{N}_eng` row because `Track::completeSetup` keeps it equal to the locked engine (point 5). So a mu-Tronic Kick file loads in mu-On (the row sets the engine) and a mu-On file loads in mu-Tronic. `engine` is a human-readable label only; do not add reads of it.
19. **Default-groove seeding in `createTrack` is correct** (point 9). mu-On does not call `createTrack` (its own create path and `layerAdded` seed the groove), so the standalone is unchanged.

### 2.2 The product UI stops depending on the product processor

Each `<Type>Content` takes `ProcessorBase&` plus one narrow host interface declared in the product's own `UI/` folder next to the content
(`ProcessorBase` is only forward-declared in the content's header: the layer-library rule forbids a `Plugin/` include, so the content's `.cpp` includes what it needs from `mu-core`)
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

**Stop-gap ruled 2026-10-20 (T1b step 1).** Until `LayerServices` exists, `mu_toni::sharedArpBank()` in `mu-toni/Source/Sequencer/ArpType.h` is a process-wide,
read-only factory bank built on first use (message thread). Accepted for Arp only. It is **not copied for another type**: the first type that needs a bank or a
`SampleLibrary` beyond Arp (Pattern at T1b, Rhythm later) first builds `LayerServices` here and Arp moves onto it in the same change.

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
  mu-Toni is still pre-release) and `Audio/TantScales.h` (mu-Tant, done in E4.7, 2.1.10); (2) a layer-library file **never includes anything under `Plugin/` or `License/`**, which section 1.3 and 2.2 already require;
  (3) **every relative path in a layer library is unique across mu-core and all four libraries** (new rule in design-naming §3, files). Both rules are
  enforced by a new `tests/scripts/check-layer-boundary.ps1` (run by `/build` beside `check-core-boundary.ps1`): fails on a `Plugin/`/`License/` include from a
  library file, on a duplicate relative path, and on a library file that includes another product's header.
- **Library sources and the UI list (ruled 2026-10-16, built at E2.6).** `mu-<product>-layer` is an `INTERFACE` target whose sources are the engine files (`Persistence/<Type>Params.cpp`, `Sequencer/<Type>.cpp`). The layer's UI source is **not** on the target but in the CMake list `MU_<PRODUCT>_LAYER_UI_SOURCES`, which a GUI consumer adds to its own sources: an `INTERFACE` library that carried UI sources could not be linked by a console test target, since the UI needs `juce_gui_basics` and the mu-core UI sources. The list is the family convention (every product's library does the same, each re-exporting it with `PARENT_SCOPE` so the later-added mu-Tronic sees it, 2.1.11 point 1); mu-Tronic adds the four lists to its target. If a second target (`mu-<product>-layer-ui`) proves cleaner at T1 it replaces the list in all four products at once. The library is declared in the product's own `CMakeLists.txt`, with no `mu-<product>-layer/` folder, and `check-layer-boundary.ps1` finds it from `add_library(mu-<product>-layer INTERFACE ...)` over `Audio/ Sequencer/ Modulation/ Persistence/ UI/` minus `UI/SettingsOverlay.*`.
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
`typeFromPreset(tree)`, `contentFolderName`, `modTargetTable`, `addLayerParams(layout, slot)` (called once per prefix, not once per id), `create(slot, type)` (the id lets one function make the five Track types, 2.1.11 point 6),
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
    bool canReorderLayers() const override { return true; }   // swap two slots, empty or not (T1a: false; the Published-mode swap is T1c)
    int  getNumActiveLayers() const override;                 // non-empty layers (CPU meter, controller)

    // Base API added by this work (message thread, same protocol as resetLayer):
    //   bool setLayerType(int slot, mu_core::LayerTypeId t, const juce::ValueTree* initial = nullptr);
    // builds and prepares the new layer, parks the old one in the RetireBin and publishes the slot in two view publications
    // with no suspend and no muting; resets the type's parameters of that slot to defaults (or applies `initial`) between them;
    // keeps the mixer strip (exact order: section 2.1.2).
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

**Cost check, measured 2026-10-19 (T1a, `mu-tronic/Source/Tests/TronicLayoutTests.cpp`).** A synthetic 16-slot layout of 6,000 float parameters (the top of the estimate)
builds into an `AudioProcessorValueTreeState` in about 535 to 541 ms in a Debug build on the development PC, about 12 MB of private memory; the real T1a layout is 269
parameters (16 strips x 12, plus the globals). The test holds the Debug figure to 1,000 ms; the under-a-second budget is for the whole editor opening in Release, so
**lazy per-type declaration is not needed yet**. Re-run the test when the first layer type is registered (T1b) and again when the fourth is.

**Re-run with Arp registered (T1b step 1, 2026-10-20).** The real layout is now 1,359 parameters (the 269 of T1a, the T2 bus rows, and Arp's 63 rows on each of the sixteen
slots, 1,008): the whole `PluginProcessor` builds in about 54 ms in a Debug build (measured while other suites ran on the same PC) and the synthetic 6,000-parameter layout still builds in
about 520 to 560 ms, inside the 1,000 ms Debug budget. Lazy per-type declaration is still not needed. Re-run again when the fourth family is registered.

**Re-run with Kick, Bass, Hat and Snare registered (T1b step 2c, 2026-10-20).** The four mu-On types share one `t{N}_` table of 33 rows, declared once per slot, so the real layout is
1,887 parameters (the 1,359 above and 33 more on each of the sixteen slots, 528): the whole `PluginProcessor` builds in about 84 ms in a Debug build (about 15.5 MiB private) and the synthetic
6,000-parameter layout still builds in about 530 ms. Lazy per-type declaration is still not needed. Two families are registered (mu-Toni, mu-On); re-run when Pattern (mu-Tant) and again when Rhythm (mu-Clid) is registered.

**Interim at T1a (ruled 2026-10-19).** No type is registered, so no `r{N}_ a{N}_ v{N}_ t{N}_` parameter exists; `TronicState.h` reserves the placeholder prefix `mtrn{N}_`
for the composed state (one prefix per slot, which is all `LayerLayout` holds). It never names a parameter. T1b replaces it: `LayerLayout` (mu-core, generic) takes a
list of prefixes per slot, a slot's rows are saved with the prefix of its **live** type only (the type attribute selects it on load; the dormant prefixes keep defaults and are
never written), and `setLayerType` resets the new type's prefix, not all of the slot's. The four list products keep one prefix and stay byte-identical.

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
  Loading a preset whose type differs from the slot's is refused for a filled slot and takes the file's type for an Empty one (2.1.11 point 8, 2026-10-20; supersedes "confirm, then stage").
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
| Note mode (`midiNoteMode`, held-note stack, final-mix gate ramp) | Tant | **As built (2.1.10 point 5):** the held keys and pitch track are `Pattern` state; the setting stays an app setting pushed to every Pattern and the final-mix gate stays a mu-Tant shell post-stage (A3, D7). mu-Tronic sets `Pattern::gatesOwnOutput` (ramp on the layer's own output) and saves the choice per layer in its own layer node, never in `Pattern::writeExtras` |
| Held-note stack, `ArpContext` root-by-MIDI, MIDI-trigger, `midiInChParam` | Toni | layer state |
| One `StepPattern` + `GrooveSequencer`, global swing/accent, five-engine pool | On | one `StepRow` driver and one engine per `Track`; swing/accent per layer (`t{N}_swing`, `t{N}_accent` rows of the `t{N}_` table, in the standalone too: the global rows are gone, 2.1.11 point 4); section 1.4 |
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
  them; no new storage). The default is `<displayName> <slot>` ("Slot 5" while Empty), **derived, not stored (as built at T1a, 2026-10-19)**: an empty `Layer::name`
  means "derive", the user's typed name is saved as the slot node's `name` attribute, and a changed type keeps a typed name and re-derives the rest, with no rename hook.
  T1b shows the type's `LayerTypeInfo::name` as `<displayName>`, not the saved id.
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
| **T1** | M | **Split 2026-10-19: T1a (built, uncommitted at this date) is the empty unit below; T1b registers the real types (see "What is left").** **The empty unit.** `mu-tronic/` (Standalone only, freeware) with the eight folders, 16 `EmptyLayer`s, `setLayerType` (only `Empty` registered), the shared mixer/FX/master/modulator panel/preset flow/transport, sidebar and mixer **paging**, a `.muTronic` round trip. No audio sources, no licence code. This is the brief's "empty unit". | new product; `LayerSidebar`/`MixerOverlay` page (core, ux-controller) | builds; opens; the standalone smoke (no pluginval: no plugin formats); 16 strips page; state round trip; the other four unchanged (byte-identical) |
| **T2** | S | **Buses** in the shared mixer (section 5), opt-in `MU_MAX_BUSES` / `MU_BUS_INSERTS`, then tests with a test-tone layer in the core tests; mu-Tronic turns them on (**3 buses, 2 inserts each**). Rename the `outputBus` UI label to Out. | `mu-core` mixer + overlay + params | `BusRoutingTests`, `BusSidechainTests`, `BusInsertTests`, `BusStateResetTests` (section 7); all four products byte-identical (buses 0); a scripted mu-Tronic render with test-tone layers through a two-insert bus chain |
| **T3** | S | `Layer::render`, `RenderContext`, `renderLayers`, `RetireBin`, `setLayerType` with real types, `LayerServices`, `bindParams`, `preloadStaged`, `loopBeats` (section 2.1), defaults only; mu-core tests drive a fake layer type. No product changes yet. | `mu-core` | `LayerRenderTests` (order, priority, empty skipped, lock miss silent and still advancing, retire fade then free off thread, no allocation under the lock), `LayerTypeSwapTests`; products byte-identical |
| **T4** | S | **Done as E2 (2026-10-16).** **mu-Toni layer** (cleanest; mu-Toni is pre-release, so this may go ahead now): `readVoice` + pointers + held notes into `Arp::render`; param table and `LayerParamIds.h` into `Persistence/`; `ArpHost`; rename `Audio/Scales.h` to `ToniScales.h`; `mu-toni-layer` target; **mu-Toni standalone moves onto `renderLayers`** (so the glue exists once); mu-Tronic registers `Arp`. | mu-toni, mu-tronic | `TONI_roundtrip` byte-identical; mu-Toni suites; a mu-Tronic render with two Arp layers equals two mu-Toni layers' renders (the equivalence test, section 7) |
| **T5** | S | **mu-Tant layer: the extraction is done as E4 (2.1.10); what remains for the mu-Tronic side is the shared wavetable bank service, per-layer Note mode and the `PatternHost` adapter (2.1.10 points 3 and 5).** Rename, render chain, engine, retire fade, param table and `renderLayers` adoption were E4. | mu-tant, mu-tronic | `TANT_roundtrip` byte-identical (Note-mode behaviour pinned by a new test first); mu-Tant suites; equivalence test |
| **T6** | S | **Done as E3 (2026-10-16).** **mu-On layers (mu-On is pre-release, so this may go ahead now).** One `Track` class, **five registered types Kick, Bass, Hat, Snare, Rumble** (section 1.4): per-layer step grid, swing/accent, per-layer engine allocation with the `engineLocked` flag, Rumble through `RenderContext::peer` (any priority-0 source), `TrackParams.h` to `Persistence/`, `TrackHost`, `typeFromPreset`; the standalone adopts and the five-engine pool goes (decided: per-layer in both). | mu-on, mu-tronic | `ON_roundtrip` byte-identical; the 5-lane preset still loads as 5 tracks; equivalence test per engine type |
| **T7** | S, O | **Done as E5 (2026-10-19, build 1419); it no longer waits for a release (Andy, 2026-10-15 later).** **mu-Clid layer.** `SampleRhythm` (engine, MIDI engine, play state, snapshot, retire tail, sample path), glue out of `PluginProcessor_Modulation.cpp`, `PresetIO` shapes onto the `Layer` hooks, `SampleLibrary` as a service, sample I/O in `preloadStaged`, `RhythmHost`. **The standalone mu-Clid adopts it** (the glue must not exist twice); its `HotSwapStager` is either kept for the standalone only (recommended until the swap timing tests prove equal timing) or replaced. Highest risk of the plan (Stage 7 of Layer ownership is the precedent and its proof list applies in full). | mu-clid, mu-tronic | `CLID_roundtrip`, `swap`, `slot swap`, retire-tail renders byte-identical; `SwapCommitTimingTests`; `mu-clid-midi-tests` (shared `Rhythm`); equivalence test |
| **T8a** | S | Cross-type features that need only the types already extracted (**can go ahead after T4 and T6**): type-carrying clips, **scenes** (`launchScene`, `LaunchScene`, section 3.6), performance files for `MuTronicState`, per-layer MIDI in, **controller pages** (`PagePrev` / `PageNext`, Launchpad top-row 5 and 6) in `mu-control`, mu-link bridge for the standalone (the hook is one line), CPU meter on the transport bar. | mu-core, mu-control, mu-tronic | tests of section 7 |
| **T8b** | M | Importers for the products extracted later: `.muTant` and `.muClid` full presets (each lands with T5 / T7; `.muToni` and `.muOn` importers land with T8a). | mu-tronic | `TronicStateTests` import cases |
| **T9** | O | Release stages: installer, manual, site page (listed as freeware), no licence work. **Launch only when all four engines are ready** (Andy, 2026-10-15); the earlier "ship before T5 and T7" option is removed. | | |

**Real order (rewritten 2026-10-19, extraction closed at E6; supersedes the earlier "T4 and T6 now, T5 and T7 after the releases" plan).** The stages above are the
design's units. What was run is [extraction-plan.md](extraction-plan.md)'s order, and the T-numbers map onto it as below. T4 to T7 are numbered by glue size, not run order.

| Run order | Extraction stage | Design stages it delivered |
|---|---|---|
| 1 | **E0** safety net: golden renders, `check-layer-boundary.ps1`, seed and design fixes | the proof machinery for every later stage |
| 2 | **E1** shared contract (`mu-core`, additive) | T0 (`MU_MAX_LAYERS`, limits, `mu-core-tests-16`, the checker) and T3 (`Layer::render`, `RenderContext`, `renderLayers`, `RetireBin`, `LayerAccess`) |
| 3 | **E2** mu-Toni (pathfinder) | T4 |
| 4 | **E3** mu-On | T6 |
| 5 | **E4** mu-Tant (first live product) | T5, less the mu-Tronic-side wavetable bank service, per-layer Note mode and `PatternHost` adapter (2.1.10 points 3 and 5) |
| 6 | **E5** mu-Clid (riskiest, last; E5.1 to E5.7b, build 1419) | T7, with the mu-Clid standalone adopting `renderAndMix` and `HotSwapStager` kept for the standalone only |
| 7 | **E6** close-out: `mu-layer-link-test`, checker with all four libraries, docs | the guard that T1 can start |

Both live products were extracted before any mu-Tronic product work, on Andy's ruling of 2026-10-15 (later); the earlier plan to wait for their next releases was dropped.
Each extraction kept the standalone's behaviour identical: `*_roundtrip` byte-identical or a signed golden update.

**T0 and T1a as built (2026-10-19).** T0: the limits, `LayerTypeId`/`LayerTypeInfo`, `check-layer-boundary.ps1` (E1), the 16-slot cost check (3.4). T1a, the empty unit: `mu-tronic/`
(Standalone only, freeware, `MU_MAX_LAYERS=16` for its targets, eight Source folders, empty ones kept with `.gitkeep`) with sixteen `EmptyLayer`s and `mu_core::LayerTypeRegistry`
(`mu-core/Sequencer/`, empty in this build, filled by `Persistence/LayerTypes.cpp`); in `mu-core`, generic: `ProcessorBase::layersAreSlots`, `getLayerTypes`, `setLayerType` (the
non-muting protocol of 2.1.2, Published mode only), `deleteLayer`, `publishLayers`, a state restore that keeps a slot host's count, sidebar and mixer **paging**, the empty-slot look,
`MuTheme::apps.ice`, and the strip sidechain-source range sized by `kMaxLayers` (a latent 16-layer defect, identical at 8). `.muTronic` round-trips the type by name; an absent
`<Slot>` is Empty. Rule 10 of `check-layer-boundary.ps1`: mu-Tronic names a layer type's header only under `Persistence/` and `Tests/`. **Deliberately not in T1a, and where each lands:**
`swapLayers`/reorder (`canReorderLayers` is false until the Published-mode quiesce exists: T1c, before release); the mixer-follower reset after a type change (C17: T1b, before the
first type is registered); per-type prefixes and the multi-prefix layout (T1b); mu-link bridge, controller pages, scenes, CPU meter (T8a); icon, installer, manual, site (T9).

**What is left, in order.** (1) The small owed items before T1b: one shared `mu_core::HeldNoteStack` (done: mu-Toni and mu-Tant share it), the mu-Tant wavetable bank service
and the `PatternHost` adapter (both only before the Pattern type is registered). (2) **T1b** register real types, one at a time (Arp first, the cleanest, then the five Track types, then Pattern, then Rhythm): link the layer library and
its `MU_<P>_LAYER_UI_SOURCES`, one line in `registerLayerTypes`, multi-prefix `LayerLayout` and the live-type-only save (3.4), C17 follower reset, the type's `addLayerParams` in the layout,
`getLayerPresetDir`/`slotPresetFiles` from the slot's type (not a mu-Tronic `Layers` folder; done at 2a), **layer-preset load and save in a slot (step 3, 2.1.11 point 8; before Pattern)**, a `TronicProcessorTests` case and an equivalence test per type, the cost test re-run;
the `dsp-controller` reviews the C17 and multi-prefix changes. (3) **T1c** `swapLayers` on a Published host. (4) **T2** buses (`mu-core` mixer; it edits audio code every product renders
through, so it follows E5, which has now closed). (5) **T8a** scenes, controller pages, per-layer MIDI in, type-carrying clips; **T8b** importers for `.muTant` and `.muClid`
(`.muToni` and `.muOn` with T8a). (6) **T9** release: only with all four engines ready, freeware, no licence work. The cost measurement and the sidebar/mixer paging, T1's first
checks, are done at T1a.

Order rationale, kept: T1 is small and safe; buses are pure `mu-core` and testable with synthetic sources; the contract (T3) was proven on a fake type before any product was
guessed from one; then products by ascending risk (Toni, On, then the live pair Tant and Clid last, the reversal of the Stage 7 engine ruling). The equivalence test in
each product stage is the proof that "completely self contained" holds, and `mu-layer-link-test` is the proof that the four libraries coexist in one binary.

### What each product must change (summary)

- **mu-Toni** (done 2026-10-16, Stage E2; 2.1.8): `Arp::render`, held notes per layer, `Persistence/Toni*Params`, `ArpHost`, `ToniScales.h`, `mu-toni-layer`, adopt `renderLayers`.
- **mu-On** (done, E3): per-layer step grid and swing/accent, one engine allocation per Track (`engineLocked` in mu-Tronic), five registered engine types, Rumble via the context, `TrackParams.h` to `Persistence/`, `TrackHost`, `mu-on-layer`, adopt `renderLayers`.
- **mu-Tant** (E4, built, 2.1.10): `Pattern::render` + engine binding + cached pointers, `Persistence/PatternParams`, `PatternHost`, `TantScales.h`, `mu-tant-layer`, adopt `renderLayers`;
  `SwapBoundary.h` stays in `Plugin/`, Note mode per layer is mu-Tronic's (point 5), the shared wavetable bank service stays owed (2.4).
- **mu-Clid** (done, E5): `SampleRhythm`, glue out of the processor, `PresetIO` onto the hooks, `SampleLibrary` service, `RhythmHost`, `mu-clid-layer`, adopt `renderLayers`.
- **mu-Clid MIDI**: nothing (core `Rhythm`).
- **All**: no `Plugin/`/`License/` include from library files; unique relative paths; param table as `addLayerParams`; every live-count store through
  `storeLayerCount`, the count policy set in the constructor, the P7 oversize golden re-recorded at adoption (2.1.7).

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

mu-link today is the **performance hub**: it owns the hardware output, sums the standalones' audio rings, is the master clock, offers Scenes, MIDI-clock out. (An Ableton Link peer was dropped 2026-10-20.) mu-Tronic gives the same rig in one process: one transport (already sample-accurate in-process), a mixer with buses and the shared
FX, a performance file and scenes (section 3.6; the `Performance` level exists), hardware output owned by the standalone, and the controller work in `mu-control`.
Andy's view (2026-10-15): "a new performance framework that might make mu-link redundant".

**Ruling (Andy, 2026-10-15: "leave it as is for now"): coexist, revisit later.** mu-link is **not frozen and not retired**; its stages continue as
planned. Not "replace" because mu-link still does what mu-Tronic cannot: sum *separate processes* (another mu product instance) into one output with one clock.
Not "absorb" because that would copy mu-link's server, IPC and registry into a product that does not need them.

- mu-Tronic **is a mu-link client like the other standalones** (the bridge is one line in `StandaloneApp.cpp`), so the two can run together; when attached it
  slaves to the mu-link clock via `resolveTransport`.
- **No duplicated clock/MIDI code.** MIDI clock out, the tempo PLL and the transport sources already live in `mu-core`
  (`TimedMidiOut`, `MidiClockTempo`, `TransportResolver`); mu-Tronic and mu-link both use them. Anything mu-link would still need to build for performance
  use (Scenes) is built in `mu-core` or the shell where it is generic, so the second product that wants it does not copy it. This is the
  standing one-framework rule, not a freeze: **no feature freeze on mu-link**.
- **Revisit later.** When mu-Tronic's T8a is done, Andy may judge whether mu-link is redundant (the earlier draft's test: hardware output ownership, MIDI-clock out,
  scenes, controller workflow). Nothing is scheduled and no decision is needed before then.

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
   Before that only additive `mu-core` stages (T0 to T3) reach them, byte-identical or reverted; the release-cut rule is moot now that both live products are extracted. None of the stages
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
