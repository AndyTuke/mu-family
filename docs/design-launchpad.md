# μ Family — Playing Live from a Novation Launchpad X

**Status: research and plan, 2026-10-09. Nothing built, nothing tried on hardware** (work laptop, no device).
Part 1 is read from Novation's *Launchpad X Programmer's reference manual*
([the PDF](reference/Launchpad-X-Programmers-Reference-Manual.pdf), converted to text and read in full, including the
two pad-layout and colour-palette pictures). Part 2 is our proposal and is not decided.

Why it matters: playing live with other musicians means hands on pads, not a mouse. The Launchpad X is the
owner's controller. It also fits the one-framework direction in [design-future.md](design-future.md): the
controller code goes **once into `mu-core`** as a device-independent controller layer, so other surfaces slot in with one driver and every app gets them (§3).

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
| Map a control to an action or knob | Only Program Change to preset (`MidiPresetMap`, `MidiFullPresetMap`). | **No CC / note to parameter mapping** (design-future: "MIDI CC remote control", 🟡). This is the main missing piece. |
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
| **Preset** | A full preset (`.muClid`, `.muTant`, ...): up to **8 layers** and their mixer, FX and modulation. | **Top row of buttons (8)** = preset 1-8 of the performance. | Yes, one at a time, with full-preset hot-swap. |
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

**Edge buttons** (a Launchpad X has only 16 plus the logo, so the rest go on a second page behind shift; a surface
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
are unrelated); the `Layer` and `Clip` names should be settled together when #1265 is done. A *clip* is a stored state
of one layer, so it is not an audio clip.

### 3.2a Where the code lives: a separate `mu-control` library (owner deferred to this recommendation, 2026-10-09)

Controller code goes in a new **`mu-control`** library, a sibling of `mu-core` (a library, not a product). It is created
when #1276 starts, not before, so there is no empty target.

| In `mu-core` (small, stable, no devices) | In `mu-control` (devices) |
|---|---|
| The **action vocabulary** (launch clip, mute, select layer, transport, panic, preset prev / next). | The **driver interface** and the Launchpad X driver (and later others). |
| A **control sink** interface that `ProcessorBase` and the layers implement, so actions reach them. | The **surface description**, the saved **mapping** and the **surface model**. |
| A read-only **"what to show"** interface (playing, queued and muted clips, layer colours, beat). | Opening the MIDI ports, and sending lights on a timer from the message thread (directly, not through mu-core's `TimedMidiOut`; a driver that also sends MIDI clock lends its port to one, see [Device MIDI output](design-plugin-family.md#device-midi-output--family-standard)). |
| The generic **MIDI CC / note to parameter mapping** (#1275): any MIDI-in feature needs it, it is not device-specific. | |

Why separate: (1) it is **optional per build**, since controllers need OS MIDI ports that a plugin inside a DAW may not
get (R2) and a freeware build may not want them, the same reason Link sits behind a build option; (2) it **keeps growing**
with every new device, which `mu-core` should not; (3) the boundary can be **enforced**, as it is for products; (4) there is
precedent: the small `MuLinkBridge.h` sits in `mu-core` and the heavy parts are in `mu-link`.

The dependency is one-way and checked by `tests/scripts/check-core-boundary.ps1`: **`mu-control` may use `mu-core`;
`mu-core` must never include `mu-control`; neither may name a product.** Because `mu-core` cannot depend on
`mu-control`, `ProcessorBase` does not own a controller: the standalone shell (or the editor) creates the controller
manager on the message thread and connects it to the sink and the "what to show" interface above. A product that wants no
controller support simply does not link `mu-control`.

### 3.2b Layer colours must come from the controller's palette (owner, 2026-10-09)

The pad for a layer must be the same colour as that layer on screen. The owner's rule: **pick the layer colours from
the colours the controller can show, and stick to common hues** so most other pad controllers are covered too (some
will not match exactly; a common hue still degrades gracefully).

**Today:** the family has one shared 8-colour layer palette, `MuLookAndFeel::channelPalette` (Red, Cyan, Orange,
Magenta, Lime, Rose, Silver, Copper), used by all four products and documented in
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
| **Copper** `#B87445` | 11 (`#B4765D`) | **Poor**: brown does not exist on an LED pad; it would look like a dim orange, next to Orange. |

**Recommendation:** keep the shared palette, but **re-pick each colour so its on-screen value is exactly a Launchpad
palette entry**, and **replace Copper with Blue** (entry 45, `#5B5DFD`), a hue every RGB pad has. Seven of the eight keep
their hue, so saved colour indices keep meaning (a voice saved with index 7 just becomes blue). Open check: blue and
three other common hues are also the **app primary** colours the palette was built to avoid (so a layer highlight
does not blend into its app's paint, see `MuTheme::AppPrimaries`); try the new set on screen on the build PC before
committing to it.

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
| R8 | **Colours differ between screen and pad.** *Open: owner-attended comparison still to do (feeds #1281).* The palette picture is not the real LED output, and some hues (orange, silver, brown) shift. | Pick the eight colours on the real device, check dim levels, and keep the choice in one shared table with a per-driver hue map (§3.2b). |

### 3.5 Hardware findings (Launchpad X on the build PC, 2026-10-10)

From a throwaway JUCE 9.0.3 probe on Windows 11 with Windows MIDI Services running (MidiSrv.exe).

- **Ports (R3).** Inputs "LPX MIDI" and "MIDIIN2 (LPX MIDI)"; outputs "LPX MIDI" and "MIDIOUT2 (LPX MIDI)". The first pair answers the device inquiry `F0 7E 7F 06 01 F7` with `F0 7E 00 06 02 00 20 29 03 01 00 00 00 04 02 02 F7` (manufacturer 00 20 29, family 03 01, firmware 00 00 04 02 02) and takes the layout read (`...0C 00`) and the mode switches. **Pad and button input arrives on the second input, "MIDIIN2 (LPX MIDI)", not the first.**
- **Mode (R1).** Programmer mode `F0 00 20 29 02 0C 0E 01 F7` works and reads back 01; Live (`...0E 00`) restores. The mode does not survive a USB power cycle (read back Live).
- **Input.** Pad note = 10*row + col (bottom row 11-18 seen). Press = note-on with velocity (64-115 seen); release = note-on velocity 0 (JUCE reports a note-off). Edge buttons are CC on channel 1, press 127 and release 0: top row CC 91-98, right column CC 19, 29, ... 89.
- **Aftertouch (R4).** Polyphonic aftertouch is on by default in Programmer mode. `F0 00 20 29 02 0C 0B 02 01 F7` turns it off (none seen afterwards); the driver must send it.
- **Lighting.** Static (channel 1), flashing (channel 2) and pulsing (channel 3) note-on lighting on pads 11/12/13 with palette 5/21/45 showed red, green flash and blue pulse; confirmed by eye. The all-pad lighting SysEx (`...0C 03 ...`) and RGB lighting are not yet tested.
- **Sharing (R2, R5).** Two processes opened the same input and output pair at once without failure, so a plugin-hosted driver beside a standalone one is feasible here. A WinMM-only machine was not tested.

**Still open:** the R8 colour and dim comparison (owner-attended); the all-pad lighting SysEx and RGB test; behaviour with a DAW holding the ports.

**Recommendation:** the `LaunchpadXDriver` opens the first output pair for commands and lighting, listens on the second input for pads and buttons (match by name, let the user override), sends Programmer mode and aftertouch-off on connect, and Live on clean exit.

## 4. Order of work (all on the build PC; the owner said nothing is coded today)

1. **Hardware check** (§3.4): list the port names, try the Programmer-mode switch and a lighting SysEx from a small
   console test, confirm button up values, power-cycle behaviour and whether two apps can open it.
2. **MIDI input mapping in `mu-core`** (CC / note to parameter or action, learn, saved), which the Launchpad and any
   other controller will use.
3. **The controller layer, in a new `mu-control` library** (§3.2a) (driver interface, surface description, mapping, surface model) with the `LaunchpadXDriver`: lights out, the clip grid, layer select / mute, transport. Test in mu-On or mu-Clid first. A second driver (any other grid) is the proof the layer is device-independent.
4. **The per-layer clip bank** (eight clips a pad can address, saved with the preset) and the generic launch actions on the parent `Layer`, as it and its derived types land.
5. **Live full-preset switch** through the existing full-preset hot-swap (check that FX tails and held voices survive the commit; measure first, R6) and the **Performance** level above it (a list of eight preset pointers, R7).
6. **Clock out** to the device for flashing / pulsing (after the clock-out fixes).

## 5. Open questions (owner)

**Decided (owner, 2026-10-09):** a performance is a list of **pointers** to preset files; the launchable things are
called **clips** (as in Ableton and Bitwig; "slot" stays for FX slots); **clips play until another is selected** (no automatic chain); all switching goes through the
existing **hot-swap**; pad lights are playing = bright, available = dim, empty = off, queued = pulsing, muted = flashing; pressing the playing clip mutes and pressing it again unmutes. **A pad never writes a preset** (no shift + pad store); clips are stored from the screen.

**Still open:**

- Standalone only to start, or a plugin in a DAW too (R2)?
- Rows = layers and columns = clips, or the other way round?
- Which other actions matter live besides clip launch and mute: tempo, a filter sweep on a pad?
- Which other controllers do you have or plan (so the first second driver is a real one)?
