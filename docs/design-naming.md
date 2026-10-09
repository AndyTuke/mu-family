# μ Family — Naming Conventions

How the apps name things, so the family reads as one codebase. Written 2026-10-09 from a survey of every
source tree (script + reading; nothing compiled). Items marked **✔ owner** are decisions; the rest are
**proposed** until confirmed.

This serves the one-framework / one-instance direction in [design-future.md](design-future.md): every
shared concept gets one name in `mu-core`, and a product adds only what makes it different (its sound
engine and its sequencer). Rule of thumb for any rename: *does it make combining the engines in one
place easier or harder?*

Related: the family consistency rule in [CLAUDE.md](../CLAUDE.md), and the platform contract in
[design-plugin-family.md](design-plugin-family.md).

---

## 1. The vocabulary

| Term | Means | Not used for |
|---|---|---|
| **Layer** ✔ owner | One engine + one sequencer + one mixer channel. The parent class every product's layer type derives from. | — |
| **Engine** | The sound-producing part of a layer (synth voice bank, kick, sampler, arp voice). | the whole layer |
| **Sequencer** | The part of a layer that decides *when* (Euclidean hits, gate pattern, arp scan, step grid). | the whole layer |
| **Channel** | The mixer strip only (`MixerChannel`, `ChannelState`). One layer feeds one channel. | a layer |
| **Slot** | An FX or insert slot only (`DelaySlot`, `EffectSlot`, `FXSlotBase`, insert slots). | a layer |
| **Voice** | One polyphonic voice *inside* an engine (`ToniVoice`, `SynthVoice`). | a layer |
| **Preset** | A saved full state of one product: up to 8 layers with their mixer, FX and modulation (`.muClid`, `.muTant`, ...). | a layer preset |
| **Layer preset** | One layer's saved state (`.muRhythm`, `.muPattern`, ...). | a full preset |
| **Performance** (proposed) | A list of up to 8 pointers to presets for a gig; the new top level (backlog #1279). ✔ owner | a preset |
| **Clip** ✔ owner | One of 8 stored states of a layer that a controller pad launches, as in Ableton Live and Bitwig; plays until another is chosen; pads never write presets (backlog #1277). "Slot" stays for FX / insert slots only. | an audio clip, an FX slot |

### Derived layer types ✔ owner (2026-10-09)

Each product's layer is a subclass of `Layer`, named for what that product's layer *is*:

| Product | Layer type | Adds to `Layer` |
|---|---|---|
| mu-Clid | `Rhythm` | Euclidean generators, sample binding |
| mu-Tant | `Pattern` | gate / filter / pitch patterns, wavetable |
| mu-Toni | `Arp` | scale / chord pool, scan, accent pattern |
| mu-On | `Track` | step rows, lane engine selection |

Notes on these names:

- **mu-Tant is `Pattern`** (owner, 2026-10-09). It matches the layer files it already saves
  (`.muPattern`), and it keeps every product's layer type name unique (`Rhythm`, `Pattern`, `Arp`, `Track`),
  so the layer-type registry and the preset extensions never collide. (An earlier note considered `Rhythm`
  for mu-Tant; it was dropped because mu-Clid already owns that name and `.muRhythm`.) Its code currently
  calls a layer a *voice*; that word is kept only for a polyphonic voice inside an engine (`SynthVoice`).
- **`Pattern` next to `GatePattern` and `StepPattern`.** mu-Tant's `GatePattern` (and its filter / pitch
  patterns) become *members* of its `Pattern` layer, and mu-On's `StepPattern` belongs to a different
  product and namespace, so there is no clash; it just reads as "a Pattern holds Patterns". If that proves
  confusing, rename the members (`GateLane`, say) rather than the layer.
- **mu-On is `Track`** (owner); its extension is already `.muTrack`. The code mixes lane / channel /
  track today and collapses to Track.

---

## 2. The Layer parent class

**Today.** `VoiceSlot` ([mu-core/Sequencer/VoiceSlot.h](../mu-core/Sequencer/VoiceSlot.h)) is the intended
parent: it holds `voiceParams`, the `controlSequences`, the `modulationMatrix`, the two spin-locks,
`name` and `colourIndex`. But only mu-Clid's `Rhythm` actually derives from it (`class Rhythm : public
VoiceSlot`). The other three products keep a plain `VoiceSlot` in an array and store the rest of each
layer in separate parallel arrays on the processor:

| Product | Per-layer data, held in arrays indexed by layer |
|---|---|
| mu-Clid | `std::vector<Rhythm>` (derived — the closest to the target) |
| mu-Tant | `voiceSlots`, `gatePatterns`, `filterPatterns`, `pitchPatterns`, `inserts`, `voiceRingBuffers` |
| mu-Toni | `voiceSlots`, `runners`, `inserts`, `insCfg` |
| mu-On | `voiceSlots` (engines and step data live elsewhere) |

**Target.** `Layer` (renamed from `VoiceSlot`) owns everything every layer has, and a product's
derived type adds only its sequencer data and its engine binding. Candidates to move into `Layer`:
the insert processor and its config (duplicated in mu-Tant and mu-Toni), the persistence hooks
(`SlotExtras`-style write / apply), hot-swap staging state, and the mixer-channel binding. A layer
add / reorder / delete in `mu-core` then works on `Layer` and never needs product code.

Open: virtual dispatch vs a registered layer-type table (see the layer-type registry in
design-future.md); who owns layers (`vector<unique_ptr<Layer>>`); the audio-thread rules (no
allocation, locks) a polymorphic layer must keep.

---

## 3. Rules

### Namespaces
- Every product type lives in `mu_<product>` (`mu_tant`, `mu_toni`, `mu_on`, `mu_clid`). mu-Tant, mu-Toni
  and mu-On comply; **mu-Clid does not** (see §4).
- Product code never goes inside a `mu-core` namespace (mu-Clid defines functions in `mu_pp`).
- `mu-core` uses `mu_core`, `mu_ui`, `mu_audio`, `mu_mod`, `mu_pp`, `mu_link`, … by area. The ~130
  `mu-core` types that are still global (filters, UI components) are left alone for now.
- `createPluginFilter()` stays global (JUCE requires it).
- `using namespace juce;` only at function scope in a `.cpp`, never in a header.

### Class-name roles

| Role suffix | Meaning |
|---|---|
| `Panel` | an inline region of the editor |
| `Overlay` | a full-editor screen shown through `OverlayHost` (`SettingsOverlay`, `MixerOverlay`) |
| `Sidebar` / `Section` / `Subsection` / `Row` / `Bar` | as their names say |
| `Engine` / `Sequencer` | the two halves of a layer |

New layer UI is `LayerPanel` / `LayerSidebar` in `mu-core`; a product supplies content, not a copy of the
class.

### Files and folders
- File name = class name. One main class per file.
- **Identical `Source/` layout in every instrument product ✔ owner (2026-10-09):** mu-Clid, mu-Tant, mu-Toni
  and mu-On each have exactly these eight subfolders, whether or not they have anything to put in them:
  **`Audio`, `License`, `Modulation`, `Persistence`, `Plugin`, `Sequencer`, `Tests`, `UI`**. An empty
  folder carries a `.gitkeep` so git keeps it. A new product starts with all eight. (mu-link is
  infrastructure, not an instrument, and keeps its own layout.)
- What goes where: `Audio/` the engine (voices, samplers, DSP specific to the product); `Sequencer/` the
  sequencer data and generators; `Modulation/` the product's modulation-target table and snapshot;
  `Persistence/` preset and state (de)serialisation and migrations; `License/` licensing glue; `UI/` the
  panels, overlays and sidebars; `Tests/` the unit tests.
- `Plugin/` holds only the processor (and its `PluginProcessor_<Aspect>.cpp` parts), the editor and the
  standalone entry. Engine, sampler and preset code belongs in `Audio/` and `Persistence/`.
- The processor splits as `PluginProcessor_<Aspect>.cpp` with a fixed aspect set:
  `APVTS`, `Preset`, `Modulation`, `Content`.

### Identifiers
- Members: **no suffix** (`proc`, `sampleRate`). No `m_`, no trailing `_`.
- Constants: `kName`. Enums: `enum class`. Callbacks: `onThing`.
- Capacity constants come from `mu_limits` (`kMaxLayers`); a product does not redeclare them.
- Acronyms: caps in **type** names (`FXChain`, `LFOEditor`, `VUMeter`); word-case in functions and
  variables (`startFxParamSync`, `bindStripToApvts`, `getBpm`, `Midi…`).

### Saved-data names (change only with a migration)
Parameter-id prefixes (`r0_`, `v0_`, `k_ b_ h_ s_ r_`), preset extensions (`.muRhythm`, `.muPattern`,
`.muArp`, `.muTrack`), content folders, state identifiers (`Mu<Product>State`) and plugin codes
(`MCld MTnt MTni MuOn`) are *data*: renaming them touches presets and DAW sessions. **Leave them until
the combined instance is scheduled** — the layer-type registry should own prefixes then. The composed
slot state already stores ids without their prefix, so layer data is prefix-agnostic on disk.

---

## 4. Audit — where the code differs from §3 (2026-10-09)

| Area | Finding |
|---|---|
| Layer noun | Rhythm (mu-Clid, ~1580 uses), Voice (mu-Tant, ~820), Layer (mu-Toni), Lane / Channel / Track (mu-On). |
| Parent class | Only `Rhythm` derives from `VoiceSlot`; the others use parallel arrays (§2). |
| mu-core vocabulary | `kMaxChannels` is the layer cap; core types say both `Voice*` and `Channel*`; "Slot" has three meanings (11 types). |
| Namespaces | mu-Clid: all but one of 46 types are global; also stray namespaces `md`, `ModDest`, `mu_pp_migrate`. |
| Overlay vs Panel | `AboutPanel`, `ActivationPanel`, `MidiPresetsPanel`, `MidiFullPresetsPanel` are overlays; `SaveDialog`, `PresetBrowser` too. |
| Main sound panel | `VoiceSection` (mu-Clid), `VoicePanel` (mu-Tant), `EnginePanel` (mu-Toni, mu-On), `VoiceBand` (core). |
| Sequencer names | `SequencerEngine`, `GatePattern`, `Arpeggiator` + `ArpVoiceRunner`, `GrooveSequencer` — no shared role. |
| Capacity constants | `kMaxRhythms`, `kMaxVoices` (twice in mu-Tant), `kMaxChannels` (redeclared in mu-Toni and mu-On) beside `mu_limits::kMaxChannels`. |
| Acronyms | `…APVTS` function names in mu-Clid vs `…Apvts` elsewhere. |
| Members | Trailing `_` in seven classes (mu-Clid helpers, `MidiClockSync`, `VocoderInsert`). |
| Enums | plain `enum` in mu-Toni (3), mu-On (3), mu-Tant (2). |
| Folders | Missing from the standard eight: mu-Clid has no `Audio`; mu-On has no `License` or `Persistence`. mu-Tant and mu-Toni `Persistence/` hold only `.gitkeep`. mu-Clid `Plugin/` holds `RhythmManager`, `SampleLibrary`, `SamplePreview`, `PresetIO`, `HotSwap*`, `ModulationSkew` that belong in `Audio/`, `Persistence/` and `Modulation/`. |
| Saved data | Four param-id schemes; layer-preset extension and folder nouns disagree (mu-Tant: `.muPattern` in `Voices/`); mu-Tant's `.muPattern` already matches its `Pattern` layer type. |

---

## 5. Migration order (safest first)

C++ identifier changes never touch saved files; the data renames at the end do.

1. **Cosmetic, any product, any time:** trailing `_` → none; plain `enum` → `enum class`; `…APVTS` →
   `…Apvts`; drop the redeclared `kMax*` in favour of `mu_limits`. (Do the `MidiClockSync` one while
   rewriting it for the sync work.) Backlog #1267.
2. **mu-Clid into `mu_clid`** — the largest mechanical rename; do on the build PC, one file group at a
   time, after the sync fixes that touch the same files. Backlog #1262.
3. **Overlay / Panel renames** for the shell screens; shared `LayerPanel` / `LayerSidebar` in `mu-core`. Backlog #1266.
4. **`VoiceSlot` → `Layer`**, then hoist the shared per-layer data into it product by product
   (mu-Tant and mu-Toni first — they have the parallel arrays); `Slot*` persistence types → `Layer*`. Backlog #1265.
5. **Derived layer types** (mu-Tant `Pattern`, mu-Toni `Arp`, mu-On `Track`; mu-Clid's `Rhythm` already
   exists) replace the parallel arrays; sequencer role names (`<X>Sequencer`) follow. Backlog #1264.
6. **Saved-data names** — only with the combined instance, with migrations. Backlog #1263 (On Hold).

Separately, the **standard `Source/` layout** (eight folders in every instrument product) is created now
for the empty ones and completed by moving the misplaced files. Backlog #1268.

Each step: Debug build + unit tests + the round-trip listening tests on the build PC before the next.

## 6. Open questions


- Is the shared `Layer` polymorphic, or a table of registered layer types?
- Do the ~130 global `mu-core` types move into `mu_*` namespaces, or stay as the shared "house" types?
