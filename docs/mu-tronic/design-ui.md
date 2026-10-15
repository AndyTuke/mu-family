# mu-tronic - UI Design

Product-specific layout and behaviour. The look, tokens, controls and metal style are the family's: [../design-ui-family.md](../design-ui-family.md); none are repeated here. Structure, data model and naming of the product are in `design.md` (architecture-steward). Ruled 2026-10-15 (Andy's brief: an empty unit with the family's mod, effects and mixer; 16 layer slots filled from mu-Clid, mu-Tant, mu-Toni and mu-On; buses in the mixer; a possible successor to mu-link as a performance tool). **Design only: nothing here is built.**

**Principle.** mu-tronic is not a new look. It is the family shell around the four existing content panels, plus three additions: a page switch (16 slots, 8 at a time), an empty-slot chooser, and a bus page in the mixer. Everything else (window 1170 x 870, TransportBar, StatusBar, `LayerSidebar`, `LayerHeaderBar`, `ClipStrip`, `ModulatorPanel`, `MixerOverlay`, Settings, Performance mode) is reused unchanged. No new window, panel height or sidebar width.

## 1. Words (ruled)

| Word | Means in mu-tronic | Notes |
|---|---|---|
| **Layer** | what sits in a slot: a Rhythm, Voice, Arp or Track | The family parent noun ([design-naming.md](../design-naming.md)). Sidebar, header tooltips and dialogs say "Layer" |
| **Slot** | one of the 16 positions, numbered 1 to 16, which may be empty | User-facing use is limited to "Slot 5" in the empty state, status lines and menus. design-naming reserves "Slot" for FX and insert slots in code; the architecture-steward picks the code name. **Andy's own word "voice" is not used for a slot**: "Voice" is mu-Tant's layer noun and "voice" inside an engine is a polyphonic voice |
| **Type** | which product a layer comes from. Shown as **Clid, Tant, Toni, On** (the mu- prefix is dropped where space is short) and in full ("mu-Clid") in the chooser | Type nouns for default names: Rhythm (Clid), Voice (Tant), Arp (Toni), Track (On). Default name = "<type noun> <slot number>", e.g. "Track 5" |
| **Bus** | a mixer group that channels are assigned to instead of Main | Four, "Bus 1" to "Bus 4" |
| **Main** | the master output (the existing label in the strip's Output selector) | unchanged |
| **Out 1 to Out 8** | the existing DAW multi-out routing (plugin builds only) | The code calls it `outputBus`; that identifier now collides with "Bus". Follow-up for the architecture-steward to rename in code; the user-facing words above stand |
| **Channel** | the mixer strip of a layer | unchanged (design-naming) |

## 2. Shell (what is the family's, unchanged)

TransportBar (Play, BPM, Tap, Nudge, loop section, preset group, gear, Perf, Mixer), StatusBar, `LayerSidebar` 82 px, per-layer panel stack (header strip 32 / content 464 / modulator 318 in 1088 x 814). Standard Settings. The loop section carries the swap-timing control with the choices Master loop, Layer loop, Bar (as mu-Tant; a layer type with no loop of its own follows the bar).

**Product colour: owner decision pending (question 1).** Until Andy chooses, call `setMetalStyle(true)` with the colour left transparent (unpainted brushed steel; LCD lettering `globalAccent`), exactly as mu-link does ([../mu-link/design-ui.md](../mu-link/design-ui.md) section 1). No new token is added until he chooses.

**The main area is painted in the selected layer's type colour (ruled, confirm in question 2).** The shell chrome (TransportBar, StatusBar, sidebar, mixer, settings, empty-slot chooser frame) is mu-tronic's own colour. The *main area* (header strip, content band, modulator panel) takes the app primary of the layer's type: `appPurple` Clid, `appGreen` Tant, `appBlue` Toni, `appYellow` On, applied by the same `setAppAccent` / `MuLookAndFeel::appAccent(component)` route and re-applied when the selection changes. Why: the whole reason to reuse each product's content panel is that it looks and feels like that product, and the paint tells the player which engine is under their hands without reading anything. Main-page knobs follow (family rule "main-page knobs are the app colour" reads "the colour of the page's type"). The layer *identity* colour (`layerPalette`, section 3) is unaffected and never an app primary, so it cannot blend into any of the four paints. Mixer and Settings keep the category colours.

## 3. Paging the 16 slots (ruled)

### 3.1 The model: a page is a view, the selected slot always belongs to the shown page

Two pages: **page 1 = slots 1 to 8, page 2 = slots 9 to 16**. The sidebar shows the eight slots of the shown page; the Launchpad's eight rows are the eight slots of the shown page; the mixer's channel strips are the eight of a page (section 5). One page state in the shell (`shownPage`), shared by sidebar, surface and mixer.

- **Switching page selects the slot at the same position on the new page** (slot 3 goes to slot 11), so there is always a selected slot and the main panel never shows "nothing". If that slot is empty the panel shows the empty-slot chooser (section 4).
- **Selecting a slot on the other page by any route** (surface, the mixer's strip click, a status link) flips the page to it. The page follows the selection; it is never an independent hidden state.
- The page is **session state**, not saved in a preset. A preset load or New shows page 1, slot 1 (the family "select layer 0" rule).

### 3.2 The control: the sidebar's bottom slot

mu-tronic has no "+ Layer" button (empty slots are the add affordance, section 4; `onAddLayer` is not wired, so the family rule hides the button). **Its place, the `kAddBtnH` 34 px slot at the sidebar bottom, holds the page switch**: one `SegmentControl`, Lcd draw style (metal; the family tab-bar look), two segments, labels **"1-8"** and **"9-16"**, full item width (`kWidth` less the screwed item inset on each side = 60 px, 30 per segment), height `kAddBtnH`. No new constant, no reflow, the sidebar budget is the other products' (8 x `kItemH` 80 = 640, plus 34, plus edges, 685 of 774 worst case, 89 spare). The coder checks that "9-16" fits 30 px at the control's standard font; if it does not, report it back (fallback ruling: labels "1" and "2" under an engraved "Page" caption in the same slot), do not shrink the font locally.

- Status bar on hover: `Page` / `Layers 9 to 16`. Tooltip (sparingly): "Show slots 9 to 16 (Page Down)".
- A segment lights as the selected segment in the standard LCD selected look. **No activity marks in version 1** (section 3.5).
- In Performance mode the control is read-only (locked by the shield, not on the 12.7 Active list), and it **follows the surface**: the lit segment is the page of the surface's selected layer.

### 3.3 Keyboard

| Key | Action |
|---|---|
| **Page Up** | Show page 1 (stays on 1) |
| **Page Down** | Show page 2 (stays on 2) |

No wrap (two pages; a wrap would be a toggle and hide which key goes where). Active whenever no text field has focus and no modal is open; with the Mixer overlay open the same keys switch **the mixer's** page (section 5). Not available in Performance mode (the shield swallows all keys, 12.4 stands: no performance keys). These are the first non-modifier navigation keys of the family; they are registered in the shell's key map (`ShellChrome`), not in the product. No new modifier combinations: Ctrl+P stays the only entry key to Performance mode.

### 3.4 Launchpad, clips, Performance mode

- **Rows follow the shown page.** Right column button n = slot (8 x (shownPage - 1)) + n; grid row n likewise; a row whose slot is empty is off (the mu-Toni "rows with no layer stay off" rule). Clip numbering 1 to 8 per layer is unchanged; the `ClipStrip` in the header bar acts on the selected layer, as now.
- **Row colours = `layerPalette` of the layer's colour index** (section 3.6).
- **The surface can change page and the screen follows.** A press on a right-column button of the shown page selects that slot (no page change). Page change from the surface is a new `ControlAction` pair `PagePrev` / `PageNext` (device-independent, `mu-core/Control`). **Which button carries it is Andy's decision (question 4)**; the top-row buttons 5 and 6 (`ClipPrev` / `ClipNext`) are provisional and not adopted (design-launchpad 3.6), so recommended: 5 = page 1 / 6 = page 2, lit in the neutral white of buttons 3 and 4, bright for the shown page, dim for the other. Nothing else in the surface mapping changes.
- **Performance mode.** The screen is the normal screen, locked, and shows what the surface did: the sidebar page control (read-only) shows the page, the sidebar outline moves to the selected slot, the main area switches type and paint (section 2). The sidebar's "Layer n/N" indicator that replaces Add in the other products (12.3) is **not used**: the page switch occupies that slot and already shows the page, and the selected slot is outlined. The `StepIndicator` (performance items) is unchanged.

### 3.5 An "all 16" overview: not in version 1 (recommendation, question 5)

No overview screen and no 16-lamp strip. Reasons: the main area is full (no height to spare, 5), the sidebar shows a page by design, the mixer's Buses page and the master VU already show the sum, and the Launchpad is 8 rows by hardware. The cheap, reversible mitigation if testing shows lost sight of the other page: a small lit mark on the *other* page's segment when any of its layers is playing and not muted, using the existing `lampOn` state (no new token). Add it only if Andy asks.

### 3.6 Layer colour with 16 layers

`layerPalette` has 8 entries and stays 8: the Launchpad needs colours it can tell apart, and a ninth hue would collide with the four app primaries (design-ui-family section 8). **Rule: a layer's default colour is the first palette entry not used by another layer on the same page** (page-local uniqueness), so within any view (sidebar, mixer page, Launchpad page) every layer has a distinct colour, and slots 1 and 9 may share Red. Colours travel with their layer on reorder, as now. The mixer and the surface show one page at a time, so no two visible layers ever share a colour.

## 4. The empty slot and the voice chooser (ruled)

### 4.1 Fresh instance and empty state

A fresh instance holds **no layers** (all 16 empty), page 1, slot 1 selected; the first thing seen is the chooser. This is a deliberate exception to the family "fresh instance = 1 layer" rule (design-ui-family section 5, "Layer add / delete / reorder") because the brief is an empty unit; every other rule there stands.

### 4.2 Sidebar items

Filled slot: the family item (`createMiniVisual(index)` returns the **type's own mini-visual**, so the picture says the type; the slot's layer colour lights the icon and outline as in section 8). The item also shows its **slot number**, engraved, top-left (1 to 16). Empty slot: no mini-visual; the slot number and a "+" in ghost lettering (`Lighting::lcdGhost`), an unlit lamp-base outline; it can be selected like any item. Empty slots stay in the list (the positions are fixed; the sidebar never reflows or compacts).

### 4.3 What the main area shows for an empty slot

- **Header strip (32):** the family `LayerHeaderBar` with the name LCD reading `Slot 5` in ghost lettering and `Empty`; the **preset dropdown stays enabled and lists the layer presets of all four types** (`.muRhythm`, `.muPattern`, `.muArp`, `.muTrack`, grouped by type in that order); choosing one fills the slot with a layer of that type and loads it (the family rule "a layer preset lists in every layer's menu", extended to all types). Save, Reset, Delete, the clip strip and the type tag are hidden (not disabled) in the empty state: there is nothing to act on.
- **Content band (464) and modulator band (318) together, one metal panel, no modulator panel** (an empty slot has nothing to modulate). In mu-tronic's own colour (section 2). Inside it, the **chooser**: a title `Add a layer to slot 5` (engraved, name-plate size) and **four cards** side by side, then a one-line hint under them.
  - **Card** = a raised sub-panel painted in **that type's app primary** (the colour its main area will have, section 2), screws, name plate "mu-Clid" / "mu-Tant" / "mu-Toni" / "mu-On", the type's mini-visual, a two-line engraved descriptor, and an `AddButton`-look text button **"Add"**. Descriptors (wording ruled): mu-Clid "Euclidean rhythms and samples", mu-Tant "Wavetable drone, gate patterns", mu-Toni "Generative arpeggiator", mu-On "909 kick, bass, hat, snare grooves". Whole card clicks too (the button is the visible affordance; Enter on the focused card adds).
  - **Geometry (budget first).** Chooser panel = `kChannelPanelW` x (`kLayerContentH` + `kLayerModulatorH`) = 1088 x 782 (worst case with both banners 774 less the header 32 = 742: the panel derives its height from `getHeight()`). Card width = (panel width - 2 x `kScrewedPanelInset` - 3 x `kSpaceM`) / 4; card height = `kLayerContentH` (464), vertically placed under the title; hint band takes the rest. No new constant: all expressions of existing tokens through `mu_ui::s()`; at Large it fits when Medium does.
  - Hint (engraved): "Or load a layer preset from the menu above." Nothing else.
- **Choosing a type** creates a layer of that type with its defaults, default name and the colour of section 3.6, selects it (already selected), and shows the type's normal panel. Sound is silent until the user does something (the layer is created with the product's default state; whether the product defaults make sound by themselves is the layer's own behaviour, as for "+ Layer" today).
- **Wording of status lines:** `Layer` / `Added Track 5` (3 s, `kPerfStatusMs`); hover on a card: `Add` / `mu-On: 909 groove track in slot 5`.
- **Do not** make the chooser a popup menu or a dialog (a card is the product's identity and the first-run experience); offer "Add" in the header bar; auto-fill slot 1.

### 4.4 A filled slot: how it shows its type

1. **Sidebar:** the type's mini-visual (4.2).
2. **Header bar:** a **type tag**, a small raised name plate (`drawNamePlate`, `kNamePlateH`, width `namePlateWidth(text, h)`) with the engraved text `Clid`, `Tant`, `Toni` or `On`, between the colour dot and the name LCD. Opt-in on the shared bar (`LayerHeaderBar::setTypeTag(text)`; empty = hidden), off in the four products. Costs about 40 px of the preset dropdown's flex width (about 600 at Medium; its 80 px floor is untouched).
3. **The paint** of the whole main area (section 2).

### 4.5 The content, per type: reuse, do not rebuild

The main area for a filled slot is the shared `LayerPanel` with the type's content component, exactly as in the product (`<Type>Content`: Clid's sample bar + Euclid circle + voice band; Tant's Root / Scale + oscillator, X-Mod, filter, effects + gate panel; Toni's source + voice + Appergater; On's engine boxes + Groove / steps), the shared `ModulatorPanel`, the shared header bar and clip strip. All four are already ruled to fit the 464 / 318 stack ([design-ui-family.md section 5](../design-ui-family.md#per-layer-panel-stack-every-instrument-product)), so **no content panel is touched and no per-type height exists in mu-tronic**. The sidebar switches the panel when the selected slot's type differs from the shown one; the swap is a normal selection change (nothing animates).

### 4.6 Delete, reset, reorder, rename (the family standard with these exceptions)

| Family rule | mu-tronic |
|---|---|
| Delete shifts the layers above down | **No shift.** Delete empties the slot and every other slot keeps its place (positions are the Launchpad rows and the player's muscle memory). The layer's mixer strip, clips and modulators go with it; its sound is cut at the next block, as now |
| Delete disabled at one layer | **Never disabled** (an instance may be empty). Confirm wording unchanged: title "Delete Layer", `Delete "<name>"?` / `This cannot be undone.`, red "Delete" |
| Add button, disabled at the maximum | **None.** The empty slot is the add affordance |
| Reorder by drag in the sidebar | Drag **within the shown page** swaps two slots (empty included: dragging onto an empty slot moves the layer there). Moving a layer to the **other page** is a right-click menu on a sidebar item: `Move to slot...` with a submenu `1` to `16` (filled slots marked `swap`), because a drag cannot cross a page. The mixer strip, clips, modulators and colour travel with the layer. Right-click is an extra here, not the only route: a layer on the other page is also reachable by switching page and dragging a neighbour |
| Layer name editable only where saved | **Editable and saved for every layer** (recommendation, question 3): sixteen mixed layers, bus feed lists and sidechain menus need real names |
| Fresh instance = 1 layer | Empty (4.1) |

## 5. Mixer: pages and buses (ruled)

The mixer is the family `MixerOverlay`: strips, three FX returns, the FX rack (`kMixerFXAreaH` 261), the Master strip and the master insert panel. Sixteen channels at the family strip width do not fit (11 strips at 73 px already fill 1088), so the mixer **pages exactly as the sidebar does, plus one page for buses.**

### 5.1 The page selector

In the mixer's header strip (`kMixerHeaderH` 22), right-aligned, as mu-link's Names / Presets control sits ([../mu-link/design-ui.md](../mu-link/design-ui.md) section 3): one `SegmentControl`, Bar style, `ActiveStyle::General`, three items **"1-8"**, **"9-16"**, **"Buses"**. Opening the mixer shows the page of the sidebar (1-8 or 9-16); afterwards the two are independent (the mixer's page does not move the sidebar's, so the player can set a bus while the main area stays on a layer). Page Up / Page Down switch between the pages "1-8" and "9-16" (Buses is reached by click). The Mixer is locked in Performance mode (12.7), so no performance state is involved.

### 5.2 Channel pages ("1-8", "9-16"): identical geometry to today

Eight channel strips, a divider, the three return strips, a divider, Master plus insert panel: `kMixerChanW` 73, `kMixerNumChans` 11, no new number. Strip names show the layer name; the name pill keeps its layer-colour outline. An empty slot's strip is drawn inactive (`setActive(false)`, as mu-link's empty client) and stays operable (its fader and Output are saved with the slot). Sidechain source list on a channel: `-`, then the other layers by `"<slot> <name>"` (all 16, not only the page; self excluded; empty slots omitted), then the DAW input in plugin builds. The strip's reserved-section alignment rule (family) is unchanged.

### 5.3 Assigning a channel to a bus (the control)

The strip's existing **Output selector** (`outBusBox`, `kOutBusH` 18, the LCD dropdown at the strip foot, channel strips only) is the control. It is extended, not duplicated: items **Main**, **Bus 1** to **Bus 4**, a separator, then **Out 1** to **Out 8** (plugin builds only; the standalone build lists only Main and the buses). Default Main. The label is "Output" in its tooltip and status text: `Output` / `Bus 2` ("Main", "Bus n", "Out n"). A channel routed to a bus shows the bus name lit in the LCD; Main shows the dimmer Main text, so a quick scan of a page shows what is grouped. A return strip has no selector (returns go to Main). Sends to the FX returns are unaffected by the assignment (what the channel sends to the returns is independent of its output; the signal path is the dsp-controller's and the architecture-steward's to confirm, the UI only shows the choices). **No bus-to-bus routing in version 1**: a bus's Output lists Main and the Out items only.

### 5.4 The Buses page

Four **Bus strips**, a divider, the three return strips, a divider, Master plus insert panel. A new strip type `MixerChannel::Type::Bus` (not a local copy: it shares the name pill, sidechain section, fader group, VU, dB readout, mute / solo and Output selector with the other types).

**Width.** Bus strip = `kMixerBusW`, new token in `MuLookAndFeel`, derived like `kMixerChanW`: (`kMixerOverlayW` - `kMixerLabelPanelW` - 2 x `kMixerDivW` - `kMixerMasterTotalW` - returns group (3 x `kMixerChanW` + 2 x `kMixerChanGap`) - 3 x `kMixerChanGap`) / 4 = (1088 - 38 - 8 - 216 - 225 - 9) / 4 = **148**, with a `static_assert(kMixerBusW >= kMixerInsertPanelW)` (136, the width of an insert box). Arithmetic, Medium: 4 x 148 + 9 = 601; 38 + 601 + 4 + 225 + 4 + 216 = 1088. Nothing is spare and nothing is empty; at Large everything scales together.

**Anatomy, top to bottom** (heights Medium; sections reserve the same slots so faders, pans and mute rows align across the page):

| Part | Spec | Height |
|---|---|---|
| Name pill | "Bus 1" to "Bus 4", plain pill (no layer-colour outline: buses, like returns, have no layer). Fixed names in version 1 | 30 (`kNamePadding` x 2 + `kNameH`) |
| Feeds line | one LCD text line (`lcdFont(kLcdTextH)`, `kClientNameH` 18) listing the slots assigned to the bus, e.g. `1 3 4 9`, ellipsised; `none` in ghost lettering when empty; tooltip and status text list the full names. Replaces hunting across pages | 18 + 4 gap |
| Sidechain | **the channel strip's section, unchanged**: source selector, Amount, Attack, Release (`kSidechainH`). Same wording and ranges. Sources: `-` and the 16 layers by `"<slot> <name>"`, and the DAW input in plugin builds. **Buses are not sources in version 1** (a bus ducking another bus or a channel needs processing-order rules; deferred, question 6) | 112 |
| Insert | the **layer's insert**: one `DropdownSelect` of the shared algorithm list over the 2 x 2 `knobInsertPad` grid, plate "Insert", exactly the box of `MasterInsertPanel`. The box is **extracted** from `MasterInsertPanel` as a shared `mu_ui::InsertBox` (the panel then holds two of them, the bus strip one); do not copy the layout. 2 x 2 for every algorithm, EQ included, as the family rule | 150 |
| Pan | the strip pan knob (`kMixerStripKnobH` 46) | 46 |
| Fader + VU, dB readout, Output selector, Mute / Solo | as the channel strip (the Output selector lists Main and, in plugin builds, Out 1 to Out 8) | fader takes the rest, 54 below |

No sends on a bus (the sends slot of a channel strip, 186, is replaced by Insert + Pan, 196). **Budget, Medium:** strip area `kMixerStripH` 531: 30 + 22 + 112 + 150 + 46 + 54 = 414 fixed, fader **117** (the overlay cap is 200, the minimum 40). **Worst case** (774 overlay, strip area 491): fader **77**, at least 40. Both pass. Bus knob colours are the mixer's category colours (sidechain, inserts), never an app colour.

### 5.5 How many buses

**Four.** The brief gives no number. Four is the useful DJ and band grouping (drums, bass, synths, FX) and is what the Buses page can carry at full strip anatomy with an insert box each: eight would need 8 x 136 = 1088 on their own and leave no room for the returns and Master, which a bus page must keep visible. UI cost: one tab, one strip type, one extracted insert box, one derived width token. Engine cost is the architecture-steward's and dsp-controller's to size (four sums, four inserts, four sidechain followers). `mu_limits::kMaxBuses` = 4 in `MuLimits.h` (architecture-steward to confirm).

### 5.6 What the mixer is not

No EQ stack per channel (mu-link's client strips carry one; the family channel strip does not, and an EQ insert exists on every layer and every bus). No per-strip Dropouts lamp (one process). The Master, Master inserts and FX rack are the family's, unchanged.

## 6. Performance mode and the mu-link story (ruled where UI)

mu-link's job is to sum separate apps and be the clock. mu-tronic replaces the first with one process and uses the family transport for the second, so **what mu-link has that mu-tronic must have** is below; what it does not need is listed with the reason.

| mu-link has | mu-tronic | Ruling |
|---|---|---|
| Eight client strips with name, fader, VU, mute / solo | 16 layer channels (two pages), buses, returns, Master | Section 5. Covered |
| Master strip + two master inserts | The family Master and `MasterInsertPanel` | Covered, unchanged |
| **Scenes**: 8 buttons, each recalls a program (a layer or a full preset) per client in one press | **Gap.** The family equivalents are clips (one layer, 8 per layer) and Performance items (a whole preset). Neither is "launch clip n on every layer at once" | **Question 7.** Recommendation: a *scene* is a new surface-only column gesture (a scene pad = launch clip n on every layer of the shown page), with its screen twin a Shift-click on the clip pad number in the `ClipStrip`; no new panel. Decide before the Launchpad mapping for mu-tronic is frozen |
| Clock source Internal / MIDI In as a live choice on the bar, with the Clock lamp and Position | Clock source stays in Settings ("Source", standalone), the TransportBar already has the clock lamp and position readout where the family shows them | No change; the bar has no spare slot (the loop section holds the swap control) |
| Latency choice, Dropouts lamps, Choose devices | Choose devices only (Settings, standard). Latency and Dropouts exist for inter-process rings and do not apply | Not needed |
| Hardware output mix of separate apps and outboard gear | The DAW sidechain input and Out 1 to Out 8 in plugin builds; the device selector in standalone | mu-link stays the tool for separate apps and outboard ([design-future.md](../design-future.md)) |
| Tempo as the master, Tap, Nudge | The standard TransportBar (Tap and Nudge standalone only, family section 6.5) | Covered |
| Launchpad, Performance mode | Section 3.4 and the family rules (sections 12 to 14) | Covered |

Performance mode adds nothing else to the UI: the sidebar page control, the mixer and the chooser are all locked by the shield (they are not on the 12.7 Active list), the screen follows the surface, and there is no new exemption.

**Large display on a touch screen** is the family advice (12.4); every size here goes through `mu_ui::s()`.

## 7. Plugin identity (follow-up)

Name "μ-tronic" per [design-ui-family.md section 9](../design-ui-family.md#9-plugin-identity); plugin code, icon, and about card credits are the architecture-steward's and web-designer's. The icon needs the product colour of question 1; until then the icon is unpainted.

## 8. New tokens and components (all rulings above, for the coder)

| Item | Where | Value / note |
|---|---|---|
| `kMixerBusW` | `MuLookAndFeel` | 148, derived as in 5.4, `static_assert` against `kMixerInsertPanelW` |
| `kMixerBusFeedsH` | `MuLookAndFeel` | = `kClientNameH` (18), the feeds line |
| `mu_limits::kMaxBuses` | `MuLimits.h` | 4 (architecture-steward) |
| `mu_limits::kMaxLayers` | `MuLimits.h` | 16 for mu-tronic (the architecture-steward decides whether the constant is per product; the four existing products stay 8) |
| Page switch | `LayerSidebar` slot (`kAddBtnH`) | a `SegmentControl` Lcd, labels "1-8" / "9-16"; no new constant |
| `LayerHeaderBar::setTypeTag(text)` | `mu-core/UI` | opt-in, hidden by default |
| `mu_ui::InsertBox` | `mu-core/UI` | extracted from `MasterInsertPanel`; panel uses two |
| `MixerChannel::Type::Bus` | `mu-core/UI` | section 5.4 |
| Mixer page `SegmentControl` | `MixerOverlay` header | 3 items |
| `ControlAction` `PagePrev` / `PageNext` | `mu-core/Control` | device-independent; refused by the Perf shield only for the screen (the surface may use them) |
| Colour tokens, strengths | none new | everything else is an existing `MuTheme::Lighting` / `MuLookAndFeel` value |

## 9. Do not

- Do not add a ninth `layerPalette` colour, a second sidebar, a second window size or a taller main area for 16 slots.
- Do not shrink strips below the family knob size to fit 16 channels on one mixer page.
- Do not make a one-off "bus insert" layout; extract and share the insert box.
- Do not build the chooser as a menu or a dialog, or auto-fill an empty slot.
- Do not reflow, compact or renumber the sidebar when a slot is emptied.
- Do not add a performance key (Page Up / Page Down are off in Performance mode with every other key).
- Do not call a slot "a voice" in the UI.

## 10. Questions for Andy

1. **Product colour for mu-tronic.** The four app primaries (purple, green, blue, yellow) belong to the four products and the eight layer colours are barred as app primaries. Options: (a) unpainted steel with the family `globalAccent` for lettering, as mu-link, which reads as mu-Clid's purple on lit parts; (b) unpainted steel with a **pale ice-white** accent for lit parts (a new token `appIce`), distinct from every primary and layer colour, which fits "the unit that holds all the others"; (c) a fifth hue. **Recommendation: (b).**
2. **Main area painted in the layer's type colour** (section 2): yes means each slot looks like its own product. **Recommendation: yes.**
3. **Layer names editable and saved for every layer** in mu-tronic (4.6). **Recommendation: yes.**
4. **Which Launchpad buttons switch page** (3.4). **Recommendation:** top-row 5 and 6 (page 1 and page 2), replacing the provisional clip prev / next.
5. **An "all 16" overview** (3.5). **Recommendation: no** in version 1; add the other-page activity mark if wanted.
6. **Buses as sidechain sources** and bus-to-bus routing (5.3, 5.4). **Recommendation: not in version 1** (processing order); channels and the DAW input only.
7. **Scenes**: does mu-tronic need "launch clip n on all layers" to replace mu-link's Scenes (section 6)? **Recommendation: yes, as the surface column gesture plus a Shift-click on the screen**, decided before the surface mapping is frozen.
8. **Bus count and insert count**: four buses, one insert each (the layer's), fixed names "Bus 1" to "Bus 4" (5.4, 5.5). **Recommendation: accept.** Two inserts per bus would not fit the strip; renamable buses are a cheap later addition.
