# mu-link UI

Layout of the mu-link window on the family design system. Colours, sizes, controls and the metal style are in [../design-ui-family.md](../design-ui-family.md) and are not repeated here; this page is arrangement only. Ruled 2026-10-11 (backlog "mu-link adopts the mu-core UI"). Replaces the bespoke 1200 x 880 window (`MuLinkComponent`), which drew its own title, buttons and sliders.

mu-link has no layers, so it has no sidebar and no per-layer panel. It is one window of the standard chrome around a mixer: the mixer is the app.

## 1. Window and budget

| Item | Value |
|---|---|
| Content size | `kWindowWidth` x `kWindowHeight` = 1170 x 870 (the family standard, through `mu_ui::s()`); non-resizable; native title bar as today ("mu-link", "mu-link - Debug") |
| TransportBar | top, `kTransportBarH` 36, full width |
| StatusBar | bottom, `kStatusBarH` 20, full width |
| Main area | 1170 x 814 (`kWindowWidth` x `kChannelPanelH`), no sidebar so the full width. mu-link has no licence, so no support link and no banner: the 774 worst case of the family doc does not apply |
| Panels | two metal panels edge to edge, each drawn `reduced(2)`, screws on (`setMetalStyle(true)` then `setScrews(true)`): Mixer panel above, Scenes panel below |
| Display size | follows the family Small / Medium / Large scale (`mu_ui::scale`); set in Settings (section 5) |

Vertical budget of the main area: Mixer panel `814 - kScenesPanelH` = 662, Scenes panel `kScenesPanelH` = 152.

App colour: **owner decision pending** (mu-link is not one of the four app primaries). Until Andy picks, call `setMetalStyle(true)` with the colour left transparent: the panels stay unpainted brushed steel and LCD lettering takes `globalAccent`. No new token is added until he chooses.

## 2. TransportBar (shared `TransportBar`, no loop-section pane)

Left to right, standard widths from the shared bar: `[logo "μ-link", kLogoW 88] [play 36] [BPM] [clock lamp slot 44] [position 56] [clock source, in the loop-section slot, 150] [preset dropdown 240] [New] [Save] [gear 28]`.

| Element | Rule |
|---|---|
| Logo | `setLogoText("μ-link")`; click opens About |
| Play | the shared play button. Under MIDI In it is disabled and mirrors the master's run state (as today) |
| BPM | the shared `NudgeInput` in decimals mode: one decimal always shown (`120.0`), range 20.0 to 300.0, arrows step 1.0, Shift+click 0.1, entry per [family section 6.4](../design-ui-family.md#64-standard-value-formatters). Under MIDI In the field is disabled and shows the clock estimate rounded to 0.1. This replaces the `juce::Slider` and closes the "move mu-link to NudgeInput" follow-up |
| Clock lamp | the shared `StatusLamp` "Clock" in the 44 px slot, **visible only while Clock source = MIDI In** (hidden under Internal). States in section 4 |
| Position | the shared bar.beat.sixteenth readout (new in mu-link; free from the shared bar) |
| Clock source | one `SegmentControl`, Bar style, `ActiveStyle::General`, items **Internal** and **MIDI In** (the same two words as the family Settings "Source" dropdown), 150 px wide, height of the BPM field, placed with `setLoopSection(&clockSource, 150)`. It is a live performance choice, so it stays on the bar instead of moving to Settings. Status bar on hover: `Clock source` / `Internal (mu-link is the master)` or `MIDI In (slaved to external clock)` |
| Preset dropdown, New, Save | the shared ones. They replace the old Save / Presets buttons and the preset-name label. Browsing the folder is the shared dropdown's own "Browse" entry (the `PresetBrowser` overlay). **New** restores the defaults (120.0 BPM, all gains unity, EQ flat, both inserts None, scenes cleared) |
| Mixer button | hidden (`setShowMixerToggle(false)`): the mixer is the main area |
| Gear | opens the Settings overlay (section 5); it replaces the old "Options" button |
| Tap, Nudge | not present in mu-link (family section 6.5 stands): hide them with a shared bar option, not a local copy |

## 3. Mixer panel (1170 x 662)

One metal panel. Contents, top to bottom, using the strip anatomy of the family mixer (`MixerChannel`):

1. **Header strip** (`kMixerHeaderH` 22, inside the panel inset): left, the name plate "Clients"; right, a `SegmentControl` Bar **Names | Presets** (replaces the "Show: Names" toggle), which chooses what each strip's name LCD shows. Tooltip "Strip labels: app name or loaded preset" (icon-less control with no room for a caption, so the tooltip rule applies).
2. **Eight client strips** side by side, then a divider (`kMixerDivW` 4), then the **Master column**.

**Client strip** (width = `(panelW - 2 x kScrewedPanelInset - kMixerMasterTotalW - kMixerDivW - 7 x kMixerChanGap) / 8` = about 112; the 73 px knobs fit with 39 to spare), top to bottom:

| Part | Spec |
|---|---|
| Name | an LCD text field (`drawLcdGlass` / `lcdFont(kLcdTextH)`) one line, ellipsised. Connected: the app name or preset name lit in the **voice colour `channelPalette[slotIndex]`** (slot 1 = Red ... slot 8 = Copper; fixed by slot position, so a slot keeps its colour). Empty slot: unlit ghost lettering "-" (`Lighting::lcdGhost`) |
| EQ stack | four `KnobWithLabel`, `knobInsertPad`, size `kMixerStripKnobW` x `kMixerStripKnobH` (73 x 46), top to bottom **High, Mid Hz, Mid, Low** (desk order, as today). Configured with `mu_ui::configureKnobFromSlot` for the 3-Band EQ algorithm so labels, ranges and value text are the products' own ("Low", "Mid", "Mid Hz", "High"; dB and Hz formatters). The old raw sliders and the abbreviated "Hz" label go. Double-click or Ctrl+click resets to flat |
| Fader + VU | the family fader and `VUMeter` (10 px, right-adjacent, no gap). **The fader takes all the height left in the strip** (the 200 px `kFaderMaxH` cap is for strips that also carry sends and sidechain; a Client strip has neither, so the cap does not apply). Range 0 to 1.5 linear, unity 1.0, double-click returns to unity |
| dB readout | 9 pt `labelText` under the fader, one decimal (`0.0`, `-6.0`, `+3.5`); the status bar shows `Client 3 level` / `-6.0 dB` |
| Mute, Solo | the family M / S buttons (`vuMeterClip` red when muted, `knobLevel` amber when soloed, as in the mixer overlay) |

Budget, per strip, Medium: inner height 662 - 2 x `kChannelInset`(7) - header 22 - plate/gap 4 = 622. Parts: name 18 + gap 6 + EQ 4 x 46 = 184 + gap 6 + dB 18 + gap 4 + mute/solo 24 + `kLabelGap` 6 = 266. Fader = 622 - 266 = **356** (the minimum is 40). Fits with the knob grid and the strips' widths.

An **empty slot** is drawn inactive (`MixerChannel::setActive(false)`: dimmed) but stays operable, so a level or EQ can be set before the app connects. A slot's mute / solo / EQ state is kept as today.

**Master column** (`kMixerMasterW` 80 + insert panel `kMixerInsertPanelW` 136 = 216), the same as the family mixer's master:

- Master strip: name plate "Master", the fader (also uncapped), `VUMeter`, dB readout. No mute / solo. Its fader top aligns with the client faders.
- Insert panel: two stacked boxes **Insert 1** and **Insert 2** (name plates), each a `DropdownSelect` of the shared algorithm list (`populateInsertAlgoDropdown`, alphabetised like the products' `InsertSubsection`) over a **2 x 2 grid** of `knobInsertPad` `KnobWithLabel`s configured by `configureKnobFromSlot` (family section 5, "Overlay and dialog fit rules": 2 x 2 for every algorithm, EQ included). Each box needs dropdown 24 + 2 x 46 knob rows + plate and padding, about 150 of the 622 available, two of them 300: fits with 300+ spare.

Knob colours: **the category colours, not the app colour.** mu-link's main area is a mixer, and the metal-style rule is that a mixer looks the same in every app (family section 11): do not call `useAppColourKnobs` over it.

## 4. Clock lamp states (mu-link)

The shared `StatusLamp` "Clock", family colours and strengths ([family section 3.5](../design-ui-family.md#35-specialised-components)), with one state added for the external-clock phase servo:

| State | When | Lamp | Status bar (hover "MIDI Clock"; entering Lost posts it once) |
|---|---|---|---|
| Waiting | MIDI In chosen, no pulses yet | amber, `lampDim` | `Waiting for clock` |
| Locked | pulses arriving and the beat grid is within tolerance of the master | green, `lampOn` | `Locked at 127.5 BPM` |
| Drifting | pulses arriving but the grid is further off than the tolerance (a tempo change or a Start, still servoing) | amber, `lampOn` (lit, brighter than Waiting) | `Drifting, re-aligning` |
| Lost | pulses stopped past the watchdog | red, `lampOn + lampPlayhead` | `Lost, transport stopped` |

Tolerance: Locked when the phase error is at most half a clock pulse (1/48 beat); Drifting above it; return to Locked below 1/96 beat (hysteresis, so the lamp does not flicker). The value is a named constant in the clock code, not in the drawing. The lamp reads the error from the server through an atomic, polled by the bar's existing timer. Waiting and Drifting differ by lamp strength and by status text, never by colour alone.

## 5. Settings overlay (the shared `StandardSettingsOverlay`)

The gear opens the shared two-column settings page, with these groups only (everything fits trivially; no per-product constants):

| Group | Rows |
|---|---|
| General | **Display size** (Small / Medium / Large, as every product); **Audio / MIDI devices** as a single button row "Choose devices..." that opens the existing native `AudioDeviceSelectorComponent` dialog unchanged (the device list is open-ended and the JUCE selector already does it) |
| MIDI Clock | **Sync offset**: `NudgeInput` integer, range -50 to +250 ms, step 1, Shift+click 10, default 0, signed (`+25`), exactly as [family section 6.5](../design-ui-family.md#65-transport-timing-controls-standalone) (mu-link always shows it). Row label "Sync offset" |

This supersedes the 2026-10-11 audit note that mu-link has no settings overlay: that was true of the old bespoke window. Source and Messages are not repeated here (Source is on the bar; mu-link takes Clock and Transport from the master).

## 6. Scenes panel (1170 x `kScenesPanelH` 152)

One metal panel under the mixer, owned by a `ScenesPanel` component that holds `kScenesPanelH` and its other constants.

| Part | Spec |
|---|---|
| Plate | name plate "Scenes" at the left of the plate band (`kSectionPlateH` 14) |
| Scene row | one `SegmentControl`, **Pills** style, `ActiveStyle::Warning` (amber, the colour the scene buttons use today), items "1" to "8", full panel width inside the screw inset, 32 px high. A click selects the scene for editing **and** recalls it (sends each enabled client its program change). Clicking the already-selected scene must recall it again, so `SegmentControl` needs an opt-in `setFiresOnReselect(true)` (today `setSelectedIndex` returns early on the same index): extend the shared control, do not use eight `TextButton`s. Status bar: `Scene 3` / `Recall: sends each ticked app its program change` |
| Cells | one column per client, aligned under the strip above (same column width and x), top to bottom: a `SlideSwitch` "On" (22 px), a `NudgeInput` integer **Program** (0 to 127), a `NudgeInput` integer **Channel** (1 to 16), each with its engraved caption. Cell height 22 + 4 + 24 + 4 + 24 = 78 |
| Wording | status bar for Channel: `Channel 9 loads a full preset, 1 to 8 swap a single layer` (the existing meaning, now written down) |

Budget, Medium: panel inner 152 - 2 x 7 = 138; plate band 14 + scene row 32 + gap 6 + cells 78 = 130, spare 8, at least `kPad` 6. The cell column is 112 wide against a 73 px minimum `NudgeInput`.

## 7. Overlays

`SaveDialog` and `PresetBrowser` are the shared overlays, over the whole window as in the products (the browser covers the main area, the save dialog is a modal card). No change to their geometry; they already pass the fit rules.

## 8. Do not

- Do not keep the bespoke `MuLookAndFeel lnf` member, hand-placed labels or the `styleLabel` helper: colours and sizes come from `MuLookAndFeel` and the shared components.
- Do not abbreviate: "Hz" becomes "Mid Hz", "CONNECTED CLIENTS" becomes the plate "Clients", "MASTER FX 1" becomes the plate "Insert 1".
- Do not use plain `juce::TextButton`s for scenes, mute / solo styling overrides, or `juce::Slider`s for the EQ, tempo or fader without the shared look.
- Do not add a per-product constant for anything that exists as a family token.
