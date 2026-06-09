# μ Family — UI Design System

This document is the authoritative reference for the visual and interaction language shared across all μ (mu) plugins — currently **μ-Clid**, **μ-Tant**, **μ-On**, and the **μ-Toni** scaffold. Every new plugin in the family starts here. Plugin-specific layout docs (e.g. `design-ui.md` for μ-Clid) describe arrangement only — they defer to this document for every colour, size, and behaviour.

---

## 1. Design Principles

| Principle | Meaning in practice |
|---|---|
| **Dark and precise** | Near-black backgrounds, high-contrast text/values, thin separator lines at 0.5px |
| **Colour carries meaning** | Every knob colour maps to a functional category — don't use colour decoratively |
| **StatusBar-first feedback** | Parameter names + values go through the StatusBar (one consistent model — never a tooltip for a value). Tooltips are used **sparingly**, only as a one-line hint on icon-only buttons and a few non-obvious controls (layer reset/delete, modulator randomise, the bipolar curve slider) where there's no room for a label |
| **Full words everywhere** | No label abbreviations. "Attack" not "ATK", "Frequency" not "Freq" |
| **Controls are consistent** | Use the shared component library. Never build a one-off version of a standard control |
| **Fixed-PX baseline** | Every layout dimension is a named Medium constant in `MuLookAndFeel.h` wrapped in `mu_ui::s()` at the call site. The global `mu_ui::scale` (default 1.0) is the single Small/Medium/Large switch — no per-component responsive math. The plugin window itself is non-resizable; size variants arrive as scale-factor presets, not user-draggable corners. |

---

## 2. Shared LookAndFeel

The shared class is **`MuLookAndFeel`**, located at `Source/UI/Components/MuLookAndFeel.{h,cpp}` and compiled into `mu-core`. `MuClidLookAndFeel.h` is a backward-compat shim (`using MuClidLookAndFeel = MuLookAndFeel`) — all existing include sites work unchanged.

Plugin-specific subclasses are allowed only for:
- Overriding a small number of colour tokens with a plugin-specific accent
- Adding plugin-specific `ColourIds` in a non-overlapping ID range

### ID ranges

| Range | Owner |
|---|---|
| `0x10000001 – 0x1000009F` | MuLookAndFeel shared tokens (do not reuse) |
| `0x100000A0 – 0x100000FF` | Shared component tokens (AddButton, etc.) |
| `0x10001000+` | Plugin-specific tokens (per plugin, non-overlapping) |

---

## 3. Colour Tokens

All colours are accessed via `MuLookAndFeel::colour(ColourId)`. Never hardcode a hex value in component drawing code.

### 3.1 Backgrounds

| Token | Hex | Usage |
|---|---|---|
| `windowBackground` | `#1C1C1B` | Plugin root fill |
| `panelBackground` | `#232322` | Secondary panels, FX rows, delay row |
| `sidebarBackground` | `#1A1A19` | Left sidebar tray |
| `sidebarItemBackground` | `#252524` | Individual sidebar items |
| `sidebarItemSelected` | `#2D2D2B` | Selected sidebar item |
| `overlayBackground` | `#111110` | Modal/overlay backgrounds |

### 3.2 Semantic Knob Colours

These map to functional categories — use the same token regardless of which plugin you're in.

| Token | Hex | Functional category |
|---|---|---|
| `knobEuclidean` | `#7F77DD` purple | Rhythm/sequencer parameters (steps, hits, rotate, pitch) |
| `knobPrePad` | `#2BB5C5` cyan-teal | Pre-trigger padding / pre-delay |
| `knobPostPad` | `#1D9E75` teal | Post-trigger padding, filter cutoff/resonance, filter envelope |
| `knobInsertPad` / `knobModulation` | `#D4537E` pink | Insert padding, modulator depth/controls |
| `knobLevel` | `#EF9F27` amber | Amplitude (ADSR, gain, velocity, accent) |
| `knobFxSend` | `#D85A30` coral | Effect/delay/reverb sends and intra-FX routing |
| `knobReverb` | `#378ADD` blue | Reverb parameters (size, diffusion, damp, pre-delay) |
| `knobPan` | `#888780` grey | Stereo pan |

`knobPadding` is a legacy alias for `knobPostPad` — do not use in new code.

### 3.3 Segment Control States

Three states: **General** (default), **Positive** (confirmation/on), **Warning** (caution/mode).

| Token | Hex | Usage |
|---|---|---|
| `segmentActiveBg` | `#3C3489` | General active fill |
| `segmentActiveBorder` | `#7F77DD` | General active border/text |
| `segmentPositiveBg` | `#085041` | Positive active fill |
| `segmentPositiveBorder` | `#1D9E75` | Positive active border/text |
| `segmentWarningBg` | `#854F0B` | Warning active fill |
| `segmentWarningBorder` | `#EF9F27` | Warning active border/text |
| `segmentInactiveBg` | `#2A2A2A` | Inactive fill (all states) |
| `segmentInactiveBorder` | `#444444` | Inactive border (all states) |
| `segmentInactiveText` | `#888888` | Inactive text (all states) |

### 3.4 Text

| Token | Hex | Usage |
|---|---|---|
| `headingText` | `#E8E8E6` | Panel headings, rhythm names |
| `valueText` | `#CCCCCC` | Parameter values |
| `labelText` | `#888780` | Secondary labels, knob labels |
| `mutedText` | `#555554` | Disabled / placeholder text |

### 3.5 Specialised Components

**VU Meter:**

| Token | Hex |
|---|---|
| `vuMeterLow` | `#1D9E75` green |
| `vuMeterMid` | `#EF9F27` amber |
| `vuMeterClip` | `#E24B4A` red |
| `vuMeterPeakHold` | `#FFFFFF` |
| `vuMeterBackground` | `#111110` |

**StatusBar / AddButton / SampleBar:** see `MuLookAndFeel.h` for full token list — not repeated here.

---

## 4. Typography

JUCE font. No external typeface dependency.

| Context | Size | Colour token |
|---|---|---|
| Panel headings, rhythm names | 13pt | `headingText` |
| Knob labels | 9–11pt (fits available height) | `labelText` |
| Knob values | 9–10pt | `valueText` |
| Row / slot labels | 11pt | `labelText` |
| Status bar | 11pt name / 11pt value | `statusBarText` / `statusBarValue` |
| dB readout in mixer | 9pt | `labelText` |
| Button text | scaled to button height | `segmentInactiveBorder` (inactive) / `segmentActiveBorder` (active) |

`juce::Font(float)` is deprecated in current JUCE — defer migration to `FontOptions` until the dedicated polish stage.

---

## 5. Spacing and Sizing

### Standard control sizes

| Control | Width | Height | Notes |
|---|---|---|---|
| `KnobWithLabel` (knob + label below) | 64px | row height | Width is the canonical unit |
| `DropdownSelect` (algorithm) | 120px | 24px | Standard row dropdown |
| `SegmentControl` (bar style) | fits content | 22–24px | |
| `SegmentControl` (pills style) | fits content | varies | pill height = component height − 4px |
| `TextButton` (on/off toggle) | 36px | 22px | Standard enable toggle in FX rows |
| `VUMeter` | 10px | fills fader height | Always right-adjacent to fader, no gap |
| `StatusBar` | full width | 20px | Bottom chrome |
| `TransportBar` | full width | 36px | Top chrome |
| Mixer fader cap | — | 200px max | `kFaderMaxH`; minimum 40px |
| Mixer channel strip / sends / FX rows | proportional | proportional | Sizes are computed from the available `MixerOverlay` bounds in `resized()` rather than fixed pixel constants (Stage 32). Send knob height tracks pan knob height; channel widths scale to fit 8 rhythm + 3 return + master within the strip area. |

### Padding

| Constant | Value | Usage |
|---|---|---|
| Row internal padding | 4px | Between controls within a row |
| Panel padding | 6px | Between panels |
| Separator line weight | 0.5px | All horizontal/vertical dividers |

All layout constants live as `static constexpr int k…` in the component that owns them. No magic numbers in `resized()` or `paint()`.

---

## 6. Standard Controls

### KnobWithLabel

The primary parameter control. Rotary knob drawn by `MuLookAndFeel::drawRotarySlider`, label below.

- **Knob colour**: pass the appropriate `ColourIds` token at construction — the LookAndFeel maps it to the knob arc colour
- **Label**: always a full word, rendered in `labelText` colour
- **Value display**: `textFromValueFunction` must be set for any non-trivial unit (Hz/kHz formatting, percentage, dB)
- **Hz params**: use the family-standard formatter (see §6.4)
- **Callbacks**: use `onValueChanged(double)` for data binding — never override `getSlider().onValueChange` directly

### DropdownSelect

Wraps `juce::ComboBox` with the shared LAF. All item IDs are 1-based positive integers (JUCE ComboBox requirement).

### SegmentControl

Mutually exclusive toggle bar. Use **Bar** style for binary or 3-way choices inline with other controls. Use **Pills** style for horizontal option groups that need visual separation.

- `ActiveStyle::General` — default purple accent
- `ActiveStyle::Positive` — green accent (on/enabled/yes states)
- `ActiveStyle::Warning` — amber accent (caution/special mode states)

### TextButton (on/off)

Used only for enable/disable toggles on FX/Delay/Reverb rows. Size 36×22px. State drawn by `MuLookAndFeel::drawButtonBackground`.

### StatusBar

One global StatusBar per plugin. All controls report to it — no tooltips anywhere in the UI. `onStatusUpdate(name, valueString)` fires on `mouseEnter` (hover) and on value change.

---

## 6.4 Standard Value Formatters

These must be applied consistently wherever the parameter appears.

**Frequency (Hz):**
```cpp
slider.textFromValueFunction = [](double v) -> juce::String {
    if (v < 1000.0)  return juce::String((int)v) + " Hz";
    if (v < 10000.0) return juce::String(v / 1000.0, 2) + " kHz";
    return juce::String(v / 1000.0, 1) + " kHz";
};
slider.valueFromTextFunction = [](const juce::String& t) -> double {
    const juce::String s = t.trim().toLowerCase();
    if (s.containsIgnoreCase("khz")) return s.getDoubleValue() * 1000.0;
    return s.getDoubleValue();
};
```

**Percentage (0–100):**
```cpp
slider.setNumDecimalPlacesToDisplay(0);
// suffix "%" is shown via the units field in FXAlgorithmDef / displayed by FXRow
```

**dB:**
```cpp
slider.setNumDecimalPlacesToDisplay(1);
// suffix "dB" displayed by status bar via onStatusUpdate
```

**Time (ms):**
```cpp
slider.setNumDecimalPlacesToDisplay(0);
```

---

## 7. Interaction Patterns

These apply to all mu plugins uniformly.

| Interaction | Behaviour |
|---|---|
| Knob drag up | Increase value |
| Knob drag down | Decrease value |
| Double-click knob | Enter text value |
| Double-click then Enter | Confirm typed value |
| Ctrl+click knob | Reset to default |
| Knob hover (`mouseEnter`) | Fire `onStatusUpdate` immediately — status bar shows name + value |
| SegmentControl click | Select segment, fire `onChange(index)` |
| Delete rhythm | Confirmation popup with rhythm name, red-tinted button |

---

## 8. Rhythm Colour Palette

30 colours shared across all plugins that use rhythm slots. Index into `MuLookAndFeel::rhythmPalette[30]`. The palette provides enough variety for 8 active rhythms with visually distinct colours.

Plugins that do not use rhythm slots ignore this palette.

---

## 9. Plugin Identity

Each plugin customises the shared base with:

| Element | Location | How to customise |
|---|---|---|
| Plugin name / logo | TransportBar | Draw plugin-specific logo; `headingText` for text fallback |
| Window default size | `PluginEditor` constructor | Set per-plugin; min/max limits appropriate to content |
| Plugin-specific knob colour tokens | Plugin subclass of `MuLookAndFeel` | Add in non-overlapping ID range `0x10001000+` |
| Sidebar (layers) | Shared `mu-core/UI/ChannelSidebar` + `SidebarItem` | Identical select/add/delete/reorder UX family-wide; inject only the per-layer mini-graphic via `createMiniVisual` (mu-clid `RhythmMiniVisual`, mu-tant voice glyph). Do NOT re-implement the sidebar per product. |
| Per-layer header bar | Shared `mu-core/UI/ChannelHeaderBar` | Colour dot · editable name · reset (↺) · delete (✕) · per-layer preset dropdown · Save — identical across products. Wire callbacks (`onReset`/`onDelete`/`onSave`/`onPresetSelected`/`onNameChanged`) to product semantics (mu-clid hot-swap rhythm presets; mu-tant per-voice `.muPattern`). Do NOT re-implement the header per product. |
| Insert-effect subsection | Shared `mu-core/UI/Voice/InsertSubsection` | Algorithm dropdown + 4 generic slot knobs; reads/writes APVTS via a constructor **channel prefix** (`"r"` / `"v"`). Optional product hooks: `isSlotModulated`/`slotModValue`/`isPlaying` (mod-arc indicators), `getInsertGR` (Comp/Limiter GR meter), `runBulkChange` (wrap the algo-switch multi-write). Part of the voice section (engine→insert→mixer). **REQUIRED wiring if the product also shows the modulator panel:** connect `insertSub.onInsertAlgorithmChanged = [&](int charId){ modulatorPanel.setInsertAlgorithm(charId); };` — otherwise the four `insert.pN` modulation destinations show the generic "Insert P1..P4" instead of the active algo's slot names. `setChannel()` re-fires this callback, so it also covers layer-select + initial show. (mu-clid: [RhythmPanel.cpp](../mu-clid/Source/UI/RhythmPanel.cpp); mu-tant: [VoicePanel.cpp](../mu-tant/Source/UI/VoicePanel.cpp).) |
| Per-voice modulator panel | Shared `mu-core/UI/ModulatorPanel` (+ `ModMatrixPanel` / `ModulatorEditor`) | Bind with `setVoiceSlot(&slot)` (a `VoiceSlot&`) + `setDestProvider(&provider)` (the product's `ModDestProvider` listing its destination ids/labels). Drive the playhead via `setPlayheadBeat`. If the voice has an insert, keep `setInsertAlgorithm` in sync (see the InsertSubsection row). Do NOT re-implement the modulator UI per product — only the `ModDestProvider` is product-specific. |
| Mixer / FX rack params | Shared `mu_mixfx::addGlobalFxParams` (`mu-core/Plugin/MixerFxParams.h`) | The MixerOverlay / FXRow / DelayRow bind to a fixed id set (`eff_`/`dly_`/`rev_`/`eff2*`/`echo_`/`ret_*`/`mstr_lvl`/`mstr_pan`/`mst_ins*`). Declare them via this helper (don't hand-roll the layout); the product adds only its own `ch{N}_` strips + product globals. Route their `parameterChanged` to `ProcessorBase::syncGlobalFxParam`. |
| Modal dialogs / prompts | Shared `mu-core/UI/ModalDialog` + builders in `mu-core/UI/ConfirmDialog.h` | **The themed replacement for `juce::AlertWindow`** — an in-editor overlay card (dim backdrop + centred rounded `panelBackground` card + icon/title + body + right-aligned button row), matching the SaveDialog look. Use the one-line builders: `mu_ui::messageAsync` / `confirmAsync` (warning + confirm) / `promptTextAsync` (single text field) / `confirmQuitAsync` (Cancel/Save/Close). **Each takes an `anchor` `Component*`** (any component in the editor — pass `this`; standalone quit passes the window); the dialog hosts itself over the enclosing `AudioProcessorEditor`, self-owns, and Esc/click-outside = cancel. For a richer dialog construct a `mu_ui::ModalDialog` directly and inject a content component via `.content(...)`. Do **NOT** build a one-off `juce::AlertWindow`. |
| TransportBar controls | Plugin-specific layout | Standard chrome (play, BPM, preset, mixer) always present; plugin-specific additions to the right |

---

## 10. Shared Module Plan (`mu-shared`)

The following modules are candidates for extraction into a shared library once a second plugin begins development. The boundary is: **anything with no reference to `PluginProcessor` or a specific plugin's data model is a candidate**.

| Module | Contents | Status in mu-clid |
|---|---|---|
| `mu_ui` | `MuLookAndFeel`, all `UI/Components/` (KnobWithLabel, DropdownSelect, SegmentControl, NudgeInput, AddButton, VUMeter, StatusBar, StepEditor, LFOEditor) | In `Source/UI/Components/` |
| `mu_fx` | `FXSlotBase`, `FXAlgorithmDef`, `EffectSlot`, `DelaySlot`, `ReverbSlot`, `FXChain`, all 8 effect algorithms, `OversampledProcessor` | In `Source/FX/` |
| `mu_modulation` | `ControlSequence`, `ModulationMatrix`, modulator editors | In `Source/Modulation/` |
| `mu_voice` | `VoiceEngine`, `SamplePlayer`, `VoiceChain`, `TimeStretcherBase` | In `Source/Audio/` (voice-related) |
| `mu_envelope` | ADSR implementation (currently embedded in VoiceEngine) | Extract when a second plugin needs it standalone |

**Not shared:** `PluginProcessor`, `PluginEditor`, plugin-specific panels (`RhythmPanel`, `MixerOverlay`, etc.), `SequencerEngine`, `Rhythm` data model — these are inherently plugin-specific.

### Extraction approach

When the second plugin is started:
1. Create `mu-shared/` as a sibling git repository
2. Add as a git submodule: `git submodule add ../mu-shared deps/mu-shared`
3. In each plugin's `CMakeLists.txt`: `add_subdirectory(deps/mu-shared)` + `target_link_libraries(${PROJECT_NAME} PRIVATE mu::shared)`
4. Rename `MuClidLookAndFeel` → `MuLookAndFeel`, update all includes
5. Move `UI/Components/` and `FX/` verbatim — no API changes needed

All existing ID values in `ColourIds` are frozen — never renumber them.
