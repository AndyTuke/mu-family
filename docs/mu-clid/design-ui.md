# μ-Clid — UI Layout Design Reference

## Root Layout

```
TransportBar     36px  (top) — Stage 10
LayerSidebar   82px  (left, always visible)
RhythmPanel or MixerOverlay  (remaining area)
StatusBar        20px  (bottom, full width)
```

Window: fixed at 1170×870 (Medium baseline). The window is non-resizable; Small / Large variants arrive as global `mu_ui::scale` multipliers on the Medium constants.

```cpp
setResizable(false, false);
setSize(mu_ui::s(MuLookAndFeel::kWindowWidth),
        mu_ui::s(MuLookAndFeel::kWindowHeight));
```

Every layout dimension is a named Medium constant in `MuLookAndFeel.h` wrapped in `mu_ui::s()` at the call site. Toggling the global `mu_ui::scale` rescales the whole UI uniformly — no per-component responsive math.

## Transport Bar

Left→right: μ-CLID logo (click=About), play/stop, BPM, position (bar.beat), loop dropdown, preset selector (fills available width), save, mixer button, gear icon.

| Element | Plugin mode | Standalone mode |
|---|---|---|
| Play/stop | Reflects DAW transport | Drives internal clock |
| BPM | Read-only from host | Editable (nudge + tap tempo) |
| Position | Read-only from host | Resets on stop |
| Mixer button | Opens mixer overlay (replaces rhythm panel) | Same |

## Rhythm Sidebar (82px fixed, always visible)

```cpp
kWidth   = 82;   // sidebar fixed width
kItemH   = 80;   // per SidebarItem
kAddBtnH = 34;   // add button at bottom
```

- One `SidebarItem` per active rhythm, in creation order
- `SidebarItem` content: small `RhythmCircle` (top portion), colour dot + rhythm name (bottom 14px)
- Selected item: right-edge 3px vertical tab line in rhythm colour (connects visually to main panel)
- Hit pulse: sidebar item circle pulses with rhythm colour on each hit
- Add rhythm button at bottom: dashed border, "+" prefix, always visible, disabled at 8 rhythms
- Scrollable via `juce::Viewport` if more items than fit (unlikely with max 8)
- Drag-to-reorder: fully implemented. `LayerSidebar` supports variable item order.
- Deleted rhythm: item removed immediately, no placeholders
- Right border line drawn in `paint()` to visually separate from main panel

## Rhythm Panel (main editing surface)

The main panel is the family's shared `LayerPanel` stack ([design-ui-family.md section 5](../design-ui-family.md), "Per-layer panel stack"); mu-Clid supplies the 464 px content band (`RhythmContent`). All values are `MuLookAndFeel` tokens:

```
Header strip        32  (kLayerHeaderStripH) shared LayerHeaderBar: dot, name, preset dropdown, Save, Reset, Delete
Content band       464  (kLayerContentH), RhythmContent:
    Sample bar      32    sample LCD + Sample | MIDI selector (the same strip height as the header)
    Circle row     288    RhythmCircle (kCircleSize, square) | EuclideanPanel (800 wide)
    Voice band     144    VoiceSection (or, in MIDI output mode, the MIDI Out section)
Modulator panel    318  (kLayerModulatorH) shared ModulatorPanel, absorbs banner loss
```

The sample bar, circle row and voice band tile the content band exactly (32 + 288 + 144 = 464, `static_assert`ed in `RhythmContent.h`) with no flex panel.

### Header Bar

The shared `LayerHeaderBar`, identical in every product (not described again here): colour dot (click opens the palette), layer name (click to rename), preset dropdown listing `.muRhythm` files, Save, Reset (circular arrow, confirms) and Delete (x, confirms). Mute and solo are on the mixer channel strip, not the header.

### Sample Bar

A 32 px strip holding the **sample LCD** (left, fills the strip) and the **Sample | MIDI** output selector (right end; see "Output mode" below).

- No sample: ghost lettering "drop sample here or click to browse"
- Loaded: the file name on the LCD
- Missing: amber "Missing: <name> - click to find"; clicking locates a replacement
- Browse mark at the LCD's right edge; click anywhere on the LCD opens `juce::FileChooser`
- Drag and drop accepts `.wav .aiff .aif .mp3 .flac`
- MIDI output mode: ghost lettering "MIDI output", browse mark hidden

### RhythmCircle

Concentric rings, outside→inside: Euclid A (purple), Euclid B (coral), Euclid C accent (dashed amber), then mod rings in assigned colours.

**Ring geometry** (proportional, works at both 300px panel and ~50px sidebar size):
```
maxR     = min(cx, cy) - 3
ringW    = max(5, maxR * 0.20)    // ring band width
ringGap  = max(2, maxR * 0.05)   // gap between rings
Ring A:  outer = maxR,  inner = maxR - ringW
Ring B:  outer = ringA_inner - ringGap,  inner = outer - ringW
Ring C:  outer = ringB_inner - ringGap,  inner = outer - ringW  (dashed)
```

**Step rendering in rings:**
- Hit steps: filled arc in ring's colour
- Empty steps (no hit, no pad): dim outline arc
- Pre-padded steps: filled arc in `ringPrePad` colour (cyan-teal)
- Post-padded steps: filled arc in `ringPostPad` colour (teal)
- Insert-padded steps: filled arc in `ringInsertPad` colour (pink)

This lets the user see at a glance which gaps in the ring were created by which pad type.

- Active step indicator on each ring
- Centre filled with `panelBackground`
- On hit: pulse alpha set, decays by 0.06 per timer tick at 30Hz
- *(Stage 10: + expanding pulse ring animation from step position; rings rotate during playback, step 1 at 12 o'clock at position 0.0.0.0)*
- ControlSequence rings: variable-width band showing modulation shape — wider = higher value

### EuclideanPanel

The shared `mu_ui::EuclideanPanel` (mu-core, also used by mu-Clid MIDI). Three identical rows named by name plates, **Euclid A**, **Euclid B** and **Accent** (the Accent row is Euclid C, shaped exactly like A and B). Each row is three raised boxes plus, on the far right of the panel, one shared switch column:

```
[ Euclid box ] [ Pad box ] [ Insert box ]                      Trig | Leg
 Steps Hits Rotate  PrePad[sw] PostPad[sw]  InsStart InsLen [sw]   Poly | Mono
```

All knobs are Size 1 (`kKnobSize1W` x `kKnobSize1H` = 68 x 70). **All knob labels use full words, no abbreviations, no units.**

| Box | Controls | Colour |
|---|---|---|
| Euclid | Steps `1-64`, Hits `0-steps`, Rotate `0-(steps-1)` | `knobEuclidean` purple (Accent row: `knobLevel` amber) |
| Pad | Pre Pad `0-12`, Post Pad `0-12`, each with its own **Pad / Mute** `SlideSwitch` beside the knob | `knobPrePad` cyan-teal, `knobPostPad` teal |
| Insert | Insert Start `0-(steps-1)` (shown from 1), Insert Length `0-8`, one shared **Pad / Mute** switch to their right | `knobInsertPad` pink |

The Accent row has the same Pad and Insert boxes as A and B (it is not reduced to three knobs). Row geometry (box heights, gaps, plate size, switch gaps) comes from `EuclideanPanel` constants derived from `MuLookAndFeel` tokens; the Euclid, Pad and Insert box widths are what is left of the 786 px inner width after the switch column (`kModeColW`).

**Pad | Mute:** Pad takes the silent steps out of the pattern and spreads the hits over the rest; Mute keeps the pattern spread over every step and silences the hits that land in the silent steps. The status bar spells out the current setting.

**Logic selector.** One `DropdownSelect` (`getLogicControl()`, `kLogicDropW` x `kLogicDropH`) placed by `RhythmContent` in the bottom-right corner of the circle panel, just inside its border, over the circle's empty corner. `kLogicDropW` 64 x `kLogicDropH` 15. Five choices shown as logic symbols (the status bar spells each out): `A ∨ B` (OR), `A ∧ B` (AND), `A ⊕ B` (XOR), `A∧¬B` (A not B), `B∧¬A` (B not A); param `r{N}_logic`. It is not in the panel's own bounds.

**Trig | Leg and Poly | Mono.** Two `SlideSwitch`es stacked in the right-hand column (`legatoCtrl` over `monoCtrl`, centred on the panel height, `kModeSwGap` apart).
- **Trig | Leg** (`r{N}_patLeg`, Pattern Legato): Trig retriggers the pitch, filter and amp envelopes on every hit; Leg lets back-to-back hits skip the retrigger so the envelopes ride through them as one note.
- **Poly | Mono** (`r{N}_vMono`): Poly gives each hit its own voice; Mono lets each hit cut off the one before.
- Both are **voice-engine switches**: they act on the sample voice, so they are meaningful only where there is a voice. See "Shared switches in mu-Clid MIDI" below.

**Accent (Euclid C).** Marks which hits are accented: a step where the combined A and B pattern fires at the same time as a C hit is accented. The accent gain is the Accent knob of the Amp box (sample mode) or of the MIDI Out section (MIDI mode). The C ring is the dashed amber ring between ring B and the mod rings.

Ranges update dynamically: when Steps changes, Hits max, Rotate range and Insert Start max clamp to the new value, and the pads and insert can never use more steps than the pattern has.

### Shared switches in mu-Clid MIDI (ruled 2026-10-12)

mu-Clid MIDI has no voice, so **Trig | Leg and Poly | Mono do nothing there** (its `MidiRhythmCore` never reads `patternLegato` or `voiceMono`; every hit is one fixed-length note, 20 ms). They are hidden in mu-Clid MIDI, not relabelled and not kept: a control that changes nothing is a lie in a product whose whole interface is its sequencer. The `patLeg` and `vMono` parameters stay in the parameter layout and in saved state (so `.muRhythm` presets and old sessions stay compatible and the ids never change); only the controls are hidden. The switch column is left empty (the panel layout does not reflow), so the Pad and Insert boxes line up with mu-Clid's. If MIDI ever gains a gate length, Leg (tie to a held note) and Mono (cut the last note) would become meaningful and the switches return, with a status-bar wording about notes rather than envelopes; that is a new ruling, not a revert.

### Pad Colour System

Three pad types each have a distinct colour used consistently across knobs and ring display:

| Pad type | Knob ColourId | Ring ColourId | Suggested hex | Purpose |
|---|---|---|---|---|
| Pre Pad | `knobPrePad` | `ringPrePad` | `#2BB5C5` cyan-teal | Silence before pattern |
| Post Pad | `knobPostPad` | `ringPostPad` | `#1D9E75` teal | Silence after pattern |
| Insert | `knobInsertPad` | `ringInsertPad` | `#D4537E` pink | Silence within pattern |

The existing `knobPadding` ColourId is retained as a legacy alias for `knobPostPad` in `MuLookAndFeel.h` (`0x10000011`). It can be removed once all callsites migrate.

### VoiceSection

The shared `VoiceBand` (mu-core) filled with mu-Clid's subsections: 144 px tall, four raised boxes **Pitch | Filter | Amp | Effects** with name plates in a 14 px plate band (`kVoiceLabelH`) and a 6 px divider (`kVoiceDivW`) between boxes; below the plates, two Size 2 knob rows (`kKnobSize2H` 56) 4 px apart (`kVoiceGap`), 116 px tall (`kVoiceSubH`). The band starts `kLeftGap` (7) in so the Pitch box clears the panel's corner screw.

```
kVoiceUnitW = 54       // Size 2 knob cell width (= kKnobSize2W)
kVoiceFilterColW = 50  // Filter's narrower column
Pitch   4 x 54  = 216      Filter  6 x 50 = 300      Amp  4 x 54 = 216
Effects = 308 (324 less kLeftGap; the three sends are right-aligned in its top row)
```

**Column layout (as built):**

```
PITCH (4 cols)               FILTER (6 cols)                      AMP (4 cols)               EFFECTS
Row 1:                       Row 1:                               Row 1:                     Row 1:
  Octave|Semitone|Fine|Depth   Type (2 cols)|Drive|Cutoff|Res|Low Cut   Level|Accent|(gap)|Pan   Insert dropdown | Effect | Delay | Reverb sends
Row 2 (envelope):            Row 2 (envelope):                    Row 2 (envelope):          Row 2:
  Atk|Dec|Sus|Rel              Atk|Dec|Sus|Rel|Depth                Atk|Dec|Sus|Rel            4 insert slot knobs
```

**All knob labels use full words, no abbreviations, no units** (units appear in the status bar). The Pitch envelope Depth sits in row 1 above Release, so Pitch needs only four columns.

**Pitch** (purple, `knobEuclidean`):
- Octave -3..+3 (step 1), Semitone -12..+12 (step 1), Fine -100..+100 cents (step 1)
- Envelope: Attack, Decay, Release 0-10 s (skew 0.3), Sustain 0-100, Depth 0-24 semitones (row 1, column 4)

**Filter** (teal, `knobPostPad`):
- Row 1: Type (`DropdownSelect`, 2 cols, half row height, `kDropdownEdgeGap` off the box edge; 16 modes in the canonical `populateFilterTypeDropdown` order: LP 6 / LP 12 / LP 24 / BP 12 / BP 24 / HP 6 / HP 12 / HP 24 / Notch / Notch 24 / AP 12 / Comb + / Comb - / Peak / Lo Shf / Hi Shf), Drive 0-100, Cutoff 20-20000 Hz (mid-point skew 640 Hz), Resonance 0-0.99 (shown 0-100), Low Cut 0-1000 Hz (0 reads Off)
- Envelope: Attack, Decay, Release 0-10 s, Sustain 0-100, Depth 0-48 semitones of cutoff sweep

**Amp** (amber, `knobLevel`):
- Row 1: Level -60..+6 dB (bottom reads -inf), Accent 0..+12 dB, Pan -1..+1 (column 4, no value text on the knob; the same pan as the mixer strip)
- Envelope: Attack, Decay, Release 0-10 s (Release at 10 reads End), Sustain 0-100

**Accent.** When a step is accented (Ring C fires with a Ring A+B hit), that step's amplitude is boosted by the Accent amount above Level. 0 dB = no accent effect. The Accent knob sits immediately right of Level.

**Effects** (`knobInsertPad` pink; the shared `InsertSubsection` plus the three sends):
- Row 1: Insert Character dropdown (narrowed so the sends fit; `setAlgoWidth`) and the three FX sends **Effect, Delay, Reverb** (0-100, `knobFxSend` coral; the Effect send is relabelled with the mixer's current effect name, for example "Phaser"). They are the same sends as on the mixer channel strip, 51 px wide (`kSendColW`), right-aligned so the box ends short of the panel's corner screw.
- Row 2: 4 slot knobs whose labels, ranges and meaning are set per algorithm by the shared `kInsertAlgoSlots` table (e.g. Drive / Output / Dither / LPF for Soft Clip; Bits / Rate / Dither / LPF for Bitcrusher; Threshold / Ratio / Attack / Release for Compressor; Low / Mid / Mid Hz / High for 3-Band EQ). Per-algorithm values are cached so switching algorithms restores the last values for each.
- 14 algorithms, alphabetised after None: None / 3-Band EQ / Bitcrusher / Clipper / Compressor / Fold / Hard Clip / Karplus-Strong / Limiter / Ring Mod / Soft Clip / Tape Sat / Vocoder / Vocoder St. None passes audio through at unity. Character switch is message-thread only.

**Output mode (Sample / MIDI), ruled 2026-10-11.** The earlier idea of a header-bar dropdown is dropped: the header bar is the shared `LayerHeaderBar` (dot, name, reset, delete, preset, Save), identical in every product, and has no room or place for a product control. The selector lives in mu-Clid's own content band, in the **sample bar** strip (32 px, the first row under the header strip):

- **Selector:** a `SegmentControl`, `DrawStyle::Lcd`, `ActiveStyle::General`, items **Sample** and **MIDI**, at the right end of the sample bar, **112 px wide** (2 x 56, `RhythmPanel::kOutputSelectorW`) and `kStripDisplayH` (20 px) tall, vertically centred, its right edge `kScrewedPanelInset` (9 px) from the strip end so it clears the corner screw (non-screw style: `kHeaderInsetX`). The sample LCD takes the rest of the strip and stops `kSpaceXS` (4 px) short of the selector. Per rhythm; param `r{N}_outMode` (0 Sample, 1 MIDI), default Sample. Status bar: `Output` / `Sample (plays the loaded sample)` or `MIDI (sends notes, no audio from this rhythm)`.
- **Sample mode:** unchanged.
- **MIDI mode:** the sample LCD shows ghost lettering "MIDI output" and its browse mark is hidden; the 144 px voice band is replaced by **one raised section "MIDI Out"** (`drawSections`, name plate) of the same 144 px height, holding, left to right: **Note** (`DropdownSelect` of note names, the same list as mu-Clid MIDI's note selector, default 36), **Channel** (`DropdownSelect` 1 to 16, default 1), **Velocity** (`KnobWithLabel`, `knobLevel`, 1 to 100 %, default 100), **Accent** (`KnobWithLabel`, `knobLevel`, 0 to 100 %, default 0; adds to the velocity of accented hits, as mu-Clid MIDI's Accent knob does). Param ids `r{N}_midiNote`, `r{N}_midiChan`, `r{N}_midiVel`, `r{N}_midiAccent`; none is a modulation target in this first version (a target would be one appended `ModTarget` row later).
- **Built sizes (commit 7a83b42, `MidiOutSection.h`).** Box: x = `VoiceBand::kLeftGap` (7, where the Pitch box starts), y = `kVoiceLabelH` (14), **324 px wide** (6 x `kVoiceUnitW` 54: Note 2 cols = 108, Channel 2 cols = 108, Velocity 1 col = 54, Accent 1 col = 54), `kVoiceSubH` (116) tall; it is wider than the 216 px Pitch box and ends inside the Effects box edge (static_assert). One control row of `kKnobSize2H` (56) centred in the box. Each dropdown cell: engraved caption ("Note" / "Channel", `kKnobLabelFont` 10, `labelText`) in a 14 px band, then the `DropdownSelect` 2 px below it, 28 px tall (`kKnobSize2H / 2`), kept `kDropdownEdgeGap` (6) off each side of its cell, so 96 px wide. The Note popup lists all 128 notes named C-1 to G9 by `mu_core::midiNoteName` (middle C = note 60 = C4, so the default note 36 is C2; id = note + 1); Channel lists 1 to 16.
- **Mixer:** a MIDI-mode rhythm's strip is drawn inactive (`MixerChannel::setActive(false)`, through the `MixerOverlay::isChannelSilent` hook set in the mu-Clid editor) and its sends and voice controls are ignored; it makes no audio.
- **Sidebar and header:** unchanged (the mini-circle still pulses on hits; a small "MIDI" tag is not added).
- **Fit:** the section is 144 px tall in the 144 px band (same as the voice band) and 324 px wide, well inside the 1088 px panel.
- **Shared note:** two rhythms set to the same note and channel share one downstream voice, so one rhythm's 20 ms note-off cuts the other's note; this is by design.
- **Token review (2026-10-12):** every dimension is derived from family tokens (`kVoiceUnitW`, `kVoiceSubH`, `kVoiceLabelH`, `kKnobSize2H`, `kDropdownEdgeGap`, `kStripDisplayH`, `kScrewedPanelInset`, `kSpaceXS`); 324 px is not a token and should not become one (it is 6 columns). Two residuals are product-local magic numbers: `kOutputSelectorW = 112` and `MidiOutSection::kCaptionH = 14` (+2 gap). Brief: add `MuLookAndFeel::kLcdSegmentW = 56` (one LCD segment window) and set the selector width to `2 * kLcdSegmentW`, and set `kCaptionH = kVoiceLabelH` (the same 14 px name-plate band) with the 2 px gap as `kSpaceXS / 2`. Low priority; no on-screen change.

### ModulatorPanel (Stage 7)

Tabs: Mod A – Mod H, Matrix. Inactive tabs dimmed.

**Mod tab contents:**
- Header: modulator name, colour dot, smooth/stepped toggle, internal/CC toggle
- LFO curve editor (smooth) or step bar graph (stepped) — fills central area
- Vertical playhead line showing current loop position
- Loop length row: `[Loop label] [DropdownSelect: 1/1/2/1/4/1/8/1/16/1/32 + T/. variants] [NudgeInput multiplier]`
- Stepped mode: additional step length row with same DropdownSelect layout + step count readout
- Target list: destination dropdown + bipolar depth bar per assignment
- Add target button at bottom

All modulator timing rows use `DropdownSelect` with a small identifying label ("Loop" / "Step").

**Matrix tab:** Full table of all active assignments across all modulators. Columns: source (name + dot), destination (param name), depth (bipolar bar + value). Remove button per row. Add assignment button at bottom. Meta-modulation destinations shown with full path.

## Mixer Overlay (Stage 9 / 9.6)

Replaces rhythm panel when mixer button active. Sidebar stays visible. Transport "Mixer" button relabels to "Sequencer" when active (fixed width so layout doesn't shift).

**Channel strip layout (top→bottom):**
- 3px colour bar (rhythm colour for rhythm channels, return colour for FX channels)
- Channel name (dimmed for inactive slots)
- Send rotaries (rhythm: Eff/Dly/Rev; Effect return: →Dly/→Rev; Delay return: →Rev; Reverb: none)
- Pan rotary (no value display)
- Fader (vertical) + VUMeter side by side — fader start Y is identical across all channel types so all faders align
- Level readout in dB below fader
- Mute and Solo buttons side by side (not on Master)

**Always 8 rhythm channels shown.** Inactive slots (no rhythm assigned) display a grey colour bar, name "-", and a translucent dark overlay on all controls below the name. Peaks remain 0; no clicks or artefacts from binding to inactive engine slots.

**Channel order (left→right):** 8 rhythm channels | divider | Effect return | Delay return | Reverb return | divider | Master (slightly wider)

**FX rows (below channel strips):** Three fixed rows (Effect, Delay, Reverb), each in its own rounded bordered sub-panel with a 6px gap between panels. Each row: on/off toggle, name label, algorithm dropdown, parameter knobs.

**Intra-FX routing:** Effect return channel shows send knobs to Delay and Reverb returns. Delay return channel shows a send knob to Reverb return. FXChain.processSends() applies these sequentially. Implemented.

**Echo mode (pending F7):** When Effect algo = Echo, a full DelayRow appears between the Effect and Delay FX rows, showing identical controls to the Delay unit.

## Settings Overlay (Stage 10)

The family-standard two-column settings page ([design-ui-family.md section 5](../design-ui-family.md#settings-page-layout-standardsettingsoverlay)); no scrolling, everything fits at Medium and Large. Groups, in order: **General** (Audio master volume, Display size, Output multi-bus), **MIDI** (Hot-swap timing, MIDI Clock in standalone, MIDI Mode in the plugin, Program Change tables), **Locations** (Sample Library, Content Folder). With the shorter-column rule General is left, MIDI right, Locations right under MIDI (standalone: Locations also right, as MIDI is shorter than General there by 4 px only; if it flips, the rule decides). mu-Clid's tempo lives in the transport bar, so there is no Transport section.

## About Panel (Stage 10)

Opened by clicking μ-CLID logo. Contains: large logo, version badge, company name, links (website/manual/changelog). Credits: JUCE, Signalsmith Reverb (MIT, header-only), Monocypher (BSD-2-Clause), clap-juce-extensions (MIT), Bjorklund algorithm. Easter egg: frickin lasers (v4 roadmap).

## Standard Control Behaviours

- **Knobs**: drag up to increase, drag down to decrease, double-click to type value, double-click return = range minimum
- **Knob labels**: always full words, no abbreviations (e.g. "Attack" not "ATK", "Rotate" not "ROT")
- **Knob hover**: fires `onStatusUpdate` immediately on `mouseEnter` — status bar updates without clicking
- All knob interactions report to StatusBar — no tooltips anywhere in the UI
- **StatusBar**: 20px, shows last interacted control name + value + rhythm colour tag, never clears automatically
- **DropdownSelect timing**: 18 items covering 1, 1/2, 1/4, 1/8, 1/16, 1/32 × {none, T, .}. Item id = index+1. Identified by a small "Loop" or "Step" label to the left.
- **NudgeInput**: ▲/▼ arrows + step size buttons (1, 5, 10) + direct text entry on double-click
- **StepEditor**: drag bar up/down. All bars same teal colour regardless of sign. Centre = zero. +100 = top.
- **LFOEditor**: click to add point, drag to move, right-click to remove, ALT-click segment for bezier handle
- **AddButton**: dashed border, "+" prefix, click opens PopupMenu with already-added items greyed
- **SegmentControl Pills style**: individual rounded buttons with 2px gaps, corner radius = `pillH * 0.45f`, font 8–11pt clamped
- **SegmentControl Bar style**: connected segments, single rounded rect outline
- Delete rhythm: shows confirmation popup with rhythm name and red-tinted delete button

## Colour Coding

### Knob colours (from MuClidLookAndFeel)

| ColourIds enum | Hex | Used for |
|---|---|---|
| `knobEuclidean` | `#7F77DD` purple | Steps, hits, rotate, pitch |
| `knobPrePad` | `#2BB5C5` cyan-teal | Pre pad knobs |
| `knobPostPad` | `#1D9E75` teal | Post pad knobs, filter cutoff/res, filter env |
| `knobInsertPad` / `knobModulation` | `#D4537E` pink | Insert start/length, insert mode, modulator controls |
| `knobLevel` | `#EF9F27` amber | Amplitude ADSR, Euclid C (accent) controls |
| `knobFxSend` | `#D85A30` coral | Effect/delay/reverb sends, intra-FX routing |
| `knobReverb` | `#378ADD` blue | Reverb size, diffusion, damp, pre-delay |
| `knobPan` | `#888780` grey | Pan |

Note: `knobPadding` (old combined teal) is replaced by `knobPrePad` and `knobPostPad`.

### Ring colours (RhythmCircle)

| Ring / element | ColourIds | Hex | Notes |
|---|---|---|---|
| Euclid A hits | `ringEuclidA` | `#7F77DD` purple | Always outermost ring |
| Euclid B hits | `ringEuclidB` | `#D85A30` coral | Second ring |
| Euclid C hits | `ringEuclidC` | `#EF9F27` amber | Dashed outline ring (accent), third ring |
| Pre-padded steps | `ringPrePad` | `#2BB5C5` cyan-teal | Matches `knobPrePad` |
| Post-padded steps | `ringPostPad` | `#1D9E75` teal | Matches `knobPostPad` |
| Insert-padded steps | `ringInsertPad` | `#D4537E` pink | Matches `knobInsertPad` |
| Mod A | `ringModA` | `#1D9E75` teal | |
| Mod B–D | `ringModB/C/D` | amber/pink/blue | |

---

## Stage 11 — Animations and Polish

All animations run on the message thread via `juce::Timer`. Audio-thread values are read via atomics. Animations must never block the message thread — use linear or ease-out curves that finish within their window even if a timer tick is missed.

### Ring rotation (RhythmCircle)

During playback, all rings rotate continuously so the **current step is always at 12 o'clock**.

- Timer: 30Hz
- Angle = `(currentStep + subStepFraction) / totalSteps * 2π` — subStepFraction interpolates within a beat using the host/internal beat position fractional part
- At 120BPM, one 1/16 step lasts ~125ms → 3-4 timer ticks per step → smooth visually
- Ring A, B, C all rotate by the same angle (they share the same step count base; ring C accent is pinned relative to A)
- Draw: all arc segments are offset by `-angle - π/2` (subtract π/2 to put step 0 at the top)
- When stopped: rings snap to step 0 position with a 150ms ease-out deceleration

### Hit pulse (ring arc)

On every hit detected (audio thread sets a `juce::Atomic<bool>` flag per rhythm per step):

- An expanding semi-transparent filled arc radiates outward from the hit-step arc position
- Arc starts at ring outer radius, expands to `outerRadius + 14px` over 150ms
- Alpha: 0.7 → 0 with ease-out curve
- Colour: rhythm colour (same as sidebar accent)
- Multiple hits may overlap (pool of up to 4 simultaneous pulse objects per ring)
- Centre hub: fills with rhythm colour, alpha 0.5 → 0 over 300ms

### Sidebar item pulse

On hit:
- An expanding pulse ring radiates outward within the `SidebarItem`, rhythm colour, alpha 0.4 → 0 over 200ms
- Ease-out (quadratic), not linear — snappy leading edge, smooth tail
- The RhythmCircle inside the sidebar item already benefits from the ring rotation/pulse above

### VU meter ballistics

- Attack: 5ms RMS window (approximately instant for short transients)
- Release: 300ms fall-off — `-0.05 dB/timer tick at 30Hz ≈ -1.5 dB/frame at normal levels`
- Peak hold: green peak tick held for 2.5 seconds, then decays at −0.1 dB/tick
- Clip indicator (red): lights at 0dBFS, held for 3 seconds, manual clear via click
- VU meter draws at 30Hz timer in MixerChannel paint

### Modulator playhead

- Smooth scrolling vertical line in LFOEditor and StepEditor
- Position = `(beatPosition / loopLengthInBeats) mod 1.0`
- Drawn in white at 50% alpha, 1px wide
- Updates at 30Hz — interpolates between timer ticks using sub-tick beat fraction

### Panel transitions (mixer ↔ rhythm panel)

- When switching: outgoing component fades to alpha 0 over 80ms, incoming fades from 0 to 1 over 80ms
- Implemented via `juce::ComponentAnimator` — no custom alpha painting required
- Do not animate bounds; only alpha
- Skip animation if the switch is triggered during playback with latency concerns (i.e., just swap instantly if the message thread is busy)

### Sidebar add/remove

- Add rhythm: new `SidebarItem` slides in from below — starts at `y + itemH`, animates to correct y over 120ms
- Remove rhythm: item fades to alpha 0 over 80ms, then items below animate upward to fill the gap (80ms)
- Use `juce::ComponentAnimator::animateComponent()` for both

### Algorithm change in FX rows

- When algo dropdown changes: param knobs cross-fade — old labels/ranges fade out at alpha 0→1 over 80ms as new params slide in
- Implemented by re-calling `FXRow::setSelectedAlgorithm()` and letting the row rebuild; no custom animation required if the rebuild is fast enough
- If needed: `juce::ComponentAnimator` fade on the entire row content area

### Font modernisation (Stage 11 only)

Replace all `juce::Font(float size)` constructor calls with `juce::Font(juce::FontOptions{}.withHeight(size))` to eliminate C4996 deprecation warnings. Do this as a single sweep; do not mix old and new constructors.
