# μ Family — Playing Live from a Novation Launchpad X

**Status: research and plan, 2026-10-09; hardware probed 2026-10-10 (§3.5); `mu-control` build ruled 2026-10-13 (§3.2d); M1 and M2 built 2026-10-13; M3 built and run on the real pad 2026-10-14 (RGB lighting, input decode and Live restore verified, §3.5; the palette fallback forms, logo press and screen-versus-pad colour match are still open).**
Part 1 is read from Novation's *Launchpad X Programmer's reference manual*
([the PDF](reference/Launchpad-X-Programmers-Reference-Manual.pdf), converted to text and read in full, including the
two pad-layout and colour-palette pictures). Part 2 is our proposal and is not decided.

Why it matters: playing live with other musicians means hands on pads, not a mouse. The Launchpad X is the
owner's controller. It also fits the one-framework direction in [design-future.md](design-future.md): the
controller code is written **once** as a device-independent layer (the action vocabulary in `mu-core`, the surface layer in the `mu-control` library, §3.2a), so other surfaces slot in with one driver and every app gets them (§3).

---

## 1. What the Launchpad X does (from the manual)

### 1.1 Two USB MIDI ports

| Port | Name | Use |
|---|---|---|
| **MIDI** | "LPX MIDI In / Out" (the *second* interface on Windows) | **The one we use.** Note mode, Custom modes, Programmer mode, lighting. |
| **DAW** | "LPX DAW In / Out" (the *first* interface on Windows) | Ableton-style Session / drum-rack / fader control. Not needed for us. |

Windows may rename them (the second often shows as "MIDIIN2 (…)"). Match by name pattern and let the user pick.
The pad sends Note On with velocity 0 for Note Off; it accepts either form back.

### 1.2 Modes

- **Live mode** (default): Session, Note, four Custom modes. The device does its own thing.
- **Programmer mode** (layout `7Fh`): the whole surface is ours: every pad and button sends plain MIDI and we can
  light all of them. **This is the mode for a performance surface.** It disables the on-device Setup menu, so
  *we must send the switch back to Live mode when we finish* (see §3, risk R1).
- **Lighting Custom Modes** (Custom 3 and 4 by default): lighting only on the 8×8 pads, the device stays usable
  as normal. A fallback if Programmer mode proves awkward.

Switch (Programmer / Live): `F0 00 20 29 02 0C 0E <mode> F7`, mode 1 = Programmer, 0 = Live. Reading back: same
message with no mode byte. Alternative: select layout `7Fh` with command `00h`.

### 1.3 What each control sends in Programmer mode

Decimal numbers; the 8×8 pads are notes, the edge buttons are Control Changes (all on channel 1):

```
   CC91 CC92 CC93 CC94 CC95 CC96 CC97 CC98 | CC99 (logo)
    81   82   83   84   85   86   87   88  | CC89
    71   72   73   74   75   76   77   78  | CC79
    61   62   63   64   65   66   67   68  | CC69
    51   52   53   54   55   56   57   58  | CC59
    41   42   43   44   45   46   47   48  | CC49
    31   32   33   34   35   36   37   38  | CC39
    21   22   23   24   25   26   27   28  | CC29
    11   12   13   14   15   16   17   18  | CC19
```

Pad note = 10 × row + column (row 1 at the bottom, column 1 at the left). So a pad is easy to turn into an
(x, y) pair: `row = note / 10`, `col = note % 10`. The right-hand column is CC 19, 29 … 89; the top row is CC 91–98.
Pads send velocity and, if enabled, pressure. Button presses send CC value 127 down and 0 up (the manual does not
spell the up value out; check on hardware).

### 1.4 Lighting

Two ways, and we want both:

1. **MIDI events (one pad at a time).** Note On (pads) or Control Change (buttons) *back to the device*:
   - channel 1 (`90h` / `B0h`) = static colour,
   - channel 2 (`91h` / `B1h`) = flashing (alternates with the static colour at 50 %, one beat per cycle),
   - channel 3 (`92h` / `B2h`) = pulsing (dark to full, two beats per cycle).
   The velocity or CC value is the colour, 0–127 from the fixed palette (0 = off, 5 = red, 13 = yellow, 21 =
   green, 37 = cyan, 45 = blue, 3 = white, and so on; the full 128-colour picture is in the PDF).
   Example: `90 0B 05` lights the bottom-left pad static red.
   **Flashing and pulsing follow the MIDI clock sent to the device** (120 bpm if none), so if we send our MIDI clock
   to the Launchpad its lights pulse in time with the music.
2. **Lighting SysEx (many pads, any colour).** `F0 00 20 29 02 0C 03 <colourspec>… F7`, up to **81 entries**,
   so the entire surface in one message. Each entry is: type, LED index (the same 11–88 / 91–99 numbers), then
   - type 0 static: 1 byte palette index,
   - type 1 flashing: 2 bytes (colour B, colour A),
   - type 2 pulsing: 1 byte palette index,
   - type 3 **RGB**: 3 bytes red, green, blue, each 0–127.
   RGB lets us colour pads by layer or app colour (the family tokens), not just the nearest palette entry.

### 1.5 Other messages worth using

| What | Message | Use for us |
|---|---|---|
| Device inquiry | `F0 7E 7F 06 01 F7`, reply contains `00 20 29 13 01` | Recognise a Launchpad **X** (not a Mini or Pro) and read its firmware. |
| Brightness | `…08 <0–127>` | Stage vs bedroom brightness. |
| LED feedback | `…0A <internal> <external>` | Switch off the device's own press-lighting so *we* decide every colour. |
| Sleep | `…09 <0/1>` | Blank the lights between songs. |
| Velocity curve | `…04 <curve 0–3> <fixed>` | Soft / medium / hard / fixed velocity (fixed = repeatable levels live). |
| Aftertouch | `…0B <type> <threshold>` | 0 poly, 1 channel, 2 off; threshold low / medium / high. Poly pressure is a lot of traffic: default **off**. |
| Text scroll | `…07 <loop> <speed> <colour> <text>` | Show tempo, preset name or "REC" across the pads. |
| Read back | most commands accept the same message with no data | Check state after connecting. |

Everything above starts `F0 00 20 29 02 0C` (the header) and ends `F7`.

### 1.6 Not needed

DAW mode (`10h`), DAW faders (`01h`), drum-rack modes (`0Fh`, `13h`), Session colours (`14h`), Note-mode scale and
configuration (`15h`, `16h`), fader velocity (`0Dh`). They exist for Ableton-style integration. The Note mode
configuration could be a nice extra later (set the Launchpad's own scale from mu-Toni's scale).

---

## 2. What the family has today, and what is missing

| Need | Today | Gap |
|---|---|---|
| Receive pad notes | Every product's `processBlock` already gets a `MidiBuffer` (mu-Toni and mu-Tant play held notes). | Pads would play notes as a keyboard only; nothing maps a pad to *mute layer 3*, *start*, *next preset*. |
| Map a control to an action or knob | Only Program Change to preset (`MidiPresetMap`, `MidiFullPresetMap`). | **No CC / note to parameter mapping** (design-future: "MIDI CC remote control", 🟡). This is the main missing piece. Ruled 2026-10-10 in [design-plugin-family.md](design-plugin-family.md#midi-control-mapping--family-standard-ruled-2026-10-10): `mu-core/Control/`, writes the APVTS base value from the message thread, global per-user file. |
| Send lights back | `MidiOutputEngine` sends 20 ms notes for mu-Clid MIDI-output mode. | **No controller output.** Lights need a MIDI output to the Launchpad, plus SysEx. |
| Clock to the device | `mu-link` has `MidiClockOut` (with known issues, backlog #1254, #1258). | Needed so pulsing / flashing follows the song; fix the clock out first. |
| Transport buttons | `mu_core::resolveTransport` (Host > HostTempo > MidiClock > Internal). | A pad must go through the resolver, not around it (a Start press must do nothing when a host owns the clock). |

### 3.1 A controller layer: four parts, only the first is device-specific

The Launchpad X is the first device, not the design. The 8×8 grid is common to many controllers (other Launchpads,
Ableton Push, APC, Novation and Akai grids), so the family gets a **controller layer** in `mu-core` that a new surface
slots into by supplying one small driver:

| Part | Knows about | Example |
|---|---|---|
| **Driver** (one per device family) | The wire protocol only: ports, mode switch, how a press arrives, how a light is sent. | `LaunchpadXDriver` (Part 1 of this doc); later Launchpad Mini / Pro, Push, APC40, a generic MIDI-learn device. |
| **Surface description** (data, from the driver) | Geometry and abilities: grid size (W×H), edge buttons, faders / encoders, colour support (none, palette, RGB), flash / pulse, pressure. | Launchpad X: 8×8 grid, 8 top + 8 right buttons + logo, RGB, flash, pulse, pad pressure. |
| **Mapping** (shared, saved) | Which surface control means which **action**. No device bytes, no product names. | "Grid cell (3, 2) = launch clip 3 on layer 2". |
| **Surface model** (shared, in `mu-core`) | What to show: layer colours, playing clip, queued clip, muted, clock beat. Emits LED state; consumes actions. | The same model drives any surface that has the abilities needed. |

A different surface then costs one driver plus (if its geometry differs) one default mapping. Nothing above the driver
changes, and no app changes. This is the same shape as the engine swap point in
[design-plugin-family.md](design-plugin-family.md): a stable interface in `mu-core`, the variable part behind it.

**Two mappings, one destination (ruled 2026-10-10).** The MIDI-message table (CC / note → action, #1275) is in `mu-core`
(`MidiControlMap`) and is global per user, not in the preset. The Launchpad's *surface mapping* (grid cell → action)
is in `mu-control` and produces the same `ControlAction` values. Both end in `ControlSink::perform` on the message thread, so
a pad and a hardware knob share one set of actions, quantise and transport rules. A driver never writes a
parameter itself. A Launchpad in Programmer mode that arrives as MIDI notes may simply use the `mu-core` table.

Actions are the family's own vocabulary, defined once: **launch clip (layer, index)**, **select layer**, **mute /
solo layer**, **transport (play, stop, tap)**, **preset prev / next**, **panic**, **modifier (shift)**, and a generic
**knob-to-parameter** (the CC mapping of backlog #1275, which this reuses). Because the actions are generic, they live
on the parent `Layer` class and work for `Rhythm`, `Pattern`, `Arp` and `Track` alike, and for the combined one-instance
engine in design-future.

### 3.2 Four levels of "what is playing" (owner, 2026-10-09)

The controller needs something to switch at every level a musician thinks in. The family has the middle two today;
the owner's proposal adds the top level and a bank of **clips** at the bottom:

| Level | What it is | Switched by | Exists today? |
|---|---|---|---|
| **Performance** (new) | A set for a gig: a list of **up to 8 pointers to presets** ✔ owner. | Rarely, between songs (screen, or a second page on the surface). | No. |
| **Preset** | A full preset (`.muClid`, `.muTant`, ...): up to **8 layers** and their mixer, FX and modulation. | **Top row, buttons 3 and 4** = previous / next preset of the performance (revised 2026-10-14, [3.6](#36-surface-mapping-defaults-for-the-launchpad-x-ruled-2026-10-13-backlog-1276); no direct jump to preset n). | Yes, one at a time, with full-preset hot-swap. |
| **Layer** | One engine + one sequencer + one channel. | **Right-hand column buttons (8)**: select the layer (for the second page and the screen). | Yes. |
| **Clip** ✔ owner (per layer) | **8 stored states of a layer**, any of which can play. | **The 8×8 pads**: row = layer, column = clip. | Layer presets exist; a bank of eight per layer does not. |

So a performance is 8 presets × 8 layers × 8 clips, and the Launchpad's top row, right column and grid map onto the
three lower levels one-to-one.

**Why the MIDI Program Change table cannot do this.** `MidiPresetMap` is a flat list of 128 preset files indexed by
program number and gated by MIDI channel. It has no idea of *which layer a pad belongs to* or *which preset set is
loaded*, and its job (an external keyboard calling a preset by number) is different. The clip bank is **new data owned
by each layer and saved with the preset**. The table stays for external MIDI. What is reused is the loading
machinery, below.

**Hot-swap, always (owner, confirmed against the code).** Every change a controller makes to what is playing goes
through the shared `mu_hotswap::Stager`, never a direct load: it already stages one payload per layer **and** one
full-preset payload, parses and pre-loads them off the audio thread, and commits at the loop or bar boundary
(immediately when stopped). All four products already stage a loaded full preset while playing. So a clip launch =
stage the layer payload, and a top-row press = stage the full preset, with the same boundary rule and the same
audio-thread contract as today. Staging a full preset **drops any pending per-layer launch** (existing behaviour of
the stager), so the pads show the new preset's clips once it commits.

**Pad behaviour (owner, 2026-10-09).**

| Pad state | Light |
|---|---|
| The clip **playing** on that layer | **Bright**, in the layer's colour. |
| An **available** (stored) clip | **Dim**, in the layer's colour. |
| An **empty** clip | **Off**. |
| A clip **queued** for the next boundary | Pulsing (owner approved). |
| A **muted** layer's playing clip | Flashing (owner approved). |

- **Press a dim clip:** launch it at the next boundary. **Clips play until another is selected**: there is no automatic
  advance; the player works through the clips by hand.
- **A pad never writes a preset** (owner): clips are stored from the screen with the layer, not from the surface.
- **Press the playing clip:** **mute the layer**; press it again to unmute (owner approved).
- **Next / previous clip** for a layer is an action on the second page (works through the clips in order with one
  button), so "a sequence of patterns to work through" is still covered by hand.
- An automatic chain (play clip 3 for two loops, then 5, ...) was in the earlier draft and is **dropped** by this
  decision; it can come back later as an option on top of the clip bank without changing it.

**Edge buttons.** *Superseded for version 1 by [section 3.6](#36-surface-mapping-defaults-for-the-launchpad-x-ruled-2026-10-13-backlog-1276): no shift, no second page, no transport or panic pad. The table below is the later-version idea.* (A Launchpad X has only 16 plus the logo, so the rest go on a second page behind shift; a surface
with more buttons uses fewer pages):

| Control | Action |
|---|---|
| Top row CC 91-98 | Preset 1-8 of the performance (the current one lit, loading one pulsing). |
| Right column CC 19-89 | Select layer 1-8 (the lit one is the layer the second page acts on). |
| Shift (page / modifier) | Second page: play / stop, tap tempo, panic (all notes off, lights reset), previous / next performance, next / previous clip for the selected layer. |
| Logo CC 99 | Clock heartbeat (pulses on the beat, dark when the clock is lost). Whether the logo also sends a press is not stated in the manual; check on hardware. |

A surface with no edge buttons, or a smaller grid, gets fewer actions at once or more pages; the surface description
(§3.1) lets the shared code decide.
**Naming note.** The launchable thing is a **Clip** ✔ owner, as in Ableton Live and Bitwig. This also removes the clash
with "slot", which the code already uses for FX / insert slots (`FXSlotBase`, `DelaySlot`), the composed `LayerState`,
`Layer` (the layer base, renamed by backlog #1265) and the per-layer index inside the hot-swap `Stager`; those keep
the word. No code type called `Clip` exists today (the only hits are the clipper insert and the VU clip indicator, which
are unrelated); `Layer` settled in #1265 and `Clip` is its own concept (ruled 2026-10-13: `mu_core::ClipBank`, held by
`Layer`, see [design-plugin-family.md, Clip bank](design-plugin-family.md#clip-bank--family-standard-ruled-2026-10-13-backlog-1277)).
A *clip* is a stored state of one layer, so it is not an audio clip.

### 3.2c The Performance file (architecture ruling, 2026-10-12; backlog #1279)

The architecture half. The look of the browser is for the `ux-controller` (list at the end).

- **Name and extension:** `.muPerformance` (not product-specific: the extension convention is `.mu<LayerType>` for a
  layer and `.mu<Product>` for a full preset, and a performance belongs to neither). Content folder `Performances/`
  in the product's content root, like `Rhythms/`; it moves to a family root when the one-instance rig exists, and
  nothing in the file changes when it does.
- **Shape (XML, one `PerformanceFile` format version):**
  `<MuPerformance format="1" name="" description=""> <Item idx="0..7" product="MuTantState" path="" name=""/> ... </MuPerformance>`.
  At most `mu_limits::kMaxPerformanceItems` (8) items; an absent `idx` is an empty item. `product` is the state
  identifier of the preset's product (already unique per product), so a mixed-engine list needs no format change.
  `path` is relative to the product's preset folder when the file lies under it, absolute otherwise; `name` is a cached
  display name. Pointers only: the file never embeds a preset, and a preset never references a performance.
- **A missing or foreign file is shown, never dropped:** its item stays in the list marked missing (or "other
  product" until the one-instance rig), the switch action skips it, and nothing is rewritten silently.
- **Code:** `mu-core/Persistence/PerformanceFile.{h,cpp}`, `mu_pp::PerformanceFile` (plain data, load / save / resolve, no
  processor dependency, unit-tested in `mu-core-tests`: round trip, relative and absolute paths, missing file, more than 8
  items, unknown format version, empty list). The current performance (path and item index) is per-user processor
  state saved in the host session as a root property of the product state, not in a preset.
- **Switching:** item `n` is "stage the full preset at that pointer" through the existing full-preset path
  (`stageFullPreset`, #1278), so the boundary rule and audio-thread contract are unchanged. The `ControlAction` vocabulary in
  `mu-core/Control/` gains `PerformanceItem(n)` and `PerformancePrev / Next`; the driver mapping (top row = items 1-8)
  lives in `mu-control`. A product loads only items of its own `product`.
- **Performance mode (ruled 2026-10-12, revised 2026-10-13):** standalone only for now; the PC screen is locked and only the surface acts.
  The live state (mode, current and queued item, selected layer, playing and queued clips), the actions and the per-preset swap timing
  that sets when a pad launch lands are in
  [design-plugin-family.md, Performance mode and live state](design-plugin-family.md#performance-mode-and-live-state--family-standard-ruled-2026-10-12-revised-2026-10-13-for-the-owners-decisions-backlog-1279).
  Pad semantics: grid row = layer, column = one of that layer's eight stored layer presets (clips), top row = presets 1-8; a press queues,
  the swap lands at the preset's swap timing, and the PC selects that layer at once. `PerformancePrev / Next` step the items of the loaded
  performance and wrap, skipping empty and missing items; pressing the current item cancels a queued switch. They are not "previous / next
  performance" (the second-page entry in §3.2 for that is not scheduled).
- **Readiness:** the file type with its tests can be built at any time. The browser waits on the `ux-controller`; the
  actions and pad mapping wait on #1276 (mu-control), #1278 and the clip bank (#1277); nothing consumes the file earlier.
- **Ruled by the `ux-controller` (2026-10-12): see [design-ui-family.md section 12](design-ui-family.md#12-performance-mode-ruled-2026-10-12-backlog-1279)** (standalone only for now; the PC UI is locked and read-only while the mode is on and only the surface acts; chooser is its own `OverlayHost` screen reachable with the mode off; items use the app accent; a read-only `StepIndicator` in the TransportBar; press-and-hold exit; the screen follows pad presses; surface previous / next wrap; sizing). The list below is the original question set, kept for reference.
- **Was for the `ux-controller` to rule:** where the performance is chosen (a tab in the preset browser, or its own
  `OverlayHost` screen); the 8-item editor (add from the preset list, drag to reorder, remove, rename, a missing-file
  state); how the current item and a queued item show on screen to match the pad lights (current bright, queued
  pulsing); whether the item colours come from the layer palette or a neutral set; how a performance is saved (the shared
  `SaveDialog`, name and description only); the keyboard binding and any status-bar text; and the touch / standard sizing
  (no per-product layout constants).

### 3.2a Where the code lives: a separate `mu-control` library (owner deferred to this recommendation, 2026-10-09)

Controller code goes in a new **`mu-control`** library, a sibling of `mu-core` (a library, not a product). It is created
at stage M1 of #1276 (§3.2d), not before, so there is no empty target. Its structure (folders, namespace, build option, the
standalone seam, threads, device ownership) is ruled in
[design-plugin-family.md, Controller library](design-plugin-family.md#controller-library-mu-control--family-standard-ruled-2026-10-13-backlog-1276).

| In `mu-core` (small, stable, no devices) | In `mu-control` (devices) |
|---|---|
| The **action vocabulary** (launch clip, mute, select layer, transport, panic, preset prev / next). | The **driver interface** and the Launchpad X driver (and later others). |
| A **control sink** interface that `ProcessorBase` and the layers implement, so actions reach them. | The **surface description**, the saved **mapping** and the **surface model**. |
| A read-only **"what to show"** interface (playing, queued and muted clips, layer colours, beat). | Opening the MIDI ports, and sending lights on a timer from the message thread (directly, not through mu-core's `TimedMidiOut`; a driver that also sends MIDI clock lends its port to one, see [Device MIDI output](design-plugin-family.md#device-midi-output--family-standard)). |
| The generic **MIDI CC / note to parameter mapping** (#1275): any MIDI-in feature needs it, it is not device-specific. | |

Why separate: (1) it is **optional per build**, since controllers need OS MIDI ports that a plugin inside a DAW may not
get (R2) and a freeware build may not want them, the same kind of reason that keeps heavy optional parts out of `mu-core`; (2) it **keeps growing**
with every new device, which `mu-core` should not; (3) the boundary can be **enforced**, as it is for products; (4) there is
precedent: the small `MuLinkBridge.h` sits in `mu-core` and the heavy parts are in `mu-link`.

The dependency is one-way and checked by `tests/scripts/check-core-boundary.ps1`: **`mu-control` may use `mu-core`;
`mu-core` must never include `mu-control`; neither may name a product.** Because `mu-core` cannot depend on
`mu-control`, `ProcessorBase` does not own a controller: the standalone shell (or the editor) creates the controller
manager on the message thread and connects it to the sink and the "what to show" interface above. A product that wants no
controller support simply does not link `mu-control`.

### 3.2b Layer colours must come from the controller's palette (owner, 2026-10-09)

**Update 2026-10-20 (Andy): the proposed table below is approved and built, with Blue 45 as layer 8.** `MuLookAndFeel::layerPalette` is now Red `FF6259` (5), Cyan `65EBFF` (37), Orange `FFB064` (9), Magenta `FF62FF` (53), Lime `C7FE6D` (75), Rose `FF5EC5` (57), Silver `DDDDDD` (2), Blue `5B5DFD` (45); this supersedes the "screen colours stay as designed" line in the 2026-10-14 decision below. The device correction table keeps the pad RGB tuned by eye on 2026-10-14 for each hue, now keyed by the new screen colours and set directly, so the pad look is unchanged; only the on-screen colours moved. The pad check of the new screen/pad pairing (and whether the lighter Blue wants a brighter pad value than the old Navy `0000B4`) is still for a hardware session (#1281).

The pad for a layer must be the same colour as that layer on screen. The owner's rule: **pick the layer colours from
the colours the controller can show, and stick to common hues** so most other pad controllers are covered too (some
will not match exactly; a common hue still degrades gracefully).

**Today:** the family has one shared 8-colour layer palette, `MuLookAndFeel::layerPalette` (Red, Cyan, Orange,
Magenta, Lime, Rose, Silver, Navy; Navy replaced Copper, see the decision below), used by all four products and documented in
[design-ui-family.md](design-ui-family.md). The Launchpad X palette (128 fixed colours, sampled from the manual's
picture; the picture's RGB is an illustration, **not** a measurement of the real LEDs) gives these matches:

| Layer colour today | Closest Launchpad entry | Verdict |
|---|---|---|
| Red `#E5484D` | 5 (`#FF6259`) or 6 | Good. |
| Cyan `#2EC4C9` | 37 (`#65EBFF`) | Good. |
| Orange `#F28C28` | 9 (`#FFB064`) or 84 (`#FF9F66`) | Fair; choose on the real LEDs. |
| Magenta `#D04FC8` | 53 (`#FF62FF`) | Good. |
| Lime `#A6D63A` | 75 (`#C7FE6D`) | Good. |
| Rose `#F07CA6` | 57 (`#FF5EC5`) | Good; keep distinct from Magenta on the pad. |
| Silver `#C9CCD1` | 2 (`#DDDDDD`) | Fair (a grey LED reads as white). |
| ~~Copper `#B87445`~~ (replaced) | 11 (`#B4765D`) | **Poor**: brown does not exist on an LED pad; it looked like a dim orange, next to Orange. |

**Decided (2026-10-14, Andy, tuned by eye on the real pad):**
- **Layer 8 is Navy `#2B3FD6`** (`layerPalette[7]`), replacing Copper; the other seven keep their hue and on-screen value, so saved colour indices keep meaning (a voice saved with index 7 just becomes navy). The earlier plan to re-pick every screen colour to a palette entry and use Blue is dropped: the screen colours stay as designed and the **device corrects them instead**.
- **Device colour correction.** `mu-control/Devices/LaunchpadX/LaunchpadXColourMap.h` maps a screen colour to a tuned pad RGB (the pad's LEDs are not the screen's colours); the **dim level is applied after the correction**. Colours not in the table pass through unchanged. The correction belongs to the Launchpad X driver, never to `mu-core`.
- **Dim level is `MuTheme::Lighting::padDimScale` = 0.20** (was 0.45, then 0.30 proposed; the correction makes the dim read brighter on the pad, so a lower scale is right).
- The table below was tuned by eye by Andy on the real pad and approved; he may revisit it once control works. Screen colour, pad RGB (bright), pad RGB at dim 0.20:

| Colour | Screen | Pad (bright) | Pad (dim) |
|---|---|---|---|
| Red | `E5484D` | `FF0004` | `330001` |
| Cyan | `2EC4C9` | `00FFCF` | `003329` |
| Orange | `F28C28` | `FF7700` | `331800` |
| Magenta | `D04FC8` | `FF0084` | `33001A` |
| Lime | `A6D63A` | `8BFF00` | `1C3300` |
| Rose | `F07CA6` | `FF4656` | `330E11` |
| Silver | `C9CCD1` | `B39A6D` | `241F16` |
| Navy | `2B3FD6` | `0000B4` | `000024` |
| Purple (mu-Tant primary) | `7F77DD` | `2400FF` | `070033` |
| Green (mu-Clid primary) | `4ADC8E` | `50FF00` | `103300` |
| Blue (mu-On primary) | `3D8BEB` | `1028FF` | `030833` |
| Yellow (mu-Toni primary) | `F2C53D` | `FFD700` | `332B00` |

The four app primaries are in the table because the top row and the app-accent pads use them. Open check, kept: the layer colours must stay distinct from the app primaries on screen (`MuTheme::AppPrimaries`); Navy against the Blue primary differs by lightness (see the design-ui-family §8 note).

**Where the mapping lives (one-framework rule):**
- `mu-core` keeps the layer palette and gives each entry a **hue name** (red, orange, lime, cyan, blue, magenta, rose,
  silver). It knows nothing about any controller.
- Each **driver in `mu-control`** owns a table from hue name to its device colours (Launchpad X: hue to a bright palette
  index and a dim palette index). A driver for a device with fewer colours maps by nearest hue.
- The **surface description** says how a device shows colour: none, a fixed palette, or full RGB. The Launchpad X can do
  both: palette entries (one MIDI message per pad) or **RGB via SysEx**, which also gives exact dimming.
- **Dim** (the "available clip" state) needs a darker partner of each hue. In the palette picture the dark entries are
  only about 65 to 70 % as bright as the bright ones (for example red 5 and 7, magenta 53 and 55, cyan 37 and 39, blue 45
  and 47, rose 57 and 59, lime 75 and 19), so **prefer RGB scaled to about 25 to 35 %** where the device supports it, and the dark
  palette entries only as a fallback. Check which looks right on the real pad.

Everything above is from the manual's picture and reading; the real LED colours, how distinct neighbouring hues look
in a dim room or on stage, and the dim levels all need checking on the device (backlog #1274).

### 3.2d Building `mu-control` (ruled 2026-10-13; backlog #1276)

Structure (folders, `mu_control`, `MUFAMILY_WITH_CONTROL`, `mu_control_attach`, the `ControlAttachment` seam, threads, ownership, boundary rules) is in
[design-plugin-family.md, Controller library](design-plugin-family.md#controller-library-mu-control--family-standard-ruled-2026-10-13-backlog-1276). This
section is what the four parts contain, what the first device maps, and the order to build them in. **Settled by Andy 2026-10-13: grid rows = layers,
columns = clips 1 to 8** (this closes the old open question in §5).

**The four parts (§3.1) and their supporting types, as data (shapes, not final signatures).**

| Part | File | Shape |
|---|---|---|
| Description | `Surface/SurfaceDescription.h` | `struct SurfaceDescription { juce::String id, name; int gridWidth, gridHeight; std::vector<SurfaceButtonGroup> buttons; ColourSupport colour (None, Palette, Rgb); bool canFlash, canPulse, hasPressure; }`. A button group is `{ kind: Top / Side / Special, count, label }`. The Launchpad X: 8 x 8, Top 8, Side 8, Special 1 (logo), `Rgb`, flash, pulse. Pure data, no behaviour. |
| Event and LED | `Surface/SurfaceEvent.h` | `SurfaceControlId { kind (Pad, Top, Side, Special), x, y }` with y counted from the **top** of the surface so the model and mapping never see a device's numbering; `SurfaceEvent { id, Press / Release / Value, float value }`; `LedState { id, uint32 rgb (final: dim scale and blink phase already applied), Mode (Static, Flash, Pulse) }`. With RGB the model animates (own timer, §3.6) and always sends `Static` colours; `Mode` is the intent a driver without RGB maps to its native flash or pulse. Colours come from `SurfaceLook` (design-plugin-family, item 4), so `mu-control` includes no UI header. The driver converts to and from its own bytes and numbering. |
| Driver | `Driver/SurfaceDriver.h` | `describe()`; `open(DevicePorts&)` / `close()` (settled in M2: the **driver owns the open and the close** of the ports it is given, which must outlive the open driver; `open` opens them by its own `PortSpec`, enters the host mode and installs the callbacks; `close()` restores the device's own mode on **every** path including the destructor, releases the ports and is harmless when repeated); `setLeds(const std::vector<LedState>&)` (one batched send, **delta lighting**: only what differs from what the device is known to show); `onEvent` callback (MIDI thread, see the threads rule); `isOpen()`. No `ControlAction`, no `LiveView`. |
| Ports | `Driver/DevicePorts.h` | The byte pipe (settled M2): `outputNames()` / `inputNames()`, `open(const PortSpec&)` / `close()` / `isOpen()`, `isConnected()` (opened device still attached), `send(data, size)` (one whole MIDI message or SysEx; false if it did not go out) plus `sendMessage(vector)`, and two callbacks: `onBytes` (raw bytes of one received message, MIDI thread) and `onConnectionChanged(bool)` (device vanished or returned, on the thread that noticed). `PortSpec { outputPattern, inputPattern }` are case-insensitive whole-name wildcards (`*`, `?`); the first match of each is opened and both must match or nothing stays open. `JuceDevicePorts` is the real one (M2; **M3 added the hot-plug watcher**: while open it polls the system port lists every 500 ms on the message thread, raises `onConnectionChanged(false)` when the opened pair vanishes, and on return opens a fresh pair, because the old handles are dead, before raising `true`; `isConnected()` is `isOpen() && attached`); `FakeDevicePorts` (Tests/) records sends, injects input and simulates `unplug()` / `replug()`. Drivers never include a JUCE MIDI port type directly, so they test with no hardware. |
| Mapping | `Surface/SurfaceMapping.{h,cpp}` | `SurfaceControlId -> ControlAction` rules plus the inverse for lights; the default is generated from the `SurfaceDescription` in code; only **user overrides** are saved, as a `juce::ValueTree` written as XML (the library already links `juce_data_structures`; no JSON layer, ruled 2026-10-13) in `<settingsDir>/mu-controller-mapping.xml` (ruled 2026-10-14: ONE file shared by the four standalone apps, because the settings folder the shell passes is the folder of the app's `.settings` file, which on Windows and Linux is the per-user data root itself, and the mapping is per device, not per app; the `mu-` prefix keeps it recognisable there; the first code used `controller-mapping.xml`, to be renamed, nothing writes the file yet) with a `version`, keyed by the description `id` (global per user and not in the preset, for the same reasons as `MidiControlMap`; actions saved by their stable names). |
| Model | `Surface/SurfaceModel.{h,cpp}` | Takes `ControlSink&`, `const LiveView&`, a `SurfaceDescription` and a `SurfaceMapping`. `handle(SurfaceEvent)` maps and performs; `render()` returns the `LedState` frame from `LiveView` and is diffed by the manager. Device independent: it asks the description what is possible and degrades (no flash: a muted playing clip shows as dim static; a narrower or shorter grid shows what fits). |
| Manager | `Manager/ControllerManager.{h,cpp}` | Finds a matching driver among the registered ones (by port-name pattern then device inquiry, with a user override of the port), opens it on entering Performance mode, closes it on leaving, drives the model from `LiveView` changes, retries on hot-plug. Implements `mu_core::ControlAttachment`. |

**`LiveView` additions (mu-core, M1).** The model needs three things `LiveView` does not yet give: `getLayerCount()`, `getLayerColourIndex(layer)` (the
index into the shared layer palette; the driver's `LaunchpadXPalette` turns it into a hue and the colour, §3.2b) and, with the clip bank's C5,
`isLayerMuted(layer)`, `getPlayingClip(layer)`, `getQueuedClip(layer)`, `getClipMask(layer)`. `LiveState` gets the first two through callbacks the owner supplies
(`layerCount()`, `layerColourIndex(i)`, the same pattern as `canLoad`), so it stays free of `ProcessorBase`; `ProcessorBase` wires them to `ProcessorCore`.
They are plain getters; nothing here changes how the screen works.

**Default mapping and lights for the Launchpad X** are ruled in §3.6 (top row = `PerformanceItem`, right column top to bottom = layers 1 to 8 = `SelectLayer`, grid row = layer and
column = clip = `LaunchClip`, no second page and no transport or Panic in version 1) and are not repeated. The default is generated in code by `SurfaceMapping` from the
`SurfaceDescription` (description coordinates, y from the top, so layer 1 is the top row, `y = 0`, via `layerToRow` / `rowToLayer` in `SurfaceMapping.h`; flipped 2026-10-14 after Andy's pad test so the pad matches the on-screen order); the driver owns the Launchpad's own numbering. The
"Controller" Settings group and the sticky `Lost` status line of design-ui-family §14 read `ControlAttachment` (status, device label, `release()`, `connect()`).

**Mock grid driver (the second-driver proof).** `Tests/MockGridDriver.h`, header-only and test-only: a 4 x 4 grid, 4 top buttons, **no side buttons, no flash or
pulse, palette colour only**, a different id and a different coordinate order (origin bottom-right) from the Launchpad X. It implements `SurfaceDriver` against
`FakeDevicePorts`. If the same `SurfaceModel` and `SurfaceMapping` drive both with no `if (device)` anywhere in `Surface/`, the layer is device independent;
`SurfaceModelTests` runs every case against both descriptions. This is the acceptance test of the architecture, not a convenience.

**Staged build order.** Each stage is one reviewable batch with its tests written in the same stage (test-steward owns the catalogue entry); all
tests are in `mu-control-tests` unless stated; every stage ends with `check-core-boundary.ps1` clean and a Debug build of `mu-control-tests` plus
`mu-core-tests` where `mu-core` changed. Hardware work (M3, M4) is run on the build PC with the Launchpad connected and recorded in §3.5.

| Stage | Work | Tests |
|---|---|---|
| **M1** library, description, mock, model (no hardware, no app changes) | Create `mu-control/` (CMake, `mu_control_attach`, `MUFAMILY_WITH_CONTROL` OFF, `mu-control-tests` in `ALL_BUILD`); `SurfaceDescription`, `SurfaceEvent`, `SurfaceDriver`, `SurfaceMapping` (default generation + ValueTree/XML round trip; `DevicePorts` and `FakeDevicePorts` move to M2), `SurfaceModel` (top row to `PerformanceItem`, side column to `SelectLayer`, lights and their blink animation from a `SurfaceLook`); `MockGridDriver`, `FakeDevicePorts`, `FakeLiveView`, `FakeControlSink`. In `mu-core`: `ControlAttachment.h`, `SurfaceLook.h`, the `LiveView` additions with `LiveState` callbacks and `ProcessorBase` wiring. Extend `check-core-boundary` (item 7 of the structure section). | `SurfaceDescriptionTests`; `SurfaceMappingTests` (default for both descriptions, override round trip, unknown action name and unknown description id ignored, version field); `SurfaceModelTests` on **both** descriptions (press maps to the right `ControlAction`, release does nothing, rows past `getLayerCount()` dark, lit item states, queued pulses or degrades per `canPulse`, listener change re-renders, a press with the sink returning false changes no light, dim = `dimScale` x bright, pulse and flash phases at the look's rates from an injected clock, blink animation only while something blinks); `mu-core-tests`: `LiveState` layer-count and colour callbacks, `ControlAttachment` factory default empty |
| **M2** `LaunchpadXProtocol` and `LaunchpadXDriver` against fake ports (**built 2026-10-13; reviewed by the architecture-steward: Pass on structure, with the findings recorded on #1276**) | `LaunchpadXProtocol` (numbering `10 * row + col` and CC 19..89 / 91..98 to `SurfaceControlId` and back; lighting messages; the batched lighting SysEx up to 81 entries; Programmer / Live / aftertouch-off / LED-feedback messages; device-inquiry reply recognition); `LaunchpadXPalette` (nearest palette index for a colour, used only when RGB lighting is unavailable; dim scale comes from `SurfaceLook`, not from here); `LaunchpadXDriver` over `DevicePorts`. **As built:** the driver defaults to RGB lighting (`LaunchpadXDriver(bool rgbLighting = true)`; `false` selects the palette forms with the device's own flash and pulse and reports `ColourSupport::Palette`); default ports `"LPX MIDI"` (output) and `"MIDIIN2 (LPX MIDI)"` (input); `open` sends Programmer mode then aftertouch off; `close` (and the destructor) lights all 80 pads and buttons off in one message, then sends Live mode; unplug marks the link down (lights are remembered, nothing sent) and a replug repeats the setup and the whole last frame; the logo is never lit and its CC 99 is ignored. | `LaunchpadXProtocolTests`: every pad and button round trip; note-on velocity 0 is a release; CC 127 / 0; exact bytes of every message above, including the §3.5 recorded ones (inquiry reply, `...0C 00` read, `...0E 01` / `00`, `...0B 02 01`); a 82nd lighting entry splits into a second message; malformed, short and foreign SysEx ignored. `LaunchpadXDriverTests` (fake ports): open sends Programmer mode then aftertouch off, close sends Live mode, input bytes become `SurfaceEvent`s with y from the top, `setLeds` sends only what changed, a closed driver sends nothing, a second `close()` is harmless |
| **M3** real device I/O, Programmer and Live on every exit path (**built 2026-10-14, 18 tests in `ControllerManagerTests`, run on the real pad via `mu-control-probe`; as built: see "M3 as built" below**) | `JuceDevicePorts` (first output pair for commands and lights, second input pair for pads, match by name pattern with override, §3.5); `ControllerManager` without the shell (a test and a small probe harness call it); hot-plug; `ControlStatus` (`NotFound`, `Found`, `InUse`, `Released`, `Lost`), `release()` and `connect()`; the connect sequence read back; all-pad lighting SysEx and RGB tried on the device; logo press behaviour and button up values recorded. **M3 must verify on the hardware what M2 could only assume from the manual (record each in §3.5):** the RGB lighting layout `F0 00 20 29 02 0C 03 <03 idx r g b>... F7` (channels 0..127, and the 81-entry limit per message); the palette fallback forms (static `00 idx pal`, flashing `01 idx B A` in that colour order, pulsing `02 idx pal`); the palette RGB values of `LaunchpadXPalette`; pads and buttons dark after `close` (the pad-off message before Live mode); the refresh after an unplug and replug; the logo press (CC 99) value and whether it needs the Programmer-mode setup. The driver's callbacks run on the MIDI thread or the thread that noticed a hot-plug while `setLeds` runs on the message thread: the manager must serialise them (hand events and connection changes to the message thread) before M3 passes. Programmer mode **only while Performance mode is on**; detection is by port name with nothing opened. | `ControllerManagerTests` with fake ports and a fake hot-plug source: status follows plug, unplug, enter, leave, release and connect (`Lost` only when in use and the device vanished; `Found` opens nothing); no port opened until `isPerformanceMode()` turns true; leaving the mode, destroying the manager, disabling it, an unplug-replug and a driver swap each send Live exactly once when open; nothing sent on a manager built and destroyed with the mode off; an event during teardown is dropped, never delivered to a destroyed sink. **Hardware (recorded in §3.5):** the connect and Live restore on quit, window close and OS-requested quit; RGB and batch lighting; the logo; two processes sharing the ports |
| **M4** the shell, top row, right column | `ControlRegistration.cpp`; the `StandaloneShell.h` hook, `SurfaceLook` fill and shutdown order; `ProcessorBase::setControlAttachment` and `ShellBackend::getControlAttachment`; the "Controller" Settings group and sticky `Lost` line of design-ui-family §14 (the `ux-controller` ruling is in; the build is the coder's, shared code in `mu-core/UI`); `mu_control_attach` calls in mu-Clid, mu-Tant, mu-Toni, mu-On; Launchpad X default mapping of §3.6; lights for the top row and the side column. The first thing Andy can play. | `mu-core-tests`: the shell creates an attachment only with the factory set, a non-null sink and view, `supportsPerformanceMode()` true and not in `--render`, and destroys it before the processor; the "Controller" Settings group exists only with an attachment and its Release / Connect call through (fake attachment). `ControllerManagerTests` end to end with `MockGridDriver` and a real `LiveState` (press top 3 queues item 2 through `ControlSink`; pressing the current item cancels a queued switch; a side press selects the layer and the lights follow). Hardware: top row switches presets in a real performance, side column selects layers, the screen follows, Live restored on exit |
| **M5** the clip grid (after the clip bank's C5) | Grid mapping to `LaunchClip`; playing, stored, empty, queued and muted lights; press on the playing clip mutes (and the queued-launch rule once Andy answers the clip bank's open question 6). | `SurfaceModelTests` on both descriptions for each pad light state and the mute press; a mock without flash shows muted as dim static; an unlaunchable state (full preset staged) shows pads dark; `LiveState` integration with fake callbacks |
| **M6** beyond version 1 (not scheduled; each item needs Andy, §3.6 and §5) | Top-row buttons 1 to 4 and 8 of §3.6 (`AppPrev / AppNext`, `PerformancePrev / Next`, transport toggle; Andy's table, 2026-10-14) move into version 1 with the multi-app switching; buttons 5 to 7 (`ClipNext` / `ClipPrev` / mute selected, recommended) once Andy picks. Still M6: `Panic` (`ProcessorBase` performs it), a second page, the logo heartbeat (`ProcessorBase` performs it), a second page with `ClipNext` / `ClipPrev`, the logo heartbeat and the MIDI clock out ([Device MIDI output](design-plugin-family.md#device-midi-output--family-standard)). | When scheduled |

**M3 as built (2026-10-14).**
- **Backend seam.** `Driver/ControllerBackend.h`: `struct ControllerBackend { unique_ptr<DevicePorts> ports; unique_ptr<SurfaceDriver> driver; PortSpec detect; }`, `ControllerBackendFactory`, and the one declaration `ControllerBackend makeDefaultBackend()`, defined in `Devices/LaunchpadX/LaunchpadXBackend.cpp`. `ControllerManager` takes a factory (default `makeDefaultBackend`), so `Manager/` includes no `Devices/` header (checked by `check-core-boundary`) and tests pass fake ports and `MockGridDriver`. A second device later becomes a list of factories here, not a change to the manager.
- **`ManagedPorts`** (`Manager/ManagedPorts.h`) wraps the driver's `DevicePorts`: the MIDI thread writes each message (3 bytes; longer ones such as SysEx replies are dropped) into a 256-slot lock-free FIFO and triggers an `AsyncUpdater`; connection changes use a short locked list; the message thread delivers connection changes first, then bytes, in order. `close()` stops the input first and drops everything pending, so nothing reaches a closed driver. This is the serialisation M2 asked of the manager: the driver and model run on the message thread only.
- **Lifetime.** `ControllerManager` opens the device only while `wantOpen()` (Performance mode on, not released, not quitting); a 1 Hz scan runs only while the device is not in use and reads port names without opening; the blink timer (about 30 Hz) runs only while a pad pulses. Release, leaving the mode, destruction and the quit hook all end in `closeDevice()`: pads dark, Live mode, ports closed. `Lost` is reported only for a device that was open and vanished.
- **Probe.** `mu-control-probe` (`mu-control/Tools/`, `EXCLUDE_FROM_ALL`, a developer console app, never shipped): port listing, numbered lighting and input steps, a manager demo and a colour lab, run by hand with the pad connected.
- **Dim level.** Chosen by eye on the pad: first 0.45 (0.30 was too dark to read on the uncorrected colours); **superseded 2026-10-14: 0.20** (`MuTheme::Lighting::padDimScale` and the `SurfaceLook::dimScale` default), applied after the device colour correction (§3.2b).
- **The quit hook** was ruled 2026-10-14 (design-plugin-family, Controller library item 4) and is built in M4.

**M4 as built (2026-10-14; architecture review Pass with minors, hardware acceptance pending).**
- **Shell wiring** (all `mu-core`, no `mu-control` include, no `#if`): `Control/ControlHost.h` (owns the attachment from the factory slot and the give-back order: `start`, `closeNow`, `shutdown`), `Control/AttachmentWatch.h` (a piece of UI follows the attachment pointer and is told when it dies), `Plugin/QuitFlow.h` (`mu_standalone::QuitFlow`: the Close / Save / Cancel answers in a testable order), `UI/SurfaceLookFromTheme.h` (`mu_ui::makeSurfaceLook()`, the pad colours read from the screen tokens), and `ControlAttachment::closeNow()`, `controlAttachmentDestroyed()` and `controlStatusText()`. `StandaloneShell.h` starts the host in the windowed branch only, calls `closeNow` through `onQuitConfirmed` on Close and Save (not Cancel) and as the first line of `shutdown`.
- **Screen:** `StandardSettingsOverlay` "Controller" group (via the new `Section::shown` predicate) and the `TransportBar` status line (`syncController`, run from its refresh timer).
- **Build:** `mu_control_attach` in the four Standalone targets; with `MUFAMILY_WITH_CONTROL` OFF (the default, so release builds and CI) it is a no-op, the factory slot stays empty, `ControlHost::start` returns false and no group or status line appears. The plugin formats are untouched apart from the inert `ProcessorBase` pointer.
- **Mapping file:** `<settings folder>/mu-controller-mapping.xml`, see the Mapping row in 3.2d.
- **Still to do for M4 acceptance:** run the four apps with the option ON on the real pad (attach, Release / Connect, hot-plug, Close and Save quit).

**Andy's decisions (each with the build assumption until he answers).** The pad order and the version 1 button set are already ruled in §3.6 (layer 1 is the top row, no
shift, no second page, no transport or Panic); the layer order is therefore not asked again.
1. **`MUFAMILY_WITH_CONTROL` default and whether released apps contain it.** Recommend OFF now, ON in the dev configuration after M4 passes on the hardware, and a release
   decision made then (the apps still never touch a port outside Performance mode).
2. **Programmer-mode lifetime is §5 question 2.** The build assumes the recommendation there (ports opened and Programmer mode entered only while Performance mode is on). Architecture
   consequence if Andy chooses the alternative (at app start): the manager opens the ports at construction instead of entry, `Release` becomes the only escape, and nothing else changes.
3. **Two apps in Performance mode at once** both receive the pad (the ports are shared, §3.5). Recommend leaving it until the mu-link focus idea (Performance mode item 7), and
   playing one app at a time meanwhile.

### 3.3 Things that matter on stage

- **Launch quantise.** A mute or clip change should land on the next bar, not when the finger lands (a clip launch
  already gets this from the shared hot-swap). Mute and select need the same one setting (off / beat / bar), resolved against the sync position.
- **Never fight the transport owner.** Transport pads are ignored (and visibly greyed) when a DAW or MIDI clock owns
  the tempo.
- **Timing.** Pad events arrive with the audio block they fall in; they must be applied with their sample offset
  (see the sync audit items), not at the start of the next block.
- **Mapping must be saved** in the preset or a family settings file (everything in APVTS / persisted state), and
  need a "learn" mode for other controllers later; the Launchpad profile is the first user of the general
  CC/note mapping, not a special case.

### 3.4 Risks to check on the build PC (R = risk)

| # | Risk | Why it matters |
|---|---|---|
| R1 | **Programmer mode is sticky.** *Checked 2026-10-10: a USB power cycle does reset it to Live.* The manual says Setup is disabled until we switch back; a crash leaves the device in our mode. | Send Live mode on shutdown, on a clean plugin unload, and offer a "release controller" button. Still send Live on clean exit; a crash is cleared by unplugging. |
| R2 | **Windows MIDI ports are often single-client.** *Checked 2026-10-10: not a problem with Windows MIDI Services; legacy WinMM and a DAW holding the ports untested.* If a DAW or another app has the Launchpad open, we cannot open it. Windows MIDI Services (newer Windows 11) allows several clients. | Likely means the standalone (or one combined instance) owns the controller; a plugin in a DAW may not get the port. It also explains why running four apps at once cannot each use the pad: another point for one instance. |
| R3 | **Which port is which.** *Checked 2026-10-10: see Hardware findings; input and output roles are split across the two pairs.* DAW vs MIDI port naming differs on Windows. | Match by name and let the user choose; read back after connecting. |
| R4 | **LED traffic.** *Checked 2026-10-10: polyphonic aftertouch is ON by default in Programmer mode (about 100 messages/s per pressed pad); the driver must switch it off.* One lighting SysEx per tick is small; polyphonic aftertouch is not. | Batch LED updates, default aftertouch off. |
| R5 | **Plugin form.** *Checked 2026-10-10: shared ports work between two processes on this machine; DAW-held ports untested.* In a DAW the plugin gets pad notes only if the DAW routes them, and cannot light pads unless it opens the output itself (see R2). | Standalone first; plugin support after a hardware check. |
| R6 | **Switching a whole preset live.** Loading eight layers of samples, wavetables and FX in one go can stall or click, and FX tails and held voices must not be cut off. | Use the existing full-preset hot-swap (parsed off the audio thread, committed at the bar); check on the build PC that FX tails and held voices survive the commit. Measure the load time of the largest preset on the build PC before promising a bar-accurate switch. |
| R7 | **Preloading a performance.** Holding all 8 presets ready to switch costs memory and CPU for 64 layers. | Preload only the next likely preset (neighbour or chosen) and load the rest on demand; set a memory budget and show a "loading" state on the button. |
| R8 | **Colours differ between screen and pad.** *Partly checked 2026-10-14: dim level set to 0.45 by eye; whether each layer colour matches its screen colour is still being judged by eye with the probe colour lab and the ControlDesign "Surface colours" window (feeds #1281).* The palette picture is not the real LED output, and some hues (orange, silver, brown) shift. | Pick the eight colours on the real device, check dim levels, and keep the choice in one shared table with a per-driver hue map (§3.2b). |

### 3.5 Hardware findings (Launchpad X on the build PC, 2026-10-10)

From a throwaway JUCE 9.0.3 probe on Windows 11 with Windows MIDI Services running (MidiSrv.exe).

- **Ports (R3).** Inputs "LPX MIDI" and "MIDIIN2 (LPX MIDI)"; outputs "LPX MIDI" and "MIDIOUT2 (LPX MIDI)". The first pair answers the device inquiry `F0 7E 7F 06 01 F7` with `F0 7E 00 06 02 00 20 29 03 01 00 00 00 04 02 02 F7` (manufacturer 00 20 29, family 03 01, firmware 00 00 04 02 02) and takes the layout read (`...0C 00`) and the mode switches. **Pad and button input arrives on the second input, "MIDIIN2 (LPX MIDI)", not the first.**
- **Mode (R1).** Programmer mode `F0 00 20 29 02 0C 0E 01 F7` works and reads back 01; Live (`...0E 00`) restores. The mode does not survive a USB power cycle (read back Live).
- **Input.** Pad note = 10*row + col (bottom row 11-18 seen). Press = note-on with velocity (64-115 seen); release = note-on velocity 0 (JUCE reports a note-off). Edge buttons are CC on channel 1, press 127 and release 0: top row CC 91-98, right column CC 19, 29, ... 89.
- **Aftertouch (R4).** Polyphonic aftertouch is on by default in Programmer mode. `F0 00 20 29 02 0C 0B 02 01 F7` turns it off (none seen afterwards); the driver must send it.
- **Lighting.** Static (channel 1), flashing (channel 2) and pulsing (channel 3) note-on lighting on pads 11/12/13 with palette 5/21/45 showed red, green flash and blue pulse; confirmed by eye. RGB lighting is verified (2026-10-14, see below).

**Verified on the real pad with `mu-control-probe`, 2026-10-14 (M3):**
- **RGB lighting layout** `F0 00 20 29 02 0C 03 <03 idx r g b>... F7` works, including a full **80-entry batch** in one message (64 pads, 8 top, 8 side); all-pads-off before Live mode leaves the pads dark.
- **Input decode** of pads (note 10*row + col), top row (CC 91-98) and side column (CC 19..89) matches the driver's numbering; press and release values as recorded above (pad release is velocity 0, buttons 127 / 0).
- **Live mode is restored** on every exit path the manager has (leave Performance mode, Release, destruction, quit hook).
- **Dim level:** 0.45 chosen by eye at that time (0.30 too dark); now 0.20 after the colour correction, see §3.2b.
- **Not yet recorded:** the logo press (CC 99) value, the palette fallback forms (static, flashing, pulsing in RGB-less mode), a DAW holding the ports, and the screen-versus-pad colour match (judged by eye with the probe colour lab and ControlDesign's Surface colours window).
- **Sharing (R2, R5).** Two processes opened the same input and output pair at once without failure, so a plugin-hosted driver beside a standalone one is feasible here. A WinMM-only machine was not tested.

**Still open:** the R8 layer-colour match with the screen; the logo press; the palette fallback forms; behaviour with a DAW holding the ports.

**Recommendation:** the `LaunchpadXDriver` opens the first output pair for commands and lighting, listens on the second input for pads and buttons (match by name, let the user override), sends Programmer mode and aftertouch-off on connect, and Live on clean exit.

### 3.6 Surface mapping defaults for the Launchpad X (ruled 2026-10-13; backlog #1276)

**Top row of round buttons (CC 91-98), settled by Andy 2026-10-14; replaces the earlier "top row = `PerformanceItem(1..8)`" and the purple Performance-mode / item-prev / item-next buttons.** Left to right:

| # | Action | Light (RGB; bright = token colour, dim = `padDimScale` 0.20) |
|---|---|---|
| 1 | Up arrow: previous mu app (`AppPrev`) | The **current app's accent** (`MuTheme::AppPrimaries`, as the screen lamps): bright when another app is available; **fully off (dark) and the press ignored** when this is the only app (ruled 2026-10-14: dark, as Andy said; the earlier "dim" is dropped) |
| 2 | Down arrow: next app (`AppNext`) | Same colour as 1, same rule |
| 3 | Left arrow: previous preset (`PerformancePrev`) | **Rest: white x `kArrowRestScale` 0.75** (untuned, may be revisited by eye); flash and queued-pulse peak are full white; off when fewer than two playable items |
| 4 | Right arrow: next preset (`PerformanceNext`) | Same as 3 |
| 5 | Previous clip (`ClipPrev`), **provisional, awaiting Andy** | Layer-less neutral per the build; not yet confirmed |
| 6 | Next clip (`ClipNext`), **provisional, awaiting Andy** | As 5 |
| 7 | Mute selected layer (`MuteSelectedLayer`), **provisional, awaiting Andy** | The selected layer's colour: **dim = unmuted, flashing at `clipMutedFlashHz` = muted** |
| 8 | Play / Stop toggle (`TransportToggle`) | **Green when stopped** (a press will play), **red when playing** (a press will stop): the light shows what pressing does |

The multi-app switching mechanism (what "another app is available" means, how the switch is made) is ruled in [design-plugin-family.md](design-plugin-family.md); this table is only the surface's half.

- **Colours.** Buttons 1 and 2 carry the app accent so the pad always says which app has the surface; they change colour the moment the app changes. Buttons 3 and 4 are white (neutral) so the two groups read apart and so no app colour means two things. Button 8 uses `MuTheme::SurfaceColours::go` (new, green `0xff3CD070`, a transport colour that is not one of the four app primaries) when stopped and `indicatorFault` red when playing; both bright, steady, never dim except as below. New token, `mu-core/UI/Components/MuTheme.h`; the driver reads it through `SurfaceLook`, not a literal.
- **Arrows 3 and 4 wrap, as the screen does (12.2): previous from the first goes to the last playable item, empty and missing items are skipped.** They do not dim at the ends (a performance is a loop on stage; a dark button at the end reads as broken). They are off only when there is nothing to go to (under two playable items).
- **Press feel.** An accepted press on 1 to 4 flashes the button white for one `stepFlashMs` (150) and returns to its steady light; a press the sink refuses (no other app, mode off) changes no light, as everywhere else (3.6 rule). While a preset switch is queued or loading, the button that was pressed (3 or 4) pulses at `queuedPulseHz` in white, the on-pad twin of the queued lamp and SWP badge. Button 8 needs no flash: its colour changes at once when the transport changes, which is the confirmation.
- **Arrows 3 and 4 while a switch is queued step from the queued item**, so two presses skip two; the press that returns to the playing item cancels the queue (this replaces "press the current item to cancel", which had a pad for each item). The status line is unchanged (`Queued` / `4 Acid Line (...)`).
- **Rest light of 3 and 4 is not full white** (ruled 2026-10-14): a bright-white rest would hide a white press flash, so rest is white x 0.75 (`kArrowRestScale`) and the flash and queued pulse go to full white.
- **Multi-app arrows 1 and 2 stay dark and inert until stage F1** of the architecture ruling ([design-plugin-family.md, Several apps, one pad](design-plugin-family.md)); until then only one app exists for the pad, so the "only app" rule above always applies.
- **Pages in mu-Tronic (Andy, 2026-10-15: "the Launchpad will be important for this").** In a product whose layer count exceeds `kLayersPerPage` (mu-Tronic: 16 layers, pages of 8),
  **buttons 5 and 6 are `PagePrev` / `PageNext`**, not `ClipPrev` / `ClipNext`; the grid rows and the right column then mean the layers of the current page. The rule is derived
  from the page count (more than one page), never from the product name, so the surface description stays generic. The four existing apps (one page) keep 5 and 6 as in the
  table. Clips are launched from the grid, so mu-Tronic loses nothing. `PagePrev` / `PageNext` are new device-independent `ControlAction`s ([mu-tronic/design.md §4](mu-tronic/design.md)).
- **Scenes in mu-Tronic (ruled 2026-10-21; the "no default pad" item is closed).** A scene is `LaunchScene` (built, `mu-core/Control`): clip n on every layer that stores one, across both pages. All 16 edge buttons are spoken for (1 and 2 stay `AppPrev` / `AppNext`, dark and inert until a second app exists but needed to leave mu-Tronic then; 3 and 4 presets; 5 and 6 pages; 7 mute; 8 transport) and no shift or second page exists, so the scene gesture is **hold the logo (CC 99) and press any pad in column n**. While the logo is held the grid shows the scenes as whole columns in white (playing full, stored x `kArrowRestScale`, queued pulsing at `queuedPulseHz`, none stored off) and the logo itself lights white; releasing it restores the grid. Pad releases during the hold do nothing; a logo tap does nothing. Status line `Scene` / `Clip 3 on 9 layers`. The hold is a surface-model state in the mu-Tronic profile in `mu-control`, not a `ControlAction`. **Hardware check still owed:** whether the logo reports a press (CC 99, value 127 / 0) on a Launchpad X, which is also the open item in 3.5. **Fallback if it does not:** hold top-row 7 and press a pad in column n, a tap on 7 still mutes (fires on release when no pad was pressed during the hold), lights unchanged. Full wording and the screen twin (the sidebar's Scene pads): [mu-tronic/design-ui.md 3.4a and 3.7](mu-tronic/design-ui.md). This is a documented per-product exception to "no modifier in version 1": a hold that only changes what a pad press means, and releases cleanly.
- **Provisional, awaiting Andy:** buttons 5 to 7 above, the loss of direct jump to item n, and Performance-mode entry and exit being screen-only. Andy has not adopted any of them yet.
- **Probe demo.** `ProbeMain.cpp` still shows the old top row; it is stale and the coder should update it to this table.
- **Direct jump to preset n is gone** (provisional; the eight item pads are replaced by two arrows; Andy's table). The screen's `StepIndicator` still shows all eight lamps. `PerformanceItem(n)` stays in the `ControlAction` vocabulary for MIDI mapping; the Launchpad default no longer uses it.
- **Play / Stop and a clock owner.** Button 8 goes through `mu_core::resolveTransport` like the screen Play (12.7). When a host, MIDI clock or Link owns the transport, button 8 is a **static dim white and the press is ignored** (no flash, no action); grey is the one colour no other state uses. In the Standalone with the internal clock it is always green or red.
- **Where Performance mode is entered and left.** Not on the pad. The device is in Programmer mode only while the mode is on (3.5, question 2), so there is no lit pad to press before the mode starts, and an exit by a stray pad press on stage is the failure the 1 s hold on the screen exists to prevent. **Enter and exit stay on the screen** (the Perf button, Ctrl+P, the hold to exit, design-ui-family 12.5). The old purple Performance-mode button is removed with no replacement.
- **The logo (CC 99) stays unlit and has no action in version 1** (its press is still unverified on hardware, 3.5), **except in mu-Tronic, where holding it is the scene gesture above** (lit white while held). The earlier logo-as-Play/Stop plan is superseded by button 8. The logo is the one remaining candidate for a "hold to leave Performance mode" later; not recommended (point above).

**Buttons 5, 6, 7: candidates (Andy picks one set; recommended first).** Each has to be safe to press by mistake on stage and stays inside the screen-lock list (design-ui-family 12.7): it may only do what a pad already does.

| # | Recommended | Alternatives |
|---|---|---|
| 5 | **Clip previous** on the selected layer (wraps, skips empty; queued like a pad launch). Lit white like buttons 3 and 4 (a navigation arrow). | Layer previous (redundant with the right column); **Tap tempo** (flagged: Tap is Locked in 12.7, one stray tap rewrites the tempo; only if Andy overrules the lock) |
| 6 | **Clip next** on the selected layer (same rules). | Layer next (redundant); Tempo nudge (not on the surface list) |
| 7 | **Mute the selected layer** (quantised, same as pressing the playing clip, lit in the layer colour: bright playing, flashing muted). | Mute all layers (flagged: a stray press silences the whole band, a Panic by another name, 12.7 says no Panic on a pad); Tap tempo (as 5) |

Clip previous / next need `ClipPrev / ClipNext` actions (listed as M6 below); they would move into the version 1 set if Andy picks the recommendation.

**Settled by Andy (2026-10-13):** grid rows = layers, columns = clips 1 to 8; right column (CC 89..19, top to bottom = layers 1..8; flipped 2026-10-14 from bottom-up to match the screen) = `SelectLayer`. This is the default mapping in `mu-control`; it produces the same `ControlAction`s as the screen, so every rule below is the on-surface twin of [design-ui-family.md sections 12 and 13](design-ui-family.md#12-performance-mode-ruled-2026-10-12-revised-2026-10-13-with-the-owners-decisions-backlog-1279). The screen half (Settings group, status line) is [section 14](design-ui-family.md#14-controller-status-and-release-ruled-2026-10-13-backlog-1276).

**Colour source and method.** Layer colours are `MuLookAndFeel::layerPalette` (hue names, 3.2b); item colours are the app accent. The driver sends **RGB** (lighting SysEx type 3, batched, changed pads only); the palette index through the hue table is the fallback for a surface without RGB. **Bright** = the token colour; **dim** = the colour scaled by new `MuTheme::Lighting::padDimScale` **0.45** (chosen by eye on the real pad 2026-10-14; 0.30, the 25 to 35 % of 3.2b, was too dark). **Blink is driven by the driver's own timer, not the device's flash/pulse modes**, so the pads match the screen exactly and do not depend on a MIDI clock being sent: pulse = smooth between dim and bright at `queuedPulseHz` (2 Hz); flash = square 50 % between bright and dim at `clipMutedFlashHz` (1 Hz). The device's native modes are the fallback for a driver that cannot send RGB.

| Control | State | Light |
|---|---|---|
| Grid pad (layer row, clip column) | Playing clip | Bright, layer colour |
| | Stored, not playing | Dim, layer colour |
| | Empty | Off |
| | Queued | Pulsing, layer colour |
| | Playing clip of a muted layer | Flashing bright / dim, layer colour (the pad never goes off, so it never reads empty) |
| | Just became playing | One bright flash of `stepFlashMs` (150), then steady (as the screen) |
| | Row with no layer | Whole row off |
| Top row | Buttons 1 to 8 | See the top-row table above (the eight item pads and their current / queued / missing states are retired 2026-10-14; those states now show only on the screen's `StepIndicator`) |
| Right column (layer n) | The selected layer | Bright, that layer's colour |
| | Any other existing layer | Dim, that layer's colour |
| | No layer | Off |

- **Loading is not a separate state in v1.** From the press until the swap commits the pressed arrow (3 or 4) pulses; that covers waiting for the boundary and parsing the preset off the audio thread (both end at the commit). A distinct "parsing" cue is deferred until the largest preset's load time is measured on the build PC (R6, R7); see question 5.
- **A preset arrow press while a switch is queued steps from the queued item and replaces it; stepping back to the playing item cancels it** (see the top-row bullets; supersedes 12.2's "press the current item"). Pressing the playing clip mutes, pressing it again unmutes; with another clip queued on that layer it cancels the queue (13.3). Pressing an empty pad does nothing (a pad never writes). A press on an off pad or an absent layer does nothing and shows no light.
- **Muted layers on the right column** are not shown (the column shows selection only; the grid shows mute).
- **No shift, no modifier, no second page in version 1.** The edge buttons and all 64 pads are used by the settled mapping (top buttons 5 to 7 aside, above), and the screen is locked in Performance mode (except the exempt list, design-ui-family 12.7), so nothing else is reachable and nothing can go wrong with a half-pressed modifier.
- **Transport: top button 8 (ruled 2026-10-14, Andy)**, not the logo; rules in the top-row bullets above. **Panic has no pad** (a stray press on stage that kills the sound is worse than the problem it solves); a stuck note is cleared with Stop (button 8 or the screen), or from the PC after leaving the mode.
- **The logo stays unlit in version 1** (the beat heartbeat of 3.2 waits for the clock-out fixes).
- **Disconnect or lost device:** the pads go dark by themselves; the app shows the sticky status line of design-ui-family 14. Reconnect (the port reappears) re-sends Programmer mode, aftertouch off and a full light refresh.
- **Programmer mode lifetime** is question 2; the recommendation is that the device is in Programmer mode **only while Performance mode is on**, and Live mode is sent on leaving it, on release, on device removal where possible, and on clean exit (R1).

## 4. Order of work (all on the build PC; the owner said nothing is coded today)

1. **Hardware check** (§3.4): list the port names, try the Programmer-mode switch and a lighting SysEx from a small
   console test, confirm button up values, power-cycle behaviour and whether two apps can open it.
2. **MIDI input mapping in `mu-core`** (CC / note to parameter or action, learn, saved), which the Launchpad and any
   other controller will use.
3. **The controller layer, in a new `mu-control` library** (§3.2a) (driver interface, surface description, mapping, surface model) with the `LaunchpadXDriver`: lights out, the clip grid, layer select / mute, transport. Built in stages M1 to M6 (§3.2d). The mock grid driver is the second driver and the proof the layer is device-independent.
4. **The per-layer clip bank** (eight clips a pad can address, saved with the preset) and the generic launch actions on the parent `Layer`, as it and its derived types land.
5. **Live full-preset switch** through the existing full-preset hot-swap (check that FX tails and held voices survive the commit; measure first, R6) and the **Performance** level above it (a list of eight preset pointers, R7).
6. **Clock out** to the device for flashing / pulsing (after the clock-out fixes).

## 5. Open questions (owner)

**Decided (owner, 2026-10-09):** a performance is a list of **pointers** to preset files; the launchable things are
called **clips** (as in Ableton and Bitwig; "slot" stays for FX slots); **clips play until another is selected** (no automatic chain); all switching goes through the
existing **hot-swap**; pad lights are playing = bright, available = dim, empty = off, queued = pulsing, muted = flashing; pressing the playing clip mutes and pressing it again unmutes. **A pad never writes a preset** (no shift + pad store); clips are stored from the screen.

**Still open:**

**Decided (owner, 2026-10-13):** standalone only to start; rows = layers, columns = clips; top row = performance items, right column = select layer (3.6).

**Open from the 3.6 ruling (Andy):**

1. **Buttons 5, 6, 7** (3.6): recommended clip previous, clip next, mute selected layer. Tap and mute-all are flagged against the screen-lock list. (Transport is decided: button 8, 2026-10-14; Panic never on a pad.)
2. **When does the device enter Programmer mode?** Recommendation: only while Performance mode is on (the device is a normal Launchpad the rest of the time and a crash cannot strand it in our mode, R1). The alternative is at app start whenever a device is found, so the pads light before you enter the mode; it needs the Release button to be the real escape.
3. **Dim level (now 0.20, see §3.2b) and own-timer blink** are by-eye choices; confirm or adjust on the pad (R8, #1274).
4. **Top-row colour:** buttons 1 and 2 use the app accent (Andy's table); 3 and 4 are white. If the app accent and a layer colour look alike on the real LEDs, say so.
5. **A distinct "parsing" cue** on a top-row pad (for a slow preset load) is not in version 1; revisit after the load-time measurement (R6, R7).
- Which other actions matter live besides clip launch and mute: tempo, a filter sweep on a pad?
- Which other controllers do you have or plan (so the first second driver is a real one)?
