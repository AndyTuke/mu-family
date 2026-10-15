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
This section is the specification of record for E1. Subsections 2.1.2 to 2.1.5 are the four design fixes C1 to C4 of the real-time review
([extraction-plan.md 2.1a](extraction-plan.md#21a-contract-requirements-from-the-real-time-review-c1-to-c18), written 2026-10-15 as stage E0d); C5 to C18
stay in that table and bind this section too. **Status: specified 2026-10-15; `dsp-controller` sign-off on C1 to C4 requested; no contract code before it.**

New hooks (defaults do nothing; all called once per layer per block or per edit, never per sample). `Layer` gains a second base, the interface
`mu_core::RetireTail` (2.1.3), so the bin can hold either a whole layer or a product's engine tail:

| Hook | Thread | Purpose |
|---|---|---|
| `int typeId() const` | any | The type's id (a `mu_core::LayerTypeId`, section 3.3). |
| `void prepare(double sr, int blockSize)` | message; the layer is not reachable by the audio thread (not yet published, or the host is not processing) | Allocate everything (engine, buffers, insert). |
| `void bindParams(juce::AudioProcessorValueTreeState&, int slot)` | message, before publication and again on `onMoved` (C9) | Cache the slot's parameter atomics (the products' `voicePtrs` / `vp` / `modDestAtoms` tables move here). `render` never calls `getRawParameterValue`. |
| `void render(const RenderContext&, juce::AudioBuffer<float>& out, int n)` | audio | **Overwrite** frames `[0, n)` of `out` (two channels) with the layer's block, insert included (the former `renderVoice` / `renderChannelCb` / `GrooveVoices::renderAll`). No allocation, no lock beyond the existing in-layer spin locks, no APVTS by string, no host call (2.1.5). |
| `int renderPriority() const` | any | 0 normal, 1 after every priority-0 layer (mu-On Rumble reads a peer). |
| `double loopBeats() const` | audio | Loop length in beats for the hot-swap boundary (replaces `swapLoopBeats`, `blkPatternBeats`); cached per slot by the base each rendered block. |
| `void preloadStaged(const juce::ValueTree&)` | message | File I/O for a staged tree (sample, user wavetable) so the boundary commit does none. |
| `void onTransportStopped(const RenderContext&)` | audio | The play-to-stop edge (2.1.5): silence held notes and tails (Tant gates, On voices, Toni note-off). |
| `void onTransportJump(const RenderContext&)` | audio | A discontinuity while playing (locate, host loop, clock snap): release or re-anchor pending notes (2.1.5). |
| `RetirePolicy retirePolicy() const` | any | How this layer leaves: `None` (dropped after the grace period, mu-Toni and mu-On today), `Fade`, `Natural` (2.1.3). Replaces the earlier `tailBlocks()`: block counts depend on the block size. |
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
    captureSidechainAndClear(buffer);                      // unchanged
    queueMidiProgramChanges(midi);                         // router claims leave `midi` here
    const auto t = resolveBlockTransport(midi, buffer.getNumSamples(), kBeatWrap);   // base wrapper of resolveTransport (2.1.5); mu-Clid keeps its Note-mode source
    renderAndMix(buffer, midi, t, outs);                   // base: LayerBlock, MIDI decode, chunk loop { renderLayers, processCoreBlock }; outs = mu-Clid's direct outs, else {}
    // product shell post-stage, if any: mu-Tant's Note-mode final-mix gate (decision A3), mu-Clid's sample preview
    flagLayerSwapBoundaries(t);                            // per-slot loopBeats cached by renderLayers; runs on a miss too
}
```

`renderAndMix` (base, audio thread): (1) constructs a `LayerBlock` (2.1.2), which owns `juce::ScopedNoDenormals` for the whole render, bin and mixer (C11:
held by the block object rather than by `renderLayers` alone, so the mixer and FX are covered too); a failed `LayerBlock` renders nothing, exactly as a
missed try-lock today, while the transport (already resolved) and the boundary flagging (cached loop lengths) still advance. (2) Decodes `midi` once into the
block's `MidiEvent` array (2.1.5). (3) **Chunks** a block longer than the prepared size into pieces of at most the prepared size and runs `renderLayers` then
`processCoreBlock` per piece on a sub-buffer of `buffer` (C8; the chunking sits here and not inside `renderLayers` because `MixerEngine`'s channel buffers are
sized at prepare too). (4) A host block of 0 frames renders nothing and calls no hook; its edges are delivered on the next rendered block.

`renderLayers(const LayerBlock&, const BlockTransport&, chunk)` (base, audio thread), in this order:

| Phase | What | Result |
|---|---|---|
| 0 (first chunk) | Edges against the last *rendered* block (2.1.5); `onTransportStopped` / `onTransportJump` on every live layer in slot order. `mixCount = max(view.numLive, bin.highestActiveSlot(view.serial) + 1)`, taken **before** any tail renders (mu-Tant's `renderCount` rule). | base `RenderContext` template |
| 1 | Priority-0 live layers in slot order: `ctx.slot = s`, `layer->render(ctx, scratch[s], n)`. An Empty layer is not called; its scratch is cleared. | `scratch[s]` |
| 2 | Peer table: `peers[s] = { scratch[s] pointers, valid = 1 }` for each slot rendered in phase 1; every other entry `valid = 0`. | `PeerTap[kMaxLayers]` |
| 3 | Priority-1 live layers, with `ctx.peers` set (C10: post-insert, pre-strip, and before any tail is added). | `scratch[s]` |
| 4 | The bin (2.1.3), slot order then entry order: tails add into `scratch[s]` after the live layer, as mu-Clid's retired engines add after the live engine today. | `scratch[s]` |
| 5 (last chunk) | Cache `loopBeats()` per live slot; latch the edge state; publish nothing else. | caches |

`processCoreBlock` gets a `RenderChannelFn` that copies `scratch[r]` into the mixer's channel buffer (an exact copy, so R11 holds) for `r < mixCount`. Scratch
buffers (`kMaxLayers` x 2 channels x prepared size) and the decoded MIDI array are allocated in `prepareToPlay` / at construction, never grown.

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
    explicit operator bool() const noexcept;               // false: render nothing this block
    const LayerView& view() const noexcept;
private:
    juce::ScopedNoDenormals noDenormals;                   // C11
    // TryLock: juce::ScopedTryLock + a local LayerView; Published: const LayerView* loaded once (seq_cst)
};

// mu-core/Plugin/AudioEpoch.h: the grace period, single audio-thread writer (processBlock is never re-entered)
class AudioEpoch
{
public:
    void     enter() noexcept;                       // audio: e+1 (odd = inside a block), seq_cst; jassert(e was even)
    void     leave() noexcept;                       // audio: e+1 (even), seq_cst
    uint64_t stamp() const noexcept;                 // any: e, seq_cst
    bool     passed(uint64_t s) const noexcept;      // (s even) || (e != s): no block that could hold an old pointer is still running
    void     waitFor(uint64_t s) const noexcept;     // message thread: yield until passed(s); bounded by one audio block
};
```

The `LayerView` storage is a pool of `kLayerViewPool` (4) views owned by `ProcessorBase`; `published` is a `std::atomic<const LayerView*>`. A view is reused only after
`epoch.passed(stamp)` for the stamp taken when it was replaced; if all four are in their grace period the writer calls `waitFor` (at most one block). No
reference counting anywhere (C6): views, layers and tails are owned by `unique_ptr`s on the message thread and reached by raw pointer on the audio thread.

**What the audio thread loads and never retains.** It loads the view once, in the `LayerBlock` constructor. `Layer*`, `LayerView*`, `RetireTail*` and scratch
pointers are used only inside that block and never stored in a member that outlives it. State that survives blocks (edge latch, loop-length cache, peer
validity) holds values, never addresses.

**Memory orders and the proof.** Writer (message thread): `published.store(next, seq_cst)`, then `s = epoch.stamp()` (seq_cst); `prev` is reclaimable once
`epoch.passed(s)`. Reader: `epoch.enter()` (seq_cst store), then `published.load(seq_cst)`, use, `epoch.leave()`. In the single total order of seq_cst operations,
if `s` is even no block was running at the stamp, so every later block's load comes after the store and sees `next`; if `s` is odd the running block may hold
`prev`, and `e != s` proves it ended. This is the Dekker (store, then load of the other variable) pattern, which is why these two variables are seq_cst rather than
release/acquire; the cost is two seq_cst stores per block. The view's fields are plain data published by the pointer store (a seq_cst store is also a release).

**Who builds, prepares, binds and defaults a new layer, in what order** (`setLayerType`, mu-Tronic, T1; message thread throughout; nothing suspended, nothing
muted):

| # | Step | Why here |
|---|---|---|
| 1 | Cancel the slot's staged swaps (as every edit does). | A staged payload of the old type must not land on the new one. |
| 2 | `fresh = info.create(slot)`; `fresh->prepare(sr, bs)`. | All allocation before anything is visible. |
| 3 | Tear down the slot's `<Type>Content` and mini-visual (C16). | No UI pointer into the old layer survives into the bin. |
| 4 | `old->beginRetire()` when `old->retirePolicy().kind != None` (2.1.4). | The last moment the old layer may read its parameters: none has changed yet. |
| 5 | Publish view **V1** with the slot set to null (Empty) and `bin.park(slot, std::move(old), V1.serial, sr)` (or `None`: the old layer goes to the reclaim list). The bin entry is written before V1 is published (2.1.3, activation serial). | From V1 on, the old layer renders only as a tail. |
| 6 | `epoch.waitFor(stampAfterV1)` (at most one block). | No block that still renders the old layer **live** (it reads the slot's parameters) can overlap step 7. |
| 7 | Reset the new type's prefix of this slot to defaults, or apply `initial` (`applyLayerNode`), through the APVTS as any edit does; `fresh->bindParams(apvts, slot)`; the mixer strip `ch{N}_` is kept; request the follower reset for the strip (an atomic flag the audio thread consumes at block start, C17). | Nothing live reads these parameters now: the old layer is a tail reading its snapshot, the new one is not published. Same-prefix types (Kick to Bass, both `t{N}_`) are the reason. |
| 8 | Publish view **V2** with `fresh` in the slot. | One atomic pointer store; the new layer's first block is the first block that loads V2. Parameter values are never moved after it. |
| 9 | V0 and V1 return to the pool after their grace; the old layer is freed by the bin (2.1.3) or the reclaim list, on the message thread. | |

Two publications are always made, also when the prefixes differ, so there is one code path; the new layer starts one block later than the old one stops,
which is inaudible (it starts at the beat of its first block, 2.1.5). A scene commit (C7, T8a) uses the same pieces: publish a view with `frozenMask` set for the
scene's slots, wait one grace period, apply every layer's tree, publish with the mask clear, so every layer of the scene switches in the same block and none is
muted. The boundary-exact timing of that switch (today's commit lands up to one poll interval after the boundary) is T8a's to design on top of this.

**What remains under `layersLock`.** In `TryLock` mode, everything it guards today, with today's hold (pointer moves, strip moves, microseconds; the lock
probe of E0.8 measures it). In `Published` mode it becomes the **writers' mutex only**: structural edits, `setLayerType`, `prepareToPlay` /
`releaseResources` (which may run on a host thread other than the message thread) and the bin's message-side operations take it; the audio thread never does.
Its hold is the writer's own section; no writer holds it across a grace wait.

**Coexistence with `ScopedSuspend` structural edits** (`addLayer`, `removeLayer`, `swapLayers`, `resetLayer`, unchanged in what they do and in what they sound
like). They keep `ScopedSuspend` (`layerEditInFlight` raised, processing suspended) because they move parameter values and edit layers in place
(`adoptDefaults`), and a block between those moves must play nothing, as today. In `Published` mode their exclusion against the audio thread becomes
**quiesce**: raise the flag (seq_cst), `epoch.waitFor(stamp)` (the block that may have started before the flag ends), then do every move. Until the last product has
switched mode, every edit does both: quiesce, then `ScopedLock(layersLock)` around the moves, then publish a view, then the parameter phase, then lower the flag.
Both mechanisms are then safe in one binary, whichever mode a processor runs.

**Why it stays bit-identical for the four products.** They never call `setLayerType` and stay in `TryLock` mode through the programme, so their audio thread runs
today's lock protocol. The golden harness renders single-threaded (edits between blocks), where both modes give the same blocks. A later switch to `Published`
changes only which blocks are silent under contention with a non-edit holder of `layersLock` (fewer), which no golden contains.

**Invariants a test can assert.** (1) A block renders the old layer live, or as a tail, never both and never neither, across V0 to V1 to V2 (a recording fake layer
asserts the sequence per block, also when V1 and V2 are published inside one block). (2) A type change in one slot leaves every other slot's output bit-identical
to a run without it (the "does not mute" test). (3) No allocation or free on the audio thread during a publication (`AllocationProbe`). (4) A view, a layer or
a tail is freed only after its grace period (ASan two-thread stress: the message thread hammering `setLayerType` and structural edits while blocks run).
(5) In `Published` mode the audio thread never takes `layersLock` (lock probe). (6) No block runs while `layerEditInFlight` is up and strip state moves
(stress, ThreadSanitizer). (7) Every golden of the four products unchanged at each of these commits.

**Failure modes considered.** Pool reuse while a block still reads a view (ABA): solved by the grace period before reuse. The audio thread keeping a pointer
across blocks: forbidden by construction (stack object), caught by ASan in the stress test. Writer starvation: there is always a gap between blocks, and
`passed` also succeeds on any change of the counter, so a busy audio thread cannot starve it. Offline render on the message thread: the writer is never inside a
block, the stamp is even, reclamation is immediate. A host that calls `processBlock` from two threads at once: outside the JUCE contract; `enter()` asserts it.
`prepareToPlay` on a host thread during an edit: the writers' mutex. Destruction: `prepareForDestruction` empties the bin and the reclaim list before any product member goes.

**Rejected.** (a) Keeping the try-lock and holding `layersLock` briefly for a type change: a miss silences every layer for a block, which C1 forbids.
(b) `std::shared_ptr` views (or `std::atomic<std::shared_ptr>`): the last release can land on the audio thread (C6) and the atomic form may lock. (c) Hazard
pointers or general RCU: one reader makes a counter enough. (d) One atomic `Layer*` per slot: several slots (a scene, a reorder) could land in different blocks and
the count and the bin could not be coupled to the same block. (e) An audio-to-message acknowledgement per publication: the epoch is the same proof with no
per-publication message. (f) Running type changes through `ScopedSuspend`: it mutes everything.

#### 2.1.3 C2: the `RetireBin`

**Ruling: per-slot rows of `kRetireDepth` (4) entries, owned by `ProcessorBase`; each entry runs the four-state machine below; the message thread frees only
after it saw Done and the grace period passed.** Per-slot rows rather than one global ring because a tail renders into its own slot's strip (level, sends,
sidechain), a structural edit moves tails with their slot (mu-Clid's `layersReordered` does today), and mu-Clid's eviction is per slot. Depth 4 is mu-Clid's
`kMaxRetiredVoiceEngines`, so two retargets inside 25 ms never drop, free early or block. A global budget, `kMaxLiveRetirees`, defaults to `kMaxLayers *
kRetireDepth` (never binding in the four products); mu-Tronic sets it from the C18 measurements, and when it binds the oldest draining entry in the instance is
evicted.

```cpp
// mu-core/Sequencer/RetireBin.h  (namespace mu_core)
struct RetirePolicy
{
    enum class Kind : uint8_t { None, Fade, Natural };
    Kind  kind       = Kind::None;   // None: not parked; freed after the grace period (mu-Toni, mu-On today)
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

class RetireBin
{
public:
    enum State : uint8_t { Empty = 0, Parked, Draining, Done };

    // Message thread, under the writers' mutex (Published mode) or between blocks (TryLock mode).
    void park       (int slot, std::unique_ptr<RetireTail> owned, uint32_t activationSerial, double sampleRate);  // owned
    void parkInPlace(int slot, RetireTail& spare,                 uint32_t activationSerial, double sampleRate);  // borrowed spare (mu-Tant)
    void cutSlot (int slot);                 // every entry of the slot to Done: a remove, or a live layer published into an in-place slot
    void swapRows(int a, int b);  void removeRow(int index);   // structural edits, quiesced: tails follow their slot; the removed row is cut
    void cutAll  ();                         // prepareToPlay, releaseResources, prepareForDestruction
    int  service (const AudioEpoch&);        // free every Done entry whose grace passed (runDeferredWork and the 100 Hz poll)

    // Audio thread, inside a LayerBlock.
    int  highestActiveSlot(uint32_t viewSerial) const noexcept;
    void render(int slot, RenderContext ctx, juce::AudioBuffer<float>& slotOut, int n,
                uint32_t viewSerial, bool slotHasLive) noexcept;   // ctx.retiring = 1, empty MIDI, no peers

private:
    struct Entry
    {
        std::atomic<uint8_t> state { Empty };
        uint32_t     activation = 0;         // written while Empty, published by the Parked store
        RetireTail*  tail = nullptr;         // ditto
        RetireState  rs {};                  // ditto
        uint64_t     doneStamp = 0;          // message thread: epoch stamp taken when it first saw Done (0 = not yet)
        uint64_t     parkSeq = 0;            // message thread: park order (global-budget eviction)
        std::unique_ptr<RetireTail> owned;   // message thread only; null for an in-place entry
    };
    std::array<std::array<Entry, mu_limits::kRetireDepth>, mu_limits::kMaxLayers> rows;
};
```

**State machine (exact transitions).** Every operation on `state` is seq_cst (a store is also a release, a load also an acquire), for the grace proof of
2.1.2; the payload fields are plain and are published by the store that leaves Empty.

| From | To | Thread | Operation | When |
|---|---|---|---|---|
| Empty | Parked | message | write `tail`, `rs`, `activation`, `owned`; then `state.store(Parked)` | `park`, `parkInPlace` |
| Parked | Draining | audio | `compare_exchange(Parked, Draining)`; on success `onRetireStart(ctx)`, `rs.started = 1` | first block whose `view.serial >= activation` |
| Parked | Done | message | `compare_exchange(Parked, Done)` | cut before the audio thread started it |
| Draining | Done | audio | `compare_exchange(Draining, Done)`, then `requestDeferredWork()` | `renderRetiring` returned false; `Fade` ramp spent (`samplesLeft == 0`); `samplesLived >= maxSamples` |
| Draining | Done | message | `compare_exchange(Draining, Done)` | eviction, `cutSlot`, `removeRow`, `cutAll` |
| Done | Empty | message | first sight: `doneStamp = epoch.stamp()`; when `epoch.passed(doneStamp)`: `owned.reset()`, `tail = nullptr`, `state.store(Empty)` | `service` |

The audio thread never touches an Empty or Done entry and never writes `tail` or `owned`; the message thread never writes `rs` or `tail` of a non-Empty entry. A
failed audio compare-exchange means the message thread cut the entry: the audio thread skips it. **The epoch rule proves the audio thread has left the object:**
every audio access to `tail` happens inside a block, after a load that read Parked or Draining in that block; the free waits for a grace period stamped after
Done was observed, so the block that may still be inside `renderRetiring` has ended, and every later block reads Done (seq_cst order: the Done write precedes the
stamp, the stamp precedes the next block's `enter()`, which precedes its load).

**Same-block hand-over (activation serial).** An entry renders only in blocks whose view serial is at least its `activation`, and it is parked before that
view is published. So the block that still sees the old view renders the old layer live and skips the entry; the first block that sees the new view renders
it as a tail; there is no block with both and none with neither (C1 invariant 1). In `TryLock` mode the serial is the packed layout word that
`ProcessorCore` stores with every count change (`numLayers` stays as its mirror for existing readers), so mu-Tant's count store and its tail arm stay coupled
as they are today.

**Depth and overflow.** Placement is into the first Empty entry of the row. A full row evicts **entry 0** (mu-Clid's force-cut rule, kept for bit identity): the
message thread cuts it, `waitFor`s its grace (one block at most), frees it and reuses the entry. The evicted tail stops without a fade, as today; a declick on
eviction was considered and rejected for now (it would change mu-Clid's spam-swap render), to revisit at T1.

**Fade and lifetime.** The bin's ramp is a service the tail applies itself, at the point in its chain where today's product applies it (mu-Tant fades the engine
*before* its insert): `mu_core::applyLinearFade(buf, n, rs)` uses exactly mu-Tant's expression, and the bin decrements `samplesLeft` by `n` after the call.
Lengths are in seconds and are converted with the rate prepared when the entry is parked; a bin never crosses a prepare (below), so nothing needs rescaling.
`maxSeconds` is clamped to `kRetireHardCapSeconds` (proposed 30 s); in an entry's last 10 ms of life the bin renders it into its own prepared scratch and applies a
10 ms linear declick before adding it, then sets Done. No product's tail reaches that window in the golden set (mu-Tant 25 ms; mu-Clid's longest tail to be
checked, open point 7).

**Who frees, on which thread.** Only `service()` frees, on the message thread (the 100 Hz poll, `runDeferredWork`, `flushPendingAsyncUpdates` for headless
renders), outside every lock except the writers' mutex, which the free does not need. The audio thread's Done requests deferred work so the free is prompt.
**Flush:** `prepareToPlay` and `releaseResources` call `cutAll()` then `service()` (the host is not processing, the stamp is even, everything is freed at once);
`prepareForDestruction()` does the same before any product member is destroyed.

**Service lifetime.** The wavetable bank and the sample library outlive every layer and every tail: in the four products they are product members, and every
product destructor calls `prepareForDestruction()` first, which empties the bin; mu-Tronic's `LayerServices` (T1) are `ProcessorBase` members declared before
the bin, so they are destroyed after it. A tail reads a service only as its live layer does (until C5, under the same lock discipline). No tail, view or
layer is reference counted (C6).

**How each product maps onto it (bit identity).**

| Product | Today | In the bin |
|---|---|---|
| mu-Tant (E4.6) | count-reducing full preset: `retiring[v]` fades the slot's own engine and insert for 25 ms with a message-thread snapshot, into channel `v` past the live count | `parkInPlace(v, pattern, ...)`: the spare `Pattern` stays in `layers[v]` and keeps its engine; `Fade`, 25 ms; `renderRetiring` writes (`exclusive`) as `renderRetiringVoice` does; `mixCount` covers the slot. A layer published live into that slot cuts the entry first. |
| mu-Clid (E5.4d) | hot-swap retires the slot's `VoiceEngine` (not the `Rhythm`) into 4 per-slot entries; `markRetired` under suspend; drained by `isFullyDrained`; overflow force-cuts entry 0 | `park(r, std::make_unique<EngineTail>(std::move(oldEngine)), ...)` with `mu_clid::EngineTail : RetireTail` in the mu-Clid library; `Natural`; `markRetired` becomes `onRetireStart`; tail adds after the live engine; rows move in `layersReordered`. |
| mu-Toni, mu-On | a removed or replaced layer is reset or cut | `None`: not parked, freed after the grace period. |
| mu-Tronic (T1) | | type change and remove park the whole layer (`park(slot, std::move(layer))`). |

**Invariants a test can assert** (`RetireBinTests`, E1): the transition table above checked exhaustively by a deterministic two-actor scheduler (every
interleaving of park, cut, service against block start, render, finish); N-deep: four tails in one slot, a fifth evicts entry 0; two retargets inside 25 ms
both sound to their end; nothing is freed before Done plus grace (fake epoch); `cutAll` at prepare and release frees everything; the hard lifetime ends a tail
that never reports silence, with the declick; the ramp length at 44.1, 48, 96 and 192 kHz equals mu-Tant's `retireRampSamples()`; no allocation or free on the
audio thread; the activation serial gives the same-block hand-over; destruction order (a fake service asserts no tail is alive when it dies); an in-place
entry is cut when its slot goes live.

**Failure modes considered.** Free while the audio thread is inside `renderRetiring` (the grace period); a tail started after it was cut (the audio
compare-exchange); a tail rendered both live and as a tail (activation serial); eviction of an entry the audio thread is reading (cut, grace, then reuse); a
prepare changing the rate under a ramp (flush); a tail outliving the bank (destruction order); an endless comb or reverb tail (hard lifetime); 16 slots of tails
(global budget, C18 numbers); `-0.0` versus `+0.0` in stems (tails add straight into the slot scratch, as today; the private scratch is used only in the declick window).

**Rejected.** A global ring of 16 to 32 (slot numbers would need renumbering on every edit, and mu-Clid's per-slot eviction could not be kept); a fixed gain
ramp applied by the bin after the tail (it would move mu-Tant's fade after its insert and change the sound); fade lengths in blocks (block-size dependent);
owning only whole layers (mu-Clid retires an engine, not its `Rhythm`; mu-Tant keeps the spare's engine); freeing on the audio thread or under `layersLock`.

#### 2.1.4 C3: frozen retire snapshots

**Ruling.** A tail reads only (a) its own object's DSP state, (b) its declared snapshot, (c) the transport fields of the `RenderContext` it is given, and
(d) a service through the same discipline as its live layer. It never reads: any APVTS atomic (bound or by id), its slot's or its own `ModulationMatrix`,
`ControlSequence`s, `modLock`- or `voiceParamsLock`-guarded data, a peer (`ctx.peer()` returns null), the MIDI input (`ctx.midi` is empty), the `LayerView`, another layer, or
`ProcessorBase`. Same-prefix types are the case that makes this necessary: a retired Kick and a new Bass in one slot share `t{N}_`.

**`beginRetire` contract (message thread).** The host calls it **before** any parameter of the slot changes and **before** the layer leaves the view (step 4 of
2.1.2; mu-Tant's `applyFullPresetTree` already arms its tail before `applyComposedState`). It is the last time the layer may read its bound parameters. It copies
into the layer's snapshot everything `renderRetiring` needs that another thread may change afterwards: base parameter values (mu-Tant: `readConfig(v)`, insert
algorithm and its four parameters, exactly today's `RetiringVoice` fields), sample paths, mode switches. It must not allocate if it can be avoided (the snapshot is a
member sized at construction). Its peer on the audio thread, **`onRetireStart(ctx)`**, runs once at the first tail block: it freezes audio-owned state
(mu-Clid's `markRetired`: envelope note-offs and the drain budget), copies last-resolved values when the type's tail wants modulated values rather than base
ones, and emits owed MIDI note-offs into `ctx.midiOut` so a tail never holds a MIDI note. From then on the layer runs on what it froze.

**How a layer declares its snapshot so the test can prove it.**

```cpp
// mu-core/Sequencer/RetireBin.h
template <class Snapshot>
struct RetireSnapshot                    // a layer type with a tail holds one of these as a member
{
    static_assert(std::is_trivially_copyable_v<Snapshot>, "a retire snapshot is plain data, with no pointer into the APVTS");
    Snapshot value {};
};

// e.g. mu-Tant:  struct PatternRetire { VoiceConfig config; int insAlgo; std::array<float, 4> insP; };
//                RetireSnapshot<PatternRetire> retireSnap;   // written only by beginRetire, read only by renderRetiring
```

The proof is behavioural and generic, so it needs nothing from the type but its registration: **`mu_core::testing::checkRetireIsolation(info)`** builds two
identical instances of the type in slot 0 of a test processor, renders the same blocks, calls `beginRetire` on both and parks both, then on **one** of them
scrambles every parameter under the slot's prefix (legal random values), replaces its modulation matrix and control sequences, and feeds garbage peers; the two
tails must be bit-identical for their whole life. A second case puts a same-prefix type in the slot while the first rings (the E1 fake pair; mu-On Kick to Bass
in E3). In Debug and test builds the `bindParams` cache is a `mu_core::ParamBinding` whose `load()` asserts that `onRetireStart` has not run, so an accidental
read is a jassert in the first test that hits it, not a silent mix of two layers.

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
};

struct MidiSink                  // the layer's MIDI output (mu-Clid MIDI mode, E5); events == nullptr: none
{
    MidiEvent* events;
    int32_t    capacity;
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
    double   startBeat;          // song beat at this call's first frame; never wrapped by the contract
    double   beatsPerSample;     // the resolver's (under MIDI clock: span / frames); > 0 also while stopped
    double   callBeats;          // beats this call spans; 0 while stopped
    double   beatWrap;           // 0 = unbounded; > 0 = the HOST bounds its beat space to [0, beatWrap)
    uint8_t  playing;
    uint8_t  justStarted;        // playing, and not playing in the last RENDERED block
    uint8_t  justStopped;        // not playing, and playing in the last rendered block
    uint8_t  transportJumped;    // playing in both, and startBeat is not where the last rendered block ended
    uint8_t  retiring;           // 1 in renderRetiring (midi empty, no peers)
    uint8_t  frozen;             // 1: render from the last resolved values, read no APVTS or matrix (C7 provision, C12 rule)
    uint8_t  reserved[2];
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
```

**Edges (computed by the base, phase 0 of 2.1.1, against the last *rendered* block).** The base keeps, audio-thread only, `{ bool rendered; bool playing;
double endBeat; }`, written in phase 5 and reset by `prepareToPlay`. `justStarted = playing && !last.playing`; `justStopped = !playing && last.playing`;
`transportJumped = playing && last.playing && |d| > kJumpToleranceBeats`, with `d = startBeat - last.endBeat`, taken modulo `beatWrap` (to the nearest
multiple) when the host bounds its beat space, so mu-Tant's own 64-beat wrap is not a jump. A locate while stopped is reported by `justStarted` alone. An
unrendered block (missed try-lock, `layerEditInFlight`, a 0-frame block) does not update the latch, so its edge reaches the layers in the next rendered block:
this is what mu-On's `wasPlaying` and mu-Toni's `wasRunning` already do, because both are written only inside the lock. `onTransportStopped` and
`onTransportJump` are called in phase 0 of the block that carries the flag, before any render; the same flags are in the context so a layer whose order needs it
can act inside `render` instead (a type uses one or the other, not both). Hooks are not called on tails; tails see the flags.

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
note-on is not a transport phase, and the three release rules give the jump and stop behaviour C4 asks for. New layer types (mu-Tronic) may use either form.

**Tempo changes between blocks stay phase-continuous** by construction: the resolver makes each block start where the last one ended (internal and MIDI
clock; a host supplies its own position), positions are functions of the beat, so a new tempo changes only the next block's `beatsPerSample`; event-anchored
durations started before the change keep their frame length (today's behaviour). Tempo is constant inside a call (as today; host ramps inside a block are not
modelled).

**How each product's clock maps onto it** (and what changes at its stage):

| Product | Clock today | Kind | In the contract |
|---|---|---|---|
| mu-Toni loop mode (`ArpVoiceRunner::renderOnGrid`) | `nextStep = ceil(startBeat / stepBeats)`, cursor plus `expectedBeat`, re-find when off by more than half a step | transport-anchored cursor | Unchanged code; reads `ctx.startBeat`, `ctx.beatsPerSample`, `ctx.bpm`, `ctx.playing`. Keeps its own half-step tolerance. **No clock rework in E2**; only `ArpContext`'s source changes. |
| mu-Toni MIDI-trigger mode | `stepCounter` frames from the key press; `samplesPerStep` from `bpm` per block | event-anchored | Unchanged. `noteOnEdge`, `anyNoteHeld` and the root override come from the layer's own held-note scan of `ctx.midi` (E2.3). |
| mu-Toni gate | `gateCounter` frames from the note-on; stop: `run == false` notes off inside `render` | event-anchored | Unchanged; stop handled inside `render` (flags form). `wasRunning` stays in the runner (it includes the MIDI-trigger gate). |
| mu-On (`GrooveSequencer`) | one global `nextGlobalStep` cursor, resync beyond plus or minus one step; frame offset `round((t - start) / bps)`; stop edge `grooveVoices.reset()`; Rumble envelope at the beat | transport-anchored cursor | One cursor per track row with the same arithmetic (E3.4); stop edge as `onTransportStopped`; Rumble envelope at `ctx.startBeat`; tempo-synced taps from `ctx.bpm`. Order of `reset` against the parameter refresh is checked in E3 (open point 9). |
| mu-Tant (`gateAt(beat)`, `resolveLane` at the beat) | absolute beat, wrapped to 64 beats by the resolver | transport-anchored, no cursor | The mu-Tant standalone keeps `wrapBeats = 64` as its host policy (`beatWrap = 64`), because an unwrapped counter reaches a different double after 64 beats of internal play; `Pattern::render` applies `fmod(startBeat, 64)` itself, which is exact on already-wrapped input, so mu-Tant stays bit-identical and the layer is correct in an unbounded host (mu-Tronic). |
| mu-Clid (`RhythmSequencer`) | step from the beat (`stepAt`), `lastEffectiveStep` wrap detector, dip-hold `resolveBlockBeat`, block-granular triggers at frame 0; plugin Note mode runs its own beat counter | transport-anchored; the dip-hold and Note-mode counter are host transport policy | Dip-hold and the Note-mode counter stay host-side (they produce `startBeat`). The per-slot trigger source is open (open point 6): the context carries **no** product trigger field. MIDI note-off countdown: event-anchored. |

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
lock and a 0-frame block; `onTransportJump` exactly once per host locate, loop-back and clock snap, never on a contiguous block and never on mu-Tant's 64-beat
wrap. MIDI: `dropped == 0` in all goldens; note-offs survive an overflow. Retire contexts carry no MIDI and no peers.

**Failure modes considered.** A layer accumulating beats across blocks (drifts under tempo change, breaks on locate: the block-size and ramp tests); edges lost on a
missed lock (latch); a jump tolerance too tight for host jitter under tempo automation (false jumps) or too loose (missed small locates; layers keep their own
detectors, so the flag is a hint): open point 1; the wrap of a bounded host read as a jump (modulo `beatWrap`); MIDI overflow stranding notes (reserve); an
oversize block overrunning prepared buffers (chunking).

**Rejected.** Passing `const juce::MidiBuffer&` (a JUCE type, against A14; a per-layer filtered copy would allocate); a callback for `peer(slot)` (a host
callback); a per-layer beat wrap inside the resolver (mu-Toni's dotted rates do not divide 64); unwrapping mu-Tant's transport now (breaks D7 after 64 beats);
beat-domain note-offs for the existing products (above); a product trigger mask in the context (product vocabulary in core, and a field that mu-Tronic could
never fill).

#### 2.1.6 Open points for the `dsp-controller` (and the prototypes they need)

Settled above unless listed here. Items 1 to 4 need a measurement or a spike before E1 code; the rest are decided at the stage named.

1. **Jump tolerance** (`kJumpToleranceBeats`, proposed 1/256 beat): measure host position deltas between blocks under fast tempo automation (Bitwig, Reaper) and
   under MIDI clock with the sync offset ramp; pick a value with margin on both sides. Prototype: a logging build of mu-Toni's runner detector.
2. **Single-reader assumption** for `AudioEpoch`: confirm no supported host re-enters `processBlock` or calls it from a second thread (offline bounce while the
   realtime thread stops). Prototype: the `enter()` assert in a pluginval run of each format.
3. **mu-Tant in-place tail and regrow** (E4.6): today a count that grows back during a tail renders the slot live and leaves the tail counter armed; the bin cuts
   the entry instead. Pin today's render of "shrink then regrow within 25 ms" first (E0.6), then decide whether the difference is acceptable.
4. **mu-Clid drained-engine output** (E5.4d): today a drained retired engine is still rendered until some later deferred work frees it; the bin stops at Done. Pin that
   its output after `isFullyDrained()` is exactly zero (Karplus inserts may still ring after the 2 s budget); if not, choose between "Done also requires a silent block"
   and a signed golden update.
5. **C11 placement**: `ScopedNoDenormals` in `LayerBlock` (render, bin and mixer), not in `renderLayers` alone.
6. **mu-Clid trigger source** (E5.5): the plan said fired bits would reach `SampleRhythm` through the context. The context has no such field (it would put product
   vocabulary in core and mu-Tronic could not fill it). Recommendation: `SampleRhythm` clocks its own step cursor with `RhythmSequencer`'s shared arithmetic, as
   mu-On's tracks do in E3.4, and the master-loop wrap stays host-side for the swap boundary. To be designed with the E5 plan; it does not change this struct.
7. **Hard lifetime cap** (`kRetireHardCapSeconds`, proposed 30 s) and mu-Clid's `maxSeconds`: check the longest tail in the golden set (amp release plus the 2 s
   drain, and `ampRelToEnd` samples, which play to their end today).
8. **mu-Clid flush at prepare** (E5.4d): today `prepareToPlay` re-prepares retired engines and keeps them; the bin drops them. A re-prepare is a device restart, so the
   difference is inaudible in practice and in no golden; recorded as a deliberate change for the `dsp-controller` to accept.
9. **mu-On stop order** (E3): today `applyParams` runs before `grooveVoices.reset()` on the stop edge, and a 0-frame stop block resets at once; with the hook the
   reset runs first and a 0-frame block defers it. Pin both in E0.6; if either differs, mu-On uses the flags form inside `render`.
10. **Oversize blocks** (C8): chunking changes today's truncating behaviour, so a golden segment with a block longer than the prepared size cannot stay
    bit-identical when a product adopts the driver. Either that segment is re-recorded in a goldens-only commit signed by the `dsp-controller`, or it is kept out
    of the tolerance-0 gate and covered by the chunking test.
11. **mu-Tant bank-lock miss** (E4.5): today a missed `bankLock` skips the whole block (mixer and FX returns included); a layer-level try-lock silences only that
    layer. Contention only, in no golden; C5 removes the lock later.
12. **View pool size** (`kLayerViewPool` = 4) and the `waitFor` on exhaustion: confirm with the T1 stress test that a burst of type changes never waits more than one block.

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
