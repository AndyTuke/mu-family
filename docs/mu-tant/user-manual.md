# μ-Tant — User Manual

A wavetable drone synthesiser by Transwarp Development Project.

---

## What is μ-Tant?

μ-Tant is a **drone instrument** — not a note instrument. Each of up to 8 independent voices runs two wavetable oscillators continuously. The oscillators never start or stop; they evolve over time through modulation and are rhythmically shaped by a drawable gate pattern.

The core idea: instead of triggering notes, you sculpt a continuously evolving tone by drawing attack/decay shapes into a gate grid, and let the modulator section warp pitch, filter, and timbre while it plays.

---

## Interface Layout

```
┌──────────────────────────────────────────────────────┐
│  Transport bar (Play/Stop, BPM, Preset browser)      │
├──────────┬───────────────────────────────────────────┤
│          │  Voice name bar (reset / delete / preset) │
│          ├───────────────────────────────────────────┤
│          │  OSC 1  |  OSC 2  (wavetable + pitch)     │
│ Sidebar  ├───────────────────────────────────────────┤
│ (voices) │  X-Mod + Noise row                        │
│          ├──────────────────────┬────────────────────┤
│          │  FILTER              │  INSERT            │
│          ├──────────────────────┘                    │
│          │  NOISE panel  |  MIXER panel              │
├──────────┴───────────────────────────────────────────┤
│  Gate editor (GATE/FILT layers, full width)           │
├─────────────────────────────────────────────────────┤
│  Modulators                                          │
└─────────────────────────────────────────────────────┘
```

---

## Transport Bar

- **Play / Stop** — starts and stops the internal clock. While stopped, all voices produce a continuous drone (useful for auditioning a patch without the gate running).
- **BPM** — the internal tempo. μ-Tant does not currently sync to a DAW clock; the internal clock drives both the gate patterns and the modulators.
- **Preset browser / dropdown** — load and save full `.muTant` presets via the preset dropdown or the Save button.

---

## Sidebar — Voice Management

The left sidebar shows one entry per active voice (up to 8). Click an entry to select that voice's editor. Drag entries to reorder voices.

- **Add** (+ button at the bottom) — adds a new voice with default settings and the next unused colour.
- **Delete** (✕ in the voice header bar) — removes the selected voice. Prompts for confirmation. The last voice cannot be removed.
- **Reset** (↺ in the voice header bar) — resets the voice to defaults while keeping its colour.

Each sidebar entry shows a mini spectrum animation driven by the voice's post-insert audio.

### Voice Colours

Each voice automatically receives a distinct palette colour when added. The colour appears on the voice header bar, the sidebar entry, and the sub-panel borders inside the voice editor. The colour follows the voice through reorder and delete operations.

---

## Voice Header Bar

At the top of the voice editor panel:

- **Name** — double-click to rename the voice. Press Enter or click elsewhere to confirm.
- **↺ Reset** — reset to defaults (confirmation required).
- **✕ Delete** — remove this voice (confirmation required).
- **Preset dropdown** — load a per-voice preset (`.muPattern` file from `Documents/TDP/muTant/Voices/`).
- **Save** — save the current voice as a `.muPattern` file. Enter a name and it is saved to the Voices folder.

---

## Oscillators (OSC 1 and OSC 2)

Both oscillators run simultaneously and are **free-running** — their phases never reset. The sound is a continuously evolving tone, not a triggered note.

### Shared Tonal Centre

Both oscillators share the same **Root** and **Scale** setting (in the header row beneath the voice name bar). The root and scale define the harmonic space both oscillators move within.

| Control | Range | Notes |
|---|---|---|
| Root | C–B | Root pitch class |
| Scale | Major, Minor, Dorian, Phrygian, Lydian, Mixolydian, Locrian, Harmonic-Minor, Pentatonic-Major, Pentatonic-Minor, Blues, Chromatic | Shared by both oscillators |

### Per-Oscillator Controls

Each oscillator has four knobs:

| Knob | Range | Notes |
|---|---|---|
| Oct | –3 to +3 octaves | Octave offset from the base note |
| Semi | –12 to +12 | Scale-degree offset (integer steps through the scale) |
| Fine | ±100 cents | Off-scale detune |
| Pos | 0–255 | Wavetable frame position (morphs through the table) |

**Wavetable dropdown** — selects the wavetable for this oscillator. The built-in bank has a sine-to-saw morph table; more tables are planned.

**Modulating Semi with a step modulator** snaps cleanly between scale notes (integer jumps). **Modulating Semi with a smooth modulator** glides through frequency space, passing through off-scale pitches for a glissando effect.

---

## Cross-Mod and Noise (X-Mod Row)

### X-Mod Amount

The **X-Mod** knob (0–127) sets how much the two oscillators interact. At 0, they are fully independent. At 127, cross-mod is at full depth.

### X-Mod Mode

| Mode | Effect |
|---|---|
| Off | Oscillators are independent |
| FM | Osc 2's signal phase-modulates Osc 1 (classic FM timbre) |
| AM | Osc 2 amplitude-modulates Osc 1 (adds sidebands, tremolo-like) |
| Ring | Ring modulation (Osc 1 × Osc 2 mix, adds sum/difference frequencies) |

### Sync Button

**Sync** (toggle next to the X-Mod mode dropdown) — when active, Osc 2's phase is reset each time Osc 1 completes a cycle (hard sync). This thickens the tone and adds a formant-like quality that tracks Osc 1's pitch. Sync is independent of the X-Mod mode and can be combined with FM, AM, or Ring.

### Noise Source

A separate noise source (White or Pink) can be mixed in via the **NOISE** panel on the right. The noise type dropdown is in the NOISE panel; the Noise level knob is in the MIXER panel below it.

---

## Mixer (Source Levels)

The **MIXER** panel (bottom-right) has three level knobs:

| Knob | Controls |
|---|---|
| Osc 1 | Level of oscillator 1 (dB) |
| Osc 2 | Level of oscillator 2 (dB) |
| Noise | Level of the noise source (dB, default −60 dB / off) |

These three signals are summed before entering the filter.

The **Level** knob in the filter row sets the voice's output level into the mixer channel.

---

## Filter

The filter sits between the oscillator mix and the gate. It shapes the tone continuously while the drone plays.

| Control | Range | Notes |
|---|---|---|
| Type | LP 6, LP 12, LP 24, BP 12, BP 24, HP 12, HP 24, Notch, Comb, AP 12, Notch 24, HP 6, Peak, Lo Shelf, Hi Shelf | 15 filter algorithms |
| Cutoff | 20 Hz–20 kHz | Log-scaled; visual centre ≈ 640 Hz |
| Resonance | 0–100 % | Q / feedback |
| FEnv | –1 to +1 | Filter envelope depth: how far the **FILT** gate layer modulates the cutoff |

**FEnv (filter envelope depth)** — controls how much the filter envelope layer (drawn in the gate editor on the FILT layer) affects the cutoff. At +1.0, the filter envelope sweeps from 20 Hz (fully closed) to the base cutoff (open). At 0, the filter envelope does nothing. At –1.0, the sweep is inverted.

---

## Insert Effect

The **INSERT** sub-panel (right of the filter row) is the per-voice insert effect, placed after the gate in the signal chain (oscillators → filter → gate → **insert** → mixer).

Select the algorithm from the INSERT dropdown:

| Algorithm | Knobs | Notes |
|---|---|---|
| None | — | Passthrough (default) |
| Soft Clip | Drive, Output, Tone | Tanh soft saturation |
| Hard Clip | Drive, Output, Tone | Hard clip |
| Foldback | Drive, Output, Tone | Wavefolding |
| Bitcrusher | Bits (via Drive), Rate, Tone | Sample-rate + bit reduction |
| Chorus | Rate, Depth, Voices, Spread, Mix | 4-voice Catmull-Rom Hermite |
| Flanger | Rate, Depth, Feedback, Mix | Through-zero |
| Phaser | Rate, Depth, Stages, Mix | Notch-frequency logarithmic LFO |
| Compressor | Threshold, Ratio, Attack, Release | Gain-reduction meter visible |
| Limiter | Threshold, Knee, Attack, Release | Brickwall |
| Ring Mod | Mix, Freq | Sine-carrier ring modulator |
| Tape Saturation | Drive, Output, Tone | Tanh + DC block + tone filter |

Insert parameters P1–P4 are available as **modulation destinations** — the modulator section can automate any insert knob.

---

## Gate Editor

The gate editor is the heart of μ-Tant's rhythmic character. It occupies the full-width band below the voice panel. The gate is **purely a volume gate** (0 to 1 multiplier) — it chops rhythmic bursts out of the otherwise-continuous drone.

### Layers

The gate editor has two layers, toggled by **GATE** and **FILT** buttons:

- **GATE** — shapes amplitude. Envelope value 0 = silent, 1 = full level.
- **FILT** — shapes filter cutoff. Envelope value 0 = 20 Hz (closed), 1 = base cutoff (open).

The inactive layer is rendered as a ghost at 20% alpha so you can align both layers visually.

### Grid Controls

- **Subdivision dropdown** — sets the cell width: 1/4, 1/8, 1/16 (default), or 1/32 notes. The grid always covers 2 bars in 4/4.
- **Bypass button** — passes the raw drone through the gate without gating (useful for auditioning the oscillator/filter sound during patch design).
- **Gap slider** — forces a silence tail at the end of every envelope region. At 0 %, the envelope fills the full region; at 50 %, the envelope completes in the first half and the second half is silent. A small gap gives cleaner gate articulation.

### Toolbox

Five tools in the header (left to right):

| Tool | Icon | Action |
|---|---|---|
| Arrow | ↖ | Select an envelope to view/edit its properties |
| Pencil | ✏ | Click an empty cell to draw a 1-cell envelope; drag handles to reshape |
| Eraser | ✕ | Click an envelope to delete it |
| Glue | ◇ | Click-drag across envelopes to merge them into one wider region |
| Reverse | ⟲ | Click an envelope to flip its attack and decay |

### Envelope Shape

Each envelope covers a contiguous region of cells and has an attack/decay shape:

- **Top handle** (at the peak point) — drag horizontally to move where within the region the peak falls. Far left = instant attack (pure decay); far right = slow attack, no decay.
- **Attack bend handle** (mid-point on the rising line) — drag vertically to bow the attack line. Positive = sustain-like; negative = exponential.
- **Decay bend handle** (mid-point on the falling line) — drag vertically to bow the decay line.

Default new envelope: instant attack (split = 0), linear decay — the classic gate sound.

### Properties Strip (Arrow Tool)

When the Arrow tool is active and an envelope is selected, the properties strip appears below the grid:

| Control | Effect |
|---|---|
| **Prob** knob (1–100 %) | Probability this envelope fires each time the pattern loops. At 100 % it fires every loop; at 50 % it fires roughly half the time (deterministic per-loop hash — stable within a loop, different each pass). |
| **Loop** M dropdown (1–8) | The cycle length in loops. Position buttons 1–M select *which* loops in each cycle this envelope fires on. |
| Position buttons 1–8 | Toggle on/off which positions in the M-loop cycle this envelope fires. Buttons beyond M are greyed. |

Example: M = 4, positions 1 and 3 toggled → this envelope fires on loops 1 and 3 of every 4-loop cycle (every other pair).

### Audio Behaviour

| Transport state | Gate output |
|---|---|
| Stopped | Silent (gate closed) |
| Playing, empty pattern | Silent (nothing drawn → nothing passes) |
| Playing, has envelopes | Per-sample envelope evaluation |
| Bypass on | Raw drone passes regardless of play state |

---

## Modulator Section

The modulator section is shared from mu-core — the same engine as μ-Clid. Up to 8 modulators per voice: step sequencers, LFO shapes, or hand-drawn curves. Each voice has its own independent modulator bank.

### Adding a Modulator

Click the **+** button in the modulator area to add a slot. Choose a waveform type (Step, Smooth, Sine, Triangle, Saw, Square, Random) and a timing (in bars or Hz).

### Modulation Destinations (μ-Tant-specific)

| Destination | Range | Notes |
|---|---|---|
| Osc1/2 Octave | ±3 octaves | Integer jumps |
| Osc1/2 Semi | ±12 | Scale-degree steps or smooth glide |
| Osc1/2 Fine | ±100 cents | Off-grid detune |
| Osc1/2 Position | 0–255 | Wavetable frame scan |
| X-Mod | 0–127 | Cross-mod depth |
| Osc1/2 Level | −60..+6 dB | Per-source mix level |
| Noise Level | −60..+6 dB | Noise source level |
| Filter Cutoff | 20 Hz–20 kHz | Proportion-space (full sweep regardless of base cutoff) |
| Filter Resonance | 0–100 % | |
| Level | −60..+6 dB | Voice output level — use this for volume automation; the gate is a 0/1 multiplier |
| Insert P1–P4 | 0–1 | Insert algorithm parameters (labels update to match the active algo) |

### Modulating Tone for Pitch Melodies

**Step modulator on Semi** → integer steps through scale notes; clean chromatic jumps.

**Smooth modulator on Semi** → continuous glide through frequency space; passes through off-scale frequencies between notes for a glissando feel.

### Mod Depth and Direction

Each modulator-to-destination assignment has a depth knob (−100 % to +100 %). At +100 %, the modulator sweeps the full destination range. At −100 %, the sweep is inverted.

**Modulation ring** — when a knob is a modulation destination, a thin arc appears around it at 30 Hz showing the current modulated value relative to the base.

---

## Mixer Overlay (Shared)

Click the mixer button in the transport bar to open the full mixer overlay. Each voice occupies one channel strip.

| Control | Description |
|---|---|
| Fader | Channel output level |
| Pan | Stereo position |
| Mute / Solo | Standard per-channel control |
| Effect / Delay / Reverb send | Sends to the shared FX rack (same as μ-Clid) |
| Sidechain | Source + Amount + Attack + Release for per-channel ducking |

The master channel and Effect / Delay / Reverb return strips work identically to μ-Clid.

---

## Preset System

### Full Presets (.muTant)

Full presets save the entire plugin state — all voices, modulators, gate patterns, mixer, and FX. They live in `Documents/TDP/muTant/Presets/`.

- **Load** — select from the preset dropdown in the transport bar, or click the folder icon to open the preset browser.
- **Save** — click the Save button in the transport bar. Enter a name and optional description/category.

### Per-Voice Presets (.muPattern)

Each voice can be saved and loaded independently as a `.muPattern` file in `Documents/TDP/muTant/Voices/`. These include:
- Oscillator settings
- Filter, insert, and level settings
- Gate patterns (both GATE and FILT layers)
- Modulator assignments and sequences

Load via the voice header bar preset dropdown; save via the Save button in the header bar.

---

## Settings

Click the gear icon in the transport bar to open the settings overlay:

| Setting | Description |
|---|---|
| Master Volume | Overall output level (same as the master fader in the mixer) |
| UI Size | Medium or Large — adjusts the window size |
| Tempo (BPM) | Internal clock (also editable from the transport bar BPM field) |

---

## Signal Flow

```
[Osc 1] ─┐
          ├─ X-Mod (FM/AM/Ring/Sync) ─→ Sum ─→ Filter ─→ Gate ─→ Insert ─→ Mixer channel
[Osc 2] ─┘                                       ↑
[Noise] ──────────────────────────────────────────┘

Gate:    0/1 amplitude gate (GATE layer) — silent while stopped
         Filter-cutoff gate (FILT layer) — modulates cutoff, no sound cut

Mixer:   Channel strips → Effect / Delay / Reverb sends → master → audio output
```

---

## Tips and Techniques

**Starting from silence** — press Play. If no envelopes are drawn in the gate editor, the output is silent. Draw at least one cell on the GATE layer to hear sound.

**Auditioning the raw drone** — press Bypass in the gate editor. The uncut oscillator/filter tone plays continuously regardless of the gate.

**Layered pads** — add 2–4 voices with different Semi offsets (e.g. 0, 2, 4 for a triad within the scale) and different gate patterns with staggered timing. Each voice's Level knob controls how loud it sits in the mix.

**Filter morphing** — draw a slow attack/fast decay envelope on the FILT layer and set FEnv = 1.0. The filter opens slowly then snaps closed on each gate event — a classic resonant sweep.

**Probability for variation** — use the Arrow tool to select envelopes and set their Prob to 70–90 %. The pattern plays slightly differently each loop without feeling random, giving an organic character to repetitive grooves.

**FM drones** — set X-Mod mode to FM, raise X-Mod amount to 60–80, and modulate Osc2 Semi with a step sequencer. Each step adds a new FM timbre as Osc2's ratio changes relative to Osc1.

---

## Files and Folders

| Location | Contains |
|---|---|
| `Documents/TDP/muTant/Presets/` | Full presets (`.muTant`) |
| `Documents/TDP/muTant/Voices/` | Per-voice presets (`.muPattern`) |

---

## Not in μ-Tant (Design Decisions)

- **No amplitude envelope** — the gate stage does this job.
- **No pitch envelope** — use a modulator targeting Osc Semi/Fine/Octave.
- **No note-on / note-off** — oscillators run continuously; MIDI is not used for played notes.
- **No user samples** — the built-in wavetable bank only.
- **No granular** — pure wavetable synthesis.
