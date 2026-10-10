# mu-On — UI Design

Product-specific layout. The look, tokens, controls and metal style are the family's: [../design-ui-family.md](../design-ui-family.md). Engine and sequencer: [design-engine.md](design-engine.md), [design-sequencer.md](design-sequencer.md).

## 1. Layout: lane sidebar, all ENGINE boxes visible (owner ruling 2026-10-10)

The earlier **Lane Rack** proposal (all five lanes' strips with no sidebar, a MOD toggle and the `laneMutedDim` token) is **superseded and not built**. Owner: "Keep the side bar, the sequencer displayed will be chosen by that." Lanes are fixed (Kick, Bass, Hat, Snare, Rumble; no add / delete / rename / reorder).

### Shell

- Standard family shell: lane `ChannelSidebar` on the left, `GroovePanel` as the main panel. Mixer and Settings overlays unchanged.
- The **selected lane** (sidebar) chooses what is shown in the per-lane areas: its step row (Rumble: its envelope), the Groove box, and the modulators. The header bar and `ModulatorPanel` act on the selected lane, as in the other products.

### ENGINE area

- **All five lanes' ENGINE boxes are visible at once** in one metal panel under the header.
- Each box is as wide as its controls need (`ParamKnobGrid::getPreferredWidth`), never stretched.
- Boxes flow left to right in sidebar order and wrap to the next row when the next box would overrun the panel. At the 1088 px panel width this gives three rows: Kick / Bass / Hat + Snare + Rumble.
- The **selected lane's box is outlined in its lane colour** (`channelPalette`). Clicking any ENGINE box selects that lane and the sidebar follows.
- Box chrome is the family's (`drawSections`, plate band `kSectionPlateH`, `kVoiceDivW` gaps); main-page knobs use the app colour.

### Not part of this design

No per-lane mute / trigger lamp strip, no MOD toggle, no `Lighting::laneMutedDim`; no new tokens. Mute stays in the mixer.

### Family impact

Sidebar plus selected-lane panel is the family pattern; the all-boxes ENGINE area is a mu-On exception justified by the small per-lane control count. `getPreferredWidth` is generic in `mu-core` `ParamKnobGrid`; a flow-and-wrap box container may be lifted to `mu-core` if another product needs it.
