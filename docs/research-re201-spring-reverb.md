# Research note: RE-201 Space Echo and a spring-reverb insert (backlog row 1720)

Status: research only, nothing built. Facts carry a source link; "UNVERIFIED" means I could not confirm it from a source I could open. I found no Roland service manual or schematic online, so circuit-level claims are limited.

## 1. What our tape echo models today

Source: [TapeDelayInsert.h](../mu-core/Audio/FX/Insert/TapeDelayInsert.h), insert algorithm 15 ("Tape Delay").

- **Heads / modes:** none. One delay line, one read point. No repeat-rate modes and no multi-head combinations.
- **Time:** 20 ms to 1 s (log), glided with a one-pole of about 120 ms, so moving it bends the pitch like a tape-speed change. Read with a 4-point Hermite interpolator.
- **Feedback:** 0 to 110 %. Above 100 % the loop runs away and the tanh saturation holds it. There is no separate limiter.
- **Loop path:** low cut at about 90 Hz (also blocks DC), two-pole treble cut (9 kHz fresh, 2.5 kHz worn), then tanh saturation (drive 1 to 2.5 with Wear).
- **Wow and flutter:** wow about 0.6 Hz, flutter about 7 Hz plus a little random drift, depth scaled by Wear and by delay length. The right channel's modulation phase is offset for width.
- **Mix:** plain dry/wet crossfade (`in*(1-m) + wet*m`), smoothed. The fed-back signal is the post-filter, post-saturation signal. Echo only reaches the output through Mix.
- **Stereo:** two independent delay lines (one per channel, no cross-feed, no ping-pong). Extra channels beyond two pass through.
- **Parameters:** four slots: Time, Feedback, Mix, Wear. No bass/treble controls, no tape hiss.

The header describes it as "Echoplex / Space Echo character", so it is a generic tape delay, not a close RE-201 model.

## 2. The Roland RE-201

Facts (sources in brackets):

- Free-running tape loop, one record head and three playback heads, a variable-speed motor, and a built-in spring reverb; the RE-201 was the first Space Echo to include the reverb. [Wikipedia: Roland Space Echo](https://en.wikipedia.org/wiki/Roland_Space_Echo), [Gearnews](https://www.gearnews.com/roland-space-echo-re-201)
- Controls: Repeat Rate, Intensity, Echo Volume, Reverb Volume, Bass, Treble, and the Mode Selector. [Sound On Sound, RE-202/RE-2 review](https://www.soundonsound.com/reviews/boss-space-echo-re-202-re-2)
- Mode Selector: 12 positions. Modes 1 to 4 are echo only; modes 5 to 11 add spring reverb to the echo; mode 12 is reverb only. [Sound On Sound](https://www.soundonsound.com/reviews/boss-space-echo-re-202-re-2), [MusicRadar producer's guide](https://musicradar.com/news/producers-guide-roland-space-echo)
- Head map as reported: modes 1 to 3 use heads 1, 2, 3 singly; mode 4 uses heads 2 and 3; modes 8 to 10 are two-head combinations with reverb; mode 11 is all three with reverb. Sources disagree on the exact map, and one source puts the reverb-only position at 6 o'clock. UNVERIFIED: the full 12-position table. The Boss RE-2 manual is said to show 11 of the 12 head combinations and is the best next reference.
- Repeat Rate changes tape speed, which changes the delay time and the pitch of repeats. Tone shifts with delay time. [Sound On Sound](https://www.soundonsound.com/reviews/boss-space-echo-re-202-re-2)
- Intensity is feedback; at maximum it runs away into dub-style oscillation. [Sound On Sound](https://www.soundonsound.com/reviews/boss-space-echo-re-202-re-2)
- The spring reverb is short with a "noticeably metallic" character. [Sound On Sound](https://www.soundonsound.com/reviews/boss-space-echo-re-202-re-2)
- The RE-201's maximum delay is half that of the RE-202/RE-2. [Sound On Sound](https://www.soundonsound.com/reviews/boss-space-echo-re-202-re-2)
- The original has tape hiss, which the Boss models omit. [Sound On Sound](https://www.soundonsound.com/reviews/boss-space-echo-re-202-re-2)

UNVERIFIED (no source found): exact delay times in ms per head and speed; spring tank length, decay time (RT60) and bandwidth; whether the reverb send is taken before or after the tape loop, and whether echo repeats feed the tank (the Wikipedia article says the user sets "the ratio of the echo and reverb sounds", which suggests a parallel send, but this is not a circuit fact); Bass/Treble corner frequencies. I would not put numbers on these until we find a schematic or measure a unit.

Emulations:

- **Boss RE-202 / RE-2** (Roland's own digital models): modelled from analysis of several vintage RE-201s; they reproduce non-linear tape behaviour, wow and flutter, saturation, tape age, preamp colour, and the spring reverb (stereo on the RE-202); the RE-202 adds a fourth virtual head. [Sound On Sound](https://www.soundonsound.com/reviews/boss-space-echo-re-202-re-2), [DJ Mag](https://djmag.com/tech/boss-announces-new-space-echo-fx-pedals)
- Plugins: IK Multimedia T-RackS Space Delay is listed as a model of the RE-201 ([Full Compass](https://www.fullcompass.com/prod/619499-ik-multimedia-t-racks-space-delay-model-of-roland-re-201-space-echo-virtual)); a Universal Audio RE-201 plugin exists ([Audiofanzine](https://en.audiofanzine.com/plugin-delay-echo/universal-audio/RE-201-Space-Echo/user_reviews/r.91515.html)). I did not read their feature lists.

## 3. Comparison

| RE-201 feature | Our Tape Delay | Gap |
|---|---|---|
| 3 playback heads, 12 modes (head combos, reverb on/off) | One read point | Large. Multi-tap reads of the same loop are cheap (extra reads of one line) but need a Mode parameter and a head-spacing rule. |
| Repeat Rate (tape speed) | Time 20 ms to 1 s, glided | Equivalent in spirit. Our range is wider than the RE-201's. |
| Intensity with self-oscillation | Feedback to 110 %, tanh-limited | Covered. |
| Echo Volume / Reverb Volume | Single Mix (dry/wet) | Different: the RE-201 has separate echo and reverb levels. |
| Bass / Treble | Fixed 90 Hz low cut, Wear-driven treble cut | No user tone control. |
| Tone changes with delay time | Tone fixed by Wear only | Small gap. |
| Wow / flutter | Yes, tied to Wear | Covered (generic). |
| Tape saturation | tanh in the loop | Covered (generic). |
| Tape hiss | None | Optional. |
| Spring reverb | None | The subject of row 1720. |
| Stereo | Two independent lines | The RE-201 is mono in; fine. |

Honest summary: ours is a good generic tape echo, not an RE-201 model. Adding a spring reverb makes the pair a "Space Echo-like" combination; matching the head modes would be a further, separate step.

## 4. Proposed spring-reverb DSP

**Why springs are different:** a spring is strongly dispersive: high frequencies travel faster than low ones, so an impulse comes back as a "chirp", repeated every round trip. That is the "boing" or drip. A normal reverb (Schroeder or FDN) lacks it.

**Literature:**
- Välimäki, Parker, Abel, "Parametric Spring Reverberation Effect", J. Audio Eng. Soc., 2010: two parallel branches of long allpass chains (the chirp), with random delay modulation, cross-coupling and EQ. (via [Aalto thesis record](https://aaltodoc.aalto.fi/items/5b1ff7ca-c56a-4073-9471-06846271625f); I did not read the full text)
- Parker, "Efficient Dispersion Generation Structures for Spring Reverb Emulation", EURASIP J. Adv. Signal Process., 2011: cheaper dispersion than long chains of first-order allpasses, which can be expensive. [Springer](https://asp-eurasipjournals.springeropen.com/articles/10.1155/2011/646134) (page was paywalled to my tool; abstract only via search)
- Abel and Berners patent: dispersive waveguide, with left- and right-going waves processed separately. [US8391504](https://patents.google.com/patent/US8391504)
- Also: Arcas and Abel, "A Virtual Model of Spring Reverberation" ([Academia link](https://www.academia.edu/1370122/A_Virtual_Model_of_Spring_Reverberation)); Parker's thesis ([Aalto, Dispersive Systems in Musical Audio Signal Processing](https://aaltodoc.aalto.fi/items/5b1ff7ca-c56a-4073-9471-06846271625f)). I did not verify the details of these.

**Recommended structure** (my design, following the above):
1. Input: gentle low cut and a band limit (tanks transmit roughly the mid band; UNVERIFIED figures, to be tuned by ear).
2. Two parallel spring branches, each a feedback loop: delay line (about 30 to 60 ms round trip, giving the short-spring drip rate) containing a dispersion block (a cascade of first-order allpasses with a shared coefficient; start with about 60 to 100 stages per branch and tune by listening) plus a lowpass and a gain set by Decay.
3. Small random modulation of the delay (a few samples, slow) to avoid a static comb and give the "wobble".
4. Cross-feed between branches, output EQ, then Mix.
5. Drip control scales the allpass coefficient (more or less chirp). Tension scales the loop delay.

**CPU estimate:** a first-order allpass is about 3 flops per sample; 2 branches x 80 stages x 3 = about 480 flops per sample, around 22 M flops per second per channel at 44.1 kHz. Stereo, mono-summed spring with two output taps, is about 25 to 50 MFLOPs total, similar to a mid-weight reverb. If measured too heavy, Parker's cheaper structures or running the chain at half rate are options. These are my own estimates, not measurements.

**Parameters:** Decay (RT60 about 0.5 to 4 s), Tone (loop damping), Drip (dispersion amount), Tension (round-trip time), Mix. Five parameters; the insert slot UI currently has four (see below).

**Where it fits:**
- It is a new `InsertAlgorithmBase` subclass, like `TapeDelayInsert`, in `mu-core/Audio/FX/Insert/SpringReverbInsert.h`. The `FXSlotBase` interface is for the global FX; the insert path here uses `InsertAlgorithmBase`, so the family docs need to be checked for which is wanted. Open question 1.
- Fixed-size table, what must change (from the grep): `kNumInsertAlgos` in `InsertProcessor.h` (16 to 17); the `dispatch[]` registration in `InsertProcessor.cpp`; the name list and the display-name/sort table in `AlgorithmNames.h`; the default rows and slot config in `InsertSlotConfig.h`; the algorithm number comment in `InsertAlgorithmBase.h`; and the tests that enumerate algorithms (`InsertDSPSmokeTests.cpp`, `MixerBlockValueTests.cpp`, `StereoParityTests.cpp`). If a parameter count above four is wanted, the slot count in `InsertSlotConfig.h` is a wider change; I did not check how many slots exist. Presets that store the algorithm index need the new index appended at 16 so old presets keep working.
- Real-time safety: allocate all delay lines in `prepare()`; no allocation or locks in `process()`; add a tiny DC offset or flush denormals (the feedback tails of a long allpass chain are prone to denormals); clamp feedback below 1 so the loop cannot run away; smooth all parameter changes; make the random modulation deterministic (as Tape Delay does with its xorshift generator).

**Test plan:**
1. Impulse response: finite, decays monotonically, chirp visible in a spectrogram.
2. Measured RT60 within tolerance of the Decay setting.
3. Level: unity-ish at Mix 100 % with a reference tone; no gain jump when moving Mix.
4. Finite output (no NaN, no Inf) at all parameter extremes and with full-scale and silent input; silence in gives silence out after the tail.
5. Block-size independence: render the same input at block sizes 1, 64, 512, 4096 and compare.
6. Stereo parity with the existing insert parity test.
7. Listening render through the cross-plugin listening-test pipeline, with a drum hit and a guitar-like pluck, to judge the drip.

## 5. Open questions for Andy

1. Is a spring reverb as a new insert algorithm (17th slot) what you want, or should it be a global FX slot (`FXSlotBase`) next to the existing reverb?
2. Do you want a true RE-201 recreation (head modes 1 to 12, Echo Volume and Reverb Volume, Bass and Treble), or just a spring reverb that sits well after the current Tape Delay?
3. Is a fifth control acceptable (Decay, Tone, Drip, Tension, Mix), or must it fit four?
4. Should the reverb be mono-in with a stereo spread, or fully stereo?
5. Do you have a real RE-201 or the RE-2/RE-202 to compare against? Without a reference or service manual, decay time and tank bandwidth will be set by ear.
6. Is the "boing" at extremes (a loud, exaggerated drip) wanted, or a tamer vintage tone?
7. May I spend a research pass on the original schematic or a service manual to settle the reverb-send position and the Bass/Treble corners?
