# μ-tant — X-Mod section design

Compact cross-modulation panel for the two-oscillator (osc 1 → osc 2) section.
Goal: cover more modulation types than the current FM / AM / Ring-mod layout while
using *fewer* on-screen controls.

> **Implemented.** Both lanes ship, including SSB, laid out as two horizontal rows
> (knob · mode switch · toggles). As-built notes:
> - **Lane B is a 3-way switch — AM / RM / SSB.** AM keeps the carrier (`a·(1+depth·b)`);
>   RM crossfades dry→ring so the carrier is suppressed at full (`a·(1−k)+sgn·k·(a·b)`); SSB
>   frequency-shifts via the Hilbert split. (The first-cut spec framed AM↔RM as a knob morph;
>   discrete AM/RM modes read more clearly against the mockup.)
> - **Depth knob is bipolar, centre = OFF**; sign flips the modulator phase.
> - **SSB shift is its own param** (`xmod_ssb`, −2..+2 kHz) that the Depth knob drives in SSB
>   mode (attachment swaps with the mode), so AM/RM depth and SSB shift each persist.
> - **Feedback is a fixed-depth toggle** (conservative), per the "conservative fixed depth" note.
> - **Click-free:** the continuous controls (index / depth / SSB shift) are one-pole smoothed
>   (~5 ms); a full dual-path crossfade on discrete mode switches is a possible later refinement.
> - Modulation destinations: `xmod.index`, `xmod.depth`, `xmod.ssb`. Old `xmod.fm/.am/.ring`
>   params + assignments are best-effort migrated on preset load.

## Design principle

Group by **modulation destination (bus)**, switch *within* a bus, run buses *in parallel*.

- Modulations that write to the same bus are mutually exclusive → collapse them onto one
  knob + a mode switch (you never want two at once; they share a depth control).
- Modulations that write to different buses are additive → keep them on separate, always-live
  lanes so combinations (e.g. FM + AM at once) still work.

Result: 3 current sections → **2 lanes**, while gaining PM, TZFM and SSB at no extra panel cost.

## Lane A — Phase / index bus

One **index** knob + a 3-way mode switch, plus two independent toggles.

| Mode | What it does | Notes |
|------|--------------|-------|
| FM   | True frequency modulation of osc 2 | Modulator DC can drift osc 2 pitch — fine for noise/FX, risky for held drones |
| PM   | Phase modulation (adds modulator to osc 2 phase index) | Stays in tune regardless of modulator DC; this is what "DX-style" FM actually is. Recommend as the default mode for a drone instrument |
| TZFM | Through-zero FM/PM (modulator may push effective freq negative) | Cleaner, in-tune spectra at high index; the "bell that doesn't go sour" |

Independent toggles (do **not** fold into the mode switch — their amount isn't the index knob):

- `sync` — hard-sync osc 2 phase reset from osc 1. "Amount" is osc 2's tuning ratio (lives on
  osc 2's pitch control). Coexists with FM/PM/TZFM, which is why it's a separate toggle.
- `fdbk` — mutual / feedback FM (osc 2 also modulates osc 1). Needs a 1-sample delay in the loop;
  gets chaotic fast, so give it its own small amount or a conservative fixed depth.

## Lane B — Amplitude / multiply bus

One **depth** knob + a 3-way mode switch.

| Mode | What it does | Spectrum |
|------|--------------|----------|
| AM   | Amplitude modulation (modulator offset to stay ≥ 0) | carrier + (f_c − f_m) + (f_c + f_m) — carrier survives |
| RM   | Ring modulation (bipolar multiply) | (f_c − f_m) + (f_c + f_m) only — carrier suppressed, clangorous/inharmonic |
| SSB  | Single-sideband / frequency shift (Hilbert/quadrature) | one sideband only; shifts every partial by a fixed Hz → inharmonic shimmer. Standout for sustained drones |

AM and RM are the *same* multiply with/without the carrier offset, so prefer a continuous
**morph** rather than a hard A/B (this also preserves "both at once" — the in-between is partial
carrier passthrough). SSB stays a discrete mode that repurposes the same knob as shift amount.

## Proposed parameter mapping (first cut — tune to taste)

Phase lane, `index` knob:

| Mode | Param | Range | Unit |
|------|-------|-------|------|
| FM   | FM index | 0 … 8 | modulator gain ratio |
| PM   | PM index | 0 … 4π (~0 … 12) | radians |
| TZFM | TZFM index | 0 … 8 | modulator gain ratio (through-zero) |
| sync (toggle) | on/off | — | amount = osc 2 tuning ratio |
| fdbk (toggle) | on/off | 0 … ~0.7 | feedback amount (own control or fixed) |

Amplitude lane, `depth` knob (bipolar, centre detent):

| Knob position | AM/RM morph behaviour |
|---------------|------------------------|
| 0 (centre)    | pure AM (modulator offset = 1, carrier fully present) |
| +1 (full CW)  | pure ring mod (offset = 0, carrier suppressed) |
| −1 (full CCW) | inverted-phase AM variant |

Morph law: `offset = 1 − |knob|`, sign of `knob` sets phase. In SSB mode the same knob becomes
a bipolar shift amount, e.g. −2 kHz … +2 kHz, sign = down/up shift direction.

## Implementation notes

- The mode switch is a **bus selector in the audio graph**, not a UI relabel. Switching modes
  re-points which modulation type the active lane runs, but **keeps the knob's value** so
  FM→PM→TZFM (and AM→RM→SSB) is a smooth A/B audition, not a reset.
- **Click-free switching:** ramp the old path out / new path in over a few ms when the mode
  changes, so toggling under a sustained drone doesn't pop.
- **SSB cost:** needs a Hilbert transform (90° quadrature split) — more DSP than AM/RM. Build it
  as its own module the amplitude lane routes into.
- **Drone gotcha:** with two free-running oscillators at close frequencies, the difference
  sideband (f_c − f_m) can fall to sub-audio rate → slow beating/throbbing instead of a pitched
  tone. Often desirable for drones; if you want the X-Mod to track musically, drive the modulator
  from a *ratio* of the carrier pitch rather than a fixed Hz.

## Extension path (not in the core panel)

μ-tant is wavetable-based, so the obvious future bus is **wavetable-position modulation**
(osc 1 drives osc 2's scan/morph index) — a third lane, or a mode on the phase lane if framed as
"index destination = phase vs wavetable position." Other candidates parked for later: PWM, vector
crossfade, wavefold cross-drive, Boolean/logic (XOR) combine.
