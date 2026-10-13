# μ Family — UI Design System

This document is the authoritative reference for the visual and interaction language shared across all μ (mu) plugins — currently **μ-Clid**, **μ-Tant**, **μ-On**, and **μ-Toni**. Every new plugin in the family starts here. Plugin-specific layout docs (e.g. `design-ui.md` for μ-Clid) describe arrangement only — they defer to this document for every colour, size, and behaviour.

---

## 1. Design Principles

| Principle | Meaning in practice |
|---|---|
| **Dark and precise** | Near-black backgrounds, high-contrast text/values, thin separator lines at 0.5px |
| **Colour carries meaning** | Every knob colour maps to a functional category — don't use colour decoratively. Exception: an app's main page draws all its knobs in the app colour (§11) |
| **StatusBar-first feedback** | Parameter names + values go through the StatusBar (one consistent model — never a tooltip for a value). Tooltips are used **sparingly**, only as a one-line hint on icon-only buttons and a few non-obvious controls (layer reset/delete, modulator randomise, the bipolar curve slider) where there's no room for a label |
| **Full words everywhere** | No label abbreviations. "Attack" not "ATK", "Frequency" not "Freq" |
| **Controls are consistent** | Use the shared component library. Never build a one-off version of a standard control |
| **Fixed-PX baseline** | Every layout dimension is a named Medium constant in `MuLookAndFeel.h` wrapped in `mu_ui::s()` at the call site. The global `mu_ui::scale` (default 1.0) is the single display-size switch — **Medium (1.0) and Large (1.25) only**, `ProcessorBase::kUiScaleMedium` / `kUiScaleLarge`; there is no Small (ruled 2026-10-12: at 0.75, the only scale that fits a 768 px laptop, 9 pt labels fall to 6.75 pt, so Small would trade fit for illegibility) — no per-component responsive math. The plugin window itself is non-resizable; size variants arrive as scale-factor presets, not user-draggable corners. |

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

**Stereo, family-wide.** Every mixer strip's meter (Client, Master, plugin channel, return) is one `VUMeter` drawing two thin bars, L then R, each with its own ballistics, colour zones (same tokens, same scale) and peak-hold tick; one latched clip indicator per bar, a click clears both. `VUMeter` gets a second source `getLevelR`; when it is unset the one source is drawn on both bars, so a mono source needs no special look and the strip layout never changes. The strip meter is a post-fader, post-pan signal, so it is stereo wherever the strip is. Stereo data is a pair of peak atomics per strip (L, R). Bar and slot sizes: `kVUBarW`, `kVUBarGap`, `kVUW` in the Mixer sizing tokens (section 5).

**Status lamps** (`StatusLamp`, §11) — a state is told by colour and lamp strength, never by colour alone (the status bar names it):

| State meaning | Colour token | Lamp strength (`MuTheme::Lighting`) |
|---|---|---|
| Waiting / pending | `segmentWarningBorder` `#EF9F27` amber | `lampDim` |
| OK / locked | `segmentPositiveBorder` `#1D9E75` green | `lampOn` |
| Drifting (a locked source is off its grid and servoing back; mu-link's external clock, [mu-link/design-ui.md](mu-link/design-ui.md) section 4) | `segmentWarningBorder` amber | `lampOn` (lit, so it reads brighter than Waiting; the status text names it) |
| Fault / lost | `indicatorFault` `#E24B4A` red (`MuTheme::Indicators::fault`) | `lampOn + lampPlayhead` (brightest, draws the eye) |

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
| `DropdownSelect` (algorithm) | 120px (`MuLookAndFeel::kDropdownW`) | 24px (`kDropdownH`) | Standard row dropdown; FXRow, DelayRow, mu-Toni's boxed dropdown rows and mu-On's Engine / Source dropdowns all use these tokens, never a local copy |
| `SegmentControl` (bar style) | fits content | 22–24px | |
| `SegmentControl` (pills style) | fits content | varies | pill height = component height − 4px |
| `TextButton` (on/off toggle) | 36px | 22px | Standard enable toggle in FX rows |
| `VUMeter` (stereo, two bars) | `kVUW` 14px (2 x `kVUBarW` 6 + `kVUBarGap` 2) | fills fader height | **Beside the fader, not at the strip edge.** The fader slot is `kMixerFaderW` 20px, centred on the strip's vertical axis (the knob column's axis); the meter starts at the slot's right edge, no gap. On strips with a GR meter (`kGRW` 8) the order is fader, GR, VU. One helper in `MixerChannel` places the group for every strip type (overlay, Client, Master); no per-layout copy. If the strip is narrower than the group needs, the fader slot shrinks, never the meter |
| `StatusBar` | full width | 20px | Bottom chrome |
| `TransportBar` | full width | 36px | Top chrome |
| `StatusLamp` (lamp + label) | lamp `kStatusLampD` 8px + `kSpaceXS` + label width | lamp centred in the row | Label 11pt (row label). In the TransportBar its slot is `TransportBar::kClockW` 44px |
| Mixer fader cap | — | 200px max | Overlay `MixerChannel` strips only (computed in `MixerOverlay` layout, no token); minimum 40px. A Client strip (mu-link) has no cap: its fader takes the remaining height, minimum `kClientFaderMinH` (40) |
| Mixer channel strip / sends / FX rows | proportional | proportional | Sizes are computed from the available `MixerOverlay` bounds in `resized()` rather than fixed pixel constants (Stage 32). Send knob height tracks pan knob height; channel widths scale to fit 8 rhythm + 3 return + master within the strip area. |

### Per-layer panel stack (every instrument product)

Ruled 2026-10-11 so the four instruments share one vertical structure (the owner's "matching panel sizing"). In the 1088 x 814 main area, top to bottom:

| Band | Height | Token (new, `MuLookAndFeel`) | Contents |
|---|---|---|---|
| Header strip | 32 | `kLayerHeaderStripH` | the shared `LayerHeaderBar` (28) in its metal preset strip, a screw at each end. Nothing else lives in it |
| Content | 464 | `kLayerContentH` = `kChannelPanelH - kLayerHeaderStripH - kLayerModulatorH` | the product's sections, supplied to the shared `LayerPanel` as one component; any product row that is not a section (mu-Clid's 32 px sample bar, mu-Tant's Root / Scale row) is inside this band |
| Modulator panel | 318 | `kLayerModulatorH` | the shared `ModulatorPanel`, identical in every product |

464 is what mu-Clid already uses (sample bar 32 + circle 288 + voice band 144); 318 is its modulator height. The content band is fixed; **spare or lost height (a banner) comes off the modulator panel only**, whose display already takes the slack (the one-row assignment box stops growing at `ModulatorEditor::kMetalAssignMaxH`). Each product's content must fit 464 with at least `kPad` spare, checked by arithmetic from tokens; a product that needs more regroups its sections, it does not shorten the modulator panel or add a per-product height. The sidebar is `kSidebarW` 82 in every product.

**How the content band is filled (ruled 2026-10-12).** The band is always fully tiled: a stack of panels, each with its own `kChannelInset` (7, at least `kPad`) inside its edge, whose heights sum to `kLayerContentH`. "At least `kPad` spare" means exactly that inset, so a band with no slack left over (mu-Clid) passes. A product whose fixed panels sum to less than 464 names **one flex panel** (open-ended content: a step or gate editor, the arpeggiator box) that takes the remainder; the leftover is never left as empty padding and never moves to the modulator panel. All heights go through `mu_ui::s()`, so Large fits when Medium does. Panels inside the band are the metal kind (a `drawAccentPanel` with `drawPanelScrews`); the flat branches in the products are dead and go as each product adopts `LayerPanel`.

| Product | Content band at Medium (sum from tokens) | Flex panel |
|---|---|---|
| mu-Clid | sample bar `kSampleBarH` 32 + circle / Euclid `kCircleSize` 288 + voice band `kVoiceSectionH` 144 = **464**, tiled, nothing spare | none |
| mu-Tant | after regrouping: Root / Scale panel-top row 7 + 24 + plate 16 = 47; Row A (Osc 1, Osc 2, Noise) 64 + plate 16 = 80; Row B (X-Mod, Filter, Effects) **116** (`kVoiceSubH`) + inset 7 = 123; gate panel 200 -> 450, **14 spare** | gate panel: grid height (`GatingDesigner::kGridH` 134) + the 14 remainder = 148 |
| mu-Toni | source panel 7 + 14 + 102 + 7 = 130; voice panel 7 + `VoiceBand::kHeight` 130 + 7 = 144; Appergater panel 130 -> 404, **60 spare** | Appergater panel (box 102 -> 162, which is a second Size-2 knob row, 56 + `kVoiceGap` 4 = 60, when its controls need one; otherwise the box grows) |
| mu-On | engine boxes: 7 + rows x (14 + 85 + 7), one row 113, two rows 219; slot panel (Groove + steps) 7 + 14 + box 68 + 7 = 96; sum 209 (one row) or 315 (two) | the slot panel: its steps box takes 464 minus the engine rows (box 323 high with one row, 217 with two; never below `GrooveGrid::kBoxH` 68); the Groove box is the same height, its knobs top-aligned |

mu-Tant's Row B is the family voice-band height (`kVoiceSubH` 116 = two `kVoiceKnobCellH` 56 + `kVoiceGap` 4), not the 144 it had; its two filter rows sit 4 apart as in mu-Clid. This is the regrouping: mu-Tant's old stack (28 header + 478 content + a 280 minimum modulator panel) came to 828 in the 814 area. **Modulator panel and banners:** with both banners showing (774 area) the modulator rect is 774 - 32 - 464 = 278 and its inner height 264; the `ModulatorPanel` must lay out in 264 (its display takes the slack; the metal assignment box is capped at `kMetalAssignMaxH`). The coder asserts this and reports a shortfall; it does not shrink the content band. mu-On adopts `LayerPanel` with #1241 and is held to this table then.

### Layer add / delete / reorder (family standard, ruled 2026-10-12)

One behaviour in every instrument product (Clid, Tant, Toni, On); the structural edit itself is `ProcessorBase::addLayer` / `removeLayer` / `swapLayers` ([design-plugin-family.md](design-plugin-family.md#layer-ownership-and-structural-edits--family-standard)). The editor glue is one shared mu-core class (`mu_ui::LayerEditController`), not per-product code.

- **Noun.** Each product keeps its own word, set once as the `LayerSidebar` button label: Rhythm (Clid), Voice (Tant), Layer (Toni), Track (On). The Add button, the confirm title and the default layer name all use it.
- **Add button.** Visible whenever the product wires `onAddLayer`; **disabled, not hidden, at 8 layers** (`mu_limits::kMaxLayers`), so the sidebar never reflows. Tooltip when disabled: "Maximum of 8 <noun>s". `LayerSidebar` owns this (it enables the button from `getNumLayers()` in its timer); no product code. Adding selects the new layer.
- **New layer.** Appended at the end; name "<Noun> N" with N = slot + 1; colour = the first unused `channelPalette` entry (the base does this); all parameters at defaults.
- **Delete.** The header bar's delete (✕) acts on the shown layer. **Disabled at one layer** (the header greys it; it never silently does nothing). Confirm via `mu_ui::confirmAsync`, anchored on the layer panel: title "Delete <Noun>", message `Delete "<name>"?` newline `This cannot be undone.`, confirm button "Delete" (the red destructive button, as for reset; see "Dialog icons and the destructive button"). After delete the layers above shift down (parameters, modulators, colour, mixer strip), the selection stays on the same index clamped to the new last layer, and the sound is **cut** at the next block, not faded.
- **Reset.** The header bar's reset (↺) returns the shown layer to defaults, keeping its name and colour, and also returns its mixer strip (level, pan, sends). Confirm via `mu_ui::confirmAsync`: title "Reset <Noun>", message `Reset "<name>" to defaults?` newline `This also resets its mixer strip.` newline `This cannot be undone.` (one wording, `LayerHeaderBar::resetConfirmMessage`, used by the shared header wiring and by mu-Clid's own confirm), confirm button "Reset".
- **Wording uses the noun everywhere.** Header tooltips too: "Delete this <noun>" (disabled: "The last <noun> cannot be deleted"), "Reset this <noun> to defaults", with the noun in lower case from `LayerPanel`'s noun; no hard-coded "layer" in `LayerHeaderBar`.
- **Rename.** A layer name is editable only where the product saves it (today mu-Clid). Where the name is derived (mu-Toni "Layer N", mu-Tant "Voice N", mu-On the engine name) the header calls `setNameEditable(false)`: the name stays on its LCD (an LCD reads as a display, not an input), it is not click-to-edit and no rename ever appears to work and then revert. Decided once for the family: no free-text names for index- or engine-named products until a product saves names.
- **Layer presets.** A layer preset lists in every layer's menu when the preset carries what the layer is (mu-On `.muTrack` carries its engine and loads into any track), so no filtering by engine; loading one may change the track's engine.
- **Reorder.** Drag in the sidebar, as today; colours travel with their layer.
- **After a preset load or New,** the sidebar refreshes, selects layer 0 and the panel rebinds, in that order.
- **Fresh instance = 1 layer** in every product that has variable layers. A saved state without a layer count loads with the product's legacy count (mu-Toni 4, mu-On 5).
- **Do not:** hide Add at the limit; confirm on Add; fade on delete; let a product re-wire these callbacks itself; give the delete a different glyph or wording per product.

### Padding

| Constant | Value | Usage |
|---|---|---|
| Row internal padding | 4px | Between controls within a row |
| Panel padding | 6px | Between panels |
| Panel inner padding | 6px | Inside a bordered/boxed panel, between its edge and its content |
| **Control label gap** | **≥6px** | **Between a control's label text and the enclosing panel's border** |
| Separator line weight | 0.5px | All horizontal/vertical dividers |

**Control label gap (mandatory).** A control label (the text under a
`KnobWithLabel`, a dropdown caption, etc.) must **never touch or overflow the
panel border** — always leave a small gap (≥6px, `kPanelPad`) between the lowest
label and the enclosing panel's bottom edge. When a boxed section holds a row of
knob cells, size the box to fully contain the cell height (`kKnobSize2H`) **plus**
the label gap plus the inner padding, and reserve that gap at the bottom in
`resized()` (e.g. `inner.removeFromBottom(s(kPanelPad))`) so a label can never
render against the border. A label jammed against the frame reads as a layout
bug — this is the family standard that prevents it.

All layout constants live as `static constexpr int k…` in the component that owns them. No magic numbers in `resized()` or `paint()`.

### Everything fits (mandatory, owner rule 2026-10-11)

Every control and UI item must fit inside the panel space available to it: nothing clipped, overflowing, overlapping a sibling or the footer, or unreachable, at the standard window (`kWindowWidth` x `kWindowHeight` = 1170 x 870) and at Large (1.25x). Because every size goes through `mu_ui::s()`, Large fits exactly when Medium fits, so **verify at Medium by arithmetic from the tokens**; a Medium layout with no spare room is a fail (leave at least `kPad` spare).

- **Budget first.** A spec states the space it lives in and the sum of what it puts there. Shared budgets: overlay / main area = `kWindowWidth - kSidebarW` x `kChannelPanelH` = 1088 x 814 **when no banner is showing**. Until the DEMO banner is removed, an unlicensed copy loses `kDemoBannerH` (20 px), and the upgrade banner (`kUpgradeBannerH`, 20 px, any product) can take 20 more, so the **worst-case overlay / panel height is 774**. An overlay either fits 774 or derives its heights from `getHeight()`; it never assumes 814 from fixed tokens. Settings column = 1088 - 2 x `kPad`; `ModalDialog` card, About and the StatusBar (20 px high, full width) are fixed shared spaces: a new item must fit them without changing them.
- **Never fix overflow by shrinking a control below its standard size or by a per-product constant.** Fix it by layout: regroup, flow into columns, move the item, or (last resort, only for open-ended content such as lists) scroll. Fixed-content pages (settings, dialogs) never scroll.
- **Text that can be long** (paths, preset names) is ellipsised in a fixed width; a hint is either given the room it needs on its own line or shortened.
- **Review checklist item.** Every UI ruling and every UI review checks: (1) the budget is stated, (2) the sum fits with `kPad` spare, (3) it still fits for the worst-case product / mode (standalone vs plugin, every optional section present), (4) hidden-but-reserved rows are counted. A ruling that fails this is not issued.

### Settings page layout (`StandardSettingsOverlay`)

The page is **two columns** in the 1088 wide overlay: column width `(min(w - 2 x kPad, kTwoColMaxW) - kColumnGap) / 2` (484 at Medium, because both columns together are capped at `kTwoColMaxW` = 1000), gap `kColumnGap` = 2 x `kPad` (new token in `SettingsOverlayBase`). Groups (General, MIDI, each extra group) are never split; each group goes, in the order added, into the **shorter column so far** (ties go left). Both columns start at `contentTop()`; the "Contact Support" footer sits bottom-left under the left column. A column's bottom must stay above the footer (`height - kPad - 20 - kPad`). Rows keep the standard label/control geometry; a hint that does not fit right of its control goes on its own line from `rowLabelX()`, or is shortened (Output hint reads "(rescan host to apply)"). Full-width path rows may wrap to two lines (a `juce::Label` in a 26 px row allows two) and ellipsise beyond that. Row labels are one line in `kLabelW` (90 px): keep them to about 14 characters ("Sync offset (ms)" is at the limit; the unit is already on the `NudgeInput`, so prefer "Sync offset"). **Audited 2026-10-11 from code, not run:** the worst standalone page is mu-Clid (right column 482 of a 754 limit); mu-Tant, mu-Toni and mu-On are well inside it.

### Overlay and dialog fit rules

- **Mixer.** `MixerOverlay` keeps the FX rack at `kMixerFXAreaH` and gives the channel strips the rest: strip height = `getHeight()` - header - FX area, never the fixed `kMixerStripH`. Every strip section must still fit at the 774 worst case (fader at least 40 px). The master insert column is laid out as a **2 x 2 knob grid for every algorithm including EQ** (order Low, Mid / Mid Hz, High); the stacked four-high EQ layout needs 534 px and overflows the 531 px strip. Insert panel width is at least `2 x kKnobSize2W` plus label and padding (`kInsertPanelW` 136).
- **Strip sections** end at least `kPanelPad` above their pane border (a Client strip uses `kClientBottomGap`) (the Side Chain Attack / Release knobs end exactly on it today).
- **MIDI program-change panels** (`MidiPresetListPanel`): header 36 px holds a Close button that fits wholly inside it (`kCloseBtnH` 26 centred), and every list-row dimension and font goes through `s()` / `sf()` so Large matches Medium. The 128-row list scrolls (open-ended list).
- **Dialog icons and the destructive button** (`ModalDialog`). Icons are drawn as vectors in a `kDialogIconSize` (26) square, never font glyphs (a glyph such as U+26A0 falls back to a font that may lack it). Warning = rounded triangle outline with a "!" (stem + dot), in `segmentWarningBorder` amber; Info = circle outline with an "i", in `globalAccent`; both stroked `kDialogIconStroke` (1.6). Question stays the plain ASCII "?" in `globalAccent`. The title starts `kDialogIconSize + kDialogIconGap` (26 + 6) from the card's inner left. A dialog with the Warning icon (every `confirmAsync`: Delete, Reset, Overwrite, Discard) shows its primary button as the **destructive button**: red, not the product accent, in every product. The accent is for the default action of non-destructive dialogs (Info, Question, prompts). Red tokens: `buttonDangerBg` (`#2B0E0D`, a near-black red fill; the originally proposed `#7A2220` gave only about 2.6:1 under the red label, `#2B0E0D` gives about 4.56:1, so the red reads from the border and label on a dark, red-tinted fill), border and text `indicatorFault` (`#E24B4A`); hover and press lift the fill like any button. Amber stays the "caution / special mode" colour of `SegmentControl::ActiveStyle::Warning`; red marks an action that cannot be undone.
- **Dialog button rows** (`ModalDialog`): the row of buttons (`max(72, text + 28)` each, 8 gap) must fit the card's inner width with at least `kPad` spare; a dialog with more or longer buttons is shortened in wording, not widened per call. The support prompt measures about 325 of 360 px.
- **About card** (400 x 300): header block ends at y=114, credits are 17 px each (five today, room for eight), bottom button row at y=260. The supporter thank-you shares the bottom row left of Close in 132 px and has about 12 px spare; if the wording grows it moves to its own line above the row.

---

## 6. Standard Controls

### KnobWithLabel

The primary parameter control. Rotary knob drawn by `MuLookAndFeel::drawRotarySlider`, label below.

- **Knob colour**: pass the appropriate `ColourIds` token at construction — the LookAndFeel maps it to the knob arc colour
- **Label**: always a full word, rendered in `labelText` colour, **never with a unit** ("Mid Hz", "High", "Time"; not "High (dB)"). The unit belongs to the value text (`textFromValueFunction`, shown in the StatusBar and on edit). The label is set once and never rewritten on value change. A control whose label needs the unit to be understood is renamed, not suffixed. Slot-driven knobs follow this through `mu_ui::configureKnobFromSlot` (`SlotLabelUnits::None` default; see the ruling of 2026-10-12)
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

A click on the selected segment does nothing by default. A bank whose click is also an action (mu-link's scene pills recall the scene each time) opts in with `setFiresOnReselect(true)`, which fires `onChange(index)` again; this is an option of the shared control, never a local `TextButton` row.

### TextButton (on/off)

Used only for enable/disable toggles on FX/Delay/Reverb rows. Size 36×22px. State drawn by `MuLookAndFeel::drawButtonBackground`.

### StatusBar

One global StatusBar per plugin. All controls report to it; a tooltip never carries a parameter name or value. The only tooltips are the one-line hints allowed in §1 (icon-only buttons and a few non-obvious controls), shown by the single `juce::TooltipWindow` the editor shell owns (700 ms delay). `onStatusUpdate(name, valueString)` fires on `mouseEnter` (hover) and on value change.

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

**Tempo (BPM):** one decimal, always shown (`120.0`, `127.5`) so the field never changes width or jumps between formats. Range 20.0–300.0, resolution 0.1. Entry accepts `127.5`, `127,5`, `127.5 BPM`; the value is rounded to 0.1 and clamped to range; text with no digits cancels the edit. The ▲/▼ arrows step 1.0 and keep the fraction (127.5 becomes 128.5); Shift+click on an arrow steps 0.1. The MIDI-clock-driven display (field disabled) shows the clock estimate rounded to 0.1. Status bar and "Locked at" text use the same one-decimal form (`127.5 BPM`). Constants `kBpmDecimals = 1`, `kBpmStep = 1.0`, `kBpmFineStep = 0.1` live in `MuLookAndFeel`. mu-link uses the same shared transport bar and `NudgeInput`, so its tempo field is identical (arrows 1.0, Shift+click 0.1).

---

## 6.5 Transport Timing Controls (standalone)

Performance gestures live in the TransportBar; set-and-forget values live in the Settings overlay. Host builds (VST3/CLAP) show none of these: the host owns tempo, meter and latency compensation.

**Transport pane order (standalone):** `[play] [BPM] [Tap or Clock lamp] [position] [nudge - +]`. The Tap button and the Clock lamp share one slot (`kClockW` 44px) because they are mutually exclusive: internal clock shows Tap, a MIDI-clock source shows the lamp.

| Control | Where | Component | Spec |
|---|---|---|---|
| **Tap tempo** | TransportBar, the lamp slot, internal clock only (hidden under MIDI clock) | `juce::TextButton` "Tap", metal button helper, 44px wide, same height as the BPM field | Averages the last 4 intervals (needs 2 taps); a gap over 2 s starts a new count; result rounded to 0.1 and clamped 20.0–300.0 via the BPM path. Status bar: `Tap — tap the beat to set the tempo`. Tooltip: "Tap tempo". |
| **Nudge** | TransportBar, right of the position field, standalone always (internal or MIDI clock) | Two `juce::TextButton`s "-" (slower) and "+" (faster), `kNudgeW` 22px each, 2px apart, momentary | While held the tempo (internal) or the resolved beat rate (MIDI clock) is scaled by -/+ `kNudgeBendPercent` = 4 %; on release it returns to exactly 1.0 (no accumulated change to the BPM field). The BPM field does not change. Both held = no bend. Status bar: `Nudge — hold to slow the groove 4%` / `speed up the groove 4%`. Tooltips: "Nudge slower (hold)", "Nudge faster (hold)". |
| **Sync offset** | Settings overlay, MIDI group, in the MIDI Clock section as a third row after Messages; shown only while Source = MIDI In (always in mu-link) | `NudgeInput` (integer mode), row label "Sync offset" | Range -50 to +250 ms, step 1, Shift+click 10, default 0. Positive = the app plays earlier (it anticipates the master by N ms; enter your audio output latency, about 20-40 ms on WASAPI). Shown signed (`+25`); status bar `Sync offset: +25 ms (plays earlier)`. Applied by advancing the resolved beat by `offset * bpm / 60000` beats. |
| **Time signature** | Settings overlay, General, "Transport" section (standalone only; for products that hide the tempo row the section holds only this row) | `DropdownSelect`, row label "Time signature" | Items: 2/4, 3/4, 4/4, 5/4, 6/4, 7/4, 5/8, 6/8, 7/8, 9/8, 12/8. Default 4/4. Display and metronome grid only: pattern lengths and hot-swap points stay 4 beats a bar. Numerator = beats per bar; a beat is a quarter note in x/4 and an eighth in x/8; position reads `bar.beat.sixteenth` with 4 sixteenths per beat in x/4 and 2 in x/8. The downbeat gets the accent in the bar grid. Plugin builds show the host meter read-only in the position tooltip, no control. |

**Persistence (family-wide app settings via `ProcessorBase`, beside the MIDI-clock choice):** `syncOffsetMs` (int), `timeSigNumerator` (int), `timeSigDenominator` (int). Nudge percent and tap are not stored. mu-link mirrors Sync offset (same key, same range); it has no tap or nudge.

Constants: `kSyncOffsetMin/Max/Default` (-50/250/0) beside `kBpm*` in `MuLookAndFeel`; `kNudgeBendPercent` (4) in the clock-sync code (behaviour, not drawing); `TransportBar::kNudgeW` 22 and the tap button reusing `kClockW`.

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
| NudgeInput arrow click | Step by the field's step; Shift+click steps by the fine step (BPM: 1.0 / 0.1) |
| Nudge button hold (standalone) | Bend tempo -/+4 % while pressed, exact return on release (§6.5) |
| Tap button | Set internal tempo from tap intervals (§6.5) |
| SegmentControl click | Select segment, fire `onChange(index)` |
| Delete rhythm | Confirmation popup with rhythm name, red destructive button (§5) |

---

## 8. Voice Colour Palette

Every app gives each voice (mu-Clid rhythm, mu-Tant voice, mu-Toni layer, mu-On track) its own colour from `MuLookAndFeel::channelPalette[8]`: **Red, Cyan, Orange, Magenta, Lime, Rose, Silver, Copper**, indexed by the voice's colour index. The four app primaries (purple, green, blue, yellow — §11) are deliberately left out, so a voice's highlight never blends into its app's painted panels.

The voice colour lights the voice's icon in the left sidebar (and its selected outline), and the header bar's **name display** and **preset selector** (both LCDs backlit in it). Everything else is in the app colour.

---

## 9. Plugin Identity

Each plugin customises the shared base with:

| Element | Location | How to customise |
|---|---|---|
| Plugin name / logo | TransportBar | Draw plugin-specific logo; `headingText` for text fallback |
| Window default size | `PluginEditor` constructor | Set per-plugin; min/max limits appropriate to content |
| Plugin-specific knob colour tokens | Plugin subclass of `MuLookAndFeel` | Add in non-overlapping ID range `0x10001000+` |
| Sidebar (layers) | Shared `mu-core/UI/ChannelSidebar` + `SidebarItem` (becomes `LayerSidebar` with the Layer rename; the sidebar is one class, no product subclasses) | Identical select/add/delete/reorder UX family-wide; a product injects **only** the per-layer mini-graphic via `createMiniVisual` (mu-clid `RhythmMiniVisual`, mu-tant spectrum glyph) and its add-button word. Reorder and the hot-swap pending badge / cancel are **not** product hooks: the shared sidebar drives them from the processor's layer virtuals (swap, `hasPendingSwap`, `cancelPendingSwap`), so a product that supports hot-swap looks and behaves the same as the others. Do NOT re-implement or subclass the sidebar per product (`RhythmSidebar`, `VoiceSidebar` are only a mini-visual factory and go). |
| Per-layer panel shell | Shared `LayerPanel` (`mu-core/UI`, replaces the copies in `RhythmPanel`, `VoicePanel`, the two `EnginePanel`s) | Owns the stack of [section 5, "Per-layer panel stack"](#per-layer-panel-stack-every-instrument-product): the header strip with the shared `LayerHeaderBar` and its screws, a content slot of `kLayerContentH`, and the shared `ModulatorPanel` at `kLayerModulatorH`; draws the panel screws in `paintOverChildren`. The product supplies one content component and its `ModDestProvider`, nothing else. |
| Per-layer header bar | Shared `mu-core/UI/LayerHeaderBar` | Colour dot · editable name · reset (↺) · delete (✕) · per-layer preset dropdown · Save — identical across products. Wire callbacks (`onReset`/`onDelete`/`onSave`/`onPresetSelected`/`onNameChanged`) to product semantics (mu-clid hot-swap rhythm presets; mu-tant per-voice `.muPattern`). Do NOT re-implement the header per product. |
| Insert-effect subsection | Shared `mu-core/UI/Voice/InsertSubsection` | Algorithm dropdown + 4 generic slot knobs; reads/writes APVTS via a constructor **channel prefix** (`"r"` / `"v"`). Optional product hooks: `isSlotModulated`/`slotModValue`/`isPlaying` (mod-arc indicators), `getInsertGR` (Comp/Limiter GR meter), `runBulkChange` (wrap the algo-switch multi-write). Part of the voice section (engine→insert→mixer). **REQUIRED wiring if the product also shows the modulator panel:** connect `insertSub.onInsertAlgorithmChanged = [&](int charId){ modulatorPanel.setInsertAlgorithm(charId); };` — otherwise the four `insert.pN` modulation destinations show the generic "Insert P1..P4" instead of the active algo's slot names. `setChannel()` re-fires this callback, so it also covers layer-select + initial show. (mu-clid: [RhythmPanel.cpp](../mu-clid/Source/UI/RhythmPanel.cpp); mu-tant: [VoicePanel.cpp](../mu-tant/Source/UI/VoicePanel.cpp).) |
| Per-voice modulator panel | Shared `mu-core/UI/ModulatorPanel` (+ `ModMatrixPanel` / `ModulatorEditor`) | Bind with `setLayer(&slot)` (a `Layer&`) + `setDestProvider(&provider)` (the product's `ModDestProvider` listing its destination ids/labels). Drive the playhead via `setPlayheadBeat`. If the voice has an insert, keep `setInsertAlgorithm` in sync (see the InsertSubsection row). Do NOT re-implement the modulator UI per product — only the `ModDestProvider` is product-specific. |
| Mixer / FX rack params | Shared `mu_mixfx::addGlobalFxParams` (`mu-core/Plugin/MixerFxParams.h`) | The MixerOverlay / FXRow / DelayRow bind to a fixed id set (`eff_`/`dly_`/`rev_`/`eff2*`/`echo_`/`ret_*`/`mstr_lvl`/`mstr_pan`/`mst_ins*`). Declare them via this helper (don't hand-roll the layout); the product adds only its own `ch{N}_` strips + product globals. Route their `parameterChanged` to `ProcessorBase::syncGlobalFxParam`. |
| Modal dialogs / prompts | Shared `mu-core/UI/ModalDialog` + builders in `mu-core/UI/ConfirmDialog.h` | **The themed replacement for `juce::AlertWindow`** — an in-editor overlay card (dim backdrop + centred rounded `panelBackground` card + icon/title + body + right-aligned button row), matching the SaveDialog look. Use the one-line builders: `mu_ui::messageAsync` / `confirmAsync` (warning + confirm) / `promptTextAsync` (single text field) / `confirmQuitAsync` (Cancel/Save/Close). **Each takes an `anchor` `Component*`** (any component in the editor — pass `this`; standalone quit passes the window); the dialog hosts itself over the enclosing `AudioProcessorEditor`, self-owns, and Esc/click-outside = cancel. For a richer dialog construct a `mu_ui::ModalDialog` directly and inject a content component via `.content(...)`. Do **NOT** build a one-off `juce::AlertWindow`. |
| Support prompt (unlicensed, fully functional) | `mu_ui::ModalDialog` built in `EditorShellBase` (shell-owned, identical in every product) + a small "Support" link at the right of the StatusBar | Replaces the old DEMO banner strip (the apps are no longer limited). **Dialog:** card width 400, `Icon::Info`, title "Support mu-<Product>", message: "mu-<Product> is free to use, with every feature. If it's useful to you, you can pay what you like (suggested £20, £0 is fine). You'll get a licence key, and it helps me keep building these." Content: one "Don't show this again" checkbox row (`MuLookAndFeel` toggle, 20 px row). Buttons left to right: **Maybe later** (cancel result: Esc / click-outside / Return-less dismissal), **Enter licence key** (opens `ActivationOverlay`), **Pay what you like** (primary, Return default; opens the Lemon Squeezy checkout in the browser and leaves the dialog closed). **Rules:** shown at most once per session (first editor open of the process, not per instance or per reopen), never while a transport is running or a dialog/overlay is open (defer to idle), never for licensed users; "Don't show again" (ticked on any button) is stored in the user settings file, not APVTS; Maybe later without the tick only suppresses it for the session. The StatusBar "Support" link (muted `textSecondary`, `textPrimary` on hover, pointing-hand, hidden when licensed) reopens the same dialog any time, with the checkbox hidden; the dialog never appears twice in a row. No countdown, no disabled buttons, no feature gating in copy. URL and suggested price live in one `mu-core/License` constant, not per product. |
| Product icon | `<product>/resources/` (PNG for `ICON_BIG` / `ICON_SMALL`; `.ico` for the Windows exe) | See "Product icon" below |
| TransportBar controls | Plugin-specific layout | Standard chrome (play, BPM, preset, mixer) always present; plugin-specific additions to the right. **MIDI clock lamp** (standalone, shared): a `StatusLamp` labelled "Clock" between the BPM field and the position readout, inside the transport pane, present only while Settings → MIDI Clock → Source = MIDI In **and** Messages is not "Transport only" (the same test that disables the BPM field). Waiting (no pulses yet) = amber dim, Locked = green, Lost = red bright. Hover → status bar "MIDI Clock" with "Waiting for clock" / "Locked at 120 BPM" / "Lost, transport stopped"; entering Lost also posts that line once without hover. Lost holds until pulses return (→ Locked). The BPM field stays disabled and keeps the last clock tempo in every state; no tooltip |

### Product icon

- **Identity is the hue, then the silhouette.** Each product's icon has its own dominant hue and a round, centred emblem on a dark ground, so products are told apart in a DAW list or taskbar at a glance. The icon hue is the product's brand colour and is **not required to equal the UI app accent** (mu-Clid's icon is teal while its accent is `appPurple`).
- **Source:** one square PNG, at least 512 px, dark ground, emblem inside the central 90 percent. Opaque dark ground is the family standard (all current icons); no transparency is needed.
- **ICO:** must hold 16, 24, 32, 48, 64, 128 and 256 px, all 32-bit, PNG-compressed from 48 up is fine. The 256 entry is stored as width/height 0 in the directory.
- **Small sizes (16 and 24):** must read as one bold shape. If the large art is too detailed to do so, supply a simplified variant (outer ring plus a head or eyes silhouette, higher contrast, no fine ticks) and build only the 16/24 ICO entries from it.
- **mu-Clid MIDI (the former mu-Clid Lite):** `mu-clid-midi/resources/IconLite.png` and `IconLite.ico`, a warm red/orange ring with a horned alien skull. It is distinct from mu-Clid's teal HUD alien in hue and silhouette (horns, skull), which is the point. Accepted by owner decision. Its 16 px render reads as a red orb on dark, acceptable; the 24 px entry is the weakest. The UI keeps the mu-Clid accent (`appPurple`), as a variant of mu-Clid, so the icon hue is not the accent. `mu-clid-midi/resources/Icon.png` is a byte-for-byte copy of mu-Clid's icon and is unused; remove it.

---

## 10. Shared Module Plan (`mu-shared`)

The following modules are candidates for extraction into a shared library once a second plugin begins development. The boundary is: **anything with no reference to `PluginProcessor` or a specific plugin's data model is a candidate**.

| Module | Contents | Status in mu-clid |
|---|---|---|
| `mu_ui` | `MuLookAndFeel`, all `UI/Components/` (KnobWithLabel, DropdownSelect, SegmentControl, NudgeInput (integer, or decimals mode for fractional values such as BPM), StatusLamp (lamp + label state indicator), NoteLengthControl, AddButton, VUMeter, StatusBar, StepEditor, LFOEditor) | In `Source/UI/Components/` |
| `mu_fx` | `FXSlotBase`, `FXAlgorithmDef`, `EffectSlot`, `DelaySlot`, `ReverbSlot`, `FXChain`, all 8 effect algorithms, `OversampledProcessor` | In `Source/FX/` |
| `mu_modulation` | `ControlSequence`, `ModulationMatrix`, modulator editors | In `Source/Modulation/` |
| `mu_voice` | `VoiceEngine`, `SamplePlayer`, `VoiceChain`, `TimeStretcherBase` | In `Source/Audio/` (voice-related) |
| `mu_envelope` | ADSR implementation (currently embedded in VoiceEngine) | Extract when a second plugin needs it standalone |

**Not shared:** `PluginProcessor`, `PluginEditor`, plugin-specific panels (`RhythmPanel`, `MixerOverlay`, etc.), `RhythmSequencer`, `Rhythm` data model — these are inherently plugin-specific.

### Extraction approach

When the second plugin is started:
1. Create `mu-shared/` as a sibling git repository
2. Add as a git submodule: `git submodule add ../mu-shared deps/mu-shared`
3. In each plugin's `CMakeLists.txt`: `add_subdirectory(deps/mu-shared)` + `target_link_libraries(${PROJECT_NAME} PRIVATE mu::shared)`
4. Rename `MuClidLookAndFeel` → `MuLookAndFeel`, update all includes
5. Move `UI/Components/` and `FX/` verbatim — no API changes needed

All existing ID values in `ColourIds` are frozen — never renumber them.

---

## 11. Metal Style (family standard)

The look developed on mu-Clid in v1.1 (backlog #1099–#1118) is the **standard look for every mu-family app**. All of it lives in mu-core; a product turns it on with **one call** at the end of its editor constructor, once its children exist:

```cpp
setMetalStyle(true, MuLookAndFeel::colour(MuLookAndFeel::appPurple));   // mu-Clid
setMetalStyle(true, MuLookAndFeel::colour(MuLookAndFeel::appGreen));    // mu-Tant
setMetalStyle(true, MuLookAndFeel::colour(MuLookAndFeel::appBlue));     // mu-Toni
setMetalStyle(true, MuLookAndFeel::colour(MuLookAndFeel::appYellow));   // mu-On
```

**Each app has one primary colour** (tokens `appPurple / appGreen / appBlue / appYellow`, values in `MuTheme::AppPrimaries`). The panels show it **painted on the metal — dull, dark and washed out**, never as a neon glow: `MuLookAndFeel::paintColour(accent)` desaturates and darkens it (`Lighting::paintSaturation / paintBrightness`), and panels take a coat of that paint with the brushed grain showing through (`paintCoat`) plus a slightly lighter painted edge (`paintEdgeLift`). The bright primary is kept for things that light up: LCD lettering and the selected state of segments, tabs and toggle buttons (`setAppAccent` points the theme's active-segment colours and the global accent at it, so no family purple is left over).

`EditorShellBase::setMetalStyle` sets the flag + app accent on the editor's `MuLookAndFeel` and sends a LookAndFeel change, so the shared components (`ChannelSidebar`, `ModulatorPanel` → `ModulatorEditor` / `ModMatrixPanel`) switch themselves in `lookAndFeelChanged()`. Products that have not opted in keep the flat look, untouched.

**Rule: never hard-code depth.** Every shadow, highlight, tint and lamp strength is a field in `MuTheme::Lighting` (masters `shadowAmount` / `highlightAmount` scale them all). Colours come from `MuLookAndFeel` tokens or `MuLookAndFeel::appAccent(component)`.

### Elements and where they come from

| Element | Use for | mu-core API |
|---|---|---|
| **Metal panel** — a coat of the app colour's paint + brushed grain + diagonal sheen + top-right glow, **painted border** a touch lighter | Every top-level panel, the per-voice preset bar included — always the app colour | `MuLookAndFeel::drawAccentPanel(g, bounds, appAccent)` |
| **Raised sub-panel** — cast shadow down-left, lit face with metal finish, bevelled edge | Groups of controls inside a panel (Euclid / Pad / Insert boxes, sidebar layers, modulator header + assignments) | `drawRaisedSubPanelShadow` then `drawRaisedSubPanel` — **draw every shadow before any face** so no shadow lands on a neighbour |
| **Sections** — a panel's sections, each a raised sub-panel with its corner screws and its name plate centred in the `kSectionPlateH` band above it | Every app's main panels (mu-Clid voice band; mu-Tant OSC / X-MOD / FILTER / EFFECTS / MIXER; mu-Toni OSCILLATOR / PITCH … / APPERGATER; mu-On ENGINE / GROOVE / STEPS) | `drawSections(g, owner, { { box, name } … }, appAccent)` — shadows, faces, screws and plates in one call |
| **Metal finish** | Custom surfaces that should match the panels | `drawMetalFinish(g, shape, bounds, brush, bands)` |
| **Name plate** — small raised dark plate, screws, engraved text | Section / row names (PITCH, FILTER, Euclid A…). Same height everywhere | `drawNamePlate(g, bounds, text)`, `namePlateWidth(text, h)`, height `kNamePlateH` |
| **Engraved text** | Plain labels on metal (automatic for `juce::Label` and knob labels in metal style) | `drawEngravedText(g, text, area, just, ink)` |
| **LCD window** — dark glass, recessed bezel, glowing monospaced lettering, glare | Selectors: every ComboBox (automatic in metal style), tab bars (`SegmentControl::DrawStyle::Lcd`), readouts | `drawLcdGlass` / `drawLcdFront`, `lcdLitColour(component)`, `lcdFont(h)`; `DropdownSelect::setLcdStyle` for a one-off |
| **Recessed screen** — inner shading, dark top-right / light bottom-left | Displays set into the panel (LFO / step editor) | `drawRecessedScreen(g, bounds)` |
| **Cross-head screws** (every app: `setScrews(true)` straight after `setMetalStyle`) — domed steel head, cross recess, lit top-right | Panel corners (`kPanelScrewD` at `kPanelScrewInset`); smaller ones in raised sub-panel corners (`kSubPanelScrewD` at `kSubPanelScrewInset`); **one at each end of a thin strip** (header bar, sample bar), vertically centred. Always drawn in `paintOverChildren` so content never hides them | `EditorShellBase::setScrews(true)`; `hasScrews(component)`, `drawPanelScrews` / `drawSubPanelScrews` / `drawStripScrews`, `drawScrew(g, centre, d, angle)`; strengths `Lighting::screw*` |
| **Lamp display** — the ring's lamps as a display: each step a lens over the darker lamp base, lit with a hot-spot up to its value, the playhead's step brighter; an LFO's curve as a lit line over a dimly lit area | Modulator step / LFO editors (automatic in metal style) | `StepEditor::paintLamps`, `LFOEditor`; sizes `kLampCellGap / kLampCellCorner / kLampCurveGlowW / kLampCurveW`, strengths `Lighting::lampGapDarken / lampHotSpotReach / lampDim / lampOn` |
| **LCD text field** — an LCD window holding one line of lit lettering, a browse mark ("...") at the right; warnings lit amber, empty hints as unlit ghost lettering | Readouts such as mu-Clid's sample bar; the header bar's channel name | `drawLcdGlass` / `drawLcdFront`, `lcdFont(kLcdTextH)`, `kLcdTextPadX / kLcdBrowseW`, `Lighting::lcdGhost`; name display `kLcdNameTextH` |
| **Add buttons** — "+ Rhythm", "+ Target", "+ Assignment" are real buttons (the family button look) | Adding a channel / target / assignment. Sidebar's is full width; the modulator's are `kAddButtonW` wide and centred | `AddButton` (a `juce::TextButton`; flat style keeps the dashed border) |
| **Lamp** — lit indicator behind a dark lens, hot-spot at the centre | Step / state indicators (ring steps, gate cells). Off = bare lamp base; only lit states glow | `lampColour(clr, lit)` with `Lighting::lampOff / lampDim / lampOn / lampPlayhead`, `drawLamp(...)`, `lampBase()` |
| **Status lamp** — one round lamp (lamp-base disc, lens 1 px inside it, hot-spot at the centre) with a label beside it, engraved in metal style | A live state that must be visible at a glance (the TransportBar's MIDI clock lamp). Colours / strengths per §3.5 status lamps; label and status-bar text name the state | `StatusLamp` (`mu-core/UI/Components/StatusLamp.h`): `setLabel`, `setState(colour, lit)`, `onStatusUpdate` on hover; draws with `lampBase()` / `lampColour` / `drawLamp`, diameter `kStatusLampD`, hot-spot `kStatusLampD × lampHotSpotReach` |
| **Groove** — an engraved line: a dark line at the start edge with a light line one px to the right (vertical) or below (horizontal) | Every separator on metal in a mixer: the dividers between the channel strips, returns and master, and the lines across the strips above each section. Never a flat 1 px line or a tinted rectangle | `MuLookAndFeel::drawGroove(g, start, length, vertical)`; strengths `Lighting::grooveShade` (dark) / `grooveLight` (light) |
| **Master insert panel** — the master's two inserts as a column beside the Master strip: two stacked boxes, plates "Insert 1" and "Insert 2", each the algorithm selector (LCD dropdown, shared alphabetised list) over a 2 x 2 knob grid in `knobInsertPad` | The master inserts of every mixer: the plugins' `MixerOverlay` and mu-link. One component, callback-driven, so no app lays out its own copy | `mu_ui::MasterInsertPanel` (`mu-core/UI/MasterInsertPanel.h`), width `kMixerInsertPanelW` (136); Vocoder knobs P2 to P4 grey out inside the panel; the Master strip keeps the rest of `kMixerMasterTotalW` |
| **Knobs & slide switches** — neumorphic dial / switch, cast shadow from a top-right light | All continuous / two-state controls | `KnobWithLabel`, `SlideSwitch` (shared, unchanged API) |

### Conventions

- **Light comes from the top right** — highlights top-right, shadows fall down-left, recesses are dark top-right.
- **Even spacing** — the gap between raised boxes, between rows and to the panel edge is one token (`kSpaceXS` in mu-Clid's Euclid panel); plates sit 2 px above their boxes.
- **Voice colour is identity, not decoration** — it lights the voice's sidebar icon (+ selected outline) and the header bar's name display + preset selector (`LayerHeaderBar::setColour`, `DropdownSelect::setLcdColour`); panels stay in the app paint.
- **Main-page knobs are the app colour.** Every knob on an app's main page (the panel the shell gets from `setMainArea`) is drawn in the app colour — `MuLookAndFeel::useAppColourKnobs`, applied by `EditorShellBase`, overrides the knob's own category colour. Knobs elsewhere (the mixer overlay, settings) keep their functional category colours (pitch purple, filter teal, amp orange …).
- **The mixer looks the same in every app.** Its knobs use the one family set of category colours (sends, pan, sidechain, inserts, FX rack) — never the app colour — so it feels the same in mu-Clid, mu-Tant, mu-Toni and mu-On. In the metal style the strips and the FX rack each sit on a screwed metal panel, the section labels are engraved with grooves across the strips, and each FX row is a raised box with corner screws (`MixerOverlay::paintMetal`; `FXRow` / `DelayRow` then paint no flat background).
- **One item at a time beats a scrolling list** in small areas — mu-Clid's modulation targets show one row with an `n / N` readout and an up/down stepper (metal-style `ModulatorEditor`).
- **Voice colours** come from `channelPalette` (Red, Cyan, Orange, Magenta, Lime, Rose, Silver, Copper) — none of them an app primary, so a voice never reads as the app colour.
- **Sub-panels group a panel's sections** — e.g. mu-Clid's voice band puts Pitch / Filter / Amp / Effects each in a raised sub-panel.
- **Content clears the screws.** Inside a screwed panel, content keeps `kScrewedPanelInset` from the panel's bounds (`kScrewedChannelGap` when measured from inside the standard `kChannelInset`); inside a screwed sub-panel, `kSubPanelScrewClear` from its edge. Tabs, the modulator content and the sidebar items shrink to fit rather than overlap a screw (sidebar edges `kSidebarScrewedEdgeX / kSidebarScrewedItemX`).
- **Dropdowns keep a gap to their box's edge** — `kDropdownEdgeGap` — so a selector never touches a border or a screw.
- **Sidebar items are spread evenly** top to bottom, the gap capped at a quarter of an item's height.
- **Symbols over words where space is tight** — e.g. mu-Clid's Euclid logic selector shows the standard logic symbols (∨ ∧ ⊕ …).
- **Debug builds say so** — the standalone title bar reads "<App> - Debug" (`StandaloneShell::windowTitle`; mu-link likewise).
- **Panel structure (every app)** — panels sit edge to edge, each drawn `reduced(2)` so a thin gap shows between them: a thin **preset strip** (the shared header bar, a screw at each end), then the app's **main panels**, each holding raised **sections**, then the **modulator panel** at the bottom. Section boxes sit `kScrewedPanelInset` in from a panel's sides and `kChannelInset` from its top / bottom; panel screws are drawn in the panel owner's `paintOverChildren`. Spare height goes to the modulator panel, where the display takes it (the one-row assignment box stops growing at `ModulatorEditor::kMetalAssignMaxH`).
- **Selectors in a knob grid take two cells** (`ParamKnobGrid`) so their LCD lettering fits.
- **A mixer is the main page of mu-link** (no layers, no sidebar): the standard chrome around a mixer panel and a scenes panel, laid out in [mu-link/design-ui.md](mu-link/design-ui.md). Its knobs keep the category colours (the mixer rule above), and its strips are the shared `MixerChannel` anatomy.
- Positioning stays app-specific; only the look is shared (the per-layer panel stack in section 5 is the one layout the instruments share).

---

## Design rulings

- 2026-10-13 — mu-link VU meters hard against the strip's right edge; stereo? (1) Family-wide: the fader slot (`kMixerFaderW` 20) is centred on the strip axis and the meter sits immediately to its right, replacing "fader fills width minus 10, meter at the edge" in all three layouts (overlay, Client, Master), one placement helper. (2) All strip meters are stereo: two bars L / R in one `VUMeter` (`getLevelR`, mono source mirrored), per-bar ballistics and peak-hold, existing colour tokens. (3) New tokens `kVUBarW` 6, `kVUBarGap` 2, `kMixerFaderW` 20 in `MixerChannel.h` beside `kVUW`, which becomes 14 (derived). Data: a second peak atomic per strip in every engine. Changed §3.5, §5 table, mu-link/design-ui.md.

- 2026-10-13 — Standard dropdown size and mu-On's Groove box width as shared tokens? Yes: `kDropdownW` 120, `kDropdownH` 24 and `kGrooveBoxW` 300 live in `MuLookAndFeel` beside `kDropdownEdgeGap`; FXRow, DelayRow, mu-Toni ArpContent and mu-On TrackContent use them, the four local copies and bare `s(24)` are gone; values unchanged. Changed §5 standard control sizes, mu-on/design-ui.md "As built".

- 2026-10-13 — Warning icon missing in Delete / Reset dialogs, and their buttons using the product accent? (1) Icons are vectors drawn by `ModalDialog`, not font glyphs: Warning triangle + "!" in amber (`segmentWarningBorder`, replacing the off-token `knobLevel`), Info circle + "i" in `globalAccent` (it renders today only by font luck, so it is converted too); Question "?" is ASCII and stays; no Error icon exists. (2) Red is intended: the doc was right, the code never applied it. A Warning-icon dialog's primary button is the red destructive button (new `buttonDangerBg`, existing `indicatorFault`); the accent stays for non-destructive dialogs. Changed §5 (new "Dialog icons and the destructive button", Delete wording), §7 table (Delete rhythm row).
- 2026-10-13 — `buttonDangerBg` `#7A2220` under `indicatorFault` text fails contrast (about 2.6:1); accept the coder's `#2B0E0D` (about 4.56:1)? Accepted: only the fill token changes, border and text stay `indicatorFault` so the red is one family colour; a lighter label token would add a second red. Verified in the mu-Clid, mu-Tant and mu-On Delete dialogs. Changed §5 ("Dialog icons and the destructive button").

- 2026-10-12 — Shared Trig | Leg and Poly | Mono switches in mu-Clid MIDI (no audio, no voice)? Hide them: `MidiRhythmCore` never reads `patternLegato` or `voiceMono`, so they are dead controls. Not relabelled, not kept. A shared control that only means something with a voice engine gets a visibility switch on the shared panel (`EuclideanPanel::setVoiceModeSwitchesVisible(bool)`, default true), not a per-product copy; the `patLeg` / `vMono` parameters stay in the layout and saved state; the switch column stays empty so the boxes line up with mu-Clid. Same turn: mu-clid/design-ui.md brought in line with the built UI (LayerPanel stack, EuclideanPanel with Pad/Insert boxes on all three rows and the switch column, built VoiceBand, note names C-1 to G9 with 36 = C2). Changed mu-clid/design-ui.md "Rhythm Panel", "EuclideanPanel", "VoiceSection".
- 2026-10-12 — Review of the variable-layers UI (mu-Toni 1-8, mu-On track panel). (a) Rename: only where names are saved; mu-Toni, mu-Tant and mu-On call `setNameEditable(false)`. (b) mu-On Groove | Rumble-envelope left box: the fixed 300 wide box is accepted (it must hold the envelope; fixed so the step rows never move); Swing / Accent centred horizontally, top-aligned. (c) Header tooltips use the product noun. (d) `.muTrack` presets list on every track, no engine filter (a preset carries its engine). (e) Engine change leaves the other engines' modulator assignments inert but kept: accepted. (f) mu-Toni preset load / New selects layer 0: correct, as already ruled. Changed §5 "Layer add / delete / reorder", mu-on/design-ui.md.

- 2026-10-12 — Layer add / delete / reorder UX for variable-layer mu-Toni and mu-On? One family behaviour: Add disabled (not hidden) at 8; delete disabled at 1; confirm "Delete <Noun>" / `Delete "<name>"?` + "This cannot be undone." / "Delete"; cut not fade; new layer = "<Noun> N", first unused palette colour; fresh = 1 layer; glue lifted to `mu_ui::LayerEditController`. mu-On with variable layers shows only the selected layer's engine box (proposed, pending Andy; supersedes the all-boxes layout of 2026-10-10). Changed §5 (new "Layer add / delete / reorder"), mu-on/design-ui.md.
- 2026-10-12 — mu-link render-ahead latency and underrun indication? One global Latency `SegmentControl` (Low 2048 / Normal 4096 default / High 8192 frames) in Settings, group "Clients", applied to all slots and on each connect, saved with settings; per-strip "Dropouts" `StatusLamp` (red bright while active, amber dim latched, click to clear, hint to raise Latency). No new tokens beyond mu-link's `kClientLampH` and `kDropoutHoldMs`. Changed mu-link/design-ui.md §3, §5.

- 2026-10-12 — Built 464 fit sums recorded per product (mu-Clid 32+288+144; mu-Tant 47+64+16+116+7 + gate flex 214; mu-Toni Appergater flex). OSC wave dropdown truncates "Basic Shapes"? Ellipsis on a table name is not accepted, and the noise selector and knobs are not shrunk; shorten the factory table name to "Basic" (2026-10-12: "Square PWM" also truncated, renamed "Pulse"; rule: factory table names must fit the OSC box untruncated) (names stay full words, no abbreviations), provided tables are persisted by index; if by name, drop the knob gap in the osc box instead. Changed mu-clid, mu-tant (new design-ui.md), mu-toni docs.

- 2026-10-12 — Built sizes of mu-Clid's Sample | MIDI selector and "MIDI Out" section (112 px selector, 324 px box, captioned dropdowns, 128-note popup, dimmed mixer strip) acceptable? Yes: all derived from family tokens; 324 px stays derived (6 x `kVoiceUnitW`). Two small residuals (selector 112, caption band 14) to become `kLcdSegmentW` and `kVoiceLabelH` references. Changed mu-clid/design-ui.md "Output mode".

- 2026-10-12 — Plugins' mixer master column now the shared `MasterInsertPanel` (rotated "Main Insert N" labels and the separator line gone; Master strip narrower with the 136 px panel to its right; strip right-edge divider at the strip edge); acceptable? Yes. Named plates "Insert 1 / Insert 2" replace the rotated labels (the family rule is plates for names, engraved rotated text only for the strip section labels Side Chain / Sends / Levels / Output); the old separator is superseded by the panel's own boxes; separators between strips are `drawGroove`. The Master strip may be wider than 80 because it absorbs the spare width (`masterActualW`); its content stays centred. Changed §11 (new Groove and Master insert panel rows).

- 2026-10-12 — mu-link shell trio. (1) Display size Small? No: the family offers Medium and Large only (§1 Fixed-PX baseline corrected; mu-link doc corrected); Small is not added (0.8 gives a 936 x 696 window that still misses a 768 px screen and 7.2 pt labels; 0.75 fits but gives 6.75 pt). Owner may revisit as a design-future idea. (2) The Display hint in `StandardSettingsOverlay` names what the user reopens: "(reopen the app for label fonts to fully rescale)" when `isStandaloneApp()`, "(reopen the plugin ...)" otherwise. (3) mu-link master column = callback-driven Master strip (80) + a separate shared callback-driven insert panel (136); `setShowInsertPanel(false)` on the strip is right, the inserts are not dropped. Changed mu-link/design-ui.md §1, §3, §5.

- 2026-10-12 — `configureKnobFromSlot` puts units on labels ("High (dB)", "Mid Hz (Hz)") and rewrites the label on every value change; allowed? No: the family rule is full words, no units on labels. The helper takes a trailing parameter `SlotLabelUnits labelUnits = SlotLabelUnits::None` (enum `{ None, Suffix }`); `None` sets `cfg.label` once and the `onValueChanged` lambda no longer calls `setLabel`; `Suffix` keeps today's `composeSlotLabel` behaviour and exists only as an escape hatch (no caller uses it). The dynamic Hz/kHz and ms/s flip stays in `formatSlotValue` (value text). Affects every caller (InsertSubsection, MixerChannel_Insert x2, MixerChannel client EQ, mu-link master inserts): all lose label units, which is the intent. Changed §3.5 KnobWithLabel.
- 2026-10-12 — Docs named tokens that do not exist (`kLabelGap`, `kFaderMaxH`)? Replaced by `kPanelPad` (6, the label gap) and the real `kClient*` tokens in `MuLookAndFeel`; the 200 px mixer-overlay fader cap has no token and the docs now say so. Changed §3 sizing table, "Control label gap", "Overlay and dialog fit rules", mu-link/design-ui.md sections 2 and 3.
- 2026-10-11 — mu-link adopts the mu-core UI? Yes: standard 1170 x 870 window, shared TransportBar (NudgeInput BPM, clock lamp, Internal / MIDI In segment in the loop slot, preset dropdown, gear), a Mixer panel of eight `MixerChannel`-anatomy client strips plus the master column, a Scenes panel (pill `SegmentControl` + per-client cells), shared Settings overlay (Display size, device button, Sync offset). Supersedes "mu-link has no settings overlay". App colour is Andy's call (steel until then). New: `docs/mu-link/design-ui.md`; changed §6 SegmentControl (`setFiresOnReselect`), §6.4, §11.
- 2026-10-11 — mu-link Locked / Drifting clock indicator? The shared `StatusLamp` in the transport bar's clock slot with a fourth state, Drifting (amber, `lampOn`), tolerance 1/48 beat with 1/96 hysteresis. Changed §3.5; detail in mu-link/design-ui.md section 4.
- 2026-10-12 — Per-product fit of the 464 content band, and the `EnginePanel` name? Name: not a design matter; the architecture ruling stands (`LayerPanel` plus `<Type>Content`, mu-Clid's `VoiceSection` stays as the voice band inside `RhythmContent`; `EnginePanel` is not the family name). Fit: the band is fully tiled with panel insets as the spare; products with slack name one flex panel (mu-Tant gate +14, mu-Toni Appergater +60, mu-On steps box); mu-Tant regroups Row B to 116 (was 144); mu-Clid unchanged at exactly 464; modulator panel must lay out in 264 inner under both banners. Changed §5 "Per-layer panel stack".
- 2026-10-11 — Shared layer sidebar and panel, and matching panel sizing across apps? One `LayerSidebar` with no product subclasses (mini-visual only; swap and pending hooks come from the processor's layer virtuals) and one `LayerPanel` shell; every instrument uses header strip 32 / content 464 / modulator 318 (`kLayerHeaderStripH`, `kLayerContentH`, `kLayerModulatorH`), modulator panel absorbing banner loss. Changed §5 (new "Per-layer panel stack"), §9.
- 2026-10-11 — mu-Clid per-rhythm Sample / MIDI output selector? A Lcd `SegmentControl` at the right of the sample bar (not the shared header bar); MIDI mode swaps the voice band for a "MIDI Out" section (Note, Channel, Velocity, Accent). Changed mu-clid/design-ui.md.

- 2026-10-11 — mu-Clid MIDI uses its own icon (IconLite)? Accepted: ICO has all seven sizes (16, 24, 32, 48, 64, 128, 256), 32-bit, PNG-compressed; art is opaque on dark like the family. Distinct from mu-Clid by hue and silhouette. Icon hue need not equal the accent (mu-Clid is teal on purple). Optional simplified 16/24 variant; unused duplicate Icon.png to be removed. Changed §9 (new "Product icon").

- 2026-10-11 — Do all overlays obey "Everything fits"? Audited from code, not run, at 1088 x 814 (worst case 774 with banners). Pass: settings pages (mu-Clid, mu-Tant, mu-Toni, mu-On), About, Activation, Save, Preset Browser, support prompt. Fail: Mixer (fixed 814 token heights overflow under a banner; master insert EQ stack needs 534 of 531 px), MIDI program-change panels (Close straddles the header line, unscaled row constants). mu-link has no settings overlay: its Options is the native JUCE audio/MIDI device dialog and stays so (its only settings are devices; no `StandardSettingsOverlay` page). Changed §5 (budget worst case 774, settings column 484, new "Overlay and dialog fit rules").
- 2026-10-11 — Settings screen overflows; standing fit rule? Everything must fit its panel at Medium and Large (new §5 "Everything fits", incl. review checklist). mu-Clid settings, single column, needed y=760 (plugin) / 828 (standalone) against a 770 limit (footer) in the 814 area, 750 with the old banner; standalone clipped the Locations buttons. Ruled: two-column shorter-column flow in `StandardSettingsOverlay` (all products), no scroll, no per-product constants. Changed §5, `docs/mu-clid/design-ui.md` Settings section.

- 2026-10-11 — Pay-what-you-want support prompt for unlicensed (fully functional) users? A shell-owned `ModalDialog` (400 wide, Info icon, once per session, "Don't show this again" checkbox; buttons Maybe later / Enter licence key / Pay what you like (primary)), reopenable from a small "Support" link in the StatusBar that replaces the DEMO banner strip (`kDemoBannerH` to be removed). Changed §9 table (Support prompt row).
- 2026-10-10 — mu-On: one lane at a time or all five at once? My Lane Rack ruling (no sidebar, five strips, MOD toggle, `Lighting::laneMutedDim`) is SUPERSEDED by the owner: keep the lane sidebar (selection chooses the step row / Rumble envelope, Groove box, modulators); all five ENGINE boxes shown at once in one metal panel, each at `ParamKnobGrid::getPreferredWidth`, flowed in sidebar order and wrapped (3 rows at 1088 wide); selected lane's box outlined in its lane colour; clicking a box selects the lane. No new tokens. See [mu-on/design-ui.md](mu-on/design-ui.md) section 1.

- 2026-10-10 — MIDI clock locked / lost indicator in the TransportBar? A new shared `StatusLamp` (round lamp + "Clock" label) between BPM and position, standalone with clock sync on; amber dim / green / red bright; new token `indicatorFault`, size `kStatusLampD`; BPM field unchanged. Changed §3.5, §5, §9, §11.
- 2026-10-10 — Fractional BPM (match a track at 127.5)? One decimal, always shown ("120.0"); arrows 1.0, Shift+click 0.1; text entry parses decimals and clamps; MIDI-clock display rounded to 0.1; `NudgeInput` gains a generic decimals mode (integer users unchanged); TransportBar `kBpmW` 72 to 80; mu-link slider interval 0.1 with one-decimal text. Changed §6.4, §7.
- 2026-10-10 — Sync offset, nudge, tap tempo, time signature (standalone)? Tap (internal clock) takes the Clock-lamp slot; Nudge -/+ (hold = -/+4 %) sits right of the position field; Sync offset (-50..+250 ms, positive = plays earlier, default 0) and Time signature (dropdown, display/metronome only) go in Settings; keys `syncOffsetMs`, `timeSigNumerator`, `timeSigDenominator`. Changed new §6.5, §7.
- 2026-10-10 — Tooltips: sparingly, or none anywhere? Sparingly: §1 stands (one-line hints on icon-only buttons and a few non-obvious controls, via the shell's single TooltipWindow, 700 ms); never a name or value, which stay in the StatusBar. Matches the code. Changed §6 StatusBar line; §10 mu_ui list now names StatusLamp and NudgeInput decimals mode.

### KnobWithLabel callbacks

[KnobWithLabel](../mu-core/UI/Components/KnobWithLabel.h) has two separate callbacks. `onStatusUpdate(name, valueString)` is called automatically from the internal `slider.onValueChange` for status-bar display. `onValueChanged(double)` is called from the same lambda; use it for data mutation. Never override `getSlider().onValueChange` directly: it replaces both callbacks. Bind data with `onValueChanged`, or attach with `juce::AudioProcessorValueTreeState::SliderAttachment`.
