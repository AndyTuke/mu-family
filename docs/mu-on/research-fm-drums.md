# mu-On: FM percussion engine, research and decision paper

Status: research paper (rev 2, 2026-10-21, refocused on toms and techno percussion). **Andy approved the engine on 2026-10-21** as "FM Perc", one engine with eight algorithms and one fixed set of ten knobs; the structure, names and the answers to questions 2, 7 and 8 below are ruled in [design-plugin-family.md](../design-plugin-family.md) ("mu-On FM Perc engine") and [design-naming.md](../design-naming.md) (disc **F**, tag `fmp`; helpers stay in mu-On). Questions 3 to 6 stay open for the listening pass (Andy). No code changed yet.

Honesty note. [V] = verified from a page I actually read (URL given). [D] = my own design starting point, derived from FM theory plus the verified sources; **not auditioned, tune by ear**. [K] = from my general knowledge, not found on the web in this session. Gaps I could not close: the Digitone II "FM DRUM" parameter list, DX7/Dexed drum patch numbers, Volca FM / Jomox / Korg / Vital percussion, Chowning's drum chapter, Simmons SDS-5 circuit details, numeric dive amounts for the 808 tom (the sources say only "subtle"). Nothing is attributed to them.

---

## 0. Refocus: Toms and techno percussion

Andy's brief: a proper **tom** engine, plus **more interesting FM hits for techno** (not another kick/snare/hat; mu-On has those). Recommendation, in one line: **one "FM Perc" engine with an Algorithm selector of 8 voice topologies** (Tom and Conga first, then Zap, Click, Clank, Wood, Pluck, Burst), 10 knobs, sharing one operator core. Tom is algorithm 1 and gets the most care.

### 0.1 Toms

**What the real references do**
- 808 toms are the bass drum's bridged-T resonator at higher pitches. Nominal tunings (low / centre / high): low tom 80 / 90 / 100 Hz, decay about 200 ms; mid tom 120 / 135 / 160 Hz, about 130 ms; high tom 165 / 185 / 220 Hz, about 100 ms. Congas: low 165 / 185 / 220 Hz (about 180 ms), mid 250 / 280 / 310 Hz (about 100 ms), high 370 / 400 / 455 Hz (about 80 ms). [V] https://www.baratatronix.com/blog/808-tom-synthesis and https://www.baratatronix.com/blog/808-conga-synthesis
- The 808 pitch dive is **subtle**, "less like a boing, more like a tonk"; there is a **click at the attack**; the tom adds **filtered noise ("fake reverb")** with an envelope slightly longer than the body, which the conga lacks; light drive brightens only the attack; velocity to filter resonance/pitch is suggested as a variation (louder = brighter, softer = clickier). No numeric dive amount given. [V] same pages.
- Plaits' snare uses a drum body of two modes with the second at a fixed **1.47x** ("the ratio between the two modes of the drum"), with a 7 ms FM envelope, and reflects the phase near reset for a "knock". [V] https://raw.githubusercontent.com/pichenettes/eurorack/master/plaits/dsp/drums/synthetic_snare_drum.h
- Plaits' bass drum: FM-style pitch envelope (time constant `0.008 s*(1+decay*4)`), phase held 1.3 ms at the start (no pop), click via SVF low-pass 5 kHz Q 2, transistor-style soft clip. [V] .../synthetic_bass_drum.h
- Electro / Simmons-style toms are noise-click plus a **large, fast downward sweep** (the "pew" tom). I found no source for the SDS-5 circuit; the sweep numbers below are [D].
- Machinedrum TRX tom has a "MTAL" (metal) parameter. [V as search summary only] http://tarekith.com/assets/machinedrum_tipsandtricks.htm
- Membrane modes of an ideal circular drumhead sit at about **1.59, 2.14, 2.30** times the fundamental (ratios of Bessel zeros 3.832, 5.136, 5.520 to 2.405). [K] Verify against a membrane reference before quoting it in a manual. This is the physical basis of the "skin-mode second partial".
- Timbre-preservation rule across pitch: keep the modulation index and the C:M ratio fixed while changing pitch. [V as a search summary of an FM guide] https://www.etherdiver.com/how-to-synth-a-guide-to-programming-synthesizers/fm-basics-i/ (summary read, not the page)

**FM tom topology [D]**
```
 pitch env: f(t) = f0 * (1 + dive * e^(-t/tauD))      (all operators)
 M (ratio R, sine) --index(t)--> C (sine, f0) ---> amp env (decay) --> fold/tanh --> out
 M also heard directly at mix 0.25  = the skin-mode partial (ratio R is the partial)
 Nz (pink-ish, LP) --> amp env (1.3x decay), level = Noise knob       (the 808 "fake reverb")
 click: first 2 to 5 ms of index(t) is high (idx*2), then falls: the attack knock
 feedback on M (0 to 0.3): grain/roughness in the knock
```
- Ratio **R** is the tom's character: 1.0 gives a plain sine tom; **1.41 to 1.5** gives the round "tonk"; **1.59** is the first membrane mode (natural head); 2.14 / 2.30 are higher modes (tight, "fibreglass"); 3.5 turns it into a conga-with-ring / small timpani.
- Starting values per size [D, frequencies from the 808 table above]:

| Voice | f0 | R | Index (peak) | Idx Dec | Dive | Dive time | Decay | Noise | Fdbk |
|---|---|---|---|---|---|---|---|---|---|
| Low tom | 90 Hz | 1.5 | 1.5 | 25 ms | 0.6x | 30 ms | 260 ms | 0.15 | 0.05 |
| Mid tom | 135 Hz | 1.5 | 1.5 | 20 ms | 0.5x | 25 ms | 190 ms | 0.15 | 0.05 |
| High tom | 185 Hz | 1.59 | 1.2 | 15 ms | 0.4x | 20 ms | 140 ms | 0.12 | 0.05 |
| Electro tom (Simmons-like) | 120 Hz | 1.0 | 0.8 | 10 ms | 3.0x | 45 ms | 220 ms | 0.25 | 0 |
| Conga (high) | 400 Hz | 1.59 | 1.0 | 8 ms | 0.25x | 12 ms | 90 ms | 0 | 0 |
| Bongo | 500 Hz | 2.14 | 0.9 | 6 ms | 0.2x | 10 ms | 70 ms | 0 | 0 |
| Timbale (shell ring) | 700 Hz | 2.3 | 1.5 | 6 ms | 0.15x | 8 ms | 160 ms | 0.1 | 0.1 |

**Tuned tom set, so one engine plays as a kit across pitches**
- Several tracks, each a Tom with its own Tune (mu-On tracks are independent, so a three-tom kit is three tracks), or **one tom track modulated in pitch**.
- Make Tune an **absolute-semitone mapping** (MIDI-note-like, e.g. D#2 to A4 shown in notes, skewed range 50 to 800 Hz) so pitches read as notes; the 808 centres are F#2 (90), C#3 (135), F#3 (185). [V values via the Baratatronix tables]
- Keep **index, R and the dive ratio constant in relative terms** when Tune changes (timbre rule above), and scale **decay by about `(f0/90)^-0.5`** so higher toms are shorter as on the 808 (decays 200 / 130 / 100 ms from 90 / 135 / 185 Hz, which is about `f^-0.8`; use the exponent as a hidden per-algorithm constant) [D].
- Dive time should shrink with pitch (a fixed number of cycles is more natural than fixed ms) [D].
- Optional "Tom set" per-step pitch: today mu-On has **no per-step pitch** (the Bass doc lists it as not implemented). Until it exists, a per-track ControlSequence modulator on Tune gives tom fills (see 0.3).

### 0.2 Techno percussion: the 8 voice algorithms

Common core: operators are phase-modulated sines, `y = sin(2pi*(phase + idx(t)*mod))`; `idx(t)` is the **FM-amount envelope** (a decay-only ExpDecay, "Idx Dec"); a **pitch-dive envelope** multiplies all frequencies; amp env is decay-only; an optional noise source; a fold/tanh stage ("Fold"). Algorithms differ only in routing, fixed per-algorithm constants and a fixed output filter. All values below are [D] unless marked.

| # | Algorithm | Routing | What makes it interesting for techno | Sources / basis |
|---|---|---|---|---|
| 1 | **Tom** | M(R) -> C, M audible at 0.25, +noise | tonal, tuneable, 1.0 to 2.3 morph from tom to timbale | 0.1 |
| 2 | **Conga/Skin** | as Tom, shorter, no noise, small dive, high attack index | tuned "tonk" for polyrhythms; velocity -> index | Baratatronix conga [V] |
| 3 | **Zap** | M(R, high) -> C, big dive 4 to 12x, short | the "laser" or "pew"; Idx Dec long vs Dive time short gives sci-fi vs thud | Laser zap = one osc with fast down sweep [V, summary] https://www.musicradar.com/tuition/tech/10-classic-synth-fx-and-how-to-create-them-634904 ; ModWiggler: feeding VCA output into exponential FM gives a tom/snare-like pitch curve, velocity changes pitch change [V, summary] https://modwiggler.com/forum/viewtopic.php?t=207309 (page itself returned 403) |
| 4 | **Click/Rim** | C at 1.5 to 4 kHz, M at R about 1.4 to 2.7, index envelope 1 to 3 ms, decay 10 to 40 ms | rim/clave/wood tick; Ratio changes pitch character from "clave" to "block" | Bitwig: short modulator decays and inharmonic ratios for percussion [V] https://polarity.me/bitwig-classic-sounddesign/007-epiano-bells-percussions/ |
| 5 | **Clank** | three M->C pairs (ratios from the 808 set 1, 1.304, 1.466, 1.787, 1.932, 2.536) summed, HP about 2 to 6 kHz, decay 20 to 400 ms | industrial/metal hit, open or closed (hat-like tick at decay under 30 ms); Ratio stretches the whole set (inharmonicity) | 808 ratios [V] https://raw.githubusercontent.com/pichenettes/eurorack/master/plaits/dsp/drums/hi_hat.h ; Reid: cymbal as six oscillators in three FM pairs [V] https://www.soundonsound.com/techniques/synthesizing-bells |
| 6 | **Wood/Mallet** | M(R) -> C with R about 3.9 to 4 (tuned mallet), index envelope very short, 2nd partial decays 3x faster | marimba/wood-block melodic perc; tunable with a key track | Marimba bar overtone ratios (about 4 and 10 on tuned bars; 2.76 on a free bar) [K, unverified]; inharmonic non-integer ratios give tuned percussion [V] https://tonalux.org/blog/fm-synthesis-bessel-functions-sideband-math |
| 7 | **Pluck/Ping** | C with self-feedback, integer R (1, 2, 3), index envelope decaying over 5 to 60 ms, decay 100 to 600 ms | resonant plucks, "filter ping", acid-ish stabs; a rising dive (negative) gives upward chirps | Plaits FM engine feedback modes [V] https://raw.githubusercontent.com/pichenettes/eurorack/master/plaits/dsp/engine/fm_engine.cc |
| 8 | **Burst/Glitch** | high feedback (0.7 to 1.0) + Noise, M at non-integer R; very short 5 to 30 ms | noise-FM burst, glitch, "digital crunch" hit; feedback chaos acts as the noise | Feedback FM becomes noise-like at high feedback [K]; Operator hat/snare use noise oscillators [V] https://www.producerspot.com/ableton-live-tutorial-how-to-synthesize-percussion-in-operator-recipes/ |

Recipe detail for the techno ones [D]:
- **Zap**: f0 800 Hz, R 2.7, index 3 -> 0 over 40 ms, dive 8x over 25 ms, decay 150 ms, Fold 0.3. Knobs that matter: Dive (depth of pew), Dive Time, Index (grit), Ratio (metallic vs smooth). Raise Idx Dec for "screamer", lower for "blip".
- **Click**: f0 2.2 kHz, R 1.41, index 2 -> 0 over 2 ms, dive 0.2x over 3 ms, decay 25 ms. Increase Fdbk for a "tick with grit".
- **Clank**: f0 (scales the six ratios) 500 Hz, R 1.0, index 3, Idx Dec 60 ms, decay 120 ms, HP 3 kHz. Fold adds industrial bite; Ratio above 1 pulls the set apart (more bell/gong), below 1 packs it (more hat).
- **Wood**: f0 400 to 1200 Hz, R 3.98 (try 4), index 0.8 -> 0 over 6 ms, decay 120 ms; the audible M at 0.25 gives the upper partial.
- **Pluck**: f0 200 to 800 Hz, R 1 or 2, Fdbk 0.3, index 2.5 -> 0.2 over 40 ms, decay 400 ms, Dive negative small (+0.1x) for a "rising ping".
- **Burst**: f0 1 kHz, R 1.73, Fdbk 0.9, Noise 0.4, index 4 -> 1 over 10 ms, decay 40 ms, Fold 0.5.
- **Not in the first set (and why)**: clap (needs a multi-retrigger envelope), true resonant filter pings (the Pluck stands in), ring-mod shimmer (can be added later as an Algo 9: square x saw pairs from Plaits' RingModNoise, but carries more aliasing risk, see 0.4). Plaits ring-mod pairs: 200/7530, 510/8075, 730/10500 Hz [V] .../plaits/dsp/drums/hi_hat.h

### 0.3 One engine, 10 knobs + Algorithm; modulation and the sequencer

| id (`t{N}_fmp_<id>`) | Label | Range | Default | Mod target |
|---|---|---|---|---|
| `algo` | Algorithm (choice, 8) | Tom, Conga, Zap, Click, Clank, Wood, Pluck, Burst | Tom | no (like Bass wave) |
| `tune` | Tune | 40 to 4000 Hz, skewed; shown in notes | 90 Hz | yes |
| `ratio` | Ratio | 0.5 to 6 (fine below 3) | 1.5 | yes |
| `index` | Index | 0 to 1 -> 0 to about 8 rad (algo-scaled) | 0.35 | yes |
| `idec` | FM Env (Idx Dec) | 1 to 500 ms | 25 ms | yes |
| `fdbk` | Feedback | 0 to 1 | 0.05 | yes |
| `dive` | Dive | -1 (rise) to +12x, 0 = none | 0.6 | yes |
| `dtime` | Dive Time | 2 to 200 ms | 30 ms | yes |
| `decay` | Decay | 10 to 1500 ms | 260 ms | yes |
| `fold` | Fold | 0 to 1 (tanh into wavefold) | 0.15 | yes |
| `noise` | Noise | 0 to 1 | 0.15 | yes |

Ten floats plus the choice: the same count as Bass (10 floats + Wave), so the existing knob-grid layout should apply (not verified in `ParamKnobGrid`). Output filter per algorithm (HP for Clank, LP for Tom, none for Zap) is fixed, via mu-core `MultiModeFilter` as Bass does.
- **Defaults per algorithm**: the single APVTS default is the Tom values. Choose between (a) knobs relative to the algorithm base, with the shipped `.muTrack` factory presets giving each algorithm's starting set (tables above), or (b) loading the table on algorithm change. Recommend (a)+presets (Andy's "presets may change" rule makes this cheap). Algorithm is saved as a choice; only append rows. **Ruled 2026-10-21: (a).** Tune and Ratio stay absolute; the other eight floats are stored 0..1 and scaled per algorithm, so the units in the table above are the Tom's reading of the knob, not the stored range.
- **Interesting modulation targets** (all ten floats): Ratio (the most fun: a stepped or random ModSequence on Ratio makes bell/zap melodies and glitches), Index and Idx Dec (accent "attack"), Dive and Dive Time (pitch-fall rhythms), Tune (fills, toms played as a scale), Feedback (growl), Fold. Depth is percent of the knob range, as per family rule; ratio is a continuous knob so a modulated ratio sweeps through inharmonic values.
- **Per-step sequencing today**: the 16-step row has on/accent only (accent = velocity from `t{N}_accent`); there is **no per-step pitch/ratio** (design-engine.md lists per-step pitch as not implemented). What the engine can do with velocity alone [D]: velocity scales **Index and Dive** (harder hit = brighter, deeper dive; 808 variation: velocity to resonance/pitch [V] Baratatronix pages) and amp. Anything more (per-step Ratio/Tune lanes) comes either from the track's **ControlSequence modulators** (already supported) or from a later per-step lane; flag as a follow-up, not part of this engine.

### 0.4 CPU and aliasing notes (for this engine)
- Per voice per sample: 2 to 3 sine ops (+ up to 6 for Clank), 3 ExpDecay (index, dive, amp), noise, fold: about 100 to 200 cycles at 1x. Clank at 2x is the worst case (about 6 ops x 2 x 20 cycles + filters). Estimate, unmeasured. Silent voices return early. Eight FM Perc tracks all playing Clank at 2x would use a few percent of a core; typical use is below 1% [D, estimate].
- Sine: table (1k to 2k, linear interp) or polynomial instead of `std::sin`; keep `std::sin` as the test reference.
- Aliasing: the sources agree that oversampling plus index reduction at high pitch is the practical answer. Plaits' FM engine uses **4x oversampling with FIR downsampling** and index taming `hf_taming = 1 - (note-72)*0.025`, squared. [V] .../fm_engine.cc ; KVR threads confirm oversampling is standard, index reduction is the DX-era approach, band-limited-impulse FM is theory only. [V summary] https://www.kvraudio.com/forum/viewtopic.php?t=500251
  - Tom, Conga, Wood, Click, Pluck: carriers mostly below 2 to 3 kHz and indexes fall fast: **1x with index taming**.
  - Zap, Clank, Burst: high ratios and big dives sweep through Nyquist: **2x half-band** (mu-core `OversampledProcessor` if it fits per-voice use, otherwise a small half-band; I did not read its API) or accept aliasing as a characterful lo-fi option exposed per algorithm.
  - Feedback: average the last two feedback samples and clamp the gain; at Burst's high values accept chaos.
  - DC: 1-pole blocker at about 8 Hz after the fold. Start-click: hold the phase a quarter-cycle for about 1 ms or start at a zero crossing (Plaits' bass drum holds the phase 1.3 ms). [V] .../synthetic_bass_drum.h
  - Noise from the per-slot seeded generator (`mu_core::noiseSeedFor`) so golden renders stay deterministic.

---

## 1. mu-On today and what the engine must provide (trimmed)

- A track is `Track : Layer`; its engine is the sound (Kick, Bass, Hat, Snare, Rumble). Engines are classes in `mu-on/Source/Audio/`, wrapped by the `TrackEngine` variant (`prepare`, `reset`, `setParams(float*)`, `trigger(vel, sampleOffset)`, `render`). Sample-accurate triggers, mono, `ExpDecay` envelopes, `KickEngine` uses `std::sin`.
- Params are APVTS floats `t{N}_<tag>_<x>`, rows for every engine on every track; ids and order never change, rows append only. Mod tables (`MuOnModDest.h`) are per engine and **must match the `setParams` argument order**; choice params are not targets. Knob counts today: Kick 5, Bass 10 + Wave, Hat 2, Snare 2, Rumble 9.
- To add the engine: append `EngineType::FmPerc` (after Rumble), `TrackEngine` arm, `TrackParams` rows and name tables, `kFmPercDests[]` with `allUnderPrefix(..., "fmp_")`, `TrackTypes`, a `loadDefaultGroove` case, and a mu-core `LayerTypeId::FmPerc` that must sit between Rumble and Test because `layerTypeOf` relies on contiguity. I did not audit `Track.cpp` or the processor for other "Rumble is last" assumptions or `ParamKnobGrid` capacity.
- Reusable mu-core: `ExpDecay`, `MultiModeFilter`, `NoiseSeed`, `OversampledProcessor`, mixer inserts.

## 2. Reference recipes kept from the first pass (condensed)

The generic drum recipes (kick, hat, snare, bell, 808 metal ratios, OPL rhythm mode) are no longer the focus. What is kept and used above: the Plaits 808 ratio set, the ring-mod pair frequencies, Plaits bass-drum click/hold, the 1.47x snare mode ratio, the 1:1.4 and 1:3.5 bell ratios with a falling index [V] https://tonalux.org/blog/fm-synthesis-bessel-functions-sideband-math , and the OPL rhythm-mode trick (XOR of phase bits plus an LFSR; Nuked-OPL3 is LGPL, do not copy code) [V] https://raw.githubusercontent.com/nukeykt/Nuked-OPL3/master/opl3.c , which could be a future lo-fi "Clank" variant. Digitone II FM DRUM, from summaries only: three operators (some sources say four), sweep depth/time, noise transient, pre-filter wavefolder, two FM-index envelopes [V summary] https://www.musicradar.com/music-tech/synths/pull-your-sound-through-the-prism-and-into-a-spectrum-of-dazzling-tones-elektrons-digitone-ii-adds-fm-drums-phase-distortion-comb-filters-and-more ; FM TONE has per-operator ratios, feedback, key-tracked index reduction [V] https://synthmagazine.com/elektron-digitone-ii-dissecting-fm-tone-synthesis-and-workflow/ . Operator tom advice: use the kick patch pitched up [V summary]. Plaits is MIT; ideas and constants are free to use, code copying needs the notice.

## 3. Risks and family fit
- **Fit**: one new track type, product-side `FmPercEngine.h`, neutral mu-core `LayerTypeId`, generic operator/oversampler helpers can move to mu-core when a second product wants them (standing check: moves toward one framework, no per-product duplicate of shared behaviour).
- **Risks**: (1) one default set vs eight voicings (presets, relative knobs); (2) sound quality of every [D] number is unproven, needs a listening pass; (3) aliasing in Zap/Clank/Burst; (4) feedback chaos and DC; (5) Tom must feel better than a Kick pitched up, so the dive, knock and noise tail need most of the tuning time; (6) overlap with the sample Hat/Snare and the Kick (Click/Clank hats ticks vs Hat engine); (7) unaudited Rumble-last assumptions; (8) no per-step pitch lane yet, so "tuned toms" rely on separate tracks or a modulator.
- **Tests**: ApvtsLayout row count, mod-table order, silent-before-trigger, tail below -90 dB, no NaN at extremes for all eight algorithms, high-band energy check for Clank/Zap, golden renders.

## 4. Open questions for Andy
1. Is "FM Perc" with 8 algorithms the right shape, or should Tom be its own engine (more tom-specific knobs: skin mode, head tension) and the rest a second engine?
2. Per-algorithm defaults: relative knobs plus factory presets (recommended) or load-on-change?
3. Accept the first set (Tom, Conga, Zap, Click, Clank, Wood, Pluck, Burst), or swap any (e.g. add ring-mod shimmer, clap)?
4. Is 2x oversampling for Zap/Clank/Burst acceptable, or is deliberate aliasing wanted as a character?
5. Should Tune be shown in notes with a snap option so tom sets are tuned to a scale?
6. Do you want a per-step pitch/ratio lane in the sequencer (a separate, larger item) so this engine can play melodic perc and tuned tom fills without modulators?
7. Name and disc letter ("FM Perc", "P"?). Needs the ux-controller and architecture-steward.
8. Operator/oversampler helpers in mu-core now, or in mu-On until a second user?

## Sources actually read
- Baratatronix 808 tom and conga pages: https://www.baratatronix.com/blog/808-tom-synthesis , https://www.baratatronix.com/blog/808-conga-synthesis
- Plaits (MIT): https://raw.githubusercontent.com/pichenettes/eurorack/master/plaits/dsp/drums/synthetic_bass_drum.h , .../synthetic_snare_drum.h , .../hi_hat.h , .../plaits/dsp/engine/fm_engine.cc
- Operator recipes: https://www.producerspot.com/ableton-live-tutorial-how-to-synthesize-percussion-in-operator-recipes/
- SOS bells/cymbal: https://www.soundonsound.com/techniques/synthesizing-bells ; FM ratios: https://tonalux.org/blog/fm-synthesis-bessel-functions-sideband-math ; Bitwig: https://polarity.me/bitwig-classic-sounddesign/007-epiano-bells-percussions/
- Digitone II FM TONE: https://synthmagazine.com/elektron-digitone-ii-dissecting-fm-tone-synthesis-and-workflow/
- Nuked-OPL3: https://raw.githubusercontent.com/nukeykt/Nuked-OPL3/master/opl3.c ; KVR aliasing: https://www.kvraudio.com/forum/viewtopic.php?t=500251
- Search summaries only (page not opened or blocked): ModWiggler zap thread (403), Machinedrum tips, MusicRadar articles, javelinart FM PDF (binary, unreadable), etherdiver FM basics, DX7 drum collections.
