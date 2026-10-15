# μ-Toni - UI design

The family rules are in [design-ui-family.md](../design-ui-family.md); the band fit is in [design.md](design.md#layer-content-band-fit-464). This page holds only what is specific to μ-Toni's panels.

## Appergater step pattern

Ruled 2026-10-14 (Andy: the accent squares should be Step Off / On / Accented, three shades). Engine meaning: [design-sequencer.md](design-sequencer.md#step-pattern--off--on--accented-ruled-2026-10-14).

**Component.** The shared `StepEditor` in its **level-cell mode** (a family component; see [design-ui-family.md §11](../design-ui-family.md#11-metal-style-family-standard), Lamp display). `setQuantization(3)` alone is not enough: it only snaps the bar height and the lamps would still show a bar of different heights, not three shades, and the vertical-position gesture is wrong for cells. The mode is new, generic and lives in mu-core: `setLevelCells(3)` (replaces `setUnipolar(true)` + `setQuantization(2)` on this editor). Values stay 0 / 50 / 100 (Off / On / Accented) so the existing `setSteps` / `onStepChanged(step, value)` contract is unchanged.

**Look (metal style, the family standard).** Every cell is a whole lamp lens (not a bar); the state is the brightness of the layer colour:

| State | Lamp strength | Token |
|---|---|---|
| Off | dark lens, bare lamp base | `Lighting::lampOff` |
| On | lit, not bright | `Lighting::lampOn` |
| Accented | full | `Lighting::lampAccent` (new, 1.0) |

The step under the playhead is marked by a thin white ring round its cell at `Lighting::lampPlayheadRing` (new, 0.6 alpha), not by extra brightness, because `lampOn + lampPlayhead` would read as Accented. In the flat look (not shipped for μ-Toni) the existing bar heights 0 / 50 / 100 apply. No colours, sizes or alphas in the editor code: the two new fields are the only additions, in `MuTheme::Lighting`.

**Gesture.** Click **cycles** the cell Off -> On -> Accented -> Off; **Shift + click** cycles backwards. The first click of a drag sets the state; dragging across other cells **paints that same state** into each cell entered (so a run of rests is one drag). The cell under the mouse is the only thing that matters: height is ignored. Setting a step Off clears its accent bit.

**Labels.** The knob `accLen` reads **Steps** (was "Acc Steps"; the pattern is no longer only about accents; the Accent knob stays **Accent**). The parameter display name becomes "Pattern Steps"; the id `accLen` does not change (saved state and automation). Full words, no units.

**Status bar and tooltip.** The StatusBar-first rule applies: hovering or dragging a cell reports `Step 3: Accented` (states `Off`, `On`, `Accented`), using the existing `onStatusUpdate` path of the shell; the Steps knob reports `Steps 8` in the standard way. One sparing tooltip on the editor (an icon-less non-obvious control): "Click: Off, On, Accented. Drag to paint. Shift+click goes back." No other tooltips.

**Do not.** Keep `setQuantization(2)` and the height gesture; draw cells in the Appergater code; copy the mu-Tant gate designer; use three hard-coded colours; call the states "Gate"; show the accent bit on an Off cell.
