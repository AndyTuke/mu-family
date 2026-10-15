# mu-Tronic — design (architecture ruling, 2026-10-15)

**Status:** design only. Nothing is built. Written by the Architecture Steward for Andy's brief of 2026-10-15; the
look and feel of every screen below is the `ux-controller`'s, and real-time review of the audio-thread parts is
the `dsp-controller`'s before anything is pushed. Items marked **Andy** need his decision (section 9); everything
else is ruled.

**Andy's brief.** "An empty unit with the same mod, effects and mixer as all the other apps. The voices are empty slots, into which we can add a voice
from any of mu-clid, mu-tant, mu-toni and mu-on. mu-tronic will hold up to 16 voices, with paging on the UI from 1-8 to 9-16. The voices of the other
apps will therefore need to be completely self contained in the code so they can be used in mu-tronic. We will have buses in the mixer so channels can
be assigned to a bus instead of the master out. Buses will have the same insert effects as a layer, same side chain options. It may end up replacing
mu-link as a performance tool."

**What this is.** It is the "One framework, one instance" idea of [design-future.md](../design-future.md), scheduled as a fifth product. That
entry's open questions are answered here: it is a **fifth product** (not a mode of an existing one), capacity is **16 layers**, and the licence is
its own key (section 9). The groundwork it asked for is mostly built (layer container, `Layer` virtuals, composed slot state, clip bank, shared
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
(section 2.5). The standard UI sizing idea is helped: mu-Tronic adds no layout constants of its own except the page (8 layers), which becomes a shared token.

---

## 1. What "a voice" is

### 1.1 Vocabulary (design-naming §1 stands)

Andy's word "voice" means what the family calls a **layer**: one engine + one sequencer + one mixer channel. The code keeps **Layer**; "voice" stays
reserved for a polyphonic voice inside an engine (`SynthVoice`, `ToniVoice`). User-facing wording in mu-Tronic is the `ux-controller`'s call; the docs
and code say layer. New terms recorded in design-naming §1:

| Term | Means |
|---|---|
| **Empty layer** | A slot with no layer type chosen. Type `Empty`; silent; costs nothing per block. |
| **Page** | Eight layers: page 1 is layers 1-8, page 2 is 9-16. UI and controller concept only; the processor sees 16 layers. |
| **Bus** | An internal group strip that channels can be routed to instead of the master (section 5). Not the DAW multi-out. |
| **Direct out** | The existing DAW output routing of a channel (`ChannelState::outputBus`, parameter `outBus`). Renamed in comments and UI to "Out" so "bus" has one meaning. |

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
| **mu-On** (`Track`) | `Track : Layer` (step row, Rumble envelope, trigger counter); engines `KickEngine`, `BassEngine`, `SampleChannel`, `RumbleEngine` in `Audio/` | `GrooveVoices` holds **every engine of every slot** (5 per slot, large) and does the modulation resolve; `StepPattern` + `GrooveSequencer` are **one global grid and clock for all tracks**, with global swing/accent params; the Rumble **reads another layer's audio** (render order, `remapLayerReferences`); `Plugin/TrackParams.h` (`EngineType`, param ids) is included by the engines, `MuOnModDest.h`, `StepPattern.h`, `GrooveVoices.h`; `TrackContent`/`StepRows` include the processor | Per-layer engine instance (only the selected engine allocated; changing engine is a layer-internal swap on the message thread), per-layer step grid and swing/accent (`StepPattern` becomes a per-layer `StepRow` driver, `GrooveSequencer` clocks one row), `TrackParams.h` into `Persistence/`, the Rumble source resolved through the render context (section 2.1), `TrackContent` onto a `TrackHost&`. No per-layer insert today (hook returns `nullptr`); buses give On groups an insert. |
| **mu-Clid** (`Rhythm`) | Engine, sequencer, MIDI engine and play state are **already in `mu-core`** (`mu_core::Rhythm`, `RhythmSequencer`, `::VoiceEngine`, `MidiOutputEngine`, `RhythmPlayState`, shared with the MIDI product); `RhythmContent`, `VoiceSection`, `MidiOutSection`, `SampleBrowser`, `EuclideanPanel` | The per-block glue: `PluginProcessor_Modulation.cpp` (444 lines: mod seed and write-back, Euclid overrides), trigger firing, `voiceEngines[]`, `midiEngines[]`, retired tails and drain flags, `rhythmPlayState[]`, `modSnapshot`, `rawParams`/`pendingDirty` drain, `SampleLibrary`/sample paths, `orphanOffs`, `HotSwapStager` (prepares a `Rhythm` + `VoiceEngine` at stage time, sample I/O included), `PresetIO` (includes the processor), the master loop in `RhythmSequencer` | New `mu_clid::SampleRhythm : mu_core::Rhythm` that carries the engine, MIDI engine, play state, snapshot, sample path and retire tail, with `render`; the glue becomes its methods; `PresetIO` shapes onto `Layer::writeExtras/applyExtras` + `captureLayerPresetTree`; sample I/O at stage time via `preloadStaged`; `SampleLibrary` becomes a shared service |

Everything above is "voice code in the processor" that the docs already say belongs elsewhere (design-naming §3: `Plugin/` holds only the processor,
editor and standalone; engine and preset code belong in `Audio/` and `Persistence/`). So this is also a correction, not only new work.

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
  is in `Plugin/`, `License/` or `UI/SettingsOverlay.*`, which are not in the library. Rules: (1) rename them `Audio/TantScales.h` and `Audio/ToniScales.h`
  (the first change of Stage T0); (2) a layer-library file **never includes anything under `Plugin/` or `License/`**, which section 1.3 and 2.2 already require;
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
Registry entry `LayerTypeInfo` in `mu-core/Sequencer/LayerTypeInfo.h`; ids `enum class LayerTypeId : int { Empty = 0, Rhythm, Pattern, Arp, Track }` in
`mu-core` (neutral: the ids are family vocabulary, as `kMaxLayers` is), values saved by name (`"Arp"`), never by number.

`LayerTypeInfo` (all message-thread data): `id`, `name`, `paramPrefixLetter` (`r v a t`), `presetExtension`, `contentFolderName`, `modTargetTable`,
`addLayerParams(layout, slot)`, `create(slot)`, `createServices()`, `createContent(...)`, `createMiniVisual(...)`. It constructs and describes; it never
dispatches (design-plugin-family "Why virtual and not a table" stands).

---

## 3. mu-Tronic product layout and state

### 3.1 Identity (saved data, change only with a migration)

Product name **mu-Tronic** (prose), slug `mu-tronic`, namespace `mu_tronic`, docs `docs/mu-tronic/` (design-naming "Product naming pattern"). Plugin code
`MTrn`, bundle id `com.transwarp.mu-tronic` (same manufacturer as the family, per the existing ids), state identifier `MuTronicState`, full preset tag
`MuTronicPreset`, full-preset extension `.muTronic`. Formats VST3 + CLAP + Standalone. `Andy`: confirm the code `MTrn` and the display name (section 9).

### 3.2 The eight Source folders

```
mu-tronic/
  CMakeLists.txt            juce_add_plugin; links mu-core + mu-clid-layer + mu-tant-layer + mu-toni-layer + mu-on-layer
  resources/ installer/
  Source/
    Audio/                  empty (.gitkeep): the engines are in the layer libraries
    License/                LicenseKey.h only (own id, filename, public key, checkout URL)
    Modulation/             empty (.gitkeep): mod-target tables stay with each type (LayerTypeInfo)
    Persistence/            TronicState (type attribute, import of product presets), the registry list (which types this build offers)
    Plugin/                 PluginProcessor(+_APVTS/_Preset/_Layers/_Content), PluginEditor, StandaloneApp
    Sequencer/              EmptyLayer.h (the Empty type); nothing else
    Tests/                  the tests of section 7
    UI/                     TronicSettingsOverlay, the empty-slot "add layer" card, the layer-type picker, the page switch glue
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

`ProcessorBase` additions: `setLayerType`, `renderLayers`, `RetireBin`, `LayerServices` ownership, the registry lookup. All generic; none names a product.
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
the schemes are already unique (design-naming §3), so nothing collides and a slot's type selects which prefix is live. Estimated size is **about 4,000 to
6,000 parameters** (mu-Toni 63 per layer, mu-Tant and mu-Clid of similar order, mu-On five engines' sets). Hosts cope with it in a standalone; in a DAW a
parameter list that long is slow to build and to browse. Mitigations, in order of preference, decided by measurement at Stage T0 (build a real layout and time
the APVTS constructor and a VST3 scan): (a) ship the VST3/CLAP with only the standalone-complete set and the same list; (b) set a host-visible flag so
inactive prefixes are hidden (`isAutomatable` false for slots whose type is not live; JUCE re-queries on `updateHostDisplay`); (c) the **parameter pool**
(only if measurement fails): each slot declares 64 generic parameters `s{N}_p{M}` and each type maps its suffixes onto pool indices, `LayerLayout`
extended with a suffix to id map. (c) is a real design change to the composed state and every product's binding code, so it is not chosen now; the
composed state's prefix-free rows are what keep the door open (a pool changes only the id the row resolves to). **Decision for Andy** whether the plugin
formats are in scope for the first release or standalone first (section 9).

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
  (stopped load, hot-swap commit, host restore) is unchanged apart from this first step; a type change inside a staged commit parks the old layer in the
  RetireBin.
- **Layer presets** keep the product's extension and shape (`.muArp`, `.muPattern`, `.muTrack`, `.muRhythm`); the type is inferred from the extension, so
  **a preset saved by mu-Toni loads into a mu-Tronic slot and the reverse**, which is what makes the apps and mu-Tronic one library. The layer-preset folder
  is the product's own (`<TDP>/muToni/Arps`, ...) via `contentFolderName`: shared, not copied (Andy may prefer a separate mu-Tronic tree; section 9).
  Loading a preset whose type differs from the slot's is a type change (confirm, then stage like any swap).
- **Clips** (`Layer::clips`): a clip is the slot's type's layer preset and carries its type. A clip whose type differs from the slot's current type is not
  launchable; changing a slot's type clears its clip bank after a confirm. This keeps the clip bank "a launch is a staged layer swap, never a type change".
- **Full presets** (`.muTronic`) are the state above wrapped as `<MuTronicPreset name description category>`; performance files (`.muPerformance`)
  point at them unchanged (`product` is `MuTronicState`).
- **Import (Stage T8).** "Import product preset" reads a `.muTant`/`.muToni`/`.muOn`/`.muClid` full preset and lays its layers into free slots (type from the
  file's tag, strips and sidechain sources remapped with `mu_mix::sidechain*`, the external-sidechain value 8 mapped to this binary's `kExtSidechainSrc`).
  Global FX/master of the imported preset are ignored by default.
- Hot-swap: `LayerSwapper<ValueTree, 16>` as for the other products, boundary per slot from `Layer::loopBeats()`; `Layer::preloadStaged` is called at stage
  time. mu-Clid's own `HotSwapStager` stays in the standalone mu-Clid until Stage T7 decides (section 6).

### 3.5 Product-global state that becomes per layer

A voice that is self-contained cannot read a processor-wide setting. Moves (each is a visible-behaviour check for the owning product's roundtrip test):

| Today (global) | Product | Becomes |
|---|---|---|
| Note mode (`midiNoteMode`, held-note stack, final-mix gate ramp) | Tant | layer state; ramp applied to the layer's own output; setting saved per layer (a layer extra) |
| Held-note stack, `ArpContext` root-by-MIDI, MIDI-trigger, `midiInChParam` | Toni | layer state |
| One `StepPattern` + `GrooveSequencer`, global swing/accent | On | one `StepRow` driver per `Track`; swing/accent per layer (`t{N}_swing`, `t{N}_accent`; the standalone's global value seeds all tracks on load; a unit test pins that) |
| Master loop (`mstrLoop`), swap timing (`mstr_swap`) | Clid, all | stays a global (it is already a shared global parameter) |
| MIDI input channel | Toni, Tant | one per-layer **MIDI in** setting in the shared strip params (`ch{N}_midiIn`, 0 Omni, 1-16) for every type; types that take notes read the filtered buffer in `RenderContext`; defaults reproduce today (Omni) |
| MIDI output (Clid MIDI-out mode) | Clid | `producesMidi()` true for mu-Tronic; a layer appends to the block's output buffer through the context |

---

## 4. UI structure (structure only; the look is the `ux-controller`'s)

- **Sidebar and panel.** `LayerSidebar` and `LayerPanel` are unchanged in kind. `LayerSidebar` gains a **page** (`kLayersPerPage = 8` in `MuLimits.h`, used by the sidebar,
  the mixer overlay and `mu-control`) and an empty-slot item ("add a layer"); `LayerPanel::setContent` is already callable again so the content swaps with the
  slot's type (the 2026-10-12 ruling anticipated this). The page shown is editor state, not saved; the page of the selected layer is shown on selection.
- **Layer-type picker.** Choosing a type for an empty slot (or changing it) is a shell dialog listing the registry; the type's preset dropdown appears in the
  `LayerHeaderBar` as today, filled from the slot's type.
- **Mixer overlay.** 16 strips do not fit one row beside returns, buses and master: strips page 1-8 / 9-16 with the buses, returns and master always visible
  (`MixerOverlay` gains the page, shared with the sidebar). Bus strips are `MixerChannel` of a new `Type::Bus` (section 5).
- **Controller pages.** `mu-control` `SurfaceModel` reads `kLayersPerPage` and a page index from the `LiveView`; the pad column that is absolute for 8 layers
  becomes absolute within the page. Not built before T7.

---

## 5. Buses in the shared mixer (all `mu-core`)

### 5.1 Shape

- **Count.** `mu_limits::kMaxBuses = MU_MAX_BUSES`, default **0** (existing products unchanged, no new parameters, no UI), **4 in mu-Tronic**. Four is the
  conventional drums / bass / synth / spare grouping and keeps the strip row short; `MU_MAX_BUSES=8` is a one-line change. Andy may prefer another number (section 9).
  Any product can later opt in with one define and one `addBusParams` call (mu-Clid could group its rhythms).
- **State.** `MixerEngine::BusState` = a `ChannelState` (level, pan, mute, solo, three sends, sidechain source / amount / attack / release; `outputBus` unused)
  **plus the layer's insert**: `insertAlgo`, `insertParam[VoiceParams::kInsertSlotCount]` atomics and an `InsertProcessor`. So "same sidechain options" and "same
  insert effects as a layer" are literally the same fields and the same class, not copies. Peaks (`busPeaks*`) and `busSidechainGR` arrays beside the
  channel ones; the follower `scBusEnv[]`.
- **Assignment.** A channel strip gets one new field, `ChannelState::busAssign` (0 = master, 1..kMaxBuses), parameter `ch{N}_bus` (int), declared by
  `mu_mixfx::addChannelStripParams` only when `kMaxBuses > 0`. It is part of the strip (like level and pan): it travels with the strip on swap/remove/reset
  (`copyFrom`, `reset`, `swapChannelState` extended), resets to master, and is **not** in a layer preset or clip (strips never are). The existing
  `outputBus` stays the DAW direct out; it wins if non-zero (a channel with a direct out feeds neither a bus nor the master, as today).
- **Bus parameters.** `bus{B}_*` with the strip's 12 suffixes (B = 0..3) through `addChannelStripParams(layout, "bus{B}_", ...)`, and the insert with the
  layer-insert suffixes through a shared `mu_mixfx::addInsertParams(layout, prefix)`; `InsertSubsection` is already prefix-bound, so the bus uses it with
  prefix `bus`. `ProcessorBase::syncGlobalFxParam` gets one more prefix route (`bus{B}_`). Buses have **no modulators**: modulation is per layer
  ("channels are self-contained"), so the single-reader rule is untouched.
- **Sends.** A bus has the same three FX sends as a channel (post-bus-fader). A channel's sends are taken from the channel, post-fader, **whatever its
  routing** (a channel sent to a bus still feeds the returns; its dry goes to the bus). Returns have no bus assignment; they go to the master. Direct
  out channels skip buses and sends as they skip them today.

### 5.2 Routing order and feedback prevention

The graph is a fixed three-layer DAG, **channels -> buses -> returns -> master**, so a loop cannot be expressed:

1. Phase 1-2 unchanged (render, trim, per-channel sidechain duck), with `MaxChannels` = 16.
2. Phase 3: per channel, fader/pan, peak; the dry signal is added to `busBuf[busAssign-1]` (or `output` for master); sends go to the send buffers.
3. **Phase 3b (new): for each bus in index order**, sidechain duck (if configured), insert (the layer `InsertProcessor`, `VoiceParams` built from the bus atomics
   as the master insert does), then bus fader/pan, peak and sends, then add to `output`. Mute/solo as below.
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

**Latency.** The only built-in inserts with latency would need delay compensation across buses; none of the current insert algorithms report latency
(the `dsp-controller` confirms at review). If one is added later, buses require the compensation rule (all channel paths to the master delayed to the
longest bus path); note it as a standing constraint on `InsertProcessor`.

### 5.3 What is mu-core

All of it: `MixerEngine` (`BusState`, Phase 3b, arrays), `MixerFxParams` (`addBusParams`, `addInsertParams`, `busAssign` strip param),
`ProcessorBase::syncGlobalFxParam` route, `MixerChannel` (`Type::Bus`, an Out dropdown listing Master + buses), `MixerOverlay` (bus strips, page), `MuLimits.h`
(`kMaxBuses`, `kLayersPerPage`). mu-Tronic supplies nothing but the define. No product-specific symbol is involved, so the boundary checker is
unaffected. The unit tests are core tests (section 7).

---

## 6. Staged build plan (smallest first)

Each stage ends with a Debug build, its suites, the `*_roundtrip` listening tests of every product it touched (byte-identical where stated), and the static
checks. `dsp-controller` reviews every stage that changes the audio thread (T2, T4 to T8). A failed proof is reverted, not carried. Stage kinds as in Layer
ownership: M mechanical, S supervised (audio thread or lock), O needs Andy.

| Stage | Kind | What | Touches | Proof |
|---|---|---|---|---|
| **T0** | M | Groundwork with no behaviour change: `MU_MAX_LAYERS` / `MU_MAX_BUSES` / `kLayersPerPage` in `MuLimits.h` (defaults 8 / 0 / 8); rename `Audio/Scales.h` in Tant and Toni; add `LayerTypeId`, `LayerTypeInfo`, `Layer::typeId`; `check-layer-boundary.ps1`; build a throw-away 16-slot APVTS layout from the four param tables to **measure parameter count and construction time** (the section 3.4 decision). | `mu-core`, mu-tant, mu-toni (rename), `tests/scripts` | full Debug build of all five; every suite and `*_roundtrip` byte-identical; `mu-core-tests-16` builds and passes; measurement recorded in this doc |
| **T1** | M | **The empty unit.** `mu-tronic/` with the eight folders, 16 `EmptyLayer`s, `setLayerType` (only `Empty` registered), the shared mixer/FX/master/modulator panel/preset flow/transport, sidebar and mixer **paging**, a `.muTronic` round trip, licence stub. No audio sources. This is the brief's "empty unit". | new product; `LayerSidebar`/`MixerOverlay` page (core, ux-controller) | builds; opens; pluginval and the standalone smoke; 16 strips page; state round trip; the other four unchanged (byte-identical) |
| **T2** | S | **Buses** in the shared mixer (section 5), opt-in `MU_MAX_BUSES`, then `EmptyLayer`-free tests with a test-tone layer in the core tests; mu-Tronic turns them on (4). Rename the `outputBus` UI label to Out. | `mu-core` mixer + overlay + params | `BusRoutingTests`, `BusSidechainTests`, `BusStateResetTests` (section 7); all four products byte-identical (buses 0); a scripted mu-Tronic render with test-tone layers through a bus insert |
| **T3** | S | `Layer::render`, `RenderContext`, `renderLayers`, `RetireBin`, `setLayerType` with real types, `LayerServices`, `bindParams`, `preloadStaged`, `loopBeats` (section 2.1), defaults only; mu-core tests drive a fake layer type. No product changes yet. | `mu-core` | `LayerRenderTests` (order, priority, empty skipped, lock miss silent and still advancing, retire fade then free off thread, no allocation under the lock), `LayerTypeSwapTests`; products byte-identical |
| **T4** | S | **mu-Toni layer** (cleanest): `readVoice` + pointers + held notes into `Arp::render`; param table and `LayerParamIds.h` into `Persistence/`; `ArpHost`; `mu-toni-layer` target; **mu-Toni standalone moves onto `renderLayers`** (so the glue exists once); mu-Tronic registers `Arp`. | mu-toni, mu-tronic | `TONI_roundtrip` byte-identical; mu-Toni suites; a mu-Tronic render with two Arp layers equals two mu-Toni layers' renders (the equivalence test, section 7) |
| **T5** | S | **mu-Tant layer**: render chain, engine, retire fade (replaced by the shared bin), per-layer Note mode, shared bank, param table, `PatternHost`; standalone adopts `renderLayers`. | mu-tant, mu-tronic | `TANT_roundtrip` byte-identical (Note-mode behaviour pinned by a new test first); mu-Tant suites; equivalence test |
| **T6** | S, O | **mu-On layer**: per-layer step grid, swing/accent, per-layer engine allocation, Rumble through `RenderContext::peer`, `TrackParams.h` to `Persistence/`, `TrackHost`; standalone adopts. **Decision inside:** whether the five-engine `GrooveVoices` pool stays in the standalone (and mu-Tronic allocates one engine per Track) or the standalone also goes per-layer; recommended per-layer in both, so one code path. | mu-on, mu-tronic | `ON_roundtrip` byte-identical; the 5-lane preset still loads as 5 tracks; equivalence test |
| **T7** | S, O | **mu-Clid layer**: `SampleRhythm` (engine, MIDI engine, play state, snapshot, retire tail, sample path), glue out of `PluginProcessor_Modulation.cpp`, `PresetIO` shapes onto the `Layer` hooks, `SampleLibrary` as a service, sample I/O in `preloadStaged`, `RhythmHost`. **The standalone mu-Clid adopts it** (the glue must not exist twice); its `HotSwapStager` is either kept for the standalone only (recommended until the swap timing tests prove equal timing) or replaced. Highest risk of the plan (Stage 7 of Layer ownership is the precedent and its proof list applies in full). | mu-clid, mu-tronic | `CLID_roundtrip`, `swap`, `slot swap`, retire-tail renders byte-identical; `SwapCommitTimingTests`; `mu-clid-midi-tests` (shared `Rhythm`); equivalence test |
| **T8** | S | Cross-type features: type-carrying clips, performance files for `MuTronicState`, **import of product presets**, per-layer MIDI in, controller pages in `mu-control`, mu-link bridge for the standalone (the hook is one line), CPU meter on the transport bar. | mu-core, mu-control, mu-tronic | tests of section 7 |
| **T9** | O | Parameter pool if T0 measurement said so; release stages (installer, manual, site, licence). | | |

Order rationale: T1 is the brief's literal first request and is small and safe. T2 before any voice because buses are pure `mu-core` and are testable
with synthetic sources, and because they affect the mixer UI every later stage shows. T3 before any product so the contract is proven on a fake type, not
guessed from one product (Stage 8's objection: "built now would have one type per product and no way to be tested against a second"). Then products by
ascending glue: Toni (already composed), Tant (a long render chain in the processor), On (global sequencer and pooled engines), Clid last (engine-in-layer
reverses a Stage 7 ruling, the stager, samples, 1,580 references). The equivalence test in each of T4 to T7 is the proof that "completely self contained" holds.

### What each product must change (summary)

- **mu-Toni**: `Arp::render`, held notes per layer, `Persistence/Toni*Params`, `ArpHost`, `ToniScales.h`, `mu-toni-layer`, adopt `renderLayers`.
- **mu-Tant**: `Pattern::render` + engine + cached pointers, Note mode per layer, shared wavetable bank service, `Persistence/PatternParams`, `SwapBoundary.h` to `Sequencer/`,
  `PatternHost`, `TantScales.h`, `mu-tant-layer`, adopt `renderLayers`.
- **mu-On**: per-layer step grid and swing/accent, one engine allocation per Track, Rumble via the context, `TrackParams.h` to `Persistence/`, `TrackHost`, `mu-on-layer`, adopt `renderLayers`.
- **mu-Clid**: `SampleRhythm`, glue out of the processor, `PresetIO` onto the hooks, `SampleLibrary` service, `RhythmHost`, `mu-clid-layer`, adopt `renderLayers`.
- **mu-Clid MIDI**: nothing (core `Rhythm`).
- **All**: no `Plugin/`/`License/` include from library files; unique relative paths; param table as `addLayerParams`.

---

## 7. Tests (for the `test-steward`; listed so the stages are provable)

`BusRoutingTests` (assign, reassign mid-play, master default, bus and channel sums, a muted bus, solo-through-bus, direct out wins), `BusSidechainTests`
(legal sources per strip type, no loop, bus ducked by a channel, return ducked by a bus, source renumbering on swap/remove), `BusInsertTests` (the bus insert is the
layer insert algorithm for algorithm; GR meter), `BusStateResetTests` (`copyFrom`/`reset`/`swapChannelState` carry `busAssign`), `LayerRenderTests`,
`LayerTypeSwapTests` (type change under a staged commit; old layer fades and frees off-thread; clip bank cleared), `TronicStateTests` (empty slots, mixed types round trip,
older file without `type`), the **equivalence test per product** (a mu-Tronic slot of type X, same parameters and same MIDI, renders sample-identically to the
product's own layer; listening-test option added per product beside `roundtrip`), a two-thread stress test (type changes while `processBlock` runs: no
allocation, no crash), a CPU test (16 layers of the heaviest types within a block-time budget), the layout-size measurement of T0. `check-layer-boundary.ps1`
has a fixture of its own. `mu-core-tests-16` runs the core suites at capacity 16.

---

## 8. Relation to mu-link

mu-link today is the **performance hub**: it owns the hardware output, sums the standalones' audio rings, is the master clock, offers Scenes, MIDI-clock out and
(planned) a Link peer. mu-Tronic gives the same rig in one process: one transport (already sample-accurate in-process), a mixer with buses and the shared
FX, a performance file for scenes (the `Performance` level exists), hardware output owned by the standalone, and the controller work in `mu-control`.

**Ruling: coexist now, converge by sharing code, decide retirement after T8.** Not "replace" yet, because mu-link still does what mu-Tronic cannot: sum
*separate processes* (anything not yet a layer type, or another mu product instance) into one output with one clock. Not "absorb" because that would copy
mu-link's server, IPC and registry into a product that does not need them.

- mu-Tronic **is a mu-link client like the other standalones** (the bridge is one line in `StandaloneApp.cpp`), so the two can run together; when attached it
  slaves to the mu-link clock via `resolveTransport`.
- **No duplicated clock/MIDI code.** MIDI clock out, the Link peer, the tempo PLL and the transport sources already live or are planned in `mu-core`
  (`TimedMidiOut`, `MidiClockTempo`, `TransportResolver`); mu-Tronic and mu-link both use them. Anything mu-link would still need to build for performance
  use (Scenes, a Link peer) is built in `mu-core` or the shell, not inside `mu-link/`.
- **Scope freeze.** No new performance features go into mu-link until T8 is judged; its open increments (L-stages) continue only where they are about
  hosting external processes.
- **Retirement test** (at T8, Andy decides): mu-Tronic covers hardware output ownership, MIDI-clock out, a Link peer if licensed, scenes, and the
  controller workflow. If all four are met and Andy no longer runs separate apps, mu-link is retired or reduced to a headless clock/hub. Until then both ship.

---

## 9. Risks and open decisions

### Risks (and the answer to each)

1. **Parameter count** (about 4,000 to 6,000). Measured at T0; mitigations in 3.4; the pool is the fallback and the prefix-free rows keep it cheap to adopt.
2. **Stage 7 reversal for mu-Clid** (engine into the layer, 1,580 references). Mitigated by making it last, by `SampleRhythm` being a derived type
   (mu-Clid MIDI's `Rhythm` is untouched), and by the Stage 7 proof list applying in full. If T7 stalls, mu-Tronic ships without the Rhythm type and
   nothing else is blocked.
3. **Self-containment drift.** Without a checker a product will re-add a `Plugin/` include. `check-layer-boundary.ps1` plus the equivalence tests are the guard;
   `architecture-steward` reviews every T4 to T7 diff.
4. **CPU.** 16 layers of mixed engines (mu-On Rumble, mu-Tant filters, mu-Clid insert tails) can exceed a laptop budget. Empty layers cost nothing; a CPU
   meter lands in T8; a per-layer "freeze/mute stops processing" rule (a muted layer skips `render` but not note tracking) is a candidate saving, decided
   with the `dsp-controller` at T3. The honest limit is "16 slots", not "16 of the heaviest".
5. **Cross-layer audio (Rumble).** The only voice that reads another layer's output. Contained by `renderPriority` + `peer`; restricted to `Track` layers
   in the same instance; a Rumble whose source is not a live `Track` processes silence (as today).
6. **Same-name collisions in one binary** (global names, include paths). Handled in 2.5; the checker greps.
7. **Hot-swap of a type change** replaces an engine, not just data. Uses the shared `RetireBin` and `preloadStaged`; staged at the boundary like any swap;
   `LayerTypeSwapTests`.
8. **Colour palette** has 8 colours for 16 layers (section 3.4). `ux-controller`.
9. **Live-product impact.** mu-Clid and mu-Tant are shipping (1.1). T4 to T7 touch their audio path; every stage is byte-identical or reverted, and none
   changes saved data or parameter ids (prefixes and extensions stay). Release timing of those products relative to the stages is Andy's call.
10. **Layer count in the existing `getLayerName`/`getLayerColourIndex` overrides** (each product returns `"Voice N"`-style names): mu-Tronic names a layer from
    its type and slot (`Layer::name`), a default the base already provides.

### Decisions for Andy

1. **Fifth product, confirmed?** This doc assumes mu-Tronic ships alongside the four apps and they continue to exist (recommended: they cost nothing extra because they
   share the layer libraries; the standalones become "one layer type, 8 slots" presets of the same code, which is the design-future end state). The alternative, retiring the four
   apps once mu-Tronic is complete, changes nothing in this plan before T8.
2. **Plugin formats in the first release, or standalone first?** The performance mode and mu-link bridge are standalone-only; a 4,000+ parameter list is the
   main DAW cost. Recommendation: standalone first, VST3/CLAP once T0's measurement and pluginval are good.
3. **Bus count:** 4 (recommended), 8, or more.
4. **Name and codes:** display name mu-Tronic (pattern `mu-<Name>`), plugin code `MTrn`, bundle id; a different pun costs nothing now and a lot after release.
5. **Licence and price:** its own key, own `LicenseKey.h`. Whether it is Pay What You Want like the other paid apps or free; the support-prompt policy applies unchanged.
   (This also answers design-future's question of whose key covers a combined instance: its own.)
6. **Shared or separate layer-preset folders:** mu-Tronic reads and writes each product's existing preset folders (recommended, one library) or has its own tree.
7. **mu-On inside T6:** per-layer engine allocation in the standalone as well (recommended) or only in mu-Tronic.
8. **mu-link:** the coexist-then-test ruling of section 8, and whether to freeze new mu-link performance features now (recommended).
9. **Sequencing against the live products:** whether T4 to T7 wait for the next release of mu-Clid and mu-Tant, or run before it.

---

## 10. Owner-visible effects of this plan on existing products

None before T4. T0 renames two header files and adds compile-time defaults. From T4 each standalone is rebuilt on the shared render path and must be
sample-identical; no preset, parameter id, extension, plugin code or folder changes. mu-On's and mu-Toni's global swing/accent and held-note settings become per layer inside, with
the same defaults and the same saved results (pinned by tests before the move).
