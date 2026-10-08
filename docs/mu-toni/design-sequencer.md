# μ-Toni — Sequencer (Generative Arpeggiator)

**Draft — in progress.** Captures the agreed core; several controls and the
exact **Direction** semantics are still to be specified by the owner (marked
🔲 TBD). No code yet.

Sibling docs: [design.md](design.md) (product overview + what's reused from
mu-tant), [docs/mu-tant/design-voice.md](../mu-tant/design-voice.md) (the shared
voice engine the arp drives).

---

## Scope

μ-Toni's sequencer is a **generative arpeggiator**. Unlike mu-clid (Euclidean
trigger rings) or mu-tant (drawable volume-gate grid), there is **no stored
pattern of notes**. The player sets a handful of musical parameters — a scale, a
chord in that scale, an octave range, a direction — and the arpeggiator
*derives* the note for each step from those parameters.

**It is an arpeggiator, not a chord player.** Output is **monophonic — one note
per step.** The **chord only selects the pool of notes to arpeggiate**; it is
never sounded as a stacked chord. (Optional strum/stack is a possible later
extension, not the concept.)

The organising principle, and the reason this design is worth writing down:

> **The note at step _i_ is a pure function of the current parameters:**
> `note(i) = f(scale, chord, octaves, direction, …, i)`.
> Nothing is accumulated between steps. Each step evaluates the function fresh
> against whatever the parameter values are *at that step's moment*.

This makes the whole arp **modulatable for free**. Because every input to `f` is
just an APVTS parameter, every one of them is a valid mu-core modulation
destination. An LFO on *Octaves*, a control sequence on *Direction*, a step-mod
on *Chord* — each simply changes the inputs to `f`, and the next step re-derives
its note accordingly. "For each step we work out what the current note would be
if the current settings were in place."

---

## Controls

The arp is defined by a small parameter set. Confirmed so far:

| Control | Type / range | Meaning |
|---|---|---|
| **Scale** | dropdown (12 scales) | The tonal space. Same scale table as mu-tant ([Scales.h](../../mu-tant/Source/Audio/Scales.h)) — Major, Minor, Dorian, Phrygian, Lydian, Mixolydian, Locrian, Harmonic-Minor, Pentatonic-Major, Pentatonic-Minor, Blues, Chromatic. |
| **Root note** | dropdown (C … B) | The tonic pitch-class. Set by the dropdown, **modulated** (`arp.rootNote` — sequence it with a step-mod), or **live by a played MIDI note** (see "MIDI control"). |
| **Root octave** | 0 … 8 (integer) | Which octave the root sits in — the base register the arp plays from (mirrors mu-tant's per-osc `octave` 0–8). **Distinct from "Octaves" below** (that's the arp's *span*; this is where it *starts*). A played MIDI note sets this too (full note = pitch-class **and** octave). |
| **Chord** | dropdown (**APVTS**) | A named chord type from the **Chord library** (see "Chord model") — selects the note pool. Set by the dropdown / DAW automation, or **sequenced with a step-mod** on `arp.chord` (see "Sequencing chord & root"). |
| **Trigger mode** | Loop / MIDI-triggered | Whether the arp **free-runs** as a constant loop (like mu-tant's free-running transport) or only runs while **triggered by MIDI note-on** (see "MIDI control"). |
| **Inversion** | **bipolar ±_n_** (integer) | Re-voices the chord. `+k` moves the lowest _k_ tones **up** an octave; `−k` moves the highest _k_ tones **down** an octave (**drop voicings**). `0` = root position. Applied **before** octave stacking. See "Inversion". |
| **Octaves** (span) | 1 – 4 (integer) | How many octaves the chord tones **span** upward from the root octave. `O` octaves ⇒ the (inverted) chord-tone pool is stacked `O` times, one octave apart. |
| **Direction** | −100 … +100 | **Skewed-triangle scan** of the pool: **+100** = up-arp, **−100** = down-arp, **0** = symmetric up-down, linear between (`fUp=(Dir+100)/200`). **Deterministic — identical every loop.** See "Direction". |
| **Rate** | note values (dropdown) — **modulation target** | The arp step rate as a **tempo-synced note value** (1/4 · 1/8 · 1/16 · 1/32, each **+ dotted + triplet**), locked to the shared transport. A mod destination (`arp.rate`): a step-mod snaps between divisions mid-pattern. See "Timing". |
| **Gate Length** | 1 – 100 % of step — **modulation target** | How long each note holds before **note-off → ADSR Release**. `<100%` = staccato with a gap; **`100%` + Legato = tie/slide** (see "Articulation"). Mod dest `arp.gateLen` → per-step slides/accents. |
| **Legato** | voice on/off — **modulation target** | When on, a **100%-gate** note **ties** to the next (no amp retrigger); this is also the only condition under which **glide is heard**. Synth-voice param, mod dest `arp.legato`. See "Articulation". |
| **Portamento** (Glide) | 0 … ~500 ms — **modulation target** | Glide **time** (analogue-style ms) the pitch slews between tied notes. Heard **only** on legato (100%-gate) transitions. Mod dest `arp.portaTime`. See "Articulation". |
| _more_ | 🔲 TBD | Owner: further controls (candidates under "Anticipated further controls"). |

All of the above are **APVTS parameters** and **modulation destinations**
(see "Modulation").

---

## Chord model

**Decision (revises the earlier scale-degree sketch):** a **chord is a named
type defined by absolute intervals in semitones from its root** — Major, Minor,
Maj7, Min7, and so on (full library below). This is how a musician names chords
("Cmaj7 → Am7 → Dm7 → G7") and lets the Chord dropdown be a clean list you can
also sequence with a step-mod — the model the selector is built on.

```
chord = ordered list of semitone intervals from the root, e.g.
  Major = {0, 4, 7}
  Min7  = {0, 3, 7, 10}
  Maj9  = {0, 4, 7, 11, 14}
```

### Role of the Scale

With absolute chords the **Scale** no longer sets the chord *quality*; it keeps
the music **in key** and drives pitch behaviour:

- **Root palette / quantise** — the roots you can pick (dropdown, modulation, or
  MIDI note) snap to the scale, so a progression stays diatonic to the chosen
  key. (An "off" option lets any chromatic root through.)
- **Engine pitch grid** — the arp's output still lands on mu-tant's
  scale-quantised pitch engine, and smooth modulation glides through scale space
  ([design-voice.md](../mu-tant/design-voice.md) §"Frequency computation").
- **Diatonic-snap toggle** (`arp.diatonicSnap`, **off by default**, modulatable) —
  when on, snaps each chord *tone* to the nearest scale note (strictly-in-key
  voicings, handy for generative/random-root patches). Off = the raw chord type,
  truer to its name. Edge case: in sparse scales snapping can collapse two tones
  onto one — acceptable, it's opt-in.

### Chord library

Ordered **common → exotic**, grouped in tiers so the dropdown reads top-down
from the everyday shapes to the jazz colours. Intervals are semitones from the
root (`0` = root). Bracketed tones `(…)` are theoretically present but commonly
**omitted** for playability; the arp can ship them omitted to keep pools short.

**Tier 1 — Essentials**

| Name | Symbol | Intervals |
|---|---|---|
| Single note | 1 | `0` |
| Power (fifth) | 5 | `0 7` |
| Major | maj | `0 4 7` |
| Minor | min | `0 3 7` |

**Tier 2 — Common triads**

| Name | Symbol | Intervals |
|---|---|---|
| Suspended 2nd | sus2 | `0 2 7` |
| Suspended 4th | sus4 | `0 5 7` |
| Diminished | dim | `0 3 6` |
| Augmented | aug | `0 4 8` |

**Tier 3 — Sixths & core sevenths**

| Name | Symbol | Intervals |
|---|---|---|
| Major 6th | 6 | `0 4 7 9` |
| Minor 6th | m6 | `0 3 7 9` |
| Dominant 7th | 7 | `0 4 7 10` |
| Major 7th | maj7 | `0 4 7 11` |
| Minor 7th | m7 | `0 3 7 10` |

**Tier 4 — Extended sevenths**

| Name | Symbol | Intervals |
|---|---|---|
| Minor 7♭5 (half-diminished) | m7♭5 | `0 3 6 10` |
| Diminished 7th | dim7 | `0 3 6 9` |
| Minor-major 7th | mMaj7 | `0 3 7 11` |
| Dominant 7 sus4 | 7sus4 | `0 5 7 10` |

**Tier 5 — Ninths**

| Name | Symbol | Intervals |
|---|---|---|
| Add 9 | add9 | `0 4 7 14` |
| Minor add 9 | m(add9) | `0 3 7 14` |
| Dominant 9th | 9 | `0 4 7 10 14` |
| Major 9th | maj9 | `0 4 7 11 14` |
| Minor 9th | m9 | `0 3 7 10 14` |
| Six-nine | 6/9 | `0 4 7 9 14` |

**Tier 6 — Elevenths & thirteenths**

| Name | Symbol | Intervals |
|---|---|---|
| Dominant 11th | 11 | `0 (4) 7 10 14 17` |
| Minor 11th | m11 | `0 3 7 10 14 17` |
| Major 11th | maj11 | `0 4 7 11 14 17` |
| Dominant 13th | 13 | `0 4 7 10 14 (17) 21` |
| Minor 13th | m13 | `0 3 7 10 14 21` |
| Major 13th | maj13 | `0 4 7 11 14 21` |

**Tier 7 — Altered / colour (jazz)**

| Name | Symbol | Intervals |
|---|---|---|
| Dominant 7♭9 | 7♭9 | `0 4 7 10 13` |
| Dominant 7♯9 (“Hendrix”) | 7♯9 | `0 4 7 10 15` |
| Dominant 7♯11 | 7♯11 | `0 4 7 10 18` |
| Dominant 7♭5 | 7♭5 | `0 4 6 10` |
| Dominant 7♯5 (aug7) | 7♯5 | `0 4 8 10` |
| Major 7♯11 (Lydian) | maj7♯11 | `0 4 7 11 18` |

**All ~35 ship** (chords are just interval tables + a cheap mod-dest index). The
`DropdownSelect` groups them with **tier dividers** — Essentials / Triads /
Sixths & Sevenths up top, Ninths / Elevenths-Thirteenths / Altered below a
divider — so the common shapes are front-and-centre and the jazz colours are
there when wanted. Chord is a modulation destination (`arp.chord`), so a step-mod
can walk a progression through the list.

### Inversion  *(bipolar ±n)*

**Inversion** re-voices the chord by an octave shift of its edge tones — **bipolar**:

```
maj {0,4,7}   +2 → {12, 16, 7} → sorted {7,12,16}   (2nd inversion, up)
              +1 → {4, 7, 12}                        (1st inversion, up)
               0 → {0, 4, 7}                         (root position)
              −1 → {0, 4, −5} → sorted {−5,0,4}      (drop the top tone)
              −2 → {0, −8, −5} → sorted {−8,−5,0}    (drop-2 voicing)
```

- **`+k`** raises the **lowest** `k` tones up an octave (standard inversions).
- **`−k`** lowers the **highest** `k` tones down an octave (**drop voicings** —
  wider, more open pools).

APVTS integer, symmetric range (e.g. **±4**), clamped to the chord's tone count,
so modulating past it holds at the extreme voicing. Mod dest `arp.inversion`.

---

## Note-pool derivation

Each step's note is computed in stages — a pure function, no state. The chord
type, root note, and root octave may come from the dropdowns, **modulation /
automation**, or MIDI; whatever the current values are, the pool follows:

**1. Voice the chord** — apply (bipolar) Inversion to the chord's interval set:

```
intervals = chord type's semitone intervals
if Inversion > 0:  raise the lowest  |Inversion| tones by +12   // up-inversions
if Inversion < 0:  lower the highest |Inversion| tones by −12   // drop voicings
```

**2. Build the chord-tone pool** from the inverted chord × Octaves-span:

```
pool = []
for oct in 0 .. OctavesSpan-1:
    for s in intervals:
        pool.append( s + 12*oct )                  // semitones above the root
sort pool ascending
```

**3. Scan the pool** by Direction → the step sequence (skewed-triangle scan,
skipping on the short side; see "Direction"). `scan[i]` is the pool index at
step `i`, and the scan's length is one triangle cycle.

**4. Pick the step note & place it at the root:**

```
note(i) = pool[ scan[ i mod scanLen ] ]
rootMidi = 12*(RootOctave+1) + rootNoteClass       // C-based MIDI of the root
midi     = rootMidi + note(i)
freq     = 440 * 2^((midi-69)/12)                  // mu-tant pitch formula
```

So a triad over 2 octaves gives a 6-note ascending pool; inversion shifts which
notes are lowest, and Root note/octave place the whole thing in pitch.

Because stages 1–4 read the **current** parameter values, modulating any of them
— via a step-mod or LFO — reshapes the voicing, pool, or traversal on the very
next step.

---

## Articulation — Gate Length, Legato & Portamento

How a note starts, holds, ends, ties, and glides. Three controls interlock:

- **Gate Length** (`arp.gateLen`, 1–100 % of the step) — note-on at step start,
  **note-off at `gate% × step` → ADSR Release**. Low = staccato with a gap; 100 %
  = the note runs the full step, butting against the next.
- **Legato** (`arp.legato`, synth-voice on/off) — when on, a **100 %-gate** note
  **ties** to the next: **no amp-envelope retrigger** across the join.
- **Portamento** (`arp.portaTime`, analogue-style **ms**) — the pitch **slews**
  from the previous note to the current one, **heard only on tied (legato)
  transitions**.

### Truth table

| Legato | Gate Length | Result |
|---|---|---|
| **on** | **100 %** | **Tie + slide** — no retrigger, pitch **glides** over Portamento ms (the TB-303 slide). |
| **on** | < 100 % | Plucks (retrigger), gap per gate; **no glide**. |
| **off** | any | Always plucks (retrigger); **glide never heard**. |

So a **per-step slide** is made by **modulating Gate Length to 100 %** on the
slide steps with Legato on — no separate "portamento on/off" flag exists. Glide
is deliberately a *legato* behaviour (fingered-portamento style), which is why a
plucked note never glides even with a Portamento time dialled in.

**Engine placement.** Portamento is a `juce::SmoothedValue`-style **slew on the
pitch value** the arp writes into mu-tant's scale-quantised pitch input — pitch
path only, mu-tant oscillator DSP unchanged. Legato suppresses the note-on that
would otherwise retrigger the ADSR at a tied join. 🔲 minor: exact ms range
(≈0–500 ms) to be tuned by ear.

---

## Sequencing chord & root — via the modulator *(final design)*

The arp doesn't have to hold one static chord, and it needs **no bespoke chord
sequencer** to change over time. **Chord**, **Root note**, and **Root octave**
are ordinary scalar APVTS parameters and **mu-core modulation destinations**, so
sequencing them reuses the existing modulator with **zero new machinery**:

- A **step control-sequence** on `arp.chord` *is* a chord progression.
- A step-mod on `arp.rootNote` / `arp.rootOctave` walks the key.
- The step editor already exists, integer destinations already snap (#641), and
  the pure-function arp already reads "the current value" each step — so it just
  works. This is the family **"everything is modulation"** pattern.

**This is the final design — no dedicated chord timeline.** A bespoke bar-aligned
"place Cmaj7 → Am7 → Dm7 → G7" lane was considered and **dropped**: the modulator
gives everything needed (and more, since Chord/Root then also respond to LFOs,
not just step sequences). The only thing it doesn't do is *bundle* chord+root
into a single bar-aligned event shared across voices — deemed unnecessary. If
that ever becomes wanted it can reuse mu-tant's `GatingDesigner` grid, but it is
**not planned**.

Precedence for the Root/Chord value: **MIDI note (while held) > modulation /
automation > dropdown default**.

---

## Direction (−100 … +100)  *(resolved — skewed-triangle scan)*

**Direction skews a triangle *scan* of the pool** — it is the share of each cycle
spent **ascending vs descending**. It is a **pure calculation, identical every
loop** (no randomness):

| Direction | Behaviour |
|---|---|
| **+100** | 100% ascending — sweep up the pool, wrap, repeat (**classic up-arp**) |
| **−100** | 100% descending — sweep down, wrap, repeat (**down-arp**) |
| **0** | equal — sweep **up then down** the pool (**symmetric up-down arp**) |
| in between | **linear** — asymmetric triangle (e.g. **+50** = long up-sweep, short down-sweep) |

Formally the ascending fraction of the cycle is `fUp = (Direction + 100) / 200`.

### How the asymmetry is realised — skip on the short side

Every step is one uniform **Rate** tick, so the two sweeps get step counts in
proportion `fUp : (1−fUp)`. The **longer** sweep plays **every** pool note (one
per tick); the **shorter** sweep covers the pool in proportionally fewer ticks by
**skipping notes evenly** (a Euclidean/Bresenham spread). Rhythm stays perfectly
uniform. Example, 6-note pool `0 1 2 3 4 5`:

```
+100 : 0 1 2 3 4 5 | 0 1 2 3 4 5        up-arp (wrap)
   0 : 0 1 2 3 4 5 4 3 2 1 | repeat     up-down (symmetric)
 +50 : 0 1 2 3 4 5 3 1 | repeat         up all, down skips (short)
 -100: 5 4 3 2 1 0 | repeat             down-arp
```

- **Pattern length emerges from the pool traversal**, not from a bar — the loop is
  as long as one triangle cycle (pool size + Direction determine the step count),
  then it repeats identically. (This is why there is no separate "reset / repeat
  length" control.)
- **Boundaries wrap** (top → bottom) on the pure up/down arps.
- **Deterministic** — the same Direction + pool always produces the same sequence,
  so a patch is repeatable. Direction is a normal APVTS param + **modulation
  destination** (`arp.direction`): an LFO −100↔+100 breathes the arp between
  descending, up-down, and ascending; a step-mod flips it per section.

---

## Timing — Rate  *(resolved)*

**Rate is a core arpeggiator control**, set to **tempo-synced note values** — the
step rate, not free-running Hz:

- Values: **1/4 · 1/8 · 1/16 · 1/32**, plus **dotted / triplet** variants
  (`DropdownSelect`). Locked to the shared transport — host clock in-plugin; the
  internal free-running / **mu-link master** clock in standalone (the same source
  mu-tant's grid playhead uses), so an attached mu-link drives the arp tempo.
- The step index `i` advances once per Rate tick; `note(i)` is evaluated at each
  tick against the live parameters.
- **Rate is a modulation destination** (`arp.rate`) — a step-mod snaps between the
  note-value options, so the arp can shift division mid-pattern (e.g. drop to
  1/32 for a burst). Integer-valued → step mods snap (#641).

### Default patch — sounds on play

Trigger mode defaults to **Loop (free-run)** with a musical default (minor triad,
2-octave span, Rate 1/16, Direction +100 up-arp). Pressing **play produces an
arpeggio immediately** — no MIDI needed — mirroring mu-tant's "makes sound out of
the box". MIDI-triggered mode is opt-in. Transport **stopped ⇒ silent** (as
mu-tant).

---

## MIDI control

μ-Toni keeps **mu-tant's MIDI settings verbatim** and **adds played-note input**
on top — the one place μ-Toni consciously breaks mu-tant's "PC-only, no played
notes" rule, because an arpeggiator wants a keyboard.

### Program-change → preset (unchanged from mu-tant)

Identical to mu-tant / the family: the shared mu-core `MidiPresetMap` /
`MidiFullPresetMap` path, configured from the SettingsOverlay "MIDI Prog.
Change" row (per-slot arp vs full preset). No mu-toni-specific work beyond the
`ProcessorBase` extension virtuals. See
[design.md](design.md) §"Naming / file formats".

### Trigger mode — Loop vs MIDI-triggered

A mode control (also an APVTS parameter) selects how the arp runs:

- **Loop (free-run)** — the arp runs continuously off the shared transport, the
  same free-running clock mu-tant uses (host clock in-plugin; internal / mu-link
  clock in standalone). No note input required to make sound.
- **MIDI-triggered** — **run-while-held.** A note-on starts the arp from step 0
  and it **loops continuously until the note is released**; release stops /
  silences the arp. (Not a one-shot phrase — the loop repeats for as long as the
  key is down.) Note-on **resets the step index `i` to 0** so every press starts
  the pattern from its beginning.

### Root by MIDI note

An incoming **note-on sets the arp's root** (transpose), overriding the Root
dropdown while a note is active — so playing C then E then G walks the whole arp
through those keys. The played note maps to the root the pool is built from
(stage 3 of "Note-pool derivation": `midi = root + …`).

**Note priority: latest note-on wins.** With several notes held, the **most
recent note-on** becomes the root; when it releases, the root **falls back to the
previous still-held note** (a held-note stack, newest on top) — so playing a
legato line re-roots the arp to each new note and reverts as fingers lift. In
MIDI-triggered mode the arp keeps looping while *any* note is held; the loop only
stops when the **last** note releases. (Whether re-rooting to a new latest note
while already looping also **resets step index `i` to 0**, or continues the loop
seamlessly, is a sub-choice — default: reset only on the note that starts from
silence, continue on a re-root mid-loop.)

The played note sets the root by its **full pitch — pitch-class *and* octave**
(C2 and C4 differ): the note drives both the **Root note** and the **Root
octave** controls, so a keyboard transposes the arp across the full range, not
just within one octave.

**Division of control (the intent):** **Chord + Root** come from the dropdowns,
from **modulation / DAW automation** (sequence them with a step-mod), and the
**keyboard** can override the **Root** live (transpose by playing). Precedence:
**MIDI note (while held) > modulation/automation > dropdown default**.

### Engine-integration note

μ-Toni is **a simple analogue-style mono synth**, so each arp step is a **played
note** (integration B, below) — a note-on that retriggers the Amp and Filter
envelopes. The keyboard sets the root; the arp supplies the notes.

---

## Engine integration — how the arp drives the sound (resolved: B)

μ-Toni takes **mu-tant's timbre** (2 wavetable oscillators + X-Mod + filter +
insert) but is **note-triggered, not a drone** — so the engine gains what a mono
synth needs: **per-note Amp + Filter ADSR envelopes**. Every arp step is a
**note-on** that retriggers those envelopes; the note ends (note-off → Release)
at the step's **gate length**.

**This is integration B, and it's a clean family reuse — both halves already
exist:**

| Piece | From |
|---|---|
| Oscillators, X-Mod, filter, insert (timbre) | **mu-tant** voice |
| Amp ADSR, Filter ADSR (+ depth), Pitch ADSR | **mu-core** — the same `juce::ADSR` + `VoiceParams` env fields mu-clid's `VoiceEngine` uses ([VoiceParams.h](../../mu-core/Audio/VoiceParams.h)) |
| Note-on/off per step, gate length, **Legato** | **mu-toni** arp (new) |

So μ-Toni = **mu-tant's sound + mu-clid's envelopes + the arp**. The earlier
"identical to mu-tant" framing evolves here: the *timbre* stays mu-tant's, but the
voice is now note-triggered with real envelopes rather than a continuous drone.

**Legato / slide** is handled in §"Articulation": a note ties (no amp retrigger)
when its Gate Length is 100 % and the Legato voice param is on, which is also the
only time Portamento glide is heard. Per-step slides come from modulating
`arp.gateLen` to 100 %.

---

## Envelopes — Amp, Filter & Pitch ADSR

μ-Toni exposes three full **ADSR** envelopes, reusing the mu-core envelope model
(fields already defined in [VoiceParams.h](../../mu-core/Audio/VoiceParams.h)):

| Envelope | Controls | Destination |
|---|---|---|
| **Amp** | Attack · Decay · Sustain · Release, **Level** (dB) | The VCA — shapes each note's amplitude. |
| **Filter** | Attack · Decay · Sustain · Release, **Depth** | Sweeps the filter **cutoff**; Depth = amount (`filterEnvDepth`, bipolar). |
| **Pitch** | Attack · Decay · Sustain · Release, **Depth** | Pitch "blip"/attack-punch; **Depth 0 by default** (inert until dialled) — `pitchEnvDepth`. |

- All three retrigger on every arp note-on **except** on a tied/legato note
  (§"Articulation").
- **Depth controls:** Filter Env Depth (cutoff sweep amount), Pitch Env Depth
  (semitone blip, default 0), Amp Level (note output level).
- All twelve ADSR params + the depths are **APVTS parameters and modulation
  destinations** (see "Modulation") — e.g. a step-mod on Filter Env Depth per
  note, or an LFO on Release.

These are standard mono-synth controls and sit in the engine panel alongside the
oscillator/filter rows (layout mirrors mu-tant's `VoicePanel` bands; the gate
grid's slot is taken by the arp/envelope controls instead).

---

## Modulation (the payoff)

Every arp control registers as a mu-core **modulation destination** via a
mu-toni destination provider (`Source/Modulation/`, mirroring mu-tant's
[MuTantModDest.h](../../mu-tant/Source/Modulation/MuTantModDest.h)). Candidate
destinations: `arp.octaves`, `arp.inversion`, `arp.direction`, `arp.chord`,
`arp.scale`, `arp.rootNote`, `arp.rootOctave`, `arp.rate`, **`arp.gateLen`** (the
per-step slide/accent destination — 100 % + Legato = tie/slide), `arp.legato`,
`arp.portaTime`, `arp.diatonicSnap`, the **Amp ADSR** (`amp.atk/dec/sus/rel`,
`amp.level`), **Filter ADSR** (`flt.atk/dec/sus/rel`, `flt.envDepth`), and
**Pitch ADSR** (`ptc.atk/dec/sus/rel`, `ptc.envDepth`), plus any "more to add".

Semantics follow the family rule (#641): step mods snap integer-valued controls
(Octaves, Inversion, Chord index, Root) to whole values; smooth mods on Direction
sweep continuously. Because `f` is re-evaluated per step, a slow LFO on Octaves
gives an evolving arp that widens and narrows; a control sequence on Direction
reverses the arp mid-phrase — all without any stored pattern.

**This is also the chord/root sequencer.** A step control-sequence on `arp.chord`
is a chord progression; one on `arp.rootNote` is a key walk. No bespoke timeline
needed — see "Sequencing chord & root".

**Family rule reminder:** channels stay self-contained — an arp's modulators may
only target that arp's / voice's own parameters (no cross-voice modulation), same
as every other product.

---

## Anticipated further controls ("more to add")

Placeholder list for the owner's "more to add" — to be confirmed, not yet
designed:

- **Swing / humanise**, **step probability**, **rest pattern**.
- **Velocity / accent** (per-step accent into the amp envelope / level).

*Explicitly ruled out:* **strum / stack** (μ-Toni is strictly monophonic — the
chord only selects the pool) and a **dedicated chord timeline** (the modulator
covers chord/root sequencing — see that section).

---

## mu-core reuse map — build from shared parts

Almost everything μ-Toni needs already exists in mu-core or mu-tant. **Net-new
code is small** (the arp math + note-on/off wiring + a mod-dest provider).

### DSP / engine

| Concern | Reuse | New? |
|---|---|---|
| Oscillators, X-Mod, wavetable bank | **mu-tant** `SynthVoice` / `WavetableOscillator` / `WavetableBank` | reuse |
| Filter | **mu-core** `MultiModeFilter` | reuse |
| Insert FX | **mu-core** `InsertProcessor` + algo set | reuse |
| **Amp / Filter / Pitch ADSR** | **mu-core** `juce::ADSR` + [`VoiceParams`](../../mu-core/Audio/VoiceParams.h) env fields (as mu-clid's `VoiceEngine`) | reuse the pattern; **wire into the wavetable voice** |
| Per-voice modulation container | **mu-core** [`VoiceSlot`](../../mu-core/Sequencer/VoiceSlot.h) (`ControlSequence`×N + `ModulationMatrix`) | reuse (as mu-tant does) |
| Modulation matrix + control sequences | **mu-core** `ModulationMatrix` / `ControlSequence` | reuse |
| Mixer + FX rack + master | **mu-core** `MixerEngine` + `mu_mixfx::addGlobalFxParams` | reuse |
| Transport / clock (host · internal · mu-link · MIDI clock) | **mu-core** `resolveTransport` (TransportResolver.h) + `MuLinkBridge` | reuse |
| PC→preset | **mu-core** `MidiPresetMap` / `MidiFullPresetMap` + `scanMidiProgramChanges` | reuse |
| **Arpeggiator** (pool → triangle-scan → step→note), **note-on/off + gate length + Legato tie**, **portamento slew**, **MIDI note-in** (root + trigger) | — | **NEW (mu-toni)** |

The **only genuinely new DSP** is: the arp math (pool + skewed-triangle scan), the
per-step note-on/off + gate + Legato tie into the ADSR envelopes, the portamento
slew (`juce::SmoothedValue`-style), and parsing played MIDI notes (no family
precedent — mu-clid/-tant are PC-only). All small, all mu-toni-side.

### UI — shared widgets only (no one-offs)

Per the family rule "never build a one-off version of a standard control":

| μ-Toni control | Shared widget | Knob colour token |
|---|---|---|
| Scale, Chord | `DropdownSelect` | — |
| Root note, Root octave, Octaves span, Inversion | `KnobWithLabel` / `NudgeInput` | `knobEuclidean` (sequencer params, purple) |
| **Direction** (−100..+100) | **`BipolarSliderRow`** | `knobEuclidean` |
| Trigger mode (Loop/MIDI) | `SegmentControl` (Bar) | — |
| Gate Length (% of step) | `KnobWithLabel` | `knobEuclidean` |
| Legato (voice on/off) | `SegmentControl`/`TextButton` (Positive) | — |
| Portamento time (ms) | `KnobWithLabel` | `knobEuclidean` |
| Diatonic-snap toggle | `SegmentControl`/`TextButton` (Positive) | — |
| Amp ADSR + Level | `KnobWithLabel` (via `ParamKnobGrid`) | `knobLevel` (amber — ADSR/gain) |
| Filter ADSR | `KnobWithLabel` | `knobPostPad` (teal — filter) |
| **Filter Env Depth** (bipolar) | **`BipolarSliderRow`** | `knobPostPad` |
| **Pitch Env Depth** (bipolar) | **`BipolarSliderRow`** | `knobEuclidean` |
| **Inversion** (bipolar ±n) | **`BipolarSliderRow`** | `knobEuclidean` |
| Modulator section | `ModulatorPanel` / `ModMatrixPanel` | (shared) |
| Per-slot header (preset save/load) | `ChannelHeaderBar` | (shared) |
| Sidebar (voice select/add/reorder) | `ChannelSidebar` (subclass, as mu-tant's `VoiceSidebar`) | (shared) |

Layout follows the mu-tant `VoicePanel` band structure: Osc row · Filter+Insert
row · **arp/envelope controls** (where mu-tant's gate grid sat) · Modulators.
All sizes are `MuLookAndFeel` constants wrapped in `mu_ui::s()`; all knob
feedback routes through the shared **StatusBar** (no tooltips); labels are **full
words** ("Attack", not "ATK").

### File placement

`Source/Sequencer/` — `Arpeggiator.{h,cpp}` (pool/walk/step→note) + note-gate
logic. `Source/Audio/` — `Chords.h` (library) + `Scales.h`. `Source/UI/` — the
arp/envelope panel (replaces the blank `EnginePanel`) + a `ChannelSidebar`
subclass. `Source/Modulation/` — `MuToniModDest.h` (mirrors `MuTantModDest.h`).
`Source/Plugin/` — note-on/off + ADSR wiring in the render hook. Mixer, shell,
modulator UI, filter, insert = **mu-core, untouched**.

---

## Audio-thread contract (planned)

Follows the family invariants:

- **No allocation in `processBlock`**; buffers/`juce::ADSR`/slew state prepared in
  `prepareToPlay`.
- **`ModulationMatrix` is the single reader** (family rule) — the arp reads its
  chord/root/direction/ADSR/porta values as **post-modulation** outputs of the
  matrix, not raw APVTS, so modulation and the base value flow through one path
  (mixer-only params still use cached `std::atomic<float>*`, as in mu-tant).
- **`note(i)` evaluation** is a bounded arithmetic pass over the pool (≤ 4 octaves
  × small chord ⇒ ≤ ~28 notes), no map lookups; the triangle scan is a cheap
  deterministic index calc per step (no RNG).
- **Note-on/off** are queued by the arp (audio thread) and consumed in the same
  block into the ADSR; **step advance** is derived from the transport position
  (block-start onset for the MVP; sample-accurate step offset a later refinement,
  matching mu-on).
- Routed through `ProcessorBase::processCoreBlock(..., renderVoiceCb)` exactly
  like mu-tant — the render hook does modulation → engine (osc→filter→ADSR) →
  insert; the shared mixer owns the strip/master so VU meters stay live.

---

## Open questions (sequencer-level)

The core design is settled after the grilling pass. Only fine-tuning left:

1. **Ranges to tune by ear** — Portamento ms range (≈0–500), Octaves-span max
   (1–4), Inversion max (±4), default patch values.
2. **MIDI-in channel** — omni vs. a selectable channel for the played-note input
   (leaning omni).
3. **Velocity/accent** — whether played-note velocity (and a per-step accent mod)
   feeds the amp envelope / level (leaning yes, cheap).

**Resolved (this design + the grilling pass):**
- ~~Direction~~ — **skewed-triangle scan**: `fUp=(Dir+100)/200`, +100 up-arp /
  −100 down-arp / 0 up-down; short side **skips notes evenly**; **deterministic,
  identical every loop**; pattern length emerges from the pool; boundaries wrap.
- ~~Articulation~~ — **Legato voice param + Gate Length**: tie/no-retrigger when
  gate = 100 % & Legato on; **glide heard only on tied notes**; per-step slides via
  modulating `arp.gateLen`; **no separate porta on/off**; glide time in **ms**.
- ~~Engine~~ — **note-triggered mono synth (B)** with **Amp + Filter + Pitch ADSR**
  (Pitch depth 0 default), reused from mu-core.
- ~~Inversion~~ — **bipolar ±n** (drop voicings).
- ~~Chord library~~ — **all ~35 ship**, grouped by tier dividers.
- ~~Diatonic-snap~~ — **toggle, off by default**, modulatable.
- ~~Rate~~ — **tempo-synced note values** (straight + dotted + triplet), mod dest.
- ~~Voice count~~ — **1–8 slots, default 1**, each an independent mono arp.
- ~~Default patch~~ — **Loop mode, sounds on play** (Cm, 2 oct, 1/16, up).
- ~~Poly/strum~~ — **mono only**; strum ruled out.
- ~~Chord/root sequencing~~ — **via the modulator**; no dedicated timeline.
- ~~Chord = degrees vs. absolute~~ — absolute named types; Scale keeps roots in key.
- ~~MIDI-triggered gate~~ — run-while-held, loops until release, resets step 0.
- ~~Root-by-MIDI~~ — full pitch; latest note-on wins, held-note fallback stack.
- ~~Preset noun~~ — per-slot `.muArp`, full `.muToni`.
