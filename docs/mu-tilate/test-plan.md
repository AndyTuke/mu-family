# mu-Tilate: audition test plan

How to judge what mu-Tilate sounds like without listening. A synthetic loop with known tones and timing goes through the real
engine (`PhrasePlayback`, the same code the plugin runs), each scenario is written to a WAV and measured, and the numbers are
checked against what the design says must come out. The tests live in
[AuditionTests.cpp](../../mu-tilate/Source/Tests/AuditionTests.cpp) (category `mu-tilate-audition`) and run with the rest of
`mu-tilate-tests`.

```
cmake --build build --config Debug --target mu-tilate-tests
build/mu-tilate/mu-tilate-tests_artefacts/Debug/mu-tilate-tests.exe mu-tilate-audition
```

Output: one line of metrics per scenario on the console, `tests/_out/tilate/<scenario>.wav` (open one to hear it) and
`tests/_out/tilate/report.json` (all the metric lines). A failure names the scenario and the measure that missed.

## The material

Four one-beat slices at 120 BPM (0.5 s each). Slice k is a decaying sine at 220, 330, 440 and 550 Hz with a short noise click at
its start. The click gives a clean attack time; the tone gives a clean pitch. Both survive the stretcher and the re-pitch, so the
result can be measured: attack times from the click, pitch from the autocorrelation, level from RMS, cuts from the largest
sample-to-sample step.

## Scenarios (done)

| Scenario | What it proves | Pass when |
|---|---|---|
| `off_120` | Off plays the loop as it is | attacks on 0, .5, 1, 1.5 s (6 ms); pitches 220/330/440/550 (1 %) |
| `match_150_repitch`, `match_90_repitch` | Pitch/Stretch knob at 0: the pitch follows the tempo | attacks on the new beats; pitch x1.25 / x0.75 (3 %) |
| `match_150_stretch`, `match_90_stretch` | Knob at 1: time-stretch keeps the pitch | attacks on the new beats; pitch unchanged (3 %) |
| `match_150_blend` | Knob at 0.5: half the pitch change | pitch x sqrt(1.25) (4 %) |
| `trim_start` | Left-edge drag skips the start of the slice | first sound at 0.25 s, pitch unchanged, ends with the slice |
| `retime_x2` | Alt-drag stretches a slice over two beats, same pitch | pitch kept; still sounding in the second beat; silent after |
| `extend_into_next` | A block made longer than its slice runs on into the loop | pitch 220 first, then 330 (the next slice) |
| `cut_no_click` | A block cut mid-tone is declicked | step across the cut under 0.05 (a tone's own slope is 0.023) |
| `reverse_hit` | A reversed hit plays backwards | louder at the end than at the start |
| `layer_pitch_12` | The layer's pitch setting transposes | 220 Hz becomes 440 Hz |
| `layer_filter_lowpass_300` | The layer filter acts | 550 Hz tone at least 6 dB down |

The last two exercise the `PhraseSettings` fields that the modulation targets drive, so a target that stops reaching the sound
shows up here.

## Planned (not built yet)

- **Modulation reaching the sound.** Drive a target through the real `ModulationMatrix` (a stepped control sequence on cutoff,
  pitch, pan, amp attack) and measure the change per step. Needs a `Phrase` rig rather than the bare scheduler.
- **Detection.** The same material through the detector (Transients, Soft-tonal, Even): the slice boundaries land within a few ms
  of the clicks; Sensitivity and Gap move the count in the right direction.
- **Audition buttons.** `Phrase::startPreview` (original, at tempo, slice): length and pitch of each against the expected values,
  and the looped ones keep sounding.
- **Tape Delay and Tape Spring.** A click through each: echo spacing for known Time / Rate values, feedback decay per repeat,
  and with Sync on the repeat spacing of n sixteenths at 60, 90, 120 and 150 BPM; Wear darkens the repeats (spectral centroid
  falls per pass).
- **Dropped and split blocks.** The timeline after `Timeline::drop` rendered: the covered block's remainder sounds from where the
  dropped block ends, at the right pitch.
- **Plugin level.** The same scenarios through `PluginProcessor` with a loaded WAV, host tempo changes mid-render, and the
  mixer strip in the path.

## Rules

- A scenario's tolerance is written next to its check with the reason; do not widen it to make a failure pass: look at the WAV.
- Goldens are separate (family rule: left stale while sounds change). These tests check relationships (pitch ratio, attack
  time), so they survive deliberate sound changes.
- A new engine feature adds a scenario here in the same change.
