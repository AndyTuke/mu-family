# mu-tronic - UI Design

Product-specific layout and behaviour. The look, tokens, controls and metal style are the family's: [../design-ui-family.md](../design-ui-family.md); none are repeated here. Structure, data model and naming of the product are in `design.md` (architecture-steward). Ruled 2026-10-15 (Andy's brief: an empty unit with the family's mod, effects and mixer; 16 layer slots filled from mu-Clid, mu-Tant, mu-Toni and the mu-On engines; buses in the mixer; a possible successor to mu-link as a performance tool). Revised 2026-10-15 with Andy's answers: **Standalone only, freeware, 3 buses with 2 inserts each, ice-white product colour, main area painted in the layer's type colour, layer names editable and saved, Launchpad important (top-row buttons 5 and 6 change page), two pages of 8 kept flexible, scenes, every mu-On engine is its own choice, preset folders shared with the products.** **Design only: nothing here is built.**

**Principle.** mu-tronic is not a new look. It is the family shell around the existing content panels, plus three additions: a page switch (slots shown 8 at a time), an empty-slot chooser, and a bus page in the mixer. Everything else (window 1170 x 870, TransportBar, StatusBar, `LayerSidebar`, `LayerHeaderBar`, `ClipStrip`, `ModulatorPanel`, `MixerOverlay`, Settings, Performance mode) is reused unchanged. No new window, panel height or sidebar width.

## 1. Words (ruled)

| Word | Means in mu-tronic | Notes |
|---|---|---|
| **Layer** | what sits in a slot: a Rhythm, Voice, Arp or Track | The family parent noun ([design-naming.md](../design-naming.md)). Sidebar, header tooltips and dialogs say "Layer" |
| **Slot** | one of the 16 positions, numbered 1 to 16, which may be empty | User-facing use is limited to "Slot 5" in the empty state, status lines and menus. design-naming reserves "Slot" for FX and insert slots in code; the architecture-steward picks the code name. **Andy's own word "voice" is not used for a slot**: "Voice" is mu-Tant's layer noun and "voice" inside an engine is a polyphonic voice |
| **Type** | which product a layer comes from. Shown as **Clid, Tant, Toni, On** (the mu- prefix is dropped where space is short) and in full ("mu-Clid") in the chooser | Type nouns: Rhythm (Clid), Voice (Tant), Arp (Toni), Track (On) |
| **Engine** | a mu-On layer's sound: Kick, Bass, Hat, Snare, Rumble. Each is its own choice in the chooser | A mu-On layer is type On with an engine. Default name = "<noun> <slot number>" for Clid, Tant and Toni ("Voice 5") and "<engine> <slot number>" for On ("Kick 5"); the family rule that a rename on engine change happens only while the name is still the default stands |
| **Bus** | a mixer group that channels are assigned to instead of Main | Three, "Bus 1" to "Bus 3", each with two inserts |
| **Main** | the master output (the existing label in the strip's Output selector) | unchanged |
| **Channel** | the mixer strip of a layer | unchanged (design-naming) |

There is no "Out 1 to Out 8" in mu-tronic: it is Standalone only (section 2). The code name `outputBus` still collides with "Bus" for the other products; follow-up for the architecture-steward, unchanged.

## 2. Shell (what is the family's, unchanged)

TransportBar (Play, BPM, Tap, Nudge, loop section, preset group, gear, Perf, Mixer), StatusBar, `LayerSidebar` 82 px, per-layer panel stack (header strip 32 / content 464 / modulator 318 in 1088 x 814). Standard Settings. The loop section carries the swap-timing control with the choices Master loop, Layer loop, Bar (as mu-Tant; a layer type with no loop of its own follows the bar).

**Standalone only (Andy, 2026-10-15).** mu-tronic ships as a Standalone application, no VST3 or CLAP, so everything that exists only because of a host does not appear: no DAW sidechain input (the sidechain source lists have no DAW entry), no Out 1 to Out 8 in any Output selector, no host-transport owner, no host automation, no multi-out routing. Tap and Nudge show (family 6.5, Standalone). The clock source stays in Settings (Internal / MIDI clock / Link where the build has it). Performance mode and the Launchpad stay and matter more (section 3.4). The parameters stay in the APVTS (saving), but nothing is presented as host-automatable.

**Freeware: no licensing UI (Andy, 2026-10-15).** The family shell already treats licensing as opt-in: `EditorShellBase` shows the Support prompt, the status-bar Support link, the activation overlay and the About card's supporter lines only when `processorRef.hasLicensing()` is true (`EditorShellBase.cpp`), and `hasLicensing()` is true only for a processor that called `initLicensing`. mu-tronic never calls it, exactly as mu-Clid MIDI and mu-link. Result: the right-hand end of the StatusBar shows nothing in the Support link's place (no reserved gap), no first-run dialog, no "Enter licence key" or "Want to give more?" anywhere, and the About card shows credits only. `License/` holds a `.gitkeep`. No mu-tronic-specific code is needed or allowed; if a licensing element ever appears in the freeware product, that is a shell bug to fix in `mu-core`, not a flag to set in mu-tronic.

**Product colour: ice-white (Andy, 2026-10-15; ruled).** Pale ice-white lit parts on **bare brushed steel**: the product is the unit that holds the others, so the chrome stays neutral and the lamps are cool white. New family token `ice`:

| Token | Value | File | Note |
|---|---|---|---|
| `MuTheme::AppPrimaries::ice` | `0xffDCEBF7` (cool near-white, a touch of blue) | `mu-core/UI/Components/MuTheme.h` | Distinct from the four primaries, from every `layerPalette` entry (none is near-white), and from `SurfaceColours::white` `0xffFFFFFF` (the pad's neutral arrows and the surface's "owned elsewhere" transport: `ice` is a tint, so the two never read as one on screen) |
| `MuLookAndFeel::appIce` | colour ID alongside `appPurple / appGreen / appBlue / appYellow` | `mu-core/UI/Components/MuLookAndFeel.h` | Read through `MuLookAndFeel::colour(MuLookAndFeel::appIce)` |

Where mu-tronic sets it: the last line of its editor constructor, like every product: `setMetalStyle(true, MuLookAndFeel::colour(MuLookAndFeel::appIce));`. That one call drives LCD lettering, selected segments, toggles and the global accent (`setAppAccent`).

**Why ice paints as almost nothing (intended).** `paintColour(accent)` desaturates and darkens the accent for the paint coat; for a pale colour the result is a light grey-blue wash, so chrome panels read as lightly tinted steel and the lit parts carry the identity. That is the brief ("on bare steel"). **No new strength**: use the existing `Lighting::paintSaturation / paintBrightness / paintCoat` as they are. The coder checks the first screenshot; if the lit lettering on the dark LCD is lower contrast than the other apps' (it should be higher), or the coat is invisible on the sidebar and the TransportBar, report back with the picture and I rule a per-accent strength then. Do not special-case it locally.

**The main area is painted in the selected layer's type colour (Andy confirmed, 2026-10-15).** The shell chrome (TransportBar, StatusBar, sidebar, mixer, settings, empty-slot chooser frame) is mu-tronic's own ice. The *main area* (content band, modulator panel, and the header strip's bar and buttons) takes the app primary of the layer's type; **the header's colour dot, name LCD and preset box keep the layer's identity colour** (`layerPalette`, as in every product, ruled 2026-10-20 from the T1b screenshot: it is how a layer is recognised against the sidebar). The app primary is: `appPurple` Clid, `appGreen` Tant, `appBlue` Toni, `appYellow` On (all five engines), applied by the same `setAppAccent` / `MuLookAndFeel::appAccent(component)` route and re-applied when the selection changes. Why: the whole reason to reuse each product's content panel is that it looks and feels like that product, and the paint tells the player which engine is under their hands without reading anything. Main-page knobs follow (family rule "main-page knobs are the app colour" reads "the colour of the page's type"). The layer *identity* colour (`layerPalette`, section 3.6) is unaffected and never an app primary, so it cannot blend into any of the paints. Mixer and Settings keep the category colours.

## 3. Paging the slots (ruled)

### 3.0 Page size and count are constants (Andy: "two pages of 8, keep it flexible")

| Constant | Value | Where | Limits of the flexibility |
|---|---|---|---|
| `mu_limits::kLayersPerPage` | 8 | `MuLimits.h` | **Cannot exceed 8**: the sidebar has eight 80 px items, the Launchpad eight rows, the mixer eight channel strips. Fewer than 8 works (spare items and rows stay blank) |
| `mu_limits::kNumPages` | 2 | `MuLimits.h` | Free. `kMaxLayers` = `kLayersPerPage` x `kNumPages` (16) |

Everything below is written for "page" and "slot", never the number 2 or 16: labels, the Page Up / Page Down handlers, the surface row mapping, the mixer pages and the colour rule are all generated from the two constants. The page switch control (3.2) is the one place where the count matters visually: it is ruled for 2 pages and degrades as set out there.

### 3.1 The model: a page is a view, the selected slot always belongs to the shown page

Two pages by default: **page 1 = slots 1 to 8, page 2 = slots 9 to 16**. The sidebar shows the eight slots of the shown page; the Launchpad's eight rows are the eight slots of the shown page; the mixer's channel strips are the eight of a page (section 5). One page state in the shell (`shownPage`), shared by sidebar, surface and mixer.

- **Switching page selects the slot at the same position on the new page** (slot 3 goes to slot 11), so there is always a selected slot and the main panel never shows "nothing". If that slot is empty the panel shows the empty-slot chooser (section 4).
- **Selecting a slot on the other page by any route** (surface, the mixer's strip click, a status link) flips the page to it. The page follows the selection; it is never an independent hidden state.
- The page is **session state**, not saved in a preset. A preset load or New shows page 1, slot 1 (the family "select layer 0" rule).

### 3.2 The control: the sidebar's bottom slot

mu-tronic has no "+ Layer" button (empty slots are the add affordance, section 4; `onAddLayer` is not wired, so the family rule hides the button). **Its place, the `kAddBtnH` 34 px slot at the sidebar bottom, holds the page switch**: one `SegmentControl`, Lcd draw style (metal; the family tab-bar look), one segment per page, labels generated from the constants: **"1-8"** and **"9-16"** at two pages, full item width (`kWidth` less the screwed item inset on each side = 60 px, 30 per segment), height `kAddBtnH`. No new constant, no reflow, the sidebar budget is the other products' (8 x `kItemH` 80 = 640, plus 34, plus edges, 685 of 774 worst case, 89 spare). The coder checks that "9-16" fits 30 px at the control's standard font; if it does not, report it back (fallback ruling: labels "1" and "2" under an engraved "Page" caption in the same slot), do not shrink the font locally. **Three or four pages** switch to the plain numbers "1" to "4" (20 px or 15 px segments); more than four needs a new control and a new ruling, not a squeeze.

- **Fallback taken (2026-10-19, T1 screenshot: "9-16" renders "9-..." in 30 px).** Sidebar segments are "1" and "2". Slot `kAddBtnH` is split: the top `kSubPanelScrewClear`-free strip of `kClientNameH` carries an engraved caption "Page" (`drawEngravedText`, `labelText`, centred, no screws behind it); the `SegmentControl` (Lcd) fills the rest of the slot at full 60 px width. If the caption plus segment do not fit 34 px at the standard fonts, report it; do not shrink fonts. The mixer's "1-8 | 9-16" Bar selector fits (screenshot) and keeps its labels.
- Status bar on hover: `Page` / `Layers 9 to 16`. Tooltip (sparingly): "Show slots 9 to 16 (Page Down)".
- A segment lights as the selected segment in the standard LCD selected look. **No activity marks in version 1** (section 3.5).
- In Performance mode the control is read-only (locked by the shield, not on the 12.7 Active list), and it **follows the surface**: the lit segment is the page of the surface's selected layer.

### 3.3 Keyboard

| Key | Action |
|---|---|
| **Page Up** | Show the previous page (stays on page 1) |
| **Page Down** | Show the next page (stays on the last page) |

No wrap (a wrap with two pages would be a toggle and hide which key goes where). Active whenever no text field has focus and no modal is open; with the Mixer overlay open the same keys switch **the mixer's** page among the channel pages (section 5). Not available in Performance mode (the shield swallows all keys, 12.4 stands: no performance keys). These are the first non-modifier navigation keys of the family; they are registered in the shell's key map (`ShellChrome`), not in the product. No new modifier combinations: Ctrl+P stays the only entry key to Performance mode.

### 3.4 Launchpad, clips, scenes, Performance mode (the Launchpad is important for mu-tronic: Andy)

- **Rows follow the shown page.** Right column button n = slot (`kLayersPerPage` x (shownPage - 1)) + n; grid row n likewise; a row whose slot is empty is off (the mu-Toni "rows with no layer stay off" rule). Clip numbering 1 to 8 per layer is unchanged; the `ClipStrip` in the header bar acts on the selected layer, as now. Row colours = `layerPalette` of the layer's colour index (3.6).
- **Right column** press = select that slot of the shown page (no page change).
- **Top row (ruled): mu-tronic has its own top-row map that differs from the family default in buttons 5 and 6 only.**

  | # | mu-tronic | Light |
  |---|---|---|
  | 1, 2 | `AppPrev` / `AppNext` | as the family table ([design-launchpad.md 3.6](../design-launchpad.md)): dark and inert while mu-tronic is the only app |
  | 3, 4 | `PerformancePrev` / `PerformanceNext` | as the family table |
  | **5** | **`PagePrev`** | white x `kArrowRestScale` when there is a previous page; **dim (`padDimScale` 0.20) when on the first page** |
  | **6** | **`PageNext`** | the same, dim on the last page |
  | 7 | `MuteSelectedLayer` | as the family table (provisional there too) |
  | 8 | `TransportToggle` | as the family table |

  **The clash and how it is resolved.** The family table has `ClipPrev` / `ClipNext` on 5 and 6, but marks them *provisional, awaiting Andy*: he has not adopted them, and the other four products have no pages, so nothing breaks by giving mu-tronic its own 5 and 6. The family 5 and 6 stay as they are (still Andy's to settle for the 8-layer products) and mu-tronic's map is a documented **per-product override of two buttons**, held in the mu-tronic mapping profile in `mu-control`, not a fork of the driver. mu-tronic loses clip prev / next on the surface; it does not need them (the grid pads are the clips, the whole point of rows = layers and columns = clips). **No Andy decision needed**; if he later adopts `ClipPrev` / `ClipNext` for the family it stays a mu-tronic exception for the same reason (pages exist only here). The docs [design-launchpad.md](../design-launchpad.md) should note the override next to its 5 to 7 table (that doc is not mine to edit in this pass; the main conversation passes the note on).
  
  **Page buttons do not wrap and do dim at the end** (unlike 3 and 4). A performance preset list is a loop on stage and a dark arrow there reads as broken; a page list is a space with two ends, and the dim end button is the on-pad page indicator (page 1: 5 dim, 6 bright; page 2: the reverse). With a page change the grid and right column relight at once to the new page's layers and the same-position slot becomes selected (3.1).
  
  The action pair is the device-independent `ControlAction` `PagePrev` / `PageNext` (`mu-core/Control`), so a MIDI mapping can use it too. It is accepted in Performance mode (the surface may page; the screen follows) and refused only for the screen shield.
- **Scenes (Andy: yes).** A *scene* is "launch clip n on every layer of the shown page" in one press, the replacement for mu-link's Scene buttons. It is a new device-independent `ControlAction` `SceneLaunch(n)`, n = 1 to 8, quantised exactly like a pad launch (one swap boundary, the layer rule of the loop section). Rows whose slot is empty or whose layer has no clip n are skipped; a layer with no clips at all is untouched. Three twins, one action:
  - **Screen:** Shift-click on the clip number n in the `ClipStrip` launches clip n on every layer of the shown page (status line: `Scene` / `Clip 3 on 6 layers`). Plain click is unchanged.
  - **Launchpad:** a column gesture on the grid. All eight top-row buttons and the right column are taken, so the gesture is **hold the logo button (CC 99) and press any pad in column n**. This needs the logo press to be reliable on hardware, which is still unverified (design-launchpad 3.5); that is the one thing to check before the map is frozen. Fallback if the logo does not report: hold top-row button 7 (mute selected layer) and press a pad in column n, with the mute action moving to release-without-pad (a tap mutes, a hold-and-pad launches the scene). This is a question for Andy only if the hardware check fails (question 3).
  - **MIDI:** `SceneLaunch(n)` is in the mappable vocabulary.
  
  No new panel, no new bar button. The scene result appears on the pads and in each `ClipStrip` as the usual queued / playing states.
- **Performance mode.** The screen is the normal screen, locked, and shows what the surface did: the sidebar page control (read-only) shows the page, the sidebar outline moves to the selected slot, the main area switches type and paint (section 2). The sidebar's "Layer n/N" indicator that replaces Add in the other products (12.3) is **not used**: the page switch occupies that slot and already shows the page, and the selected slot is outlined. The `StepIndicator` (performance items) is unchanged. A scene launched from the surface shows in the clip strips and the Launchpad only.

### 3.5 An "all slots" overview: not in version 1 (recommendation, question 1)

No overview screen and no 16-lamp strip. Reasons: the main area is full (no height to spare, 5), the sidebar shows a page by design, the mixer's Buses page and the master VU already show the sum, and the Launchpad is 8 rows by hardware. The cheap, reversible mitigation if testing shows lost sight of the other page: a small lit mark on the *other* page's segment when any of its layers is playing and not muted, using the existing `lampOn` state (no new token). The Launchpad already shows the other page by dim / bright buttons 5 and 6. Add the screen mark only if Andy asks.

### 3.6 Layer colour with 16 layers

`layerPalette` has 8 entries and stays 8: the Launchpad needs colours it can tell apart, and a ninth hue would collide with the app primaries (design-ui-family section 8). **Rule: a layer's default colour is the first palette entry not used by another layer on the same page** (page-local uniqueness), so within any view (sidebar, mixer page, Launchpad page) every layer has a distinct colour, and slots 1 and 9 may share Red. Colours travel with their layer on reorder, as now. The mixer and the surface show one page at a time, so no two visible layers ever share a colour. (The new `ice` accent is not a layer colour.)

## 4. The empty slot and the layer chooser (ruled)

### 4.1 Fresh instance and empty state

A fresh instance holds **no layers** (all slots empty), page 1, slot 1 selected; the first thing seen is the chooser. This is a deliberate exception to the family "fresh instance = 1 layer" rule (design-ui-family section 5, "Layer add / delete / reorder") because the brief is an empty unit; every other rule there stands.

### 4.2 Sidebar items

Filled slot: the family item (`createMiniVisual(index)` returns the **type's own mini-visual**, and for On the **engine's** own mini-visual, so the picture says what it is; the slot's layer colour lights the icon and outline as in section 8). The item also shows its **slot number**, engraved, top-left. Empty slot: no mini-visual; the slot number and a "+" in ghost lettering (`Lighting::lcdGhost`), an unlit lamp-base outline; it can be selected like any item. **Strength (ruled 2026-10-19):** the "+" is drawn in `labelText` at a new `MuTheme::Lighting::emptyPlus` = 0.55 (an add affordance must be findable; raw `lcdGhost` read as absent), the slot number stays top-left at the filled-item position and strength, clear of the corner screw (`kSubPanelScrewClear`). Empty slots stay in the list (the positions are fixed; the sidebar never reflows or compacts).

### 4.3 What the main area shows for an empty slot

- **Empty-registry state (ruled 2026-10-19).** The title is the name plate at its standard `kNamePlateH` (not smaller) and the message "No layer types are installed in this build." is drawn in the standard engraved label at `valueText` strength, centred, one size up from the card descriptor font; both must read at 1x. A faint message is not acceptable as the only content of the panel.
- **Header strip (32):** the family `LayerHeaderBar` with the name LCD reading `Slot 5` in ghost lettering and `Empty`; the **preset dropdown stays enabled and lists the layer presets of every type** (`.muRhythm`, `.muPattern`, `.muArp`, `.muTrack`, grouped by type in that order, mu-On presets under a single "mu-On" group); choosing one fills the slot with a layer of that preset's type (for `.muTrack` the engine the preset carries) and loads it. Save, Reset, Delete, the clip strip and the type tag are hidden (not disabled) in the empty state: there is nothing to act on.
- **Layer presets and filled slots (Andy: "of any type load into a slot of that type").** In a *filled* slot the preset dropdown lists only the layer presets of **that slot's type**: a Clid slot lists `.muRhythm`, a Tant slot `.muPattern`, a Toni slot `.muArp`, an On slot every `.muTrack` whatever its engine (loading one may change the track's engine, the family mu-On rule, so a Snare preset in a Kick slot is allowed and renames per the default-name rule). A preset of another type is never listed in a slot of a different type; to use it, pick it in an empty slot. This narrows the family rule "a layer preset lists in every layer's menu" for mu-tronic only, because here the slots are of different types (the family rule is about layers of one type). The four layer-preset folders are **the products' own folders** (shared, section 4.7).
- **Content band (464) and modulator band (318) together, one metal panel, no modulator panel** (an empty slot has nothing to modulate). In mu-tronic's own colour (ice on steel, section 2). Inside it, the **chooser**: a title `Add a layer to slot 5` (engraved, name-plate size), the two card bands below, then a one-line hint.

  **Grouped, not eight flat cards (decided).** Andy wants every mu-On engine selectable, which makes eight choices: mu-Clid, mu-Tant, mu-Toni, and mu-On's Kick, Bass, Hat, Snare, Rumble. Eight equal cards would be 127 px wide, too narrow for a descriptor and would hide that five of them are one product's engines. So the chooser is **grouped by product**: a product with one choice shows a single card; a product with several choices shows a titled group of cards. Today: band 1 holds three cards (mu-Clid, mu-Tant, mu-Toni), band 2 holds one group titled **mu-On** (`drawSections` plate, painted in mu-On's yellow) with five engine cards. A future product with several engines gets a group the same way, and the bands wrap by the same rule.

  - **Chooser cards still interim (reviewed 2026-10-20, five types live).** Today's cards are plain steel with a centred name; the card spec below (type colour, mini-visual, descriptor) is not yet built. It does not block T1b 2c (the chooser works and is unchanged by it), but it must land **before T1b is signed off and before the mu-Clid / mu-Tant types are added**, as its own sub-step: cards in the type's `appColourId` paint, the type's mini-visual at card size, the descriptor line. Until then the chooser must not be shown as the finished look.
- **Card** = a raised sub-panel painted in **that type's app primary** (the colour its main area will have, section 2), screws, name plate, the (engine's) mini-visual, a two-line engraved descriptor, and an `AddButton`-look text button **"Add"**. Whole card clicks too (the button is the visible affordance; Enter on the focused card adds). Name plates: "mu-Clid", "mu-Tant", "mu-Toni" in band 1; "Kick", "Bass", "Hat", "Snare", "Rumble" in the mu-On group (the group plate already says mu-On).
  - **Descriptors (wording ruled, taken from the products' own doc wording; the manual author may refine):** mu-Clid "Euclidean rhythms and samples", mu-Tant "Wavetable drone, gate patterns", mu-Toni "Generative arpeggiator", Kick "909 kick drum", Bass "Bass line", Hat "Hi-hat", Snare "Snare", Rumble "Rumble tail from a kick" (engine cards use one line).
  - **Geometry (budget first, no new constant).** Panel = `kChannelPanelW` x (`kLayerContentH` + `kLayerModulatorH`) = 1088 x 782 (worst case with both banners 774 less the header 32 = 742: the panel derives its height from `getHeight()`). Title band = `kNamePlateH` + 2 x `kSpaceM`; hint band = `kClientNameH` + 2 x `kSpaceM`; the two card bands share the rest equally, separated by `kSpaceM` (about 350 each at Medium, about 330 worst case, enough for plate, mini-visual, descriptor and button). Band 1 card width = (panel width - 2 x `kScrewedPanelInset` - 2 x `kSpaceM`) / 3 (about 340). Band 2: the group fills the panel width inside the same inset; engine card width = (group inner width - 4 x `kSpaceM`) / 5 (about 190). All are expressions of existing tokens through `mu_ui::s()`; at Large it fits when Medium does.
  - Hint (engraved): "Or load a layer preset from the menu above." Nothing else.
- **Choosing a card** creates a layer of that type (for an engine card, with that engine) with its defaults, default name (section 1) and the colour of section 3.6, and shows the type's normal panel. **Rumble** needs a Kick layer as its Source (its own rule: the Source dropdown lists the other layers by name, default the first Kick layer; in mu-tronic the list is all slots by `"<slot> <name>"`); if there is none yet the card still works, the layer is created with Source `-` and the status line says so (`Rumble` / `Source needed: add a Kick layer`). A Rumble layer has no step row or clips (it processes another layer's audio), as in mu-On.
- **Wording of status lines:** `Layer` / `Added Kick 5` (3 s, `kPerfStatusMs`); hover on a card: `Add` / `mu-On Kick in slot 5`.
- **Do not** make the chooser a popup menu or a dialog (a card is the product's identity and the first-run experience); offer "Add" in the header bar; auto-fill slot 1; list the five engines flat beside the three products.

### 4.4 A filled slot: how it shows its type

1. **Sidebar:** the type's (engine's) mini-visual (4.2).
2. **Header bar:** a **type tag**, a small raised name plate (`drawNamePlate`, `kNamePlateH`, width `namePlateWidth(text, h)`) with the engraved text `Clid`, `Tant`, `Toni` or `On`, between the colour dot and the name LCD (an On layer's engine is already shown by the Engine dropdown and the default name, so the tag stays `On`). Opt-in on the shared bar (`LayerHeaderBar::setTypeTag(text)`; empty = hidden), off in the four products. Costs about 40 px of the preset dropdown's flex width (about 600 at Medium; its 80 px floor is untouched).
3. **The paint** of the whole main area (section 2).

### 4.5 The content, per type: reuse, do not rebuild

The main area for a filled slot is the shared `LayerPanel` with the type's content component, exactly as in the product (`<Type>Content`: Clid's sample bar + Euclid circle + voice band; Tant's Root / Scale + oscillator, X-Mod, filter, effects + gate panel; Toni's source + voice + Appergater; On's engine box + Groove / steps), the shared `ModulatorPanel`, the shared header bar and clip strip. All are already ruled to fit the 464 / 318 stack ([design-ui-family.md section 5](../design-ui-family.md#per-layer-panel-stack-every-instrument-product)), so **no content panel is touched and no per-type height exists in mu-tronic**. The sidebar switches the panel when the selected slot's type differs from the shown one; the swap is a normal selection change (nothing animates). mu-On's content shows one layer's engine at a time here (the sidebar is the per-slot list), so its "boxes flow" layout for all engines at once applies only where the content supports it; the mu-On content in mu-tronic is the single-layer view the shared `LayerPanel` already gives it.

### 4.5a How a type's content sits in a slot (ruled 2026-10-20, T1b)

**The slot panel.** `LayerPanel` owns the chrome and nothing else is added: header strip (name LCD, type tag, preset box, clip strip), the 464 content band, the shared `ModulatorPanel`. A type's content is **one `juce::Component` that fills the content band exactly and paints its own metal panels** (`drawAccentPanel`, `drawSections`, `drawPanelScrews`), as `ArpContent` and `TrackContent` do today. No extra frame, title bar or type-name label inside the band (the type tag and the sections' own plates already say what it is: `Toni` / `On` in the tag, "KICK ENGINE" on the plate). The panel's paint colour is the type's app primary (section 2), read through `MuLookAndFeel::appAccent(component)`; the content never picks a colour.

**The registry carries the look** (what `LayerTypeInfo` needs for the UI; `createContent` is the factory the design.md field list names):

| Field | Value | Note |
|---|---|---|
| `createContent` | builds the type's content component for the shown slot | message thread; the editor keeps **one content instance per type** and rebinds it (`setLayer(slot)`) when the selection moves to another slot of the same type; it swaps the component (`LayerPanel::setContent`) only when the type changes. A content exposes `setLayer(slot)` and `releaseModulationArcs()` (call before a slot is removed or moved); one small mu-core base class for these two, not a per-type convention (architecture-steward names it) |
| `appColourId` | a `MuLookAndFeel` colour ID | Clid `appPurple`, Tant `appGreen`, Arp `appBlue`, all five Track types `appYellow` |
| `typeTag` | `Clid`, `Tant`, `Toni`, `On` | the header tag text (4.4). Arp = `Toni`; Kick, Bass, Hat, Snare, Rumble = `On` |
| `createMiniVisual` | the type's (engine's) sidebar picture | Arp: the Arp mini-visual (mu-Toni's disc) showing the initial `A`, not the slot number (the item already engraves the number); Track types: mu-On's own `TrackMiniVisual`, a disc in the slot colour with the engine's initial (`K`, `B`, `H`, `S`, `R`). **Corrected 2026-10-20:** this row said "step row", but mu-On's own sidebar picture is the initial disc and 4.2 says the type's own picture; one picture per type across the family wins. A step-row picture would be a family-wide change to mu-On's sidebar and is not scheduled |

**Track content in a slot (Kick first; Bass, Hat, Snare, Rumble follow by the same rules).** It is mu-On's `TrackContent` with the engine locked. Same two panels, same order, same plates:

1. **Engine panel.** Plate "KICK ENGINE" (engine name, upper case, as today). **The Engine selector and its caption are hidden when the type is locked, and the knob grid moves left into the freed width** (no reserved gap; `leftW` = 0 and the gap before the grid goes too). The layer is a Kick; offering "Snare" in a Kick slot would be a second way to change type, which the chooser and the header menu already own. The Source selector (Rumble only) stays and takes the first position. The box keeps its `ParamKnobGrid` size, and the track-colour outline.
2. **Groove | Steps panel** (the flex panel). Groove box fixed at `kGrooveBoxW`, steps box takes the rest, plates "GROOVE" and "STEPS". **Groove holds Swing and Accent, the same two `KnobWithLabel`s as today, centred as a pair, top-aligned, `kKnobSize2W` x `kKnobSize2H`, bound to the layer's own rows `t{N}_swing` and `t{N}_accent`** (the per-layer rows of design.md point 8a; same ranges, defaults and labels as `seq_swing` / `seq_accent`). They stay in the Groove box, not the engine panel, so a Kick, Bass, Hat and Snare share one muscle-memory position; Rumble keeps its envelope there instead, as today. They are not modulation targets.
3. **One-row step component** (split out of `StepRows`, named `StepRow` view / `StepRowView` by the architecture-steward; the look is ruled here). It is `StepRows::paintRow` for a single row, unchanged in drawing: 16 lens cells (`lampOn` / `lampBeat` / `lampOff`, `lampPlayhead`, `lampHot` accent centre, `drawRecessedScreen`), beat numbers 1, 5, 9, 13 engraved on the cells (always drawn, it is the only row), the moving playhead column, left-click on/off, right-click accent (on an off step: turns it on with accent). Differences from `StepRows`, all subtractions: **no name gutter** (`kLabelW` is not used: the header and the sidebar already name the layer, and the cells take the whole box width inside `kSubPanelScrewClear` and `kSpaceS`), **no selected-row outline** (there is nothing to select), no row click callback. Cell colour = the layer's `layerPalette` colour. **Row height is exactly what mu-On gives its rows: the steps box inner height divided by `kLayersPerPage`**, top-aligned; the rest of the box stays blank metal, as in mu-On with few tracks, and no filler is added. **Reviewed on screen 2026-10-20 (first Track build): the ruling holds for now.** The steps box is about 750 x 320 with one 36 px row; the Groove box beside it is just as sparse. That is a consequence of the fixed 464 band, not of this ruling, and the fresh Kick showing an empty row makes it look emptier than it will be: wait for the default groove pattern (architecture-steward) before judging; a lit four-on-the-floor row reads as intended. Do not add filler or a mu-Tronic-only row height. If Andy still finds the blank area poor after that, the fix is one ruling for both products (taller cells), not a mu-Tronic exception. The cell geometry is the shared look; the component lives in `mu-on` UI and mu-On's `StepRows` calls the same painting function per row, so there is one drawing of a step row.

**Wording.** Plates: `<ENGINE> ENGINE`, `GROOVE`, `STEPS` (Rumble: `ENVELOPE`). Knob labels: `Swing`, `Accent`. Dropdown captions (Rumble): `Source`. Tag: `On`. Status on hover of a step: `Step` / `Step 5 on, accent`. Nothing new needs a tooltip.

**Do not.** Show the Engine selector on a locked layer; add a frame or title around a content band; draw a second step-row look; leave a gutter for the name; give a type its own paint colour in the content; hard-code a size (everything above is `kGrooveBoxW`, `kKnobSize2W/H`, `kSubPanelScrewClear`, `kSpaceS`, `kSectionPlateH`, `kChannelInset`, `kScrewedPanelInset`, `ParamKnobGrid` constants, through `mu_ui::s()`).

### 4.6 Delete, reset, reorder, rename (the family standard with these exceptions)

| Family rule | mu-tronic |
|---|---|
| Delete shifts the layers above down | **No shift.** Delete empties the slot and every other slot keeps its place (positions are the Launchpad rows and the player's muscle memory). The layer's mixer strip, clips and modulators go with it; its sound is cut at the next block, as now |
| Delete disabled at one layer | **Never disabled** (an instance may be empty). Confirm wording unchanged: title "Delete Layer", `Delete "<name>"?` / `This cannot be undone.`, red "Delete" |
| Add button, disabled at the maximum | **None.** The empty slot is the add affordance |
| Reorder by drag in the sidebar | Drag **within the shown page** swaps two slots (empty included: dragging onto an empty slot moves the layer there). Moving a layer to the **other page** is a right-click menu on a sidebar item: `Move to slot...` with a submenu of the slot numbers (filled slots marked `swap`), because a drag cannot cross a page. The mixer strip, clips, modulators and colour travel with the layer. Right-click is an extra here, not the only route: a layer on the other page is also reachable by switching page and dragging a neighbour |
| Layer name editable only where saved | **Editable and saved for every layer (Andy, 2026-10-15).** The name LCD in the header bar is click-to-edit in all four types (`setNameEditable(true)`), the name is in the layer's saved state and in a layer preset, and the default-name rule (section 1) applies until the user types. Sixteen mixed layers, bus feed lists and sidechain menus need real names. The four other products keep their rule (derived names not editable) until they save names |
| Fresh instance = 1 layer | Empty (4.1) |

### 4.7 Preset folders are shared (Andy, 2026-10-15)

mu-tronic reads and writes the **same layer-preset folders as the products**: `.muRhythm` (mu-Clid), `.muPattern` (mu-Tant), `.muArp` (mu-Toni), `.muTrack` (mu-On), factory and user. A layer saved in mu-tronic appears in the product and the other way round; no copy, no mu-tronic-only folder. The preset browser and the header dropdown therefore show the same names, and the Save dialog's wording is the family's. The full-instance preset (`.muTronic`) is mu-tronic's own file type in its own folder (design.md). Folder paths and how a layer preset's type is recognised are the architecture-steward's; the UI only requires that the dropdown rules of 4.3 hold.

## 5. Mixer: pages and buses (ruled)

The mixer is the family `MixerOverlay`: strips, three FX returns, the FX rack (`kMixerFXAreaH` 261), the Master strip and the master insert panel. Sixteen channels at the family strip width do not fit (11 strips at 73 px already fill 1088), so the mixer **pages exactly as the sidebar does, plus one page for buses.**

### 5.1 The page selector

In the mixer's header strip (`kMixerHeaderH` 22), right-aligned, as mu-link's Names / Presets control sits ([../mu-link/design-ui.md](../mu-link/design-ui.md) section 3): one `SegmentControl`, Bar style, `ActiveStyle::General`, one item per page ("1-8", "9-16" at two pages) plus **"Buses"**. Opening the mixer shows the page of the sidebar; afterwards the two are independent (the mixer's page does not move the sidebar's, so the player can set a bus while the main area stays on a layer). Page Up / Page Down switch among the channel pages (Buses is reached by click). The Mixer is locked in Performance mode (12.7), so no performance state is involved.

### 5.2 Channel pages: identical geometry to today

Eight channel strips, a divider, the three return strips, a divider, Master plus insert panel: `kMixerChanW` 73, `kMixerNumChans` 11, no new number. Strip names show the layer name; the name pill keeps its layer-colour outline. An empty slot's strip is drawn inactive (`setActive(false)`, as mu-link's empty client) and stays operable (its fader and Output are saved with the slot). Sidechain source list on a channel: `-`, then the other layers by `"<slot> <name>"` (all slots, not only the page; self excluded; empty slots omitted). **No DAW input entry** (Standalone only). The strip's reserved-section alignment rule (family) is unchanged.

### 5.3 Assigning a channel to a bus (the control)

The strip's existing **Output selector** (`outBusBox`, `kOutBusH` 18, the LCD dropdown at the strip foot, channel strips only) is the control. It is extended, not duplicated: items **Main**, **Bus 1**, **Bus 2**, **Bus 3** (no Out items: Standalone only). Default Main. The label is **"Out"** in its tooltip and status text (`Out` / `Bus 2`; ruled 2026-10-20: "Out" is the family word for a strip's destination, as design.md section 1.2 renames the direct out, so "Output" is not used). A channel routed to a bus shows the bus name lit in the LCD; Main shows the dimmer Main text, so a quick scan of a page shows what is grouped. A return strip has no selector (returns go to Main). Sends to the FX returns are unaffected by the assignment (what the channel sends to the returns is independent of its output; the signal path is the dsp-controller's and the architecture-steward's to confirm, the UI only shows the choices). **No bus-to-bus routing in version 1.**

### 5.4 The Buses page (re-ruled: three buses, two inserts each)

Three **bus units** and the Master. A bus unit is built exactly like the Master's unit: **a channel strip plus the existing `MasterInsertPanel` beside it** (Master strip 80 + insert panel 136 = 216 today), so a bus has **two inserts, "Insert 1" and "Insert 2"**, in the family's own two-box insert column, with no new insert widget and no extraction from `MasterInsertPanel` (the earlier plan of one extracted `InsertBox` per bus is withdrawn). The insert panel is reused as is; each bus gets its own binding, a bus counterpart of `MasterInsertBinding` (architecture-steward: parameter ids per bus). A new strip type `MixerChannel::Type::Bus` shares the name pill, sidechain section, fader group, VU, dB readout and mute / solo with the other types.

**The returns are not shown on this page.** The bus strip carries its own three send knobs (table below), and the return strips (225 px) take no part in a bus unit, and without them the width works out exactly. Returns stay on both channel pages; the FX rack below is unchanged on every page.

**Width (Medium, from existing tokens).** One divider between each pair of units and one before the Master (3 dividers of `kMixerDivW` 4):

`kMixerBusStripW = (kMixerOverlayW - kMixerLabelPanelW - kMixerMasterTotalW - kMixerNumBuses x kMixerDivW) / kMixerNumBuses - kMixerInsertPanelW`
`= (1088 - 38 - 216 - 3 x 4) / 3 - 136 = 822 / 3 - 136 = 274 - 136 = 138`

Check: 38 + 3 x (138 + 136) + 3 x 4 + 216 = 38 + 822 + 12 + 216 = **1088**. Nothing is spare. Master and its insert panel stay at the same x as on the channel pages. At Large everything scales together.

**Vertical budget.** Identical to the channel strip: the strip is exactly the channel strip's layout (sends and pan as a channel, Output selector slot hidden), and the insert panel is the Master's, which already fits the strip area (`kMixerStripH` 531 Medium, 491 worst case). So the budget needs no recomputation: the fader keeps the height it has on the Master strip (and on a channel with its sends). The second insert costs the height the Master already pays; the missing fourth bus gave the width.

| Part (top to bottom) | Spec |
|---|---|
| Name pill | "Bus 1" to "Bus 3", plain pill (no layer-colour outline: buses, like returns, have no layer). Fixed names in version 1 |
| Sidechain | **the channel strip's section, unchanged** (`kSidechainH` 112): source selector across the strip, Amount, Attack, Release centred. Same wording and ranges. Sources (ruled 2026-10-20, the engine's order rule, design.md section 5): `-`, every layer by `"<slot> <name>"`, and the buses **before** this one (Bus 2 lists Bus 1; Bus 3 lists Bus 1 and 2); never itself or a later bus. Channels never list a bus; the three returns list every bus. One rule for engine and UI: `mu_mix::isLegalSidechainSource` via `MixerChannel::setSidechainSources` |
| **Sends** (3 x `kSendKnobH`) | The channel strip's three FX send knobs, unchanged (Effect / Delay / Reverb, the Effect label tracking the algorithm name), because a bus has the same three post-bus-fader sends as a channel (design.md section 5; ruled 2026-10-20). Pan is in the bottom row as on a channel. **The Feeds panel is withdrawn**: a channel's bus shows on its own Out selector (5.3), and a feeds list is an unscheduled idea |
| Pan | the strip pan knob (`kMixerStripKnobH` 46), in the bottom row of the sends slot |
| Fader + VU, dB readout, Mute / Solo | as the channel strip; the fader group keeps its natural width, centred in the 138 px strip (selectors, name pill, dB readout and Mute / Solo span the strip) |
| Output selector slot | **hidden, slot reserved** (a bus can only go to Main in version 1; a one-item dropdown is noise). The slot stays so faders align; with bus-to-bus routing later it returns |
| Insert column (136) | `MasterInsertPanel` bound by `BusInsertBinding` (ruled 2026-10-20: `InsertSubsection` is 216 px wide and cannot sit in the 136 px column; `setNumSlots(n)` hides boxes beyond the bus's insert count). At the mu-Tronic count (2): "Insert 1" over "Insert 2", the shared algorithm list, 2 x 2 knobs each (EQ included), exactly the Master's |

Bus knob colours are the mixer's category colours (sidechain, inserts), never an app colour.

**Count is constant-driven.** `mu_limits::kMaxBuses` = 3 (`MuLimits.h`, architecture-steward) and `MuLookAndFeel::kMixerNumBuses` = the same value (a `static_assert` ties them where both are visible). The strip width above is derived from it. **Three is the limit of this page**: `static_assert(kMixerBusStripW >= kMixerChanW)` holds at 3 (138) and fails at 4 (68), by design: a fourth bus needs a layout ruling (a narrower insert column or a second bus sub-page), not a squeezed strip. Fewer buses than the constant leave blank metal at the right of the units, not wider strips.

### 5.5 Why three buses, two inserts

Andy's call (2026-10-15, "3 buses for now with 2 inserts"). It also fits the page exactly with the Master's own insert panel reused, which is the better answer than the earlier four-bus, one-insert plan. UI cost: one tab, one strip type, one derived width token; nothing new in the insert controls. Engine cost is the architecture-steward's and dsp-controller's to size (three sums, six inserts, three sidechain followers). A later "more buses" request is a layout question (5.4), not a constant to raise.

### 5.6 What the mixer is not

No EQ stack per channel (mu-link's client strips carry one; the family channel strip does not, and an EQ insert exists on every layer and every bus). No per-strip Dropouts lamp (one process). The Master, Master inserts and FX rack are the family's, unchanged.

## 6. Performance mode and the mu-link story (ruled where UI)

mu-link's job is to sum separate apps and be the clock. mu-tronic replaces the first with one process and uses the family transport for the second, so **what mu-link has that mu-tronic must have** is below; what it does not need is listed with the reason.

| mu-link has | mu-tronic | Ruling |
|---|---|---|
| Eight client strips with name, fader, VU, mute / solo | 16 layer channels (two pages), buses, returns, Master | Section 5. Covered |
| Master strip + two master inserts | The family Master and `MasterInsertPanel` | Covered, unchanged |
| **Scenes**: 8 buttons, each recalls a program per client in one press | **Scenes (Andy: yes)**: `SceneLaunch(n)` = launch clip n on every layer of the shown page; Shift-click on a clip number on screen, logo-hold + column pad on the Launchpad | Section 3.4. Covered |
| Clock source Internal / MIDI In as a live choice on the bar, with the Clock lamp and Position | Clock source stays in Settings, the TransportBar already has the clock lamp and position readout where the family shows them | No change; the bar has no spare slot (the loop section holds the swap control) |
| Latency choice, Dropouts lamps, Choose devices | Choose devices only (Settings, standard). Latency and Dropouts exist for inter-process rings and do not apply | Not needed |
| Hardware output mix of separate apps and outboard gear | The audio device selector in Settings (Standalone) | mu-link stays the tool for separate apps and outboard ([design-future.md](../design-future.md)) |
| Tempo as the master, Tap, Nudge | The standard TransportBar (Tap and Nudge shown, family section 6.5) | Covered |
| Launchpad, Performance mode | Section 3.4 and the family rules (sections 12 to 14) | Covered |

Performance mode adds nothing else to the UI: the sidebar page control, the mixer and the chooser are all locked by the shield (they are not on the 12.7 Active list), the screen follows the surface, and there is no new exemption.

**Large display on a touch screen** is the family advice (12.4); every size here goes through `mu_ui::s()`.

## 7. Identity (follow-up)

Name "μ-tronic" per [design-ui-family.md section 9](../design-ui-family.md#9-plugin-identity), applied to the Standalone's window title, executable and About card; there is no plugin code, VST3 class name or CLAP id (so the non-ASCII class-name problem does not apply). The icon uses the ice-white product colour on the family's dark round emblem ground; its design and the About credits are the web-designer's and architecture-steward's.

## 8. New tokens and components (all rulings above, for the coder)

| Item | Where | Value / note |
|---|---|---|
| `AppPrimaries::ice` | `MuTheme` | `0xffDCEBF7` |
| `appIce` | `MuLookAndFeel` colour IDs | read via `colour(appIce)`; set by `setMetalStyle(true, ...)` in mu-tronic's editor constructor |
| `kMixerBusStripW` | `MuLookAndFeel` | 138, derived as in 5.4; `static_assert(>= kMixerChanW)` |
| `kMixerNumBuses` | `MuLookAndFeel` | 3, equals `mu_limits::kMaxBuses` |
| `mu_limits::kMaxBuses` | `MuLimits.h` | 3 (architecture-steward) |
| `mu_limits::kLayersPerPage`, `kNumPages`, `kMaxLayers` | `MuLimits.h` | 8, 2, 16 for mu-tronic (architecture-steward decides how the constant is per product; the four products stay 8); `kLayersPerPage` <= 8 |
| Page switch | `LayerSidebar` slot (`kAddBtnH`) | a `SegmentControl` Lcd, one segment per page; no new constant |
| `LayerHeaderBar::setTypeTag(text)` | `mu-core/UI` | opt-in, hidden by default |
| `LayerTypeInfo::createContent`, `appColourId`, `typeTag` | `mu-core/Sequencer/LayerTypeInfo.h` | 4.5a; no new token (existing app colour IDs) |
| One-row step component | `mu-on` UI, drawn by the same function as `StepRows` | 4.5a; no gutter, no outline, row height = box / `kLayersPerPage` |
| Layer chooser | mu-tronic product UI, built from `drawSections`, raised sub-panels, `AddButton` look | grouped bands (4.3); no new token |
| `MixerChannel::Type::Bus` | `mu-core/UI` | section 5.4; reuses `MasterInsertPanel` unchanged beside it |
| Bus insert binding | `mu-core/UI` | bus counterpart of `MasterInsertBinding` (architecture-steward) |
| Mixer page `SegmentControl` | `MixerOverlay` header | pages + Buses |
| `ControlAction` `PagePrev` / `PageNext`, `SceneLaunch(n)` | `mu-core/Control` | device-independent |
| mu-tronic top-row map (5 = `PagePrev`, 6 = `PageNext`) | `mu-control` mapping profile | override of two family buttons |
| Colour tokens, strengths | none new besides `ice` | everything else is an existing `MuTheme::Lighting` / `MuLookAndFeel` value |
| Withdrawn | | `mu_ui::InsertBox` extraction, `kMixerBusFeedsH`, Feeds panel, Out 1 to 8 items |

## 9. Do not

- Do not add a ninth `layerPalette` colour, a second sidebar, a second window size or a taller main area for 16 slots.
- Do not shrink strips below the family knob size to fit 16 channels on one mixer page, or squeeze a fourth bus onto the Buses page.
- Do not write a one-off bus insert layout; use `MasterInsertPanel`.
- Do not make the chooser a menu or a dialog, auto-fill an empty slot, or list the mu-On engines flat beside the three products.
- Do not reflow, compact or renumber the sidebar when a slot is emptied.
- Do not add a performance key (Page Up / Page Down are off in Performance mode with every other key).
- Do not call a slot "a voice" in the UI.
- Do not show a Support link, a licence prompt or an "Out n" item in mu-tronic, and do not set a "freeware" flag to hide them: the shell already hides licensing for a processor without `initLicensing`.
- Do not hard-code 8 or 2 for the page size or count.

## 10. Questions for Andy

Answered 2026-10-15 (kept for the record): product colour = ice-white; main area painted in the layer's type colour = yes; layer names editable and saved = yes; Launchpad page buttons = top-row 5 and 6; scenes = yes; bus count and inserts = three buses, two inserts; plugin or Standalone = Standalone only; licensing = free; pages = two of eight, flexible; engines = every mu-On engine is a chooser choice; preset folders = shared.

Open (each has a recommendation that stands if Andy does not reply):

1. **An "all slots" overview** (3.5). Recommendation: no in version 1; the Launchpad's dim / bright page buttons already show the other page; add the screen mark only if wanted.
2. **Buses as sidechain sources** and bus-to-bus routing (5.3, 5.4). Recommendation: not in version 1 (processing order); channels only.
3. **Scene gesture on the Launchpad** (3.4): hold the logo button (CC 99) and press a column pad. Only becomes a question if the logo press cannot be read on the hardware, in which case the fallback is hold-button-7. Needs the logo hardware check first.
4. **Four or more buses later** (5.4): needs a layout choice then (narrower insert column or a second bus sub-page). Not needed now.
