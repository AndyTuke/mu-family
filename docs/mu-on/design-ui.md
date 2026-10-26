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

- **Engine selector:** a `DropdownSelect` labelled "Engine" (Kick, Bass, Hat, Snare, Rumble, FM Perc) as the first control of the ENGINE box, left of the engine's knobs, inside the content band. Not in the header strip (nothing else lives there). Switching engine keeps every engine's settings on that layer, asks no confirm, and renames the layer to the new engine's name only if it still has the old engine's default name.
- **ENGINE box content:** the selected engine's `ParamKnobGrid` (family chrome, `getPreferredWidth`); the Rumble box adds a "Source" `DropdownSelect` listing the other layers by name, default the first Kick layer. Bass Duck / D.Atk / D.Rel are gone (sidechain lives in the mixer).
- **Modulation arcs:** the ENGINE box knobs are `KnobWithLabel` (empty label; the grid's name label sits above) and each knob that is a row of the shown engine's mod target table shows the live arc (`ParamKnobGrid::Spec` mod binding, see [family §6](../design-ui-family.md#knobwithlabel)); arcs clear while the transport is stopped and for an engine the track is not running. Dropdowns have none.
- **Step rows:** one row per layer slot, in sidebar order, every row the same height = steps-box height / `mu_limits::kMaxLayers`, top-aligned, so adding a layer never resizes the others. The selected layer's row is outlined in its colour; clicking a row selects that layer. A Rumble layer's row is drawn dimmed, not editable, labelled "Rumble"; its envelope editor replaces the Groove box content while it is selected.
- **New layer:** engine = the first of Kick, Bass, Hat, Snare, Rumble not already used (FM Perc is never the automatic pick) by an active layer (else Kick), seeded with that engine's default groove row, named after the engine.
- **As built:** a track is named after its engine (the name is not saved and not editable; two Kicks are both "Kick", the Rumble's Source list prefixes the layer number). The ENGINE box holds the Engine dropdown (family-standard `kDropdownW` x `kDropdownH`, 120 x 24), the Rumble's Source dropdown beside it, then the knobs; the second panel's left box is the same width (`MuLookAndFeel::kGrooveBoxW`, 300) for Swing / Accent and for the Rumble envelope (Loop control above the editor), so selecting a Rumble does not move the step rows (ruled 2026-10-12: the fixed box is accepted; for a non-Rumble track the Swing / Accent pair is centred horizontally in it, top-aligned, on the standard knob cell). Changing a track's engine keeps the old engine's modulator assignments, inert while another engine runs and live again on switching back (accepted). The band is two panels: ENGINE 113 (7 + 14 + 85 + 7) and the flex GROOVE | STEPS panel 351 (steps box 323, each row 38).
- **Fresh instance:** one Kick layer with the four-on-the-floor row. Old 5-lane states load as their five layers (data mapping in [design-plugin-family.md](../design-plugin-family.md#stage-6-build-rulings-2026-10-12-ux-controller)).
- Add / delete / reorder: the family standard ([design-ui-family.md](../design-ui-family.md#layer-add--delete--reorder-family-standard-ruled-2026-10-12)); noun "Track".

### Not part of this design

No per-lane mute / trigger lamp strip, no MOD toggle, no `Lighting::laneMutedDim`; no new tokens. Mute stays in the mixer.

### Family impact

Sidebar plus selected-lane panel is the family pattern; the all-boxes ENGINE area is a mu-On exception justified by the small per-lane control count. `getPreferredWidth` is generic in `mu-core` `ParamKnobGrid`; a flow-and-wrap box container may be lifted to `mu-core` if another product needs it.

## 2. FM Perc engine (ruled 2026-10-21, Andy approved the engine; engine research: [research-fm-drums.md](research-fm-drums.md))

One engine, an **Algorithm** dropdown of eight voices, **one fixed set of ten knobs** with the same labels, positions and order in every voice. Only what a knob does changes. Nothing new is added to the shell or the tokens; the grid gains one optional field (the tooltip, below).

**Engine card.** Same card as Bass: plate `FM PERC ENGINE`, the Engine dropdown (`kDropdownW` x `kDropdownH`), then the `ParamKnobGrid`. The grid holds the Algorithm selector (two cells, as Bass's Wave) plus ten knobs = 12 cells, the same count as Bass, so the card is the same size and wraps the same way (the coder confirms Bass wraps 6 + 6; if it wraps otherwise, FM Perc follows Bass). Spec order is the layout. Knobs are the app colour (no semantic colour split), `KnobWithLabel` with modulation arcs on all ten (all are mod targets; the Algorithm is not).

| Row | Cells (left to right) | Group meaning |
|---|---|---|
| 1 | Algorithm (2 cells), Tune, Ratio, Index, FM Env | what the voice is: pitch and FM tone |
| 2 | Fdbk, Dive, Dv Time, Decay, Fold, Noise | the grit, the pitch fall, the length and the finish |

**Labels (final).** The grid label is 13 px high (`ParamKnobGrid::kLabelH`, font `kKnobLabelFont` 10) over a 54 px cell; the longest existing labels are 7 characters ("Rv Size", "Cutoff", "Rv Mix"). Rule: 7 characters or fewer, abbreviated in the existing style (`P.Dec`, `Rv Size`). Final: **Tune, Ratio, Index, FM Env, Fdbk, Dive, Dv Time, Decay, Fold, Noise** (longest "Dv Time", 7). "Feedback" (8) and "Dive Time" (9) are the full names, used in the status line, not on the knob. Dropdown caption: **Algorithm** (the selector spans two cells).

**Algorithm items, in this order (append-only choice, stored index = position):** `Tom`, `Conga`, `Zap`, `Click / Rim`, `Clank`, `Wood / Mallet`, `Pluck / Ping`, `Burst / Glitch`. Default Tom. Picking an item changes the voice only; it never moves the other knobs (the research's "relative knobs plus factory presets").

**Status line (on hover or change, `onStatusUpdate`: name, value).** The name is the full name; units never go on the knob label. Values: `Algorithm` / item text; `Tune` / note and Hz, `F#2 (90 Hz)` (shown in notes, the research's absolute-semitone mapping; if no note formatter exists in [family 6.4](../design-ui-family.md#64-standard-value-formatters) one is added there, not locally); `Ratio` / `1.50`; `Index`, `Feedback`, `Fold`, `Noise` / whole percent; `FM Env`, `Dive Time`, `Decay` / ms (`1.2 s` from 1000 ms, as the other decay knobs); `Dive` / `0.6x`, `Off` at 0, `Rise 0.2x` below 0.

**Tooltips (one line per knob, voice-aware).** The family rule (status bar for names and values, tooltips sparingly) stands: the tooltip carries **no name or value**, only what the knob does in the shown voice. This is the "few non-obvious controls" case, since one fixed set of knobs does eight jobs. It needs one shared change, not a local copy: `ParamKnobGrid::Spec` gains an optional `juce::String tooltip` (empty = none) applied to the knob; mu-On rebuilds the specs with the new voice's text when the Algorithm changes (`setSpecs` already rebinds safely). Texts (the coder checks each against the engine; a sentence that is false for the built voice is fixed in the engine or here):

| Knob | Tom | Conga | Zap | Click / Rim | Clank | Wood / Mallet | Pluck / Ping | Burst / Glitch |
|---|---|---|---|---|---|---|---|---|
| Tune | Pitch of the tom body | Pitch of the drum | Start pitch, before it falls | Pitch of the tick | Base pitch of the metal set | Pitch of the bar | Pitch of the ping | Pitch of the burst |
| Ratio | Skin tone: 1.0 plain, 1.5 round, 1.59 natural head | Overtone: 1.6 skin, 2.1 bongo, 2.3 timbale | Low is smooth, high is metallic | Clave to block character | Stretches the metal set: above 1 bell, below 1 hat | About 4 gives a tuned mallet | Whole numbers ring clean, others go inharmonic | Odd values make it rougher |
| Index | Brightness of the knock | Brightness of the slap | Grit of the zap | Hardness of the tick | Amount of metal | Strength of the upper partial | Brightness at the start of the ping | Amount of crunch |
| FM Env | How long the knock lasts | How long the slap lasts | Short is a blip, long is a screamer | Length of the tick's edge | How long the metal stays bright | How long the upper partial lasts | How long the ping stays bright | Length of the burst's bite |
| Fdbk | Roughness in the knock | Roughness in the slap | Fuzz on the zap | Grit on the tick | Dirt in the metal | Rasp on the bar | Sharper, more resonant ping | High values turn to noise |
| Dive | Pitch fall: subtle is a tonk, big is an electro tom | Small pitch fall | Depth of the pew | Small pitch fall on the tick | Pitch fall of the hit | Small pitch fall | Below zero it rises, a chirp | Depth of the swoop |
| Dv Time | How fast the pitch falls | How fast the pitch falls | Short is a thud, long is a laser | How fast the pitch falls | How fast the pitch falls | How fast the pitch falls | How fast it rises or falls | How fast the swoop runs |
| Decay | Length of the tom | Length of the drum | Length of the zap | Length of the tick | Short is closed, long is open | Length of the bar ring | Length of the ping | Length of the burst |
| Fold | Bite on the attack | Bite on the attack | Edge on the zap | Edge on the tick | Industrial bite | Edge on the bar | Edge on the ping | Distortion of the burst |
| Noise | Noise tail, the 808 "fake reverb" | Noise under the hit | Noise in the zap | Noise in the tick | Noise in the metal | Noise under the bar | Noise under the ping | Noise in the burst |

**No inert knob (recommended to Andy, question 1 in the report).** The engine lets all ten knobs act in all eight voices, so nothing needs dimming. If a knob is inert in a voice its tooltip reads `No effect in <voice>`; it is **not** dimmed or disabled (a disabled look would also hide its modulation arc and break the fixed card).

**Factory presets (`.muTrack`, engine FM Perc; algorithm and knobs from the research tables, tuned by ear).** Listed in algorithm order.

| Voice | Presets |
|---|---|
| Tom | Low Tom, Mid Tom, High Tom, Floor Tom, Electro Tom, Rack Tom Tight |
| Conga | Low Conga, High Conga, Bongo, Timbale |
| Zap | Laser Zap, Screamer, Blip, Pew Low |
| Click / Rim | Rim Tick, Clave, Wood Block, Grit Tick |
| Clank | Closed Clank, Open Clank, Gong Hit, Industrial |
| Wood / Mallet | Marimba Tap, Mallet Low, Mallet High |
| Pluck / Ping | Ping, Rising Ping, Acid Stab |
| Burst / Glitch | Glitch Burst, Digital Crunch, Noise Hit |

Names are plain (no "FM" prefix: the engine and folder say it). The three-tom kit is the Low, Mid and High Tom presets on three tracks.

**Do not.** Rename or reorder the ten knobs per voice; relabel a knob for a voice (the tooltip does that); add a per-voice knob layout; write a local tooltip in mu-On; put values or units in the tooltip or on the label; add a colour or token for the engine.
