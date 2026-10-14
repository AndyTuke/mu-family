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

**Stereo, family-wide.** Every mixer strip's meter (Client, Master, plugin channel, return) is one `VUMeter` drawing two thin bars, L then R, each with its own ballistics, colour zones (same tokens, same scale) and peak-hold tick; one latched clip indicator per bar, a click clears both. `VUMeter` gets a second source `getLevelR`; when it is unset the one source is drawn on both bars, so a mono source needs no special look and the strip layout never changes. All strips, including the plugin channel, return and master strips, bind real left and right peaks, so every meter is true stereo (the mirrored-mono interim ended with the plugin L/R peak work; layout and tokens did not change). On narrow strips the fader and meter group clamps right of the strip axis; the blank slot where the GR meter would sit is accepted. The strip meter is a post-fader, post-pan signal, so it is stereo wherever the strip is. Stereo data is a pair of peak atomics per strip (L, R). Bar and slot sizes: `kVUBarW`, `kVUBarGap`, `kVUW` in the Mixer sizing tokens (section 5).

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
- **New layer.** Appended at the end; name "<Noun> N" with N = slot + 1; colour = the first unused `layerPalette` entry (the base does this); all parameters at defaults.
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
- **Modulation rows** (`ModMatrixPanel::MatrixRow`, `ModulatorEditor::AssignmentRow`; ruled 2026-10-13). A row is: source label (`46` in the Matrix, number `20` in the modulator editor), destination selector, the `BipolarSliderRow` pair (amount slider + curve knob), remove (22), with 2 px gaps. **The destination selector is capped at `kModDestMaxW` (240, `MuLookAndFeel`); the amount slider takes all the rest** (`BipolarSliderRow` gives the depth slider everything but the curve knob, `kCurveWidth` 70). The selector shrinks first only when the row is narrower than source + 240 + pair minimum (`kDepthWidth` 160 + 2 + `kCurveWidth` 70 = 232) + remove + gaps; the pair never goes below that 232 minimum. Both rows use the same arithmetic (one rule, two call sites); the cap is a token, never a per-row literal.
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

## 6.6 Swap timing control in the Master Loop pane

Ruled 2026-10-13. The per-preset swap timing (APVTS choice `mstr_swap`, saved with the preset; type and rules in [design-plugin-family.md](design-plugin-family.md), Performance mode item 6) is preset data, not an app setting, so it leaves the Settings overlay ("Timing" in mu-Clid and mu-Tant is deleted, and the "Hot-swap" section with it) and becomes a second group in the shared `MasterLoopSection`, the transport bar's loop pane (it is the "when does a change land" control, next to the loop length it refers to).

- **Which products show it.** Only products that mount `MasterLoopSection` (mu-Clid, mu-Tant, mu-Clid MIDI) can; of those, only a product that has the parameter and calls `setSwapTimingChoices(...)` shows it. The default is no choices = group hidden and the pane keeps its loop-only width. mu-Clid and mu-Tant opt in. **mu-Clid MIDI** has no `mstr_swap` and no hot-swap: hidden, as today. **mu-Toni and mu-On** have no loop of their own and do not mount the pane (no `mstrLoop`); every `SwapTiming` value coincides with the bar there (layer loop = the bar; master loop with none set falls back to it). A one-option dropdown is a dead control (same reasoning as the 2026-10-12 hidden Trig | Leg switches), so they show **no control**; the parameter stays at its `OnBar` default and in the saved state. Do not mount a pane in them just for this. If either later gains a master loop or a second rule, it opts in then.
- **Widget and label.** The standard `DropdownSelect`, label "Swap:" right-aligned 11 pt, same row, height and vertical padding as the Loop group. The pane lays out `Loop: [dropdown] n / m   Swap: [dropdown]`. Tokens (named constants in `MasterLoopSection`, replacing the bare 36/100/56 in `resized()`; `kWidth` becomes `getPreferredWidth()` because it depends on whether Swap is shown): `kLoopLabelW` 30, `kLoopDropW` 92 ("256 steps"), `kLoopCountW` 50, `kSwapLabelW` 38 ("Swap:"), `kSwapDropW` 116 ("On master loop"), `kGroupGap` 6, inner pad 6, label-to-control gap 2. Width with Swap 350, without 172 (built sizes: the first-ruled 76 / 32 / 96 clipped their text when painted, so they were widened; the values above are final). `kDropdownW` 120 is not used: the transport bar is the tightest row in the family and 120 does not fit; this is a bar-specific compact size, ruled once here.
- **Fit.** Window width does not change. The preset dropdown is the flex item and must stay at least 180 baseline px. At the 1170 baseline with Swap shown the preset keeps about 200 of its 240 `kPresetW` (the pane grew by 42), so the floor holds with about 20 px spare; all widths scale together, so Medium and Large keep the same margin. The labels do not narrow (they were widened to stop clipping text). There is no `kPresetMinW` constant: the preset is the flex item and the 180 is a review check, not a clamp; add the constant only if a product ever lands under it. If a later group pushes the preset below 180, change the layout (shorter option wording, or move a group), do not narrow text below its fit. Outstanding: a by-eye check of the pane and of mu-Clid's Settings column flow at standalone sizes with the Perf toggle, when Andy is back at the machine (not yet seen on screen; sizes come from UIA bounds of a first-paint capture).
- **Wording (option text, "On <noun> loop").** The stored choice strings stay the neutral ones in the parameter layout; the displayed text comes from the product via `setSwapTimingChoices`, using the product's lower-case layer noun: mu-Clid: "On master loop", "On rhythm loop" (no "On bar": it has no bar rule). mu-Tant: "On master loop", "On voice loop", "On bar". Item id = `SwapTiming` integer + 1. Status bar on pick: name "Swap Timing", value = the item text. Tooltip on the dropdown: "When a queued preset change takes effect".
- **Unsupported stored value.** The control offers only the values the product supports. A stored value outside that list is **displayed** as the nearest supported one, by this fixed order: `OnBar` tries OnLayerLoop then OnMasterLoop; `OnLayerLoop` tries OnBar then OnMasterLoop; `OnMasterLoop` tries OnLayerLoop then OnBar. The display never writes the parameter (a load must not change data); only the user's pick does. For mu-Clid a stored `OnBar` therefore reads "On rhythm loop", which is also how the engine treats it.
- **Live refresh.** Same three-part pattern as `mstrLoop`: an APVTS parameter listener on `mstr_swap` (hops to the message thread with `SafePointer` + `callAsync` when not already on it, because host automation fires on the audio thread), the initial sync in the constructor, and a reconcile in the existing 30 Hz `timerCallback` (compare the shown item to the mapped parameter value; resync on a mismatch) so a preset load, a full-preset load or an editor opened on an already-loaded preset is never stale. Syncs use `dontSendNotification`.
- **Lock.** In Performance mode ([section 12](#12-performance-mode-ruled-2026-10-12-revised-2026-10-13-with-the-owners-decisions-backlog-1279)) it follows the same lock as the Loop dropdown; no special case.
- **Do not.** Add a swap dropdown to a product's own Settings or to a per-product component; hide it with a stored-value rewrite; offer a value the product cannot honour; keep the old "Timing" copy for one release.

## 6.7 Fader and linear slider thumb (ruled 2026-10-13)

Today no `drawLinearSlider` exists in `MuLookAndFeel`, so every linear slider falls back to JUCE's default: a flat track and a small round white dot. That is off-family. The linear slider gets the knob's lighting, as one shared drawing for both orientations.

- **Applies to** every `LinearVertical` / `LinearHorizontal` slider: the mixer faders (overlay strips, Client, Master) and the horizontal `BipolarSliderRow` depth slider. Pan is a knob, so it is unaffected. One function, no per-product or per-control copy.
- **Shape.** A rectangular fader cap, not a round cap (a round cap would be knob-sized on a 20 px slot and would not read as a fader). Rounded corners, a flat face lit like the knob disc: the knob's cast shadow, face gradient, top-right specular and lower-left occlusion, all clipped to the cap. Across the cap's middle, at right angles to the travel, is the **pointer**: a 2 px line in the accent with the knob dot's halo. The pointer is the value mark (the cap's centre is the value), exactly as the dot is on the knob.
- **Size.** Cap thickness across the slider is `kFaderCapW` 16 (the fader slot `kMixerFaderW` is 20, so 2 px each side hold the cast shadow); along the travel `kFaderCapH` 26. Both are scaled with `s()` and clamp to the slider's cross-axis minus 2, so a thin horizontal slider gets a thinner cap, still the same proportion rule (length along travel = 1.6 x thickness). `getSliderThumbRadius` returns half the cap length, so the travel runs from cap-centre-at-end to cap-centre-at-end and the cap never leaves the slider's bounds. Corner `kFaderCapCorner` 3.
- **Track.** A recessed groove centred in the slot, `kFaderTrackW` 4, drawn like the slide-switch track: same recessed shadow and lower lip (`Lighting::switchTrackShadow`, `switchTrackLip`). From the minimum (or from zero on a bipolar slider, e.g. the depth slider) up to the cap the groove carries an accent line (`Lighting::switchTrackGlow`), so travel reads at a glance like the knob's ring. No tick scale on the groove for now.
- **Colour.** The mixer rule applies (family category colours, never the app colour): faders use `knobLevel` (amber, the family's level colour); any other slider uses its own knob colour (`knobColour(slider)`). `BipolarSliderRow` sets `knobModulation` (pink, the modulation category) on both its depth slider and its curve knob in its constructor (a modulation control is not a Euclidean-purple one; ruled 2026-10-13 after the first build showed the depth slider in the default purple). One call, `MuLookAndFeel::knobColour(slider)`, decides; the thumb code names no colour.
- **Tokens.** New in `MuLookAndFeel.h` beside the knob scale constants: `kFaderCapW` 16, `kFaderCapH` 26, `kFaderCapCorner` 3, `kFaderTrackW` 4, `kFaderPointerW` 2, `kFaderCapMargin` 2 (cross-axis clearance), `kFaderCapAspect` 1.6 (length / thickness on a thin slider), `kDisabledAlpha` 0.4 (the one family disabled alpha: every disabled control or component, including `AddButton`, uses it and never a literal), and the colours `knobFaceLit` / `knobFaceUpper` / `knobFaceLower` / `knobFaceDark` and `switchTrackDark` / `switchTrackLight` (the slide switch and the fader share the groove; the fader's accent fill has its own `MuTheme::Lighting::faderTrackFill` 0.5, because the slide switch's thin `switchTrackGlow` 0.18 line reads as dark brown on a fader). The pure layout lives in `mu-core/UI/Components/FaderThumbGeometry.h`. Lighting reuses the knob and switch fields (`knobCastShadow`, `knobCastBlur`, `knobCastOffset`, `knobDiscShadow`, `knobOcclusion`, `knobSpecular`, `knobSpecularMid`, `knobDotHalo`, `knobDotHaloMid`, `switchTrackShadow`, `switchTrackLip`, `switchTrackGlow`, `switchRingGlow`, `switchRingGlowHover`); the same light and the same strength is the point, so no `fader*` Lighting field is added. If a later look-test needs the cap lighter or darker than the knob, add `faderSpecular` / `faderOcclusion` then, not before. The cap's face gradient uses the same fixed knob face colours as `drawRotarySlider`; hoist those literals into named `MuLookAndFeel` colours (`knobFaceLit` ... `knobFaceDark`) as part of this work so the knob and the cap share them.
- **States.** Rest: cap edge ring in the accent at `switchRingGlow`. Hover: ring at `switchRingGlowHover`. Drag: hover ring plus the full pointer halo (`knobDotHalo`); at rest the halo is `knobDotHaloMid`. Disabled: the whole thumb and the groove glow drawn at 40 % alpha, pointer halo and ring off. There is no focus ring (the mixer fader is a mouse control; keyboard handling is unchanged). Double-click-to-unity on Client and Master strips is unchanged. The stat-bar value text on hover and drag is unchanged.
- **Do not.** Draw the cap as a round dot; put per-strip colours on the thumb; add pixel constants or alpha numbers in `drawLinearSlider`; override `getSlider()` styling in `MixerChannel`; make the 20 px slot wider to fit a bigger cap (the slot and the meter group are fixed by the 2026-10-13 mixer ruling).

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

Every app gives each voice (mu-Clid rhythm, mu-Tant voice, mu-Toni layer, mu-On track) its own colour from `MuLookAndFeel::layerPalette[8]`: **Red, Cyan, Orange, Magenta, Lime, Rose, Silver, Navy** (`#2B3FD6`; Navy replaced Copper 2026-10-14 because brown does not exist on an LED pad), indexed by the voice's colour index. The four app primaries (purple, green, blue, yellow — §11) are deliberately left out, so a voice's highlight never blends into its app's painted panels. On a controller pad the same colours go through a per-device correction (screen colour to tuned pad RGB, in `mu-control`; [design-launchpad.md](design-launchpad.md) §3.2b), so the on-screen value never changes to suit a device; the dim ("available") level of a pad is `MuTheme::Lighting::padDimScale` = 0.20, applied after the correction. Navy is the darkest voice colour: use it for lamps, fills and large marks, not for small text or hairlines on the dark panels (its contrast with the window background is about 2.3:1).

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
| **Lamp display** — the ring's lamps as a display: each step a lens over the darker lamp base, lit with a hot-spot up to its value, the playhead's step brighter; an LFO's curve as a lit line over a dimly lit area | Modulator step / LFO editors (automatic in metal style) | `StepEditor::paintLamps`, `LFOEditor`; sizes `kLampCellGap / kLampCellCorner / kLampCurveGlowW / kLampCurveW`, strengths `Lighting::lampGapDarken / lampHotSpotReach / lampDim / lampOn`. **Level-cell mode** (`StepEditor::setLevelCells(n)`, n = 2 or 3): each cell is one whole lens, its strength the level (`lampOff` / `lampOn` / `lampAccent`), the playhead a ring (`lampPlayheadRing`); click cycles the level, drag paints (first used for mu-Toni's Off / On / Accented step pattern) |
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
- **Main-page knobs are the app colour.** Every knob on an app's main page (the panel the shell gets from `setMainArea`) is drawn in the app colour — `MuLookAndFeel::useAppColourKnobs`, applied by `EditorShellBase`, overrides the knob's own category colour. Knobs elsewhere (the mixer overlay, settings) keep their functional category colours (pitch purple, filter teal, amp orange …). This includes the modulation controls (the matrix and modulator-editor depth fader and curve knob): `BipolarSliderRow` sets `knobModulation` pink, which shows only where such a row sits off a main page; on a main page it is green in mu-Tant, as the rule says.
- **The mixer looks the same in every app.** Its knobs use the one family set of category colours (sends, pan, sidechain, inserts, FX rack) — never the app colour — so it feels the same in mu-Clid, mu-Tant, mu-Toni and mu-On. In the metal style the strips and the FX rack each sit on a screwed metal panel, the section labels are engraved with grooves across the strips, and each FX row is a raised box with corner screws (`MixerOverlay::paintMetal`; `FXRow` / `DelayRow` then paint no flat background).
- **One item at a time beats a scrolling list** in small areas — mu-Clid's modulation targets show one row with an `n / N` readout and an up/down stepper (metal-style `ModulatorEditor`).
- **Voice colours** come from `layerPalette` (Red, Cyan, Orange, Magenta, Lime, Rose, Silver, Navy) — none of them an app primary, so a voice never reads as the app colour.
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

## 12. Performance mode (ruled 2026-10-12, revised 2026-10-13 with the owner's decisions; backlog #1279)

Data model and pad behaviour: [design-launchpad.md](design-launchpad.md) sections 3.2 and 3.2c (a performance = up to 8 pointers to presets, each up to 8 layers, each layer 8 stored layer presets, called clips; switching is a staged hot-swap). This section is the screen half.

**Principle: in Performance mode the PC screen is the normal screen of the playing preset, locked and read-only, and only the control surface acts. Performance mode adds no panel, no height and no per-product layout; it changes only shell chrome that already exists (the TransportBar's preset group and the layer sidebar's Add slot). The screen follows the surface and shows what it did; it never acts.** The surface can disconnect mid-gig, so the Perf toggle stays the one live PC control, with a deliberate exit gesture.

**Availability (owner).** Standalone only for now, but not closed off: `EditorShellBase::setPerformanceModeEnabled(bool)` (default false). The standalone app turns it on; while false there is no Perf toggle, no stepper, no lock and no rim, and nothing else in the shell assumes standalone, so a plugin build can enable it later by that one call.

### 12.1 "Performance mode is on" (shell chrome, `EditorShellBase` / `TransportBar`)

All shell-owned, none product-drawn; the cue colour is the app accent (owner):

1. **Perf toggle** - a `juce::TextButton` "Perf" (metal button helper, `kPerfBtnW` 44, button height) in the TransportBar right group, between the gear and Mixer (right to left: Mixer, Perf, gear, as the bar lays out; Mixer stays rightmost in every app; accepted 2026-10-13 because both neighbours are locked in the mode, so a stray press beside Perf does nothing); lit in the app accent when on (the standard selected-button look). **Enter** = a single click (or Ctrl+P). **Exit** = press and hold, see 12.5.
2. **Preset group replaced** by the `StepIndicator` (12.2); the Preset dropdown, New and Save are hidden, not disabled.
3. **Frame (revised 2026-10-14 from the 2 px rim under the bar, see 12.6 "Is it on?" cue; the Perf button also reads LIVE with a lit-lamp face)** - was: a lit line along the bottom edge of the TransportBar, full width, `kPerfRimH` 2 px, app accent at `Lighting::performanceRim` = **0.85** (ruled 2026-10-13, the coder's value accepted: unmistakable from a distance yet one step under the lamps' full-strength lit lens, so the lit lamps stay the brightest thing in the bar; by-eye check outstanding).
4. **PC lock** - the whole PC UI is read-only: knobs, buttons, sidebar, header bar, modulator panel, the Mixer and Settings buttons, the preset browser and the chooser ignore clicks, drags, wheel and keys. **The screen must not dim or change colour** (it looks like the normal screen; knobs, meters, lamps and the playhead keep animating, so a surface or modulation change is visible). The mechanism (a shell-owned input shield above the content, or per-component interception) is the coder's choice; the Perf toggle and its exit hold are the only live controls. What the surface may change beyond preset and layer switching is undecided; until it is, the surface does only what design-launchpad lists.
5. No New / Save / Add / Delete / reorder is reachable (the lock covers them), so nothing structural can change on stage.

### 12.2 Preset indicator - the shared `StepIndicator` (new, `mu-core/UI/Components/StepIndicator.{h,cpp}`)

A pure indicator (no buttons, no clicks) for "n of N with a lamp per item"; the same class serves presets now and layer presets (clips) later. At Medium it fills the 332 px the preset group used, via `s()`, no per-product constant: `[ LCD ][ 8 lamps ]` = LCD 240 + gap 4 + lamps 88 (8 x `kStepLampPitch` 11).

- **LCD** - `drawLcdGlass` / `drawLcdFront`, `lcdFont(kLcdTextH)`. Lit lettering (`lcdLitColour`, app accent) `3/8 Bassline` (position, cached name, ellipsised, never less than 60 % of the width); to its right in ghost lettering (`Lighting::lcdGhost`) the **next** item: `> Acid Line` (omitted at the last playable item). The previous item is not drawn: the position and the lamps show that it exists.
- **Lamps** (`drawLamp`, diameter `kStatusLampD`), the on-screen twin of the top-row pads:

| Item state | Lamp | Pads |
|---|---|---|
| Current (playing) | `lampOn`, app accent | bright |
| Available | `lampDim`, app accent | dim |
| Queued for the next boundary | pulsing between `lampDim` and `lampPlayhead` at `Lighting::queuedPulseHz` (2 Hz, the pad rate) | pulsing |
| Empty (no item at that index) | bare `lampBase()` | off |
| Missing file / other product | `lampDim` in `indicatorFault`, steady | dim red |

  **Ghost minimum (ruled 2026-10-13):** where the LCD is narrow (mu-Clid at Medium: the group is about 240 px once Perf takes its 50, so the LCD is about 150, accepted) the ghost is **omitted** when the room left for it is under `kStepGhostMinW` (36, scaled), and the lit lettering takes the full width; never a stub such as `> Ac...`. A lit name that does not fit is ellipsised as ruled.

  Every lamp uses the **app accent**, not the layer palette (an item is a whole preset). Hover status text (hover still reports; it is not a click): `Performance "Gig" - preset 3 of 8: Bassline`; on a missing lamp `Preset 5 missing: <name>`.
- **Queued** - while a switch waits for its boundary the queued lamp pulses, the existing "SWP" badge (`presetStagingBadge`) shows, the LCD keeps showing the **playing** preset but its ghost part becomes `-> 4 Acid Line` pulsing at `queuedPulseHz`, and the status bar (name `Queued`, value `4 Acid Line (<when>)`; the bar shows name and value side by side with no colon) reads that **while the switch waits and is cleared when it commits or is cancelled**, unlike the 3 s lines; where `<when>` follows the preset's swap setting (the family-wide per-preset swap mode, today mu-Clid's `swapMode`; "now" when stopped). The content changes only when the swap commits: the new lamp takes `lampOn` and flashes to `lampPlayhead` for `Lighting::stepFlashMs` (150), the LCD updates and the main page shows the new preset. Pressing the **current** item on the surface cancels the queue; pressing another item replaces it.
- **Surface previous / next** (Launchpad top-row buttons 3 and 4, 2026-10-14) wrap at the ends and skip empty and missing items (owner); the screen shows the result as an ordinary item change. There are no per-item pads: the lamps here are the only item display. The screen has no previous / next control.
- **Empty states** - no performance chosen: LCD ghost `No performance`, lamps off. Loaded preset not in the performance: `-/8  <loaded preset name>`, no lamp current.

### 12.3 Layers and clips (the screen follows the surface)

- The 8x8 grid is row = layer, column = one of the layer's 8 stored layer presets (clips). **Pressing a pad makes the PC display switch immediately to that layer** (the sidebar outline moves and the main panel shows that layer, exactly as a sidebar click would), showing the layer's **currently playing** layer preset. If the pressed clip is waiting for its boundary, the layer's header bar shows the same "SWP" queued badge and the status bar reads `Queued: <layer> clip 5 (<when>)`; the panel content changes when the swap commits. Selecting a layer from the right-hand column behaves the same.
- In Performance mode the sidebar's Add slot (same box as `AddButton`) becomes a read-only **layer indicator**: a small LCD `Layer 3/5` (the `StepIndicator` without lamps), because the sidebar already lists every layer and outlines the selected one. No stepper buttons.
- The layer's clip row (8 stored layer states) is the shared `ClipStrip` in the layer header bar, specified in [section 13](#13-clip-bank-the-per-layer-clip-strip-ruled-2026-10-13-backlog-1277) (it supersedes the earlier "StepIndicator in the header strip" note: the strip must also take clicks off Performance mode, which `StepIndicator` by design cannot). In Performance mode the shield makes it read-only and it follows the surface like every other indicator.

### 12.4 One path, no screen actions

- Because the PC is locked there are no on-screen performance buttons and **no performance keys** (no 1-8, PageUp / PageDown or arrow keys). The only key is `performanceToggle` Ctrl+P, which **enters** only; it is ignored while the mode is on (exit is the deliberate gesture below).
- The surface's actions arrive as `ControlAction`s (`PerformanceItem(n)`, `PerformancePrev / Next`, select-layer, clip launch) through the shell's `ControlSink`. The screen is **derived from processor state, never from who acted**: the shell reads the "what to show" interface (current item, queued item, selected layer, queued clip, performance list) on the message-thread timer and repaints, so the screen and the surface cannot disagree. Every performance action also writes one status-bar line that clears after 3 s.
- No separate touch mode (owner). Large display size is advisable on a touch screen.

### 12.5 Entering, leaving, choosing, editing

- **Enter** - click Perf (or Ctrl+P). The current preset keeps playing; if it matches an item that lamp is current, otherwise none. If no performance is chosen yet, entering opens the chooser first.
- **Exit - recommendation: press and hold the Perf button for `kPerfExitHoldMs` (1000 ms).** While held, the button face fills left to right in the app accent (progress, drawn by the shared button helper) and the status text reads `Hold to leave Performance mode`; releasing early cancels; on completion the mode ends. Why: one deliberate action with no modal to read, works with a mouse or touch, cannot happen by a stray click, and needs no surface. Rejected: a confirm dialog (a modal on stage; the second click can be as accidental as the first), double-click (too easy to do by accident), Escape (closes overlays).
- Leaving restores the Preset dropdown, New, Save and the Add button and lifts the lock. The mode flag and the current performance are **session state**, not stored in any preset.
- **Choose / edit** - a **Performance chooser**, its own `OverlayHost` screen (not a tab in the Preset Browser), reachable only with the mode off (the PC is locked in it): from the "Performances..." entry at the bottom of the preset dropdown (the Preset Browser footer button is unreachable in the apps, 12.6), and automatically on first entry with none chosen. Layout in the overlay area (1088 x 814, worst case 774): left a list of saved performances (`kPerfListW` 300, scrolls), right the 8 item rows (`kPerfItemRowH` 40, 320) with header 36 and footer 40 = 396 of 774. Row: number lamp, name LCD, product label, up / down reorder (drag is an extra), remove. An empty row shows "+ Preset" (opens the Preset Browser in pick mode; other products' presets are visible but not pickable until the one-instance rig). A missing row stays with the fault lamp and "missing", replaceable or removable. **Buttons (2026-10-14):** the header row holds every file action, in this order: New, Load, Save, Rename, Delete; the footer holds only Close and Use. Load opens a native open dialog in the performances folder for a `.muPerformance`, and replaces the working list (asking "Discard unsaved changes?" first when edited). A file chosen from outside the performances folder is treated as an import: Save writes it into the performances folder (a copy, asking "Overwrite" on a name clash) and the original is never modified, renamed or deleted, so Save, Rename and Delete only ever touch the performances folder. New / Save / Rename use the shared `SaveDialog` (name and description only); delete asks `confirmAsync`. "Use" loads the list; it does not switch preset. **As built and settled (2026-10-13):** Use on an unsaved or edited list saves it first (an unsaved list opens the name card, then uses it; an edited saved list is overwritten without a prompt, because Use is the deliberate "this is the one"), then closes the chooser; Use is disabled with no preset in the list. Close, New and opening another saved performance with unsaved edits ask `confirmAsync` "Discard unsaved changes?" / "Discard"; Delete asks `Delete "<name>"? This cannot be undone.` / "Delete"; saving over another performance's name asks "Overwrite". The title shows `<name> (unsaved)` while edited. Row buttons are text buttons: "+ Preset" (empty), "Replace", "Up", "Down", "Remove". Pick mode: the Preset Browser's Load button reads "Pick", Pick or double-click hands the preset back and returns to the chooser (edits kept), Close returns without picking; the browser lists this product's presets only, which is the one-instance rig's limit. New tokens (all `MuLookAndFeel`): `kPerfListRowH` 28, `kPerfHeaderH` 36, `kPerfFooterH` 40, `kPerfNumW` 44, `kPerfProductW` 110, `kPerfRowBtnW` 64, `kPerfBrowserBtnW` 110. `SaveDialog::setNameAndDescriptionOnly` is the shared name-only card (compact height, no category / embed / default).
- **Sizing** - no new window, panel or height. New `MuLookAndFeel` tokens: `kPerfBtnW` 44, `kPerfRimH` 2 (retired 2026-10-14 for `kPerfFrameW` 3 and `kPerfGlowR` 4; new `Lighting::performanceGlow` 0.45, see 12.6), `kPerfExitHoldMs` 1000, `kStepLampPitch` 11, `kPerfItemRowH` 40, `kPerfListW` 300. New `MuTheme::Lighting`: `performanceRim` (rim alpha) 0.85, `queuedPulseHz` 2.0, `stepFlashMs` 150. Added in the build: `kStepGap` 4 (LCD to lamps), `kPerfStatusMs` 3000 (timed status lines); ruled now: `kStepGhostMinW` 36 (all in `MuLookAndFeel`). The Perf toggle needs 50 px in the TransportBar right group (standalone only): the coder checks the arithmetic at 1170 for mu-Clid (widest loop section) with `kPad` spare; if it does not fit, shrink `kPresetW` for all products, never hide the toggle.

```text
Transport [mu-Clid][> 120.0 Tap 1.2.3 -+][loop] [ 3/8 Bassline  > Acid Line ][o O o o . . . .] [gear][Perf*][Mixer]
          ======== 2 px accent rim, full width ========
+--------+------------------------------------------------------------+
|Kick    | normal main page of the playing preset (locked, not dimmed)|
|Bass  # | (the pressed layer is shown; header shows SWP if a clip    |
|Lead    |  is queued)                                                |
|Layer3/5|                                                            |
+--------+------------------------------------------------------------+
Status: Queued: Acid Line (next bar)    (lamp 4 pulsing, SWP badge, ghost "-> 4 Acid Line" pulsing)
```

### 12.6 Step 3 rulings (2026-10-13): shield, entry, exit, interim choosing

Sections 12.1 to 12.5 settle the toggle (slot, size, accent lit), the indication (preset group becomes the `StepIndicator`, rim, SWP badge, status line), tokens and the keyboard; there is no banner and no new colour. What they left open is ruled here.

- **Shield visuals: none** (the frame and LIVE cue above are separate, non-input chrome). The shield is an invisible shared `ShellChrome` component over the whole editor (sidebar, main panel, overlays, transport bar). It paints nothing, no dim, no cursor change, no tooltip text of its own. It swallows mouse down, up, drag, wheel and double-click, and key presses (it takes keyboard focus on entry and returns true from `keyPressed`, so Escape, Tab, arrows and Ctrl+P all do nothing while on). It sits below the exempt controls (revised 2026-10-14, [12.7](#127-which-controls-stay-live-in-performance-mode-ruled-2026-10-14-andy-amends-the-principle-backlog-1279): a registry, Play first): the shield's `hitTest` returns false over the **registered exempt components**: Play / Stop, the Perf toggle, the `StepIndicator` and the sidebar layer indicator (the last two have no click handling, and hover must still reach them for their status text, 12.2). The shield's size is the editor's; the host window frame, title bar and close button are native and untouched.
- **Entering closes everything open.** On entry the shell first closes every overlay and modal (Preset Browser, Mixer, Settings, About, Save, chooser, `confirmAsync` dialogs) as a cancel: nothing is saved, deleted or applied by the closing, then it releases text-editor focus, then the shield goes up. Settings changes are already live, so nothing is lost. Reason: an overlay left open under an invisible shield would be a frozen screen with no way to dismiss it.
- **Toggle states.** Off: normal metal button "Perf", tooltip `Performance mode (Ctrl+P)`. On: lit in the app accent (`lampOn` accent, as a lit metal button), tooltip `Hold to leave Performance mode`. Same width in both states (`kPerfBtnW`).
- **Entering with no performance chosen (as built, 2026-10-13).** The toggle and Ctrl+P open the Performance chooser (12.5) and stay off; only a bar with no shell wired says `Choose a performance first` (3 s). The restored session performance (persistence, step 2) also counts as chosen. The MIDI map's `PerformanceMode` action still refuses with none loaded.
- **Choosing (as built, revised 2026-10-13).** The way in to the chooser is the **"Performances..." entry at the bottom of the preset dropdown** (`TransportBar::populatePresetDropdown`, `kPerformancesItemId`, after the presets; choosing it opens the chooser and the dropdown keeps showing the loaded preset). The "Performances" button in the Preset Browser footer (`kPerfBrowserBtnW` 110, hidden in pick mode) exists in the shared browser but is **unreachable in the apps**, because the browser is not where users look; it stays as a harmless second route, and the dropdown entry is the documented one. The `FileChooser` stopgap is gone.
- **"Is it on?" cue (ruled 2026-10-14, supersedes the rim-only cue and the "no persistent label" part of the ruling below; Andy, testing in the Standalone: the lit Perf button was the same purple as the selected Mixer button and the 2 px rim under the bar was missed; he found the mode was on only because the clip pads ignored his click).** Three shell-owned cues, all on while the mode is on, none adding height or a per-product constant:
  1. **Window frame (the strong cue).** A lit frame in the app accent around the whole editor edge, `kPerfFrameW` 3 (scaled) wide, alpha `Lighting::performanceRim` 0.85, painted by `ShellChrome` on top of everything, steady (no pulse: the pulse is reserved for "queued"). It replaces the 2 px rim under the TransportBar (12.1 item 3 is now this frame; `kPerfRimH` is retired in favour of `kPerfFrameW`). It must not change the layout or take input: `hitTest` false, so it is not part of the shield; the 3 px it covers at the edge is inside existing panel padding. No dim, no tint of the content (12.1 item 4 stands).
  2. **Perf button reads LIVE.** On, the label changes from `Perf` to `LIVE` (same `kPerfBtnW`; off is `Perf`), so the word itself says the state. Tooltip unchanged.
  3. **Perf button face is not the Mixer's selected look.** On, the face is the lit-lamp look: fill `appAccent` at full strength (not `segmentActiveBg`, which is what every selected toggle, Mixer included, uses), text and 1 px border in `lampBase()` for contrast, plus an outer glow of `Lighting::performanceGlow` (new, **0.45**, the accent halo alpha) at radius `kPerfGlowR` (new `MuLookAndFeel`, **4** px, scaled) drawn by the shared metal button helper, clipped to the transport bar. Mechanism: the same literal-key property precedent as `muFill` and `muDanger`: the bar sets `muLit` on `perfBtn` and `MuLookAndFeel::drawButtonBackground` / `drawButtonText` honour it. The exit-hold fill (`muFill`) then fills from the lit face's resting (off) colour as today, so during the hold the face drains visibly: draw `muFill` as a dark wipe from the right over the lit face (`buttonColourId`, left to right shrinking the lit part is equivalent; the coder picks the simpler, the lit part must visibly shrink as the hold progresses).
  **As built and accepted (2026-10-14):** the frame is a component under the shield; the glow is drawn behind the Perf button by `TransportBar::paint`, because a button cannot paint outside its own bounds. Same look, no change to the spec above.
  Rejected: a "PERFORMANCE" caption (no room in the bar at Medium, and a second place to read the state; the frame and the word LIVE already do it), dimming the screen (12.1 item 4 forbids it), pulsing (conflicts with the queued pulse), a coloured status-bar fill (the status bar is already busy with timed lines). Standing check: all in `mu-core` shell/LookAndFeel, so every product gets it from one `setPerformanceModeEnabled(true)`; no per-product code, no height.
- **Discoverability (ruled 2026-10-13, Andy: "how to use Perf isn't intuitive").** Enough as built, no persistent label (revised 2026-10-14: the persistent state cue is now the frame and LIVE, above; the exit-gesture hints below stand). The cues are: the dropdown entry (finding it), the lit Perf button with its tooltip `Hold to leave Performance mode` (the standing cue, on hover), the 8 s status hint on entry `Performance` / `Hold the Perf button for 1 second to leave` (`kPerfHintMs`), the early-release hint `Keep the Perf button pressed for a full second to leave`, and the filling face while held. Rejected: a persistent "HOLD" label (the toggle is 44 px wide and the same in both states; the stage view stays uncluttered) and a coachmark or dialog (modal on stage). Revisit only if Andy still reports not finding the exit after this build; the next step would then be a longer first-session hint, not new chrome.
- **Exit hold.** Press on the lit Perf button starts a 30 Hz timer; the face fills left to right in the app accent over `kPerfExitHoldMs`, drawn by the shared metal button helper from a `0..1` progress property (no per-product drawing), and the status text reads `Hold to leave Performance mode` while held. Release, or dragging the pointer off the button, cancels and resets the fill. Completion ends the mode (no confirm), restores the preset group, lifts the shield and the lock and clears the rim. A plain click on the lit button does nothing.
- **Editor follows the surface.** While on, the layer selection comes from `LiveView.selectedLayer` (clamped to the layer count); outside the mode the sidebar selection stays local. On entry the current editor selection seeds it (plan section 4), so nothing jumps.
- **Status lines (as built, reviewed 2026-10-13).** Each is a name + value pair: `Performance` / `Choose a performance first` (3 s), `Performance` / `Hold to leave Performance mode` (**cleared on release, drag-off and completion**), `Queued` / `4 Acid Line (at the next swap point)`. The fixed "at the next swap point" is accepted until the live view exposes the preset's swap timing; then `<when>` is per 12.2. The exit hold's `muFill` property is a literal key like `muDanger`, accepted by that precedent.
- **Where it is not specified further:** the clip row is section 13 (#1277); the mixer strip and header-bar badges follow 12.3.

### 12.7 Which controls stay live in Performance mode (ruled 2026-10-14; Andy amends the principle; backlog #1279)

**Amended principle (Andy, 2026-10-14, after testing with a Launchpad: Play was dead and he could not start the transport).** The screen is the normal screen of the playing preset, locked and read-only, **except a short list of exempt controls**. Transport essentials (Play / Stop) must work from the screen; the surface can disconnect, and a performance cannot start without a way to run the clock. Everything not in the Active rows below stays locked. 12.1 item 4 and the "Perf toggle is the one live PC control" wording in this section's principle are read with this exception.

| Control (every product, shell-owned) | State | Reason |
|---|---|---|
| Transport **Play / Stop** | **Active** | Amendment: the transport must be startable from the screen. It goes through `mu_core::resolveTransport` as always, so with a host, MIDI clock or Link owning the clock it is greyed, exactly as outside the mode. First to be made exempt. |
| **Perf / LIVE** toggle (hold 1 s to exit) | **Active** | The one exit; unchanged (12.5). |
| `StepIndicator`, sidebar layer indicator | **Read-only** (hover status still works) | Indicators, no click handling (12.2, 12.3). |
| **Tap** tempo | Locked | One stray tap on stage rewrites the tempo. Tempo is set before the gig. (Andy: no, see decisions.) |
| **Tempo** field / drag | Locked, shows value live | Same; it still animates when a clock changes it. |
| **Loop / Swap** dropdowns (incl. Swap timing, 6.6) | Locked | Set in the preset; the surface launches within them. |
| **Preset** dropdown, New, Save | Hidden (replaced by `StepIndicator`) | 12.1 item 2. |
| **Clip strip** pads | Locked, follows the surface | 13.4; launching is the surface's job. |
| **Layer** selection (sidebar), Add, Delete, Reset, reorder | Locked | Layer selection follows the surface (12.3); nothing structural on stage. |
| **Mute** buttons (header bar, mixer) | Locked | Quantised mute is the surface's action (design-launchpad 3.3). |
| **Mixer** button and overlay | Locked | Opening an overlay under the shield would need every overlay control exempt; a frozen overlay is the failure 12.6 already guards against. |
| **Settings** (gear), About | Locked | Device and sync setup is done with the mode off (14). |
| **Master volume** | Locked | Owner decision 1 below. |
| **Panic / all-notes-off** | Not on screen | None exists; **Stop** is the on-screen recovery (the engine releases held notes on stop). A Panic control stays out until M6 (design-launchpad). |
| All knobs, faders, dropdowns, preset browser, chooser, keyboard | Locked | 12.1 item 4; no performance keys (12.4), and that includes Space for Play: the shield still swallows all keys. |

**Mechanism: an exempt-controls registry in the shell, not per-product code.** `ShellChrome` (the shield, 12.6) owns `registerExempt(juce::Component&)` / `unregisterExempt(juce::Component&)`, holding `juce::Component::SafePointer`s. The shield's `hitTest` returns false at any point inside a registered, visible, enabled component (tested in the shield's coordinates through `getLocalArea`), so mouse events reach the control itself and everything else is swallowed. Each exempt control is exempt **by registering itself**: `TransportBar` registers Play and the Perf toggle; `StepIndicator` and the layer indicator register themselves; this **replaces** the fixed list of "exempt rectangles" in 12.6. Rules: (1) the shield still swallows **all keys**; an exempt button must have `setWantsKeyboardFocus(false)` so a click never moves focus and Space / Enter do nothing. (2) Registering is a mu-core decision; a product cannot register anything (the registry is not exposed through `EditorShellBase`'s product-facing API). Adding a control to the Active list later is a ruling here plus one `registerExempt` call. (3) The exemption is unconditional on the mode: when the mode is off the shield is down and the registry is unused. Do not use `setInterceptsMouseClicks` on the whole tree, and do not disable-and-re-enable controls.

Play looks and sounds as normal: no extra cue, the existing Play lamp / lit face. It is not a per-product constant; the Active rows are the same in every product.

**Launchpad Play / Stop pad: recommend revisit.** The earlier ruling (no transport and no Panic in version 1, design-launchpad 3.6) stands for version 1, but with screen Play now active the objection (a dead transport) has gone away and the remaining case is a performer whose hands are on the pads. Decided (Andy, 2026-10-14): **Play / Stop is top-row button 8** (green stopped, red playing; greyed when a clock owns the tempo), not the logo; the top-row table is in [design-launchpad.md 3.6](design-launchpad.md#36-surface-mapping-defaults-for-the-launchpad-x-ruled-2026-10-13-backlog-1276). Performance mode is still entered and left on the screen only. **Panic never on a pad** (an accidental press must not cut the sound).

**Decisions for Andy (yes / no; recommendation first):**
1. Tap tempo active in Performance mode? **No** (a stray tap rewrites the tempo; set it before the gig).
2. Master volume fader active in Performance mode? **No** (an accidental drag is louder than a missing fader on stage; use the host or interface). Say yes if you want a screen level trim.
3. Launchpad logo button as Play / Stop (M6, not v1)? **Yes.**
4. Space bar as Play / Stop in the mode? **No** (12.4: no performance keys).

**Future (not now, owner):** several apps in a mu-link rig with one app "in focus" on the Launchpad. Nothing here closes it off: the lock, the indicators and the `ControlSink` are all per app.

---

## 13. Clip bank: the per-layer clip strip (ruled 2026-10-13; backlog #1277)

Data and pad rules: [design-launchpad.md](design-launchpad.md) section 3.2 (pad behaviour, owner 2026-10-09) and [design-plugin-family.md](design-plugin-family.md) (Performance mode, `LaunchClip`, `LiveView`). A **clip** is a stored state of one layer; a layer has up to 8; launching one is a staged layer swap through the shared `Stager`; **a pad never writes a clip** (the surface only recalls, mutes and cancels). Everything in this section is the screen half, and it is one behaviour in every instrument product.

**Principle: the clip strip is the on-screen twin of that layer's row of pads.** Same eight states, same colour, same meaning of a press; the screen can additionally store and clear, because only the PC edits. Outside Performance mode it is a normal control; in Performance mode the shield (12.6) makes it read-only and it shows what the surface did.

### 13.1 Where it lives

- **In the shared `LayerHeaderBar`** (the layer's 32 px header strip), not in the sidebar and not in the content band. Why: the strip is the one place that already means "this layer's stored states" (layer preset dropdown, Save, Reset, the SWP badge), it is identical in all four products, and it costs no height (the header strip and the 464 content band are fixed). The sidebar stays a layer list (a mini-visual per layer, 82 px wide: eight pads do not fit and would repeat for every layer).
- **New shared component `ClipStrip`** (`mu-core/UI/ClipStrip.{h,cpp}`), a child of `LayerHeaderBar`, opt-in like Reset and Delete: `setShowClips(bool)` (default false). It reuses the `StepIndicator::Lamp` states and its `litFor` lighting function (hoist both into a small shared header both include); it does **not** extend `StepIndicator`, which is a display with no click handling and is an exempt rectangle of the shield. No per-product clip drawing, no product subclass.
- **Order in the header bar, left to right:** colour dot, name LCD, SWP badge (existing), preset dropdown (the flex item), **ClipStrip**, Save, Reset, Delete. The strip sits next to the preset controls it relates to, right of the dropdown. Spare width at Medium (1088): the strip takes about 251, and the dropdown keeps about 600; the dropdown's existing 80 px floor is untouched.
- **Which layer.** The strip always shows the layer the panel shows. Selecting another layer (sidebar, or the surface's right column in Performance mode) rebinds it.

### 13.2 The strip: eight pads

`[ Clip ][ 1 ][ 2 ][ 3 ][ 4 ][ 5 ][ 6 ][ 7 ][ 8 ]` left to right = the Launchpad's columns 1 to 8.

- **Caption** "Clip" engraved (the metal engraved-label helper, 11 pt row-label size) in a `kClipLabelW` slot, so a user who does not know the term reads it at once. Hover on the caption: nothing (it is a label).
- **Pad** = a small rounded-rectangle lamp (`drawLamp` with a rounded-rect shape, the same lens as every other lamp) `kClipPadW` wide by `kStripDisplayH` high, with the clip number engraved on it in `mutedText` on an off or dim lens and in the engraved-label dark colour on a lit lens, so it stays readable (the coder picks the existing colour ids; no new colour). Pads are fixed, never wider or narrower, and keep `kClipPadGap` between them. Pad colour is the **layer's palette colour** (the dot colour, the same hue the surface shows, design-launchpad 3.2b), not the app accent: a clip is part of a layer. This is the one place the layer colour is used for lamps; 12.2's preset lamps stay app accent because an item there is a whole preset.
- **States** (the on-screen twin of the pad lights; lit amounts are existing `Lighting` fields, no new alpha):

| State | Pad light | On screen |
|---|---|---|
| Playing (this layer's current clip) | bright | `lampOn`, steady, layer colour |
| Available (stored, not playing) | dim | `lampDim` |
| Empty (nothing stored) | off | bare `lampBase()`, number still engraved so the pad reads as a place to store |
| Queued for the next boundary | pulsing | smooth pulse between `lampDim` and `lampOn + lampPlayhead` at `queuedPulseHz` (2 Hz), as 12.2 |
| Playing clip of a **muted** layer | flashing | **square-wave** flash between `lampOn` and `lampDim` at `Lighting::clipMutedFlashHz` (1.0), 50 % duty. A flash (hard on / off) is deliberately unlike the smooth queued pulse so the two never read alike; the dim half is never `lampOff`, so a muted playing clip never looks empty |
| Just became playing | bright flash | `lampOn + lampPlayhead` for `stepFlashMs` (150), then steady, as 12.2 |

  A muted layer that also has a queued clip shows both: the playing pad flashes and the queued pad pulses. If no clip is playing (see 13.6, none has been launched since load) no pad is bright; stored pads are dim.
- **Hover** (status bar, name + value): `Clip 3` / `Kick: stored`, `Clip 3` / `Kick: playing`, `Clip 3` / `Kick: playing, muted`, `Clip 3` / `Kick: queued (<when>)`, `Clip 3` / `Kick: empty, click to store`. `<when>` follows the preset's swap timing (12.2 / 6.6; "now" when stopped); until the live view exposes it, the fixed "at the next swap point" (12.6) is accepted. Tooltips (sparingly, 700 ms): on a stored pad "Play clip 3 (click again to mute)", on an empty pad "Store this <noun> here"; the noun is the product's lower-case layer noun.
- **Disabled**: none. A strip is hidden (`setShowClips(false)`) in a product or build that has no clip bank; it is never shown dead (no hot-swap, no strip: mu-Clid MIDI, see 13.7).

### 13.3 Gestures (one path, the same meaning as the surface)

A press on the screen calls the same `ControlAction`s the surface does (`LaunchClip`, `MuteLayer`, and the store / clear actions below) through the shell's `ControlSink`; the strip contains no switching logic of its own, and the state it shows is derived from `LiveView`, never from who pressed.

| Gesture | Pad is | Result |
|---|---|---|
| Click | Stored, not playing | **Launch**: queue the clip; it lands at the preset's swap timing (immediately when stopped). The pad pulses until it commits. A previously queued clip on this layer is replaced. |
| Click | Playing, nothing else queued | **Mute** the layer; click again to **unmute** (owner-approved). Immediate, like the mixer M button (the strip is locked in Performance mode, so the quantised mute of design-launchpad 3.3 applies only to the surface). The pad flashes while muted. |
| Click | Playing, another clip queued | **Cancel the queue** (the clip keeps playing, the queued pad returns to dim). Same rule as the preset indicator (12.2): pressing what is playing cancels the wait; to mute, press again. |
| Click | Queued | No change (it is already queued). |
| Click | Empty | **Store** the layer's current state here, no confirm (nothing is lost). The pad becomes the playing clip and flashes once. |
| Shift + click | Any | **Store** the layer's current state here. On a stored clip other than the playing one, ask first (`confirmAsync`, title "Replace Clip", message `Replace clip 3 of "Kick" with its current state?` newline `This cannot be undone.`, button "Replace"; red destructive button, as Delete). On the playing clip (updating it after a tweak, the normal workflow) no confirm. |
| Right-click (or Ctrl + click) | Any | A small menu: "Store here" (as Shift + click), "Clear" (enabled only when stored). "Clear" asks first (`confirmAsync`, title "Clear Clip", message `Clear clip 3 of "Kick"?` newline `This cannot be undone.`, button "Clear"). Clearing the playing clip leaves the layer playing as it is (the pad goes off, no pad is playing). |

- **Implementation rulings (C6 review, 2026-10-14).** `StoreClip` / `ClearClip` are screen-only `ControlAction`s: absent from the name list so no map can name them, refused by `perform` while Performance mode is on. `LaunchClip` is accepted with the mode off (the strip is a normal control then). Pad number ink: `mutedText` on an off or dim lens, `lampBase()` on a lit lens; the caption uses `labelText`. The pad is a rounded-rect lens inset 1 px inside a `lampBase()` cell, hot spot at its centre.
- **Edits are not clips.** Turning a knob changes the live layer only; the playing pad stays bright and the stored clip is unchanged until Store. Recalling another clip replaces the live edits without asking (the same as loading a layer preset today). Whether the playing pad should show "modified" is an open question (13.8), not built in the first version.
- **Store / recall, wording of the stored state.** What is stored is the layer's composed state, exactly what the layer preset file holds for that layer (engine, sequencer, voice parameters, its modulators); not its mixer strip (level, pan, sends, mute, FX sends), which is the mixer's. This keeps a clip launch from moving a fader mid-song. (Assumption, to confirm by the architecture-steward: the layer preset file already excludes the strip. If it does not, the clip follows whatever the layer preset holds, minus the strip.)
- **Status lines** (name + value, cleared after `kPerfStatusMs` 3 s unless stated): `Clip` / `Stored Kick clip 3`; `Clip` / `Cleared Kick clip 3`; `Queued` / `Kick clip 5 (at the next swap point)` (stays while waiting, clears on commit or cancel, as 12.2); `Mute` / `Kick muted` and `Kick unmuted`.
- **Keyboard.** None. No new binding (the family has no per-layer clip keys, and Performance mode forbids performance keys, 12.4). Shift and right-click are the only modifiers.
- **Do not.** Make a click on a dim pad store instead of launch; write a clip from the surface; offer "Launch" and "Store" as separate on-screen buttons; add an auto-advance or chain (dropped by the owner); put a clip's number or name in the sidebar; draw the pads in the app accent.

### 13.4 With Performance mode and the Swap timing

- **Performance mode on.** The strip is under the shield: it ignores every click, Shift and right-click, but keeps animating. It is **not** an exempt rectangle (it has no hover in the mode; the layer's name and clip state are read from the lamps and the status line). The sidebar layer indicator and `StepIndicator` stay the only exempt displays. A surface press selects the layer (the screen follows, 12.3), the strip shows that layer's pads, and the queued pad pulses until it commits, with the existing SWP badge in the header and the `Queued` status line. Store, Clear and the confirm dialogs are unreachable (the lock); nothing a clip does can write data on stage.
- **Entering the mode closes any open clip menu or Replace / Clear dialog as a cancel** (12.6 already closes every overlay and dialog; the pad menu is a popup menu and is dismissed by the same call).
- **Swap timing.** A clip launch is a layer swap, so it lands at the preset's `mstr_swap` choice (6.6): master loop, this layer's loop, or the bar; immediately when stopped. One setting, no second clip-specific quantise. Mute and unmute use the launch-quantise rule of design-launchpad 3.3, not the swap timing. A **full-preset switch** (a performance item) drops every pending clip launch (the stager's existing behaviour): the queued pads stop pulsing, and at the commit the strip shows the new preset's bank.
- **The preset dropdown and SWP badge.** A queued clip shows the existing SWP badge (`setStagingBadge`) in the header, exactly as a queued layer preset does. After a clip commits, the preset dropdown shows the placeholder `Clip 3` (`setPresetPlaceholder`) because the layer no longer matches a file; loading a file from the dropdown clears the playing clip.

### 13.5 Tokens

All in `MuLookAndFeel.h` unless stated; every one is scaled with `s()`.

| Token | Value | Use |
|---|---|---|
| `kClipPadW` | 24 | Pad width |
| `kClipPadH` | `kStripDisplayH` (20) | Pad height (a derived reference, not a new number) |
| `kClipPadGap` | 3 | Gap between pads |
| `kClipLabelW` | 30 | Engraved "Clip" caption slot |
| `kClipStripGap` | 8 | Gap between the strip and the dropdown |
| `MuTheme::Lighting::clipMutedFlashHz` | 1.0 | Muted-playing square flash (new, in `MuTheme.h`) |

Strip width at Medium = 30 + 8 x 24 + 7 x 3 + 8 = 251. Reused, not new: `queuedPulseHz`, `stepFlashMs`, `lampOff / lampDim / lampOn / lampPlayhead`, `kPerfStatusMs`, `kStatusLampD`-style lamp lens. Large scales everything together; the dropdown flex absorbs the rest. No product-specific layout constant exists.

### 13.6 The bank and the loaded preset (decided by Andy, 2026-10-14)

- **After a preset load or New**, the bank is whatever the file holds; **no clip is playing** until one is launched, unless the file says which was active (decision A below). Pads then read: stored dim, empty off.
- **Reset layer** returns the layer to defaults and **clears its bank**; the reset confirm gains one line only when the bank is non-empty: `This also clears its clips.` (inserted before `This cannot be undone.`; the one shared `LayerHeaderBar::resetConfirmMessage` takes a clip count). **Delete layer** takes its bank with it, no extra wording.
- **Layer preset files** (`.muRhythm`, `.muPattern`, `.muArp`, `.muTrack`): never carry the bank (decision B). Loading one leaves the layer's bank alone but clears the playing-clip marker and any queued clip, because the live state no longer matches a stored clip.
- **Clip numbering** is fixed 1 to 8 and matches the pad columns; there are no names (a stored clip is identified by its number and, in the tooltip, nothing else).

**Decisions (Andy, 2026-10-14, all as recommended). The code already implements them; tests are the clip persistence round trips in mu-Clid `ClipLaunchTests` and `LayerOwnershipTests`.**

- **A. Yes: full presets remember which clip was playing.** One small value per layer (`activeClip`, -1 for none) saved with the preset, so the Launchpad lights up correctly the moment a preset commits on stage, and a clip-based set-up reopens as left. Old files omit it and load with none playing.
- **B. No: a layer preset file does not carry the clip bank.** A layer preset is one state (a sound, a pattern) loaded into a layer; the bank belongs to the layer's place in a performance, inside the full preset. Loading a layer preset replaces the live state and leaves the bank alone; loading a full preset replaces everything including banks. Consequence: a bank is shared only by copying the full preset.
- **C. Yes: the full-preset format carries the bank.** Each layer node gains an optional `<Clips>` child holding up to 8 composed states (the same node a layer preset saves) plus the `activeClip` value from A. Old files load with empty banks; a new file opened in an older build ignores the children. No version bump, because unknown children are ignored. Size: up to 64 layer states per preset beyond today's 8, text XML, still small (sample and wavetable data are references, not embedded).

### 13.7 What the user can do, per product

The strip is the same shared component with the product's layer noun in tooltips and dialogs (Rhythm, Voice, Layer, Track). It needs the hot-swap `Stager`, so it appears wherever a product has one.

| Product | Noun | Strip on screen | Surface (standalone, Performance mode) | Swap timing choices |
|---|---|---|---|---|
| mu-Clid | Rhythm | Yes, every rhythm (up to 8) | Rows 1 to 8 = rhythms | Master loop, rhythm loop (6.6) |
| mu-Tant | Voice | Yes, every voice (8) | Rows = voices | Master loop, voice loop, bar |
| mu-Toni | Layer | Yes, every existing layer (1 to 8); rows with no layer stay off | Rows = existing layers | None shown: every timing is the bar |
| mu-On | Track | Yes, every track (1 to 8); a clip may change the track's engine, as loading a `.muTrack` does | Rows = tracks | None shown: every timing is the bar |
| mu-Clid MIDI | Rhythm | **No** (no hot-swap, no `mstr_swap`; a strip would be a dead control, as with the Trig / Leg switches). Shown later if it gains a stager | None | None |

The Launchpad and Performance mode are standalone only for now (12, availability), so in a plugin build the strip is on screen only: click to launch, mute, store, clear, with the same quantise rules; no surface lights.

### 13.8 Open questions

1. **Decisions A, B and C**: settled (13.6).
2. **Show "modified" on the playing pad?** After tweaking a recalled clip, a small mark (for example an engraved dot on the playing pad) would warn that Store has not been pressed. Recommendation: not in the first version; add if testing shows lost tweaks. It needs the stored clip compared with the live state, which costs a comparison the architecture-steward should size.
3. **Mute vs cancel on the playing pad** (13.3): the cancel-first rule adds a press to mute while a clip is queued. Recommendation: keep, for parity with the preset rule; revisit on hardware.
4. **Should the surface get a "store" gesture later?** Owner ruled no (a pad never writes a preset); not planned.
5. **Does the strip appear in mu-Clid MIDI later?** Only if it gets a stager (design-future "one instance" direction); nothing in the strip closes that off.
6. **Large display on a pad-heavy gig**: the 20 px pad height is the family strip display height; if a touch screen is used, Large (1.25) is advised, no touch-specific size (12.4).
7. **Row direction** (design-launchpad open question: rows = layers, columns = clips) is taken as settled by 12.3; the strip's left to right order is the columns.

Follow-ups for the coder / backlog (not edited by the UX controller): (a) `LiveView` gains per layer `clipOccupied(i)`, `playingClip`, `queuedClip` and `isLayerMuted`, and `ClipStrip` reads only that; (b) hoist `StepIndicator::Lamp` and `litFor` to a shared header and add the muted-flash state; (c) new `ControlAction`s `StoreClip(layer, index)` and `ClearClip(layer, index)` beside `LaunchClip` (screen-only, refused by the surface mapping); (d) `LayerHeaderBar::resetConfirmMessage` takes the clip count; (e) tests: lamp state per row of the 13.2 table, the gesture table of 13.3 against a fake sink, the 251 px width and dropdown floor, strip hidden in mu-Clid MIDI, and the shield making the strip read-only.

---

## 14. Controller status and release (ruled 2026-10-13; backlog #1276)

The screen half of the Launchpad surface mapping ([design-launchpad.md section 3.6](design-launchpad.md#36-surface-mapping-defaults-for-the-launchpad-x-ruled-2026-10-13-backlog-1276) holds the pad lights, which are the twins of 12.2 and 13.2). Standalone only, like Performance mode: the group exists when a controller driver is linked (`mu-control`), otherwise nothing is shown. No new chrome in the TransportBar, StatusBar or main page.

- **Where: a Settings group "Controller"** in `StandardSettingsOverlay` (standard group, added by the shell when a controller manager is wired; it goes in the shorter column like every group, 5 "Settings page layout"). Two rows, standard label / control geometry, labels under 14 characters:
  1. **"Launchpad"** - a shared `StatusLamp` (round lamp + text), the text read-only: `Not found` (lamp `lampDim`, amber), `Found` (lamp `lampOn`, green: detected, not in use), `In use` (green, bright: Programmer mode on, pads lit), `Released` (bare `lampBase()`), `Lost` (red, `indicatorFault` bright: was in use, device gone). Hover status text (name + value): `Launchpad` / `LPX MIDI: In use`; `Launchpad` / `Not found: check the USB cable`.
  2. **"Controller"** - one `TextButton` (metal button helper, standard button height, `kDropdownW` wide): **"Release"** while the device is found or in use (sends Live mode, turns the pads off, closes the ports; the lamp reads `Released`), **"Connect"** while released or not found (rescans and reconnects; disabled while Performance mode is on, like all of Settings, which is unreachable under the shield). Not a toggle: the label is the action.
- **Why Settings and not the TransportBar:** the Perf toggle is the one live PC control in Performance mode (12.1) and the exit hold already ends the mode; a release button is a recovery tool used with the mode off, so it belongs with the other device settings (Audio / MIDI device, Sync offset). Leaving Performance mode sends Live mode by itself (design-launchpad 3.6), so Release is for a stuck or unwanted connection, not a routine step.
- **Status line (name + value, the existing StatusBar):** `Launchpad` / `Connected` (3 s, `kPerfStatusMs`) when a device appears or connects; `Launchpad` / `Released` (3 s); `Launchpad` / `Lost: unplugged or in use elsewhere` is **sticky** (cleared on reconnect or when the mode ends), because the PC is locked on stage and this is the only warning the player will see; it uses the same slot and rules as `Queued` (12.2). While Performance mode is on and the device is `Lost`, the mode stays on (the exit hold is still available); nothing else changes on screen.
- **As built (reviewed 2026-10-14, by code; paint not yet seen):** `StandardSettingsOverlay` builds the group only when `isStandalone`, and shows it only while the shell has attached a controller (`AttachmentWatch`; the page re-flows when it appears or goes, via `Section::shown`); `controllerLook(status)` is the pure status-to-lamp/text/button map (Released = `lampOff`, the bare lens; Found `lampOn`; In use `lampOn + lampPlayhead`; Lost `indicatorFault` bright; Not found amber `lampDim`). Hover text is `<device label>: <status>`. The Connect button is not explicitly disabled in Performance mode because Settings is unreachable under the shield. The TransportBar announces `Connected` and `Released` only on a status change after attach; the first attach takes the state as baseline and says nothing.
- **No new tokens or colours.** Reused: `StatusLamp` and its states, `lampOn / lampDim / lampBase`, `indicatorFault`, `kDropdownW`, `kPerfStatusMs`.
- **Do not.** Put a Launchpad icon or banner in the TransportBar; show a "controller" lamp per product; make Release a toggle that changes meaning; dim or disable pad-less parts of the screen when the device is absent (the screen is the same with or without a surface).

---

## Design rulings

- 2026-10-14 - mu-Toni step pattern: the accent cells become Step Off / On / Accented (Andy). Ruled: the shared `StepEditor` gains a generic level-cell mode `setLevelCells(n)` (metal look: each cell a whole lamp lens whose strength is the level, Off `lampOff`, On `lampOn`, Accented new `Lighting::lampAccent` 1.0; playhead = thin ring at new `Lighting::lampPlayheadRing` 0.6 alpha, not extra brightness; gesture: click cycles, Shift+click back, drag paints the first cell's new state), not `setQuantization(3)` (that only snaps bar height). Label "Acc Steps" becomes "Steps". Rests, storage (`stepMask`, default all On) and engine rules in mu-toni/design-sequencer.md. Changed mu-toni/design-ui.md (new), mu-toni/design-sequencer.md, §11 Lamp display row.

- 2026-10-14 - Launchpad top row as built (7a2a6e0, b65454b; #1276): arrows 3/4 rest at white x0.75 (`kArrowRestScale`, untuned) so a full-white flash and queued pulse stay visible; app arrows 1/2 are fully dark and inert when no other app exists (dark, not dim) and stay so until stage F1 of the several-apps ruling; `padDimScale` 0.20 (stale 0.45 fixed); buttons 5-7 (ClipPrev, ClipNext, MuteSelectedLayer), loss of direct jump to item n and screen-only Performance entry/exit recorded as provisional, awaiting Andy; Performance cue build (frame under shield, glow drawn by `TransportBar::paint`) accepted; `ProbeMain.cpp` stale for the top row (coder). Changed design-launchpad §3.6, §12.6 here.
- 2026-10-14 - Performance mode: Play / Stop must work from the screen (Andy; #1279). Ruled: the principle gains an exempt list (Play / Stop and the Perf toggle active; everything else locked), implemented as a registry in the shield (`registerExempt`), not per-product code; Tap, master volume and Space stay locked (decisions pending); Launchpad logo as Play / Stop recommended for M6. Changed §12.6, new §12.7.
- 2026-10-14 - Performance mode "is it on?" cue revisited (#1279; Andy could not tell the mode was on: Perf looked like the selected Mixer button). Ruled: a 3 px accent window frame (replaces the 2 px bar rim), the Perf label reads LIVE when on, and the Perf face is a lit-lamp accent face with a glow, not the shared selected-toggle look. New tokens `kPerfFrameW` 3, `kPerfGlowR` 4, `Lighting::performanceGlow` 0.45; `kPerfRimH` retired. Changed §12.1, §12.6.
- 2026-10-14 - Clip bank decisions A, B, C (#1277) decided by Andy as recommended: A yes (full presets remember the playing clip, `activeClip` per layer); B no (layer preset files do not carry the bank; loading one leaves the bank alone and clears the playing marker); C yes (full-preset format gains an optional `<Clips>` child per layer plus `activeClip`, no version bump). Already implemented. Changed §13.6, §13.8.
- 2026-10-14 - Launchpad X top row (Andy; #1276): 1/2 up/down = previous/next mu app (app-accent lit), 3/4 = previous/next preset (white, wrap, no per-item pads), 5-7 tbc (clip prev, clip next, mute selected recommended), 8 = Play/Stop (green stopped, red playing, dim white when a clock owns the transport); Performance mode entry/exit stays on the screen; logo unused. New token `MuTheme::SurfaceColours::go`. Changed design-launchpad §3.6 and §12.6 wording here.

- 2026-10-14 - Layer-naming leftovers (#1588), decided by the UX controller on Andy's delegation. (1) Add button: the rule already in the layer-sidebar section stands unchanged and the code matches it (`LayerSidebar::resized` / `timerCallback`): shown whenever the product wires `onAddLayer`, hidden only in Performance mode where the "n/N" indicator takes its slot, disabled (never hidden) at `mu_limits::kMaxLayers` with the tooltip "Maximum of 8 <noun>s". No per-product exceptions. (2) Class names: `EnginePanel` is retired and no class by that name remains; the product band is `<Type>Content` (`RhythmContent`, `PatternContent`, `ArpContent`, `TrackContent`). mu-Clid's `VoiceSection` stays: it is the product's subclass of the shared `VoiceBand` (a section inside `RhythmContent`, not the main panel), and its name matches the `kVoiceSectionH` token; renaming it buys nothing and churns tokens and docs. No UI change. Changed this entry only.

- 2026-10-14 - Review of the Controller status and release UI (#1276 stage M4), by code, paint unseen. Pass on lamp colours, button label logic, standalone-with-attachment visibility and the status-line messages; section 14 now records the as-built details (first attach is silent, Connect needs no Performance-mode disable). Changed §14.

- 2026-10-13 - Launchpad X surface mapping defaults (#1276), documentation only. Grid pads use the layer palette and the 13.2 clip states, top row the app accent and the 12.2 item states (queued and loading share the pulse; missing = dim red), right column shows the selected layer bright and others dim; RGB lighting with dim = 0.30 (`Lighting::padDimScale`, new) and blink from the driver's own timer at `queuedPulseHz` / `clipMutedFlashHz`; no shift, no second page, no transport or Panic pad in version 1 (recommended logo play / stop, greyed static white under an external clock, left to Andy). New §14: Settings group "Controller" with a Launchpad status lamp and Release / Connect button, sticky `Lost` status line. Changed new §14, design-launchpad.md §3.2 edge buttons, new §3.6, §5. Andy's: transport and Panic, Programmer-mode lifetime, dim level, top-row colour.
- 2026-10-13 - Final review of modulation rows (#1599) by code, and Performance discoverability (909fa41). (1) Pass: destination capped at new token `kModDestMaxW` 240, amount slider takes the rest, pair minimum 232, same arithmetic in both rows; the knob-style fader caps are accepted by Andy. (2) The dropdown entry plus the lit button's tooltip, the 8 s entry hint and the early-release hint are enough; no persistent label; the Preset Browser footer button is unreachable in the apps and the dropdown entry is the way in. Changed §5 (Modulation rows), §12.5, §12.6.

- 2026-10-13 - Clip bank UI (#1277): where and how do clips show on screen? A shared `ClipStrip` in the layer header bar (right of the preset dropdown), 8 layer-coloured lamp pads numbered 1 to 8 with the pad states as on-screen twins (playing bright, available dim, empty off, queued smooth pulse, muted playing clip hard 1 Hz flash); click launches / mutes / cancels, click on empty or Shift + click stores, right-click menu stores or clears with confirms; lands at the preset swap timing; read-only under the Performance shield; hidden in mu-Clid MIDI. New tokens `kClipPad*`, `kClipLabelW`, `kClipStripGap`, `Lighting::clipMutedFlashHz`. Preset-file impact (active clip, layer-preset bank) left to Andy. Changed new section 13, 12.3, 12.6.

- 2026-10-14 - Performance chooser: where does Load sit, and what if the loaded file is outside the performances folder? Load joins New, Save, Rename, Delete in the header row; the footer is Close and Use only (the old footer "Load" was Use). An outside file is an import: Save writes a copy into the performances folder (Overwrite prompt on a clash) and never touches the original; the as-built "outside file becomes the working file" is not accepted, coder to change. Changed 12.5.

- 2026-10-13 - Review of the Performance chooser build (#1597), by code (no paint grabbed). Settled: the footer "Performances" button is final, Use saves first and closes, discard prompts, pick mode, name-only Save card, kPerf* tokens. Changed 12.5, 12.6. Not yet: see the review changes (status cue in pick mode, single "saved and loaded" line, discard wording, tooltips).

- 2026-10-13 - Review of Performance mode step 3 as built (570ee8e), from code and tests only. Accepted: rim 0.85, Perf between gear and Mixer (docs corrected), 8-item "n/N", fault lamps for missing and other-product items, fixed "at the next swap point", interim footer button, invisible shield with exemptions, entry order, Ctrl+P enters only, keys swallowed. Ruled: ghost omitted under `kStepGhostMinW` 36; Queued status stays while waiting and clears on commit or cancel; the hold status clears on release. Changed 12.1, 12.2, 12.5 tokens, 12.6.
- 2026-10-13 - Mixer fader thumbs are small white dots (JUCE default; no `drawLinearSlider`): what replaces them? A knob-style rectangular fader cap (16 x 26, rounded 3) with the knob disc's lighting, an accent pointer line with the knob dot's halo, over a recessed switch-style groove with an accent travel line; amber `knobLevel` for faders, `knobColour(slider)` otherwise; one shared `drawLinearSlider` for both orientations (also the `BipolarSliderRow` depth slider; pan is a knob and unchanged); reuses existing knob/switch Lighting fields, new tokens only `kFaderCapW/H/Corner`, `kFaderTrackW`, `kFaderPointerW`. Changed new §6.7.
- 2026-10-13 — Performance mode step 3 (#1328): is §12 complete? Nearly; open points ruled in new §12.6: invisible shield with exempt rectangles (toggle, StepIndicator, layer indicator), swallows keys too; entering closes all overlays and dialogs as a cancel; toggle states and tooltips; no performance chosen = status line "Choose a performance first" until the chooser overlay exists; interim "Performances" footer button using the standard async FileChooser; exit hold fills the button face with progress, drag-off cancels; editor follows `selectedLayer`.

- 2026-10-13 — #1596 stage (d): where does the per-preset swap timing control go? A second group "Swap:" (standard `DropdownSelect`) in the shared `MasterLoopSection`, opt-in via `setSwapTimingChoices`; mu-Clid offers master / rhythm loop, mu-Tant master / voice loop / bar; mu-Clid MIDI, mu-Toni and mu-On show nothing (no `mstr_swap` to set, or no loop so all values are the bar; they do not mount the pane); unsupported stored value displays as the nearest supported, never rewritten; live refresh by listener + 30 Hz reconcile like `mstrLoop`. Bar-specific compact sizes, widened 2026-10-13 after first paint clipped text (loop dropdown 92, "Swap:" label 38, swap dropdown 116; pane 350 with Swap / 172 without); preset floor 180 holds (about 200 at the 1170 baseline), no `kPresetMinW` constant needed; by-eye check outstanding. Settings "Timing" removed. Changed new §6.6, mu-clid/design-ui.md Settings.

- 2026-10-13 — #1594 review; do plugin strips need real stereo peaks now? Not a gate at the time (mono mirrored was accepted); superseded 2026-10-13, all plugin strips (channels, returns, master) now bind real L/R peaks and are true stereo, layout and tokens unchanged; narrow-strip right clamp and the empty GR slot gap accepted. Changed §3.5.
- 2026-10-13 — mu-link VU meters hard against the strip's right edge; stereo? (1) Family-wide: the fader slot (`kMixerFaderW` 20) is centred on the strip axis and the meter sits immediately to its right, replacing "fader fills width minus 10, meter at the edge" in all three layouts (overlay, Client, Master), one placement helper. (2) All strip meters are stereo: two bars L / R in one `VUMeter` (`getLevelR`; every strip now supplies real L and R peaks), per-bar ballistics and peak-hold, existing colour tokens. (3) New tokens `kVUBarW` 6, `kVUBarGap` 2, `kMixerFaderW` 20 in `MixerChannel.h` beside `kVUW`, which becomes 14 (derived). Data: a second peak atomic per strip in every engine. Changed §3.5, §5 table, mu-link/design-ui.md.

- 2026-10-13 — Standard dropdown size and mu-On's Groove box width as shared tokens? Yes: `kDropdownW` 120, `kDropdownH` 24 and `kGrooveBoxW` 300 live in `MuLookAndFeel` beside `kDropdownEdgeGap`; FXRow, DelayRow, mu-Toni ArpContent and mu-On TrackContent use them, the four local copies and bare `s(24)` are gone; values unchanged. Changed §5 standard control sizes, mu-on/design-ui.md "As built".
- 2026-10-13 — Performance mode UI (#1279), revised with the owner's decisions: standalone only for now (`setPerformanceModeEnabled`), PC UI locked and read-only (no on-screen step buttons or performance keys), the Perf toggle the one live control with a press-and-hold exit (1 s, recommended), cue colour the app accent, no touch mode, the screen follows the surface (a pad press switches the display to that layer, queued shown until the swap commits), surface prev / next wrap, pressing the current item cancels a queue. Main screen unchanged; the preset group becomes a read-only `StepIndicator` (LCD "n/N name > next" + 8 lamps matching the pads), a lit 2 px rim, the sidebar Add slot a layer indicator; the chooser is its own overlay, reachable with the mode off. Added §12; tokens listed there. Future: several apps in a mu-link rig with one in focus.

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
- 2026-10-13 — Fader thumb re-check: BipolarSliderRow rendered in default purple (no colour field). Ruled: it sets `knobModulation` on its depth slider and curve knob in its constructor; the extra fader tokens (`kFaderCapMargin`, `kFaderCapAspect`, `kDisabledAlpha`, knob-face and switch-track colours, FaderThumbGeometry.h) are now listed. Changed §6.7 Colour and Tokens.
- 2026-10-13 — AddButton disabled alpha was a literal 0.35 against kDisabledAlpha 0.4. Ruled: one shared family disabled alpha, kDisabledAlpha 0.4; AddButton::enablementChanged uses it (no deliberate difference). Changed §6.7 tokens.
- 2026-10-13 — Fader fill strength and the pink depth/curve. Fader accent fill gets its own token faderTrackFill 0.5 (switchTrackGlow stays 0.18 for the slide switch); the main-page app-colour rule wins over knobModulation on BipolarSliderRow (pink only off a main page). Changed §6.7 tokens and the main-page knob rule.
- 2026-10-14 — Layer 8 colour and pad colours (commit fb1e8b5). Ruled: layer 8 is Navy `#2B3FD6` (Andy chose it on the pad; Copper dropped); pad colours are corrected per device in `mu-control` and the screen value never moves; dim level `padDimScale` 0.20. Navy is acceptable on screen for lamps, fills and large marks (about 2.3:1 against the window background; not for small text or hairlines); if it reads dull, the recommended lift is `#4A5DEB` (about 3.4:1). Changed §8 voice colours, §11 voice colours.
- 2026-10-14 — C6 review of ClipStrip. Ruled: Pass. Pad number ink is `mutedText` on off / dim and `lampBase()` (the dark lamp base) on a lit lens (threshold `lampOn`); the pad is a 1 px inset rounded-rect lens with the hot spot centred; StoreClip / ClearClip are screen-only (never nameable by a map, refused in Performance mode); LaunchClip works with the mode off; the on-screen mute click is immediate (same as the mixer M button) because the strip is locked in Performance mode, so the quantised mute applies only to the surface. Changed §13.2, §13.3.
