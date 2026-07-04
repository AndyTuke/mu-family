# μ-Toni — Product design (overview)

**Draft.** Captures the agreed MVP shape. The novel surface is the sequencer;
everything else is inherited from `mu-core` + the mu-tant engine unchanged.
Read the family platform contract first:
[design-plugin-family.md](../design-plugin-family.md).

Sibling doc: [design-sequencer.md](design-sequencer.md) — the generative
arpeggiator, which is the whole point of the product.

---

## Concept — what mu-toni *is*

μ-Toni is the family's **generative arpeggiator**. The player chooses a **scale**
and a **chord within that scale**; the sequencer arpeggiates that chord across a
selectable octave range in a selectable direction. The sound source is
**mu-tant's wavetable synth engine, reused verbatim** — μ-Toni is "mu-tant, but
its pitch is driven by an arpeggiator instead of held as a drone."

The defining idea: **each step's note is a pure function of the current
parameter set.** There is no baked-in note list. Every step re-evaluates "what
note would this be, given the scale / chord / octaves / direction *right now*."
Because the mapping is a pure function of its inputs, **every arp control is a
mu-core modulation destination** for free — modulate the octave count with an
LFO, sweep the direction with a control sequence, and the arp re-derives its
notes live. See [design-sequencer.md](design-sequencer.md) for the model.

### Name origin + settings easter-egg

The name **μ-Toni** comes from **Tony**. However many times he was told an
arpeggiator was an *arpeggiator* — an Italian word — Tony always called them
**"appagators"**. A gentle soul; the name is an affectionate nod to him. The
**Settings screen carries his line verbatim** as a small credit:

> *Its an appagator, is it a powerful tool?*

Keep it **exactly** as written — **"appagator" is Tony's word and the whole point;
never "correct" it to "arpeggiator".** It renders as an italic quote line on the
SettingsOverlay (alongside the shared "Contact Support" chrome), styled with
`labelText` / `mutedText`, no other emphasis.

---

## What mu-toni reuses (unchanged)

μ-Toni is deliberately built from **existing family parts** — mu-tant's *timbre*,
mu-clid's *envelopes*, and mu-core's *platform* — with only the sequencer new.
It's the "engine swap-point" pattern: same UX, same mixer, same modulation.

| Subsystem | Source | Notes |
|---|---|---|
| **Oscillator timbre** — 2 wavetable oscillators, 2-lane X-Mod (FM/PM/TZFM + Sync + Feedback / AM·RM·SSB), scale-quantised pitch, mu-core filter, insert | **mu-tant engine** ([docs/mu-tant/design-voice.md](../mu-tant/design-voice.md)) | The *sound* is mu-tant's. Behavioural change: the voice is **note-triggered**, not a held drone (see below). |
| **Amp + Filter (+ Pitch) ADSR envelopes** | **mu-core** — `juce::ADSR` + `VoiceParams` env fields, as used by mu-clid's `VoiceEngine` ([VoiceParams.h](../../mu-core/Audio/VoiceParams.h)) | Added because μ-Toni is a note-triggered mono synth. **Reused, not invented** — mu-clid already drives these. |
| **Wavetable bank + oscillator** | mu-tant `WavetableBank` / `WavetableOscillator` | Same tables, same mip-mapping. |
| **Modulation section** — LFOs + control sequences + `ModulationMatrix` | `mu-core` | Unchanged. The arp + envelope + portamento parameters register as **new destinations** (the mu-toni-specific modulation addition). |
| **Mixer / FX rack** — channel strips, sends, sidechain, Effect/Delay/Reverb slots, master inserts, VU | `mu-core` `MixerEngine` + `MixerOverlay` + `mu_mixfx::addGlobalFxParams` | Standard mu mixer, unchanged. |
| **Editor shell** — TransportBar, StatusBar, About, overlays, window sizing, `MuLookAndFeel`, `ChannelSidebar` | `mu-core` `EditorShellBase` | Already wired in the scaffold. |
| **Preset system + MIDI program-change** | `mu-core` (`ProcessorBase` virtuals) | Standard, **same MIDI settings as mu-tant**. Extensions renamed to the arp's real noun — see below. μ-Toni **adds played-note input** on top (root + trigger) — the one MIDI departure, in [design-sequencer.md](design-sequencer.md) §"MIDI control". |

**Net new code** lives under `mu-toni/Source/`: the arpeggiator engine
(`Sequencer/`), the note-on/off + ADSR wiring into the wavetable voice, its
editor panel (`UI/`, replacing the blank `EnginePanel`), the scale/chord tables,
and the arp modulation-destination provider (`Modulation/`).

---

## What differs from mu-tant

| Aspect | mu-tant | mu-toni |
|---|---|---|
| Pitch source | Held drone — per-osc `octave`/`tone`/`fine`, modulator-driven | **Arpeggiator** — per-step note computed from scale + chord + octaves + direction |
| "Sequencer" | Drawable 2-bar volume-**gate** grid | **Generative arpeggiator** (this doc's sibling) |
| Voice trigger | Drone — oscillators free-run, **no envelopes** | **Note-triggered mono synth** — each step is a note-on with **Amp + Filter + Pitch ADSR** (mu-core envelopes) |
| Note articulation | None (continuous drone; gate chops volume) | Per-step: **Gate Length** (%-of-step) → note-off → Release; **Legato** voice param ties 100%-gate notes |
| MIDI note input | **None** — PC-only, no played notes | **Consumes played notes** — root selectable by MIDI note + a Loop / MIDI-triggered run mode (PC→preset unchanged). See [design-sequencer.md](design-sequencer.md) §"MIDI control". |
| Pitch glide | None (drone; no note-to-note movement) | **Portamento** (ms) slew, **heard only on legato (100%-gate) notes** — 303-style slides driven by modulating Gate Length. Pitch-path addition, no shared-engine change. |
| Licensing | Licensed (key + activation) | **Freeware** — no licence / activation / demo (see "Licensing / distribution") |
| Product identity | Evolving drone | Note-triggered arpeggiator / mono synth |

Everything not in this table is identical to mu-tant.

---

## Naming / file formats (provisional)

The scaffold ships placeholders (`.muToni` full, `.muLayer` per-slot) pending
the engine. Per the family consistency rule (per-slot preset = camelCase noun),
rename the per-slot extension to the arp's real noun once settled — candidate
**`.muArp`** (one arp configuration = one slot). Full preset stays **`.muToni`**.
Wired via the `ProcessorBase` virtuals exactly as mu-tant wires `.muPattern` /
`.muTant`.

---

## Slots / voices

Inherits mu-tant's **dynamic 1–8 voice slots, default 1**, via the shared
`ChannelSidebar`. Each slot is an **independent monophonic arp** — its own timbre,
arp settings, envelopes, and (per the self-contained-channels rule) its own
chord/root. Stacking slots = layered/detuned/interlocking arps over one shared
transport. A fresh preset opens with **1 voice** so it stays a "simple mono
synth"; the rest are opt-in.

## Voice chain (per slot)

```
Osc1 + Osc2 → X-Mod → Filter → Amp ADSR (VCA) → Insert → mixer bus
                        ↑              ↑
                  Filter ADSR      Pitch ADSR shifts Osc pitch pre-filter
```

The **Insert** is the **standard shared mu-core `InsertProcessor`** — the same
per-voice insert effect and full algo set mu-tant/mu-clid use, selected via the
shared `InsertSubsection` UI (one insert per voice, post-VCA, pre-mixer).
**Confirmed: yes, every voice has the standard insert effect.**

---

## mu-link integration (required, standalone-only)

μ-Toni **must** support mu-link, using the exact family pattern (mu-clid /
mu-tant). It is **standalone-only by design** — in a DAW the host owns the clock
and device, so there's nothing to add; the plugin builds never compile the
bridge.

- **Bridge:** the shared header-only [mu-core/Link/MuLinkBridge.h](../../mu-core/Link/MuLinkBridge.h),
  `#include`d **only** by `mu-toni/Source/Plugin/StandaloneApp.cpp` — so VST3/CLAP
  never compile it and a plugin can never attach. Never add mu-link hooks to the
  shared `PluginProcessor` beyond the playhead consult below.
- **Transport slaving:** `processBlock` reads the playhead via
  `mu_core::readHostTransport()` (carries `hasPosition`/`ppqPosition`). In
  **standalone** it slaves the beat to `ppqPosition` when present (mu-link
  attached) and free-runs the internal transport otherwise — plugin and
  free-running-standalone behaviour byte-identical. This is a **one-liner-per-
  product** hook; μ-Toni just adds it (design-future.md lists mu-toni as
  not-yet-wired).
- **Audio bus:** mu-link sums the **post-mixer** `processBlock` output, so the
  arp/synth engine is irrelevant to the bus path — μ-Toni contributes its master
  output like any client.
- **Identity + preset name:** the standalone passes its name **"mu-Toni"** via
  `mu_standalone::App` → `makeStandaloneBridge` → `MuLinkClient::attach`, and
  publishes its current preset name over the bus (the mu-link strip can show it),
  same as mu-clid/-tant.

**Arp-specific consequence:** because the arp's **Loop (free-run)** mode and its
**Rate** run off the shared transport, when mu-link is attached the arp is
**slaved to the mu-link master clock** automatically — the whole point of the
integration. mu-link **scene recall** (which fires PC across connected
instruments) drives μ-Toni's standard PC→preset path with no extra work. See
[docs/mu-link/design-mulink.md](../mu-link/design-mulink.md) and mu-tant's
integration notes for the reference implementation.

---

## Licensing / distribution — freeware

**μ-Toni ships as freeware.** No purchase, **no licence key, no activation, no
demo/limited mode** — the full plugin is freely distributed.

- **Not used:** the mu-core **License** subsystem (`LicenseManager`,
  `ActivationStore`, `OnlineActivation`, `LemonSqueezyClient`, `MachineFingerprint`)
  and the shell's **Activation/licence UI** (`ActivationPanel`). No Lemon Squeezy
  product, no signing key, no `.muToni` licence gating.
- The scaffold's `Source/License/LicenseKey.h` + the demo-gate pattern from
  design-future.md (`-DMU_<PRODUCT>_DEMO`) are **not applied** to μ-Toni.
- **About panel** shows the normal product/version/credits but **no licence
  status / activate button**.
- Everything else is standard: still builds Standalone + VST3 + CLAP, still ships
  via the installer + the cross-platform GitHub release, still deploys to testers.
  It just skips the paywall.

This is the family's first freeware product — a deliberate difference from
mu-clid/mu-tant (which are licensed). If a second freeware sibling appears, a
shared `-DMU_<PRODUCT>_FREEWARE` switch that compiles out the licence/activation
paths would be the clean generalisation.

---

## Open questions (product-level)

The core is settled. **Resolved:** monophonic (arpeggio, not chord player);
The design is settled after a full grilling pass. **Resolved:** monophonic
(chord = pool selector); **note-triggered mono synth** with **Amp + Filter +
Pitch ADSR** (integration B); each voice runs the **standard mu-core insert**;
chord/root **sequenced via the modulator** (no timeline); **Direction =
skewed-triangle scan** (deterministic, identical every loop); **articulation =
Legato voice param + Gate Length** (glide only on tied notes, ms glide);
**bipolar Inversion**; all chords ship (grouped); **diatonic-snap toggle**;
**Rate = note values** (+dotted/triplet); **1–8 slots, default 1**; **Loop-mode
default, sounds on play**; **freeware** (no licence/demo); per-slot `.muArp`.
Fine-tuning only remains — see [design-sequencer.md](design-sequencer.md)
§"Open questions".

---

## Design-standards alignment

Checked against the family "Critical architectural rules" ([/CLAUDE.md](../../CLAUDE.md))
and the [UI design system](../design-ui-family.md):

| Standard | μ-Toni | 
|---|---|
| **Everything in APVTS** | All arp / envelope / portamento controls are APVTS params (that's what makes them modulatable + savable). ✓ |
| **Audio thread never allocates** | ADSR / slew / pool buffers prepared in `prepareToPlay`; arp math is bounded, alloc-free. ✓ (design-sequencer.md §"Audio-thread contract") |
| **mu-core never depends on a plugin** | All new code under `mu_toni::`; mu-core untouched. New envelopes reuse mu-core `VoiceParams`/`juce::ADSR` without modifying mu-core. ✓ |
| **ModulationMatrix is the single reader** | The arp reads post-modulation values from the matrix, not raw APVTS. ✓ |
| **Channels self-contained** (no cross-voice mod) | Each voice's arp + modulators target only its own params — consistent with why a *shared* chord progression isn't a modulator feature (it'd be cross-voice). ✓ |
| **All UI uses the shared component library** | Direction/Filter-Env-Depth → `BipolarSliderRow`; dropdowns → `DropdownSelect`; modes → `SegmentControl`; knobs → `KnobWithLabel`; sidebar → `ChannelSidebar`. No one-offs. ✓ (reuse map) |
| **Colours/sizes only in `MuLookAndFeel`** | Knob colours mapped to family tokens (amber `knobLevel` for ADSR, teal `knobPostPad` for filter, purple `knobEuclidean` for arp/seq). Sizes via `mu_ui::s()`. ✓ |
| **StatusBar-first, full words, no tooltips** | All knobs report to the StatusBar; labels are full words ("Attack" not "ATK"). ✓ |
| **Family consistency** (Source layout, naming, preset nouns) | `{Plugin,Sequencer,UI,Audio,Modulation,…}` mirrors mu-tant; per-slot preset noun = camelCase (`.muArp`), full = `.muToni`. ✓ |
| **Engine swap-point pattern** | μ-Toni supplies engine+arp, inherits `ProcessorBase` + `EditorShellBase`; renders via `processCoreBlock(renderVoiceCb)`. ✓ |

**One item with no family precedent:** **played MIDI-note input** (root + trigger).
mu-clid/-tant/-on are all PC-only; note-in is genuinely new mu-toni code
(`processBlock` MIDI parse → root/trigger state). Small and self-contained, but
worth noting it's the first family member to consume played notes — if a second
sibling ever wants it, lift the note-priority/held-stack helper to mu-core then.

---

## Docs to add as decided

Mirror the mu-tant set (`docs/mu-toni/`): `design-voice.md` only if μ-Toni ever
diverges from mu-tant's voice (for now it points at mu-tant's), `design-hotswap.md`
when preset staging lands, `create_manual.ps1` at ship time.
