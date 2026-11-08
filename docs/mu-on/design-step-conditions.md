# mu-On step conditions, repeats and level (design note, 2026-11-08)

Andy's request (testing mu-On, v1.1.1468): a Repeat setting 0-16 that repeats a step's note at a rate set by a note-value control; a Level per step; a fade in or out of the level across the repeats; a per-step drop-down "Always play, Play on Fill, Play if previous played, Play if previous didn't play"; and a Fill button that is pressed and held, not toggled, which drives Play on Fill. "All these play modes are common to the Elektron sequencers, so research them for clarity." Not built yet; this note records what the Elektron machines do and the decisions Claude proposes. Andy confirms the names and defaults before the build.

## What Elektron does (research)

**Trig conditions** (Digitakt, Analog Rytm, Octatrack): a trig plays only if its condition is true, and it must also pass its probability.
- **FILL**: true only while Fill mode is active. **!FILL** (the negation): true only when Fill is not active.
- **PRE**: true if the most recently evaluated condition on the same track was true; **!PRE** if it was false. PRE and !PRE are skipped when another PRE looks back (a chain of PREs refers to the last non-PRE condition), so a PRE step never decides another PRE step.
- **NEI** (the neighbouring track's condition), **1ST** (first loop of the pattern) and **A:B** (the Ath of every B passes) also exist. mu-On's A:B is the Bars / Length control that replaced the old N:M ratio; NEI and 1ST are not requested.
- The Elektron manual leaves the PRE edge cases thinly described; behaviour here is taken from the manuals and user reports, so these are decisions rather than a clone.

**Retrig / Trig Repeat** (a step's rapid repeats):
- **Rate**: the time between repeats as a note value (1/16 is one per step, 1/12 or 1/24 give triplets, down to 1/80).
- **Length**: how long the repeats last, in steps or fractions of a step.
- **Velocity curve**: a fade across the repeats; negative fades out (-128 is a full fade out over the length, -64 down to half), 0 is flat, positive fades in toward full velocity.
- A repeat run stops when another trig on the track plays.

## Proposed behaviour in mu-On

**Condition drop-down** (Step panel, per step): *Always* (default), *Fill*, *Pre* ("Play if previous played"), *!Pre* ("Play if previous didn't"). The condition is checked after the step's loop (Bars) and before its probability draw; the step plays only if all three pass.
- **Fill** is true while the Fill button is held, sampled at the step's trigger time. It is a momentary state, not saved with the preset or the project.
- **Pre** looks back along the same track to the nearest earlier step that is on and whose condition is not Pre or !Pre (across the row's wrap, so step 1 sees the row's last such step), and asks whether that step actually played (loop, condition and probability all passed). With no such step, Pre is false and !Pre is true.
- **Fill button**: press and hold, never latched. Placed beside the transport controls so it can be held while playing; the status bar shows "Fill: On" / "Off" like any control. It can be mapped to a controller later (the control surface models it as momentary).

**Repeat**: a step gets **Repeat** 0-16 extra hits after the first (0 = off), **Rate** the time between hits as a note value (1/4 down to 1/64 plus triplets, default 1/16), and **Fade** -100..+100 percent, applied across the repeats (negative fades the level out to that share, positive fades in from it, 0 flat). Repeats stop at the next step that fires, as on Elektron. A repeat is a retrigger of the step's own note (a Bass repeat restarts the note, a Hat repeats the hit).

**Level**: a per-step level 0-100 percent of the step's velocity (default 100), multiplied with accent / ghost, the Fade and the layer Level. It sits with the other per-step values in the Step panel.

## Built (2026-11-08) and where it differs from the proposal
- **Conditions and Fill** (#1792): as proposed. Fill travels as `ProcessorBase::setFillHeld` -> `RenderContext::fill` (the old reserved byte) -> `Track::stepPlays(step, pass, fill)`. The button sits at the bottom-left of the steps box.
- **Repeats, Rate, Fade, Level** (#1791): in `StepRow` (`stepRepeats` 0..16, `stepRepeatRate`, `stepRepeatFade` -100..+100, `stepLevel` 0..100); `StepClock` multiplies the step's velocity by its Level and hands the Track the repeat settings and the beat of the next step that is on; `Track` plans the hits (`step_repeats::plan`, `Sequencer/StepRepeats.h`), plays them as retriggers at the level the Fade gives, and a repeat run ends before the next step that is on. A repeat is `tied = false`, and a Bass or percussion gate is cut to 0.9 of the gap.
- **Step panel:** Bars | Length, Repeat | Rate, then Octave, Note and Condition on the left; Prob, Level and Fade as knobs in the column beside them. A **Chord** step has no room for Level and the repeats in its column (they keep their defaults); Andy to say whether the Chord engine should get them.
- **Known limit:** an engine takes one trigger per audio block, so two repeats landing in the same block (a 1/64 repeat on a host block longer than about 1500 samples at 120 bpm) play as one, the later.

## Open points for Andy
1. Names: **Repeat**, **Rate**, **Fade**, **Level**, **Condition**; the Fill button called **Fill**.
2. Chord steps: Level and the repeats are not offered (no room); worth a rethink of that column?
3. Fade is -100..+100 percent (Elektron's curve is -128..+127).
