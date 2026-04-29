# μ-Clid — UI Layout Design Reference

## Root Layout

```
TransportBar     36px  (top) — Stage 10
RhythmSidebar   82px  (left, always visible)
RhythmPanel or MixerOverlay  (remaining area)
StatusBar        20px  (bottom, full width)
```

Window: default 1170×870, min 780×580, max 2400×1600. All elements scale proportionally.

```cpp
setSize(1170, 870);                           // default opening size (~50% larger than initial prototype)
setResizeLimits(780, 580, 2400, 1600);
```

Current editor (pre-Stage 10, no TransportBar):
```
sidebar(0, 0, 82, h-20) | rhythmPanel(82, 0, w-82, h-20) | statusBar(0, h-20, w, 20)
```

## Transport Bar (Stage 10)

Left→right: μ-CLID logo (click=About), play/stop, BPM, position (bar.beat), sync pill, rhythm count (n/8), preset selector, save, mixer button, + rhythm button, gear icon.

| Element | Plugin mode | Standalone mode |
|---|---|---|
| Play/stop | Reflects DAW transport | Drives internal clock |
| BPM | Read-only from host | Editable (nudge + tap tempo) |
| Position | Read-only from host | Resets on stop |
| Mixer button | Opens mixer overlay (replaces rhythm panel) | Same |
| + Rhythm | Disabled at 8 rhythms | Disabled at 8 rhythms |

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
- Drag-to-reorder: v2 feature. `RhythmSidebar` must support variable item order from day one.
- Deleted rhythm: item removed immediately, no placeholders
- Right border line drawn in `paint()` to visually separate from main panel

## Rhythm Panel (main editing surface)

Fixed vertical stacking with precise constants:

```cpp
kHeaderH    = 28;   // header bar
kSampleBarH = 22;   // sample file bar
kCircleW    = 300;  // RhythmCircle width (left of top section)
kTopH       = 300;  // RhythmCircle + EuclideanPanel height
kVoiceH     = 80;   // VoiceSection height
// ModulatorPanel: remaining height (h - kHeaderH - kSampleBarH - kTopH - kVoiceH)
```

Layout:
```
Header bar        28px  — 4px colour accent strip | colour dot | rhythm name
Sample bar        22px  — placeholder text or filename. Drag+drop target. "..." browse icon right.
[RhythmCircle 300px | EuclideanPanel rest]  300px
VoiceSection      80px
ModulatorPanel    rest of height  (placeholder until Stage 7)
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
kLogicH  = 24;    // logic selector bar height
kMaxRowH = 90;    // row height cap
// rowH = jmin(kMaxRowH, (kTopH - kLogicH) / 3)
// At kTopH=300: (300-24)/3 = 92, capped at 90 → 90px rows
// colW = (panelWidth) / 8  for rows A and B (8 controls)
// colW = (panelWidth) / 3  for row C (3 controls)
```

**All knob labels use full words — no abbreviations.**

**Euclid A — single row** (8 controls):
1. Steps `1–64` — purple (`knobEuclidean`)
2. Hits `0–steps` — purple (`knobEuclidean`)
3. Rotate `-(steps/2) to +(steps/2)` — purple (`knobEuclidean`)
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
- Rotate `-(steps/2) to +(steps/2)` — amber (`knobLevel`)
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

The existing `knobPadding` ColourId (teal) is superseded by `knobPrePad` and `knobPostPad` — remove `knobPadding` when implementing this change to MuClidLookAndFeel.

### VoiceSection

Single compact horizontal row, 80px tall. Four groups separated by 6px dividers drawn in `paint()`.

```
Amp ADSR (4 knobs) | div | Filter (2 knobs) | div | Filter Env (3 knobs) | div | Output Mode (52px toggle)
knobW = (w - 3*6 - 52 - 6) / 9
```

**All knob labels use full words — no abbreviations.**

1. **Amp envelope** (amber, `knobLevel`): Attack, Decay, Sustain, Release. Ranges: Attack/Decay 0.001–5s, Sustain 0–1, Release 0.001–10s. *(Stage 10: + Reset/Legato toggle)*
2. **Filter** (teal, `knobPostPad`): Cutoff 20–20000Hz, Resonance 0–1. *(Stage 10: + LP/HP/BP/N type selector)*
3. **Filter envelope** (teal, `knobPostPad`): Attack 0.001–5s, Decay 0.001–5s, Depth 0–1. *(Stage 10: + Sustain, Release, Legato toggle)*
4. **Output mode**: Sample/MIDI toggle, 52px wide, centred vertically at `(h-28)/2`. *(Stage 10: + FX send knobs — effect, delay, reverb)*

### ModulatorPanel (Stage 7)

Tabs: Mod A – Mod H, Matrix. Inactive tabs dimmed.

**Mod tab contents:**
- Header: modulator name, colour dot, smooth/stepped toggle, internal/CC toggle
- LFO curve editor (smooth) or step bar graph (stepped) — fills central area
- Vertical playhead line showing current loop position
- Loop length row: TimeSelector + multiplier NudgeInput + result display
- Stepped mode: additional step length row + step count display
- Target list: destination dropdown + bipolar depth bar per assignment
- Add target button at bottom

**Matrix tab:** Full table of all active assignments across all modulators. Columns: source (name + dot), destination (param name), depth (bipolar bar + value). Remove button per row. Add assignment button at bottom. Meta-modulation destinations shown with full path.

## Mixer Overlay (Stage 9)

Replaces rhythm panel when mixer button active. Sidebar stays visible. Clicking a sidebar item while mixer is open switches back to rhythm panel.

**Channel strip layout (top→bottom):**
- Colour dot (rhythm) or coloured indicator (FX returns)
- Channel name
- Send rotaries (rhythm: eff/dly/rev; Effect return: →dly/→rev; Delay return: →rev; Reverb: none)
- Pan rotary
- Fader (vertical) + VUMeter side by side, touching with no gap
- Level readout in dB below fader
- Mute and Solo buttons side by side

**Channel order (left→right):** Rhythm channels (matching sidebar order) | divider | Effect return | Delay return | Reverb return | divider | Master (slightly wider, includes Internal/External routing toggle)

**FX rows (below channel strips):** Three fixed rows: Effect, Delay, Reverb. Each: on/off toggle, name, algorithm dropdown, parameter knobs. Horizontally scrollable per row if needed.

No placeholder columns for inactive rhythm slots. Maximum 12 columns (8 rhythms + 3 FX returns + master) fits without scrolling.

## Settings Overlay (Stage 10)

Single scrollable page, sections:
- **Visual**: hit pulse style, ring expansion size, sidebar pulse, centre hub pulse, step dot size
- **Sequencer**: sync behaviour (host sync / reset on play), show ms alongside musical divisions
- **Performance**: default interpolation quality, oversampling quality (2x/4x per category)
- **Voice**: default overlap fade length (1–10ms, default 2ms)
- **Gain**: default fader (-12 to 0dB, default -6dB), default master volume
- **Presets**: default preset, restore factory presets
- **Standalone**: audio device, default BPM

## About Panel (Stage 10)

Opened by clicking μ-CLID logo. Contains: large logo, version badge, company name, links (website/manual/changelog). Credits: JUCE, SoundTouch, Signalsmith Reverb, Bjorklund algorithm. Easter egg: frickin lasers (v4 roadmap).

## Standard Control Behaviours

- **Knobs**: drag up to increase, drag down to decrease, double-click to type value, double-click return = range minimum
- **Knob labels**: always full words, no abbreviations (e.g. "Attack" not "ATK", "Rotate" not "ROT")
- **Knob hover**: fires `onStatusUpdate` immediately on `mouseEnter` — status bar updates without clicking
- All knob interactions report to StatusBar — no tooltips anywhere in the UI
- **StatusBar**: 20px, shows last interacted control name + value + rhythm colour tag, never clears automatically
- **TimeSelector**: note buttons (1, 1/2, 1/4, 1/8, 1/16, 1/32) + triplet/dotted toggles, mutually exclusive
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
