# mu-On — UI Design

Product-specific layout. The look, tokens, controls and metal style are the family's: [../design-ui-family.md](../design-ui-family.md). Engine and sequencer: [design-engine.md](design-engine.md), [design-sequencer.md](design-sequencer.md).

## 1. Layout: lane sidebar, all ENGINE boxes visible (owner ruling 2026-10-10)

The earlier **Lane Rack** proposal (all five lanes' strips with no sidebar, a MOD toggle and the `laneMutedDim` token) is **superseded and not built**. Owner: "Keep the side bar, the sequencer displayed will be chosen by that." The all-boxes ENGINE area below was the five-fixed-lanes layout; it is superseded by "With variable layers" (built).

### Shell

- Standard family shell: lane `LayerSidebar` on the left, `GroovePanel` as the main panel. Mixer and Settings overlays unchanged.
- The **selected lane** (sidebar) chooses what is shown in the per-lane areas: its step row (Rumble: its envelope), the Groove box, and the modulators. The header bar and `ModulatorPanel` act on the selected lane, as in the other products.

### ENGINE area

- **All five lanes' ENGINE boxes are visible at once** in one metal panel under the header.
- Each box is as wide as its controls need (`ParamKnobGrid::getPreferredWidth`), never stretched.
- Boxes flow left to right in sidebar order and wrap to the next row when the next box would overrun the panel. At the 1088 px panel width this gives three rows: Kick / Bass / Hat + Snare + Rumble.
- The **selected lane's box is outlined in its lane colour** (`layerPalette`). Clicking any ENGINE box selects that lane and the sidebar follows.
- Box chrome is the family's (`drawSections`, plate band `kSectionPlateH`, `kVoiceDivW` gaps); main-page knobs use the app colour.

### With variable layers (built; ruled 2026-10-12, one point awaiting Andy)

The all-boxes ENGINE area cannot hold up to 8 layers in the 464 band, so with variable layers the ENGINE area shows **only the selected layer's engine** (this supersedes the all-boxes layout above once #1241 lands; **Andy to confirm**, since the 2026-10-10 ruling was his).

- **Engine selector:** a `DropdownSelect` labelled "Engine" (Kick, Bass, Hat, Snare, Rumble) as the first control of the ENGINE box, left of the engine's knobs, inside the content band. Not in the header strip (nothing else lives there). Switching engine keeps every engine's settings on that layer, asks no confirm, and renames the layer to the new engine's name only if it still has the old engine's default name.
- **ENGINE box content:** the selected engine's `ParamKnobGrid` (family chrome, `getPreferredWidth`); the Rumble box adds a "Source" `DropdownSelect` listing the other layers by name, default the first Kick layer. Bass Duck / D.Atk / D.Rel are gone (sidechain lives in the mixer).
- **Modulation arcs:** the ENGINE box knobs are `KnobWithLabel` (empty label; the grid's name label sits above) and each knob that is a row of the shown engine's mod target table shows the live arc (`ParamKnobGrid::Spec` mod binding, see [family §6](../design-ui-family.md#knobwithlabel)); arcs clear while the transport is stopped and for an engine the track is not running. Dropdowns have none.
- **Step rows:** one row per layer slot, in sidebar order, every row the same height = steps-box height / `mu_limits::kMaxLayers`, top-aligned, so adding a layer never resizes the others. The selected layer's row is outlined in its colour; clicking a row selects that layer. A Rumble layer's row is drawn dimmed, not editable, labelled "Rumble"; its envelope editor replaces the Groove box content while it is selected.
- **New layer:** engine = the first of Kick, Bass, Hat, Snare, Rumble not already used by an active layer (else Kick), seeded with that engine's default groove row, named after the engine.
- **As built:** a track is named after its engine (the name is not saved and not editable; two Kicks are both "Kick", the Rumble's Source list prefixes the layer number). The ENGINE box holds the Engine dropdown (family-standard `kDropdownW` x `kDropdownH`, 120 x 24), the Rumble's Source dropdown beside it, then the knobs; the second panel's left box is the same width (`MuLookAndFeel::kGrooveBoxW`, 300) for Swing / Accent and for the Rumble envelope (Loop control above the editor), so selecting a Rumble does not move the step rows (ruled 2026-10-12: the fixed box is accepted; for a non-Rumble track the Swing / Accent pair is centred horizontally in it, top-aligned, on the standard knob cell). Changing a track's engine keeps the old engine's modulator assignments, inert while another engine runs and live again on switching back (accepted). The band is two panels: ENGINE 113 (7 + 14 + 85 + 7) and the flex GROOVE | STEPS panel 351 (steps box 323, each row 38).
- **Fresh instance:** one Kick layer with the four-on-the-floor row. Old 5-lane states load as their five layers (data mapping in [design-plugin-family.md](../design-plugin-family.md#stage-6-build-rulings-2026-10-12-ux-controller)).
- Add / delete / reorder: the family standard ([design-ui-family.md](../design-ui-family.md#layer-add--delete--reorder-family-standard-ruled-2026-10-12)); noun "Track".

### Not part of this design

No per-lane mute / trigger lamp strip, no MOD toggle, no `Lighting::laneMutedDim`; no new tokens. Mute stays in the mixer.

### Family impact

Sidebar plus selected-lane panel is the family pattern; the all-boxes ENGINE area is a mu-On exception justified by the small per-lane control count. `getPreferredWidth` is generic in `mu-core` `ParamKnobGrid`; a flow-and-wrap box container may be lifted to `mu-core` if another product needs it.
