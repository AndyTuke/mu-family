# μ-Clid — UI Layout Design Reference

## Root Layout

```
TransportBar     36px  (top) — Stage 10
RhythmSidebar   82px  (left, always visible)
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
- Drag-to-reorder: fully implemented. `RhythmSidebar` supports variable item order.
- Deleted rhythm: item removed immediately, no placeholders
- Right border line drawn in `paint()` to visually separate from main panel

## Rhythm Panel (main editing surface)

Fixed vertical stacking. All values are Medium-baseline constants in `MuLookAndFeel.h`:

```cpp
kRhythmHeaderH = 28;   // header bar
kSampleBarH    = 22;   // sample file bar
kRhythmTopH    = 288;  // RhythmCircle + EuclideanPanel row height
kCircleSize    = 288;  // RhythmCircle is square — width = kRhythmTopH
kVoiceSectionH = 144;  // VoiceSection height
kPanelPad      = 6;    // inset applied to each panel region
// kModulatorPanelH = kRhythmPanelH - kRhythmHeaderH - kSampleBarH
//                   - kRhythmTopH  - kVoiceSectionH                // 332
```

Layout:
```
Header bar         28px — 4px colour accent strip | colour dot | rhythm name
Sample bar         22px — placeholder text or filename. Drag+drop target. "..." browse icon right.
[RhythmCircle 288 | EuclideanPanel 800]  top row, 288px high
VoiceSection      144px
ModulatorPanel    332px
```

### Header Bar

- Left 4px: solid fill in rhythm colour (accent strip)
- 10px colour dot at x=10, vertically centred
- Rhythm name at x=26, font 13pt, `headingText` colour
- Separator line at bottom (0.5px, `segmentInactiveBorder`)
- *(Stage 10: + mute M, solo S, delete X buttons right-aligned; double-click name to rename; colour dot click opens palette)*

### Sample Bar

- Background: `sampleBarBackground`
- No sample state: italic "drop sample here or click to browse", colour `sampleBarNoSample`
- Loaded state: filename, colour `#999` normal weight
- Missing state: "sample missing — click to locate", colour amber `#EF9F27`
- Right edge: "..." browse indicator (24px wide)
- Click anywhere in bar → opens `juce::FileChooser`
- Drag+drop: accepts `.wav .aiff .aif .mp3 .flac`
- Separator line at bottom (0.5px)
- *(Stage 10: + clear button, folder icon)*

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

Three knob rows (A, B, C) plus a 24px logic bar between A and B. One row per sequence, all controls in that row.

```cpp
kLogicH = 24;                    // logic selector bar height
// Rows A and B: Size 1 knobs (kKnobSize1W × kKnobSize1H = 88 × 70) — Steps,
//   Hits, Rotate; then Size 3 knobs (kKnobSize3W × kKnobSize3H = 75 × 46) —
//   Pre Pad, Post Pad, Insert Start, Insert Length, Insert Mode.
// Row C: 3 × Size 1 knobs (Steps, Hits, Rotate only — no padding/insert).
```

**All knob labels use full words — no abbreviations.**

**Euclid A — single row** (8 controls):
1. Steps `1–64` — purple (`knobEuclidean`)
2. Hits `0–steps` — purple (`knobEuclidean`)
3. Rotate `0–(steps-1)` — purple (`knobEuclidean`)
4. Pre Pad `0–12` — cyan-teal (`knobPrePad`)
5. Post Pad `0–12` — teal (`knobPostPad`)
6. Insert Start `0–(steps-1)` — pink (`knobInsertPad`)
7. Insert Length `0–8` — pink (`knobInsertPad`)
8. Insert Mode toggle `Pad / Mute` — pink (`Warning` style), centred vertically in cell

**Logic bar** (24px, full width, Pills style):
- Five pills: `OR  AND  XOR  A Only  B Only`
- Maps to `Logic::OR, AND, XOR, AOnly, BOnly`

**Euclid B — single row**: identical layout and colour coding to A row.

**Euclid C (Accent) — single row** (3 controls):
- Steps `1–64` — amber (`knobLevel`)
- Hits `0–steps` — amber (`knobLevel`)
- Rotate `0–(steps-1)` — amber (`knobLevel`)
- No padding or insert controls — accent shapes hit emphasis, not timing

The accent pattern marks which hits are accented (higher velocity / emphasis). Steps where A or B coincide with a C hit are treated as accented. The C ring displays as dashed amber between ring B and the mod rings.

Ranges update dynamically: when Steps changes, Hits max, Rotate range, and Insert Start max all clamp to the new steps value.

*(Stage 10: + mute toggles per section)*

### Pad Colour System

Three pad types each have a distinct colour used consistently across knobs and ring display:

| Pad type | Knob ColourId | Ring ColourId | Suggested hex | Purpose |
|---|---|---|---|---|
| Pre Pad | `knobPrePad` | `ringPrePad` | `#2BB5C5` cyan-teal | Silence before pattern |
| Post Pad | `knobPostPad` | `ringPostPad` | `#1D9E75` teal | Silence after pattern |
| Insert | `knobInsertPad` | `ringInsertPad` | `#D4537E` pink | Silence within pattern |

The existing `knobPadding` ColourId is retained as a legacy alias for `knobPostPad` in `MuLookAndFeel.h` (`0x10000011`). It can be removed once all callsites migrate.

### VoiceSection

Four-column panel, 144px tall. Three 6px dividers separate the columns. A 14px label row names each column; the remaining height splits into two equal rows (config row + envelope row) with a 4px gap.

```
kVoiceUnitW = 55      // Size 2 knob width — drives every voice cell
kVoiceDivW  = 6       // divider between sub-panels
kVoicePitchW = kVoiceFilterW = kVoiceAmpW = 5 × kVoiceUnitW   // 275 each
kVoiceInsertW = 4 × kVoiceUnitW                               // 220
```

**Column layout:**

```
PITCH (5 kW)               FILTER (5 kW)                AMP (5 kW)           INSERT (4 kW)
────────────────────────   ──────────────────────────   ──────────────────   ──────────────────
Row 1 (config):            Row 1 (config):              Row 1 (config):      Row 1 (config):
  Octave | Semi | Fine       Type(2) | Cutoff | Res       Level | SendEff | SendDly | SendRev | Accent   Character (full-width dropdown)
Row 2 (envelope):          Row 2 (envelope):            Row 2 (envelope):    Row 2 (config):
  Atk|Dec|Sus|Rel|Depth      Atk|Dec|Sus|Rel|Depth        Atk|Dec|Sus|Rel     Drive | Output (or Rate in Bitcrusher mode) | LPF
```

**All knob labels use full words — no abbreviations.**

**Pitch** (purple, `knobEuclidean`):
- Config: Octave −4..+4 (step 1), Semitones −12..+12 (step 1), Fine −100..+100 cents (step 0.1)
- Envelope: Attack 0.001–5s, Decay 0.001–5s, Sustain 0–1, Release 0.001–10s, Depth 0–24 semitones

**Filter** (teal, `knobPostPad`):
- Config: Type selector (`DropdownSelect`, 1 kW — 16 modes: LP 6 / LP 12 / LP 24 / BP 12 / BP 24 / HP 6 / HP 12 / HP 24 / Notch / Notch 24 / AP 12 / Comb + / Comb − / Peak / Lo Shf / Hi Shf), Cutoff 20–20000Hz (2 kW), Resonance 0–0.99 (2 kW)
- Envelope: Attack 0.001–5s, Decay 0.001–5s, Sustain 0–1, Release 0.001–10s, Depth 0–12 semitones of cutoff sweep (#334)

**Amp** (amber, `knobLevel`):
- Config: Level 0–2, Accent 0–+12 dB
- Envelope: Attack 0.001–5s, Decay 0.001–5s, Sustain 0–1, Release 0.001–10s

**Accent** — when a step is accented (Ring C fires coincident with a Ring A+B hit), the amplitude of that step is boosted by this amount above the base Level. 0 dB = no accent effect. Amber (`knobLevel`), sits immediately right of Level in the config row.

*(Stage 13: + FX send knobs on Amp config row — Effect, Delay, Reverb; will require Amp column to expand to 5 kW, adjusting the 18-unit grid)*

**INSERT** (`knobInsertPad` pink — same colour family as the insert pad to signal it is a per-voice insert effect):
- Config row 1: Character (`DropdownSelect`, full-width) — 14 algorithms, alphabetised in the dropdown: None / 3-Band EQ / Bitcrusher / Clipper / Compressor / Fold / Hard Clip / Karplus-Strong / Limiter / Ring Mod / Soft Clip / Tape Sat / Vocoder / Vocoder St.
- Config row 2: 4 knobs whose labels, ranges and meaning are reconfigured per-algorithm by `configureInsertAlgorithm()` (e.g. Drive / Output / Dither / LPF for Soft Clip; Bits / Rate / Dither / LPF for Bitcrusher; Threshold / Ratio / Attack / Release for Compressor; Low 200 / Mid dB / Mid Hz / High 8k for 3-Band EQ, etc.). Per-algorithm knob snapshots are cached in `insertSnapshots[14]` so switching algorithms restores the user's last values for that algorithm.
- No envelope row (Insert has no ADSR). Bottom row is blank — drawn as empty space so the section border still frames correctly.
- Character = None passes audio through unity.
- Character switch is message-thread only (same constraint as `EffectSlot::setAlgorithm`).

**Output mode (Sample / MIDI), ruled 2026-10-11.** The earlier idea of a header-bar dropdown is dropped: the header bar is the shared `ChannelHeaderBar` (dot, name, reset, delete, preset, Save), identical in every product, and has no room or place for a product control. The selector lives in mu-Clid's own content band, in the **sample bar** strip (32 px, the first row under the header strip):

- **Selector:** a `SegmentControl`, `DrawStyle::Lcd`, `ActiveStyle::General`, items **Sample** and **MIDI**, at the right end of the sample bar, **112 px wide** (2 x 56, `RhythmPanel::kOutputSelectorW`) and `kStripDisplayH` (20 px) tall, vertically centred, its right edge `kScrewedPanelInset` (9 px) from the strip end so it clears the corner screw (non-screw style: `kHeaderInsetX`). The sample LCD takes the rest of the strip and stops `kSpaceXS` (4 px) short of the selector. Per rhythm; param `r{N}_outMode` (0 Sample, 1 MIDI), default Sample. Status bar: `Output` / `Sample (plays the loaded sample)` or `MIDI (sends notes, no audio from this rhythm)`.
- **Sample mode:** unchanged.
- **MIDI mode:** the sample LCD shows ghost lettering "MIDI output" and its browse mark is hidden; the 144 px voice band is replaced by **one raised section "MIDI Out"** (`drawSections`, name plate) of the same 144 px height, holding, left to right: **Note** (`DropdownSelect` of note names, the same list as mu-Clid MIDI's note selector, default 36), **Channel** (`DropdownSelect` 1 to 16, default 1), **Velocity** (`KnobWithLabel`, `knobLevel`, 1 to 100 %, default 100), **Accent** (`KnobWithLabel`, `knobLevel`, 0 to 100 %, default 0; adds to the velocity of accented hits, as mu-Clid MIDI's Accent knob does). Param ids `r{N}_midiNote`, `r{N}_midiChan`, `r{N}_midiVel`, `r{N}_midiAccent`; none is a modulation target in this first version (a target would be one appended `ModTarget` row later).
- **Built sizes (commit 7a83b42, `MidiOutSection.h`).** Box: x = `VoiceBand::kLeftGap` (7, where the Pitch box starts), y = `kVoiceLabelH` (14), **324 px wide** (6 x `kVoiceUnitW` 54: Note 2 cols = 108, Channel 2 cols = 108, Velocity 1 col = 54, Accent 1 col = 54), `kVoiceSubH` (116) tall; it is wider than the 216 px Pitch box and ends inside the Effects box edge (static_assert). One control row of `kKnobSize2H` (56) centred in the box. Each dropdown cell: engraved caption ("Note" / "Channel", `kKnobLabelFont` 10, `labelText`) in a 14 px band, then the `DropdownSelect` 2 px below it, 28 px tall (`kKnobSize2H / 2`), kept `kDropdownEdgeGap` (6) off each side of its cell, so 96 px wide. The Note popup lists all 128 notes (C-2 style names from `midiNoteName`, id = note + 1); Channel lists 1 to 16.
- **Mixer:** a MIDI-mode rhythm's strip is drawn inactive (`MixerChannel::setActive(false)`, through the `MixerOverlay::isChannelSilent` hook set in the mu-Clid editor) and its sends and voice controls are ignored; it makes no audio.
- **Sidebar and header:** unchanged (the mini-circle still pulses on hits; a small "MIDI" tag is not added).
- **Fit:** the section is 144 px tall in the 144 px band (same as the voice band) and 324 px wide, well inside the 1088 px panel.
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
