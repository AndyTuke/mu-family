# Signalsmith Stretch prototype: results (2026-10-31, corrected after the dsp-controller review)

Answers the three open questions in [design-engine.md](design-engine.md#tempo-matching-and-time-stretch) ("How the stretcher is driven"). Code: [tests/prototypes/stretch/](../../tests/prototypes/stretch/README.md) (standalone, not in the product build). Library: Signalsmith Stretch (MIT; the header reports 1.3.2, tag 1.4.0) and Signalsmith Linear 0.6.6 (MIT), vendored under `ThirdParty/`. Release build, 48 kHz, **mono**, synthetic slices (kick, snare, soft pad), 0.5 s each, stretched to 1.25x, 1.6x and 0.8x their length.

**Nothing here was listened to.** The numbers are waveform measurements on synthetic material. They decide what to build, not whether it sounds good; the sound needs Andy's ears on real loops. The `dsp-controller` reviewed this note and the harness (Pass with notes, no Blocker); its corrections are in below.

## 1. Does a varying local rate (hold, ramp, plateau) protect the attack? No gain from it.

The stretcher was driven with a varying input count per output block (rate 1.0 for the hold, a linear ramp, then the plateau that lands the slice on its target length). Over the first 12 ms and the energy of the first 10 ms, it is the same as a uniform stretch (kick, 1.25x, default window):

| drive | correlation with source | energy of first 10 ms vs source |
|---|---|---|
| uniform | 0.986 | 0.58 |
| hold 10 ms, ramp 30 ms | 0.986 | 0.57 |
| hold 25 ms, ramp 60 ms | 0.985 | 0.56 |
| dry head 10 ms + 5 ms cross-fade | 0.986 | 1.00 (by construction) |

The same holds for the snare, the other ratios and the short window. The stretcher samples the input position once per hop (default window: 30 ms), so a 10 ms hold plus 30 ms ramp is about one hop and cannot be expressed finely at all. For this library the analysis window, not the fed rate, sets how the start sounds, so **the varying-rate drive is not worth building.**

What does cost something is the stretcher itself: across the three ratios the first 10 ms of a kick keeps only **23 to 78 % of its energy** with the default window (the 1.6x case loses about 6 dB) and **75 to 100 %** with the short window. A snare keeps 73 to 105 % with the default window and 94 to 98 % with the short one. That loss, not waveform correlation, is the argument for a dry head. (Correlation is meaningless for noise: a phase vocoder does not keep a noise waveform, so the snare's 0.55 means nothing about how it sounds.) The dry head makes the first 10 ms exactly the source. Caveats: its start metrics are true by construction, so they prove the splice, not the join; the join mixes the source at time t with the stretch of the source at t divided by the ratio (2 to 6 ms of content mismatch over a 5 ms cross-fade), which for a 50 to 160 Hz kick can show as a level dip, and **that needs listening**.

## 2. Does outputSeek hide the latency? Percussive onsets: yes.

`outputSeek` primes the stretcher so the first output sample belongs to the slice start, with no 60 ms silence. The onset (first sample above 10 % of the peak) of kick and snare lands within **0.12 ms** of the source in every case, with either window. The millisecond-offset measure I first reported (2 to 3 ms late) was wrong twice: the harness passed the inverse rate to `outputSeekLength`, and the lag sign was inverted. After fixing both, the correlation lag is erratic on periodic material (a 50 Hz kick repeats every 20 ms) and is not usable, so no sub-millisecond offset is claimed for the stretched tail. The soft pad has no sharp onset, so its onset time says nothing.

## 3. What does it cost?

| | default window (120 ms block) | short window (30 ms block, 7.5 ms hop) |
|---|---|---|
| memory per voice (configure) | 634 KiB | 159 KiB |
| latency (in / out) | 60 ms / 60 ms | 15 ms / 15 ms |
| average CPU per voice | 0.77 % of a core | 0.69 % of a core |
| heap allocations in `reset`, `outputSeek`, `process` | 0 | 0 |
| **worst block, 8 voices started on the same sample, 128-sample blocks (2.67 ms)** | **2.52 ms (94 %)** | **0.63 ms (24 %)** |
| same, 256-sample blocks (5.33 ms) | 1.77 ms (33 %) | not run |
| **`outputSeek` for 8 voices inline at a slice start, 128 / 256-sample blocks** | **4.0 ms (151 %) / 3.5 ms (65 %)** | **1.0 ms (38 %) at 128** |
| same with `splitComputation` (default window, 128) | process worst block 0.43 ms (16 %); `outputSeek` 5.4 ms (201 %) | not run |

Mono, 48 kHz, one machine, Release, warm cache. Stereo roughly doubles the work, 96 and 192 kHz scale the windows with the rate, and eight interleaved voices will not stay in cache, so **treat these as a lower bound**. The average is cheap; the **peak is the risk**: a whole hop runs inside whichever block crosses it, and `outputSeek` does a reset plus a pre-roll in one call. Allocation counting covers `operator new` only (not `malloc`); the dsp-controller's read of the vendored headers found no allocation, lock or I/O on this path (the default constructor uses `std::random_device`, so only the seeded constructor may be used).

## 4. Window size is a per-material choice

The short window keeps drum attacks better (first-10 ms kick energy 75 to 100 % against 23 to 78 %), costs a quarter of the memory, a quarter of the latency and has a quarter of the worst-block peak; but it is the wrong window for tonal material (a short analysis window blurs pitch, which the pad rows suggest but the harness' dB metric cannot show: the tail "band distance" is a single-bin measure and is not quoted here). **Short window for drum and percussion layers, default window for pads, vocals and tonal loops**, a per-layer choice.

## Recommendation for mu-Tilate

1. One Signalsmith Stretch instance per voice, preallocated at `prepare` (634 or 159 KiB), seeded constructor with a fixed seed, **never** `setFreqMap` or pitch/formant mapping on the audio thread (that path can allocate), constant local rate per slice.
2. The dry-head fallback from the design: *Transient hold* ms of the source itself, then a short cross-fade into the stretch. *Strength* scales the dry head to 0. Drop the *Curve* control, the curve preview and the plateau solve (they served the varying-rate drive). This changes [design-engine.md](design-engine.md); its owner decides.
3. **Never call `outputSeek` inline for every voice at a note-on.** Use the dry head's own duration to hide the work: prime the stretcher (seek to the source position at the end of the head) one to two blocks later, stagger voices that start on the same step, or keep a spare instance per voice primed ahead of the slice start. The default window is not safe for eight aligned voices at 128-sample blocks without this. The short window with staggering is comfortable.
4. Give the stretcher a zero-padded, bounds-clamped input view at the slice end (at least `blockSamples + intervalSamples` zeros): it reads past the slice by about its latency, and feeding the next slice's audio there puts that slice's attack in the tail as pre-echo. A silent input also bypasses stretching (the library copies input to output).
5. Still to measure or prove: per-call peak times (max, p99) for stereo and 96 kHz, eight voices interleaved; adjacent slices (loud then quiet) for pre-echo and a clean tail; the join of the dry head by ear; polyphonic material; a ratio change mid-slice.

## How to reproduce

```
cmake -S tests/prototypes/stretch -B build/stretch-proto
cmake --build build/stretch-proto --config Release
build/stretch-proto/Release/stretch_prototype.exe 128 0 2    # output block, splitComputation, window preset (0 default, 1 cheaper, 2 short)
```
