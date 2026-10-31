# Signalsmith Stretch prototype: results (2026-10-31)

Answers the three open questions in [design-engine.md](design-engine.md#tempo-matching-and-time-stretch) ("How the stretcher is driven"). Code: [tests/prototypes/stretch/](../../tests/prototypes/stretch/README.md) (standalone, not in the product build). Library: Signalsmith Stretch (MIT; the header reports 1.3.2, tag 1.4.0) and Signalsmith Linear 0.6.6 (MIT), vendored under `ThirdParty/`. Release build, 48 kHz mono, synthetic slices (kick, snare, soft pad), 0.5 s each, stretched to 1.25x, 1.6x and 0.8x their length.

**Nothing here was listened to.** The numbers are waveform measurements on synthetic material. They decide what to build, not whether it sounds good; the sound still needs Andy's ears on real loops.

## 1. Does a varying local rate (hold, ramp, plateau) protect the attack? No.

The stretcher was driven with a varying input count per output block (rate 1.0 for the hold, a linear ramp, then the plateau that lands the slice on its target length). Over the first 12 ms the output matches the source no better than a plain uniform stretch does:

| Kick, 1.25x, default window | correlation with source | energy of first 10 ms vs source |
|---|---|---|
| uniform | 0.959 | 0.78 |
| hold 10 ms, ramp 30 ms | 0.960 | 0.82 |
| hold 25 ms, ramp 60 ms | 0.961 | 0.88 |
| **dry head 10 ms + 5 ms cross-fade** | **0.999** | **1.00** |

The same holds for the snare and for the other ratios and the short window: the local rate changes nothing the ear would hear at the start, because the stretcher's own analysis window sets how sharp the start is, not the rate it is fed. The varying drive also did not fix flutter at the joins (worst short-term to trend RMS ratio 1.1 to 5.1, no better than uniform). **Do not build the varying-rate drive.**

**Use the design's own fallback instead:** the first few milliseconds of the slice are the source itself, then a short cross-fade into the stretched signal. It reproduces the attack exactly (correlation 0.99 to 1.00 on kick and snare, energy ratio 1.00), is aligned by construction (lag 0.00 ms), and the join is as smooth as the stretch itself (flutter 1.2 to 3.9, the same range as uniform). On the soft pad the start is not a transient, so a dry head adds nothing there (and its different onset shows in the pad rows), so the Strength control (0 = no dry head) stays worth having.

## 2. Does outputSeek hide the latency? Mostly; a small offset remains.

`outputSeek` primes the stretcher so the first output sample belongs to the slice start, with no 60 ms silence. Measured offset of the output against the source: **about 2 to 3 ms late** with the default window (120 ms block), **0.15 to 0.35 ms** with the short window (30 ms block). The dry head removes the offset where it matters (the attack); the stretched tail keeps it, which is inaudible at a cross-fade.

## 3. What does it cost? Little.

| | default window (120 ms block) | short window (30 ms block, 7.5 ms hop) |
|---|---|---|
| memory per voice (configure) | 634 KiB | 159 KiB |
| latency (in / out) | 60 ms / 60 ms | 15 ms / 15 ms |
| CPU per voice, Release | 0.78 % of a core | 0.70 % of a core |
| 8 voices | 6.2 % | 5.6 % |
| heap allocations in `reset`, `outputSeek` and `process` | 0 | 0 |

CPU is flat across output blocks of 128, 256 and 512 samples. `splitComputation` raises the output latency to 90 ms and the CPU slightly (0.82 % per voice), so it is not worth using unless a spike per block turns out to matter. Debug builds are far slower than these numbers; the Release figure is what counts.

## 4. Window size is a per-material choice

The short window keeps drum attacks better (kick correlation 0.988 against 0.959 uniform) and costs a quarter of the memory and latency, but tonal material is further from the source with it (pad band distance 35 dB against 23 dB at 1.25x). So: **short window for drum and percussion layers, default window for pads, vocals and tonal loops**, chosen per layer (a layer parameter, not a global).

## Recommendation for mu-Tilate

1. Stretch mode per layer = Signalsmith Stretch, one instance per voice, preallocated at `prepare` (634 KiB or 159 KiB, depending on the window choice), driven with a **constant** local rate per slice (input count = output count divided by the tempo ratio, error-diffused per block).
2. Each slice starts with `outputSeek`, a dry head of *Transient hold* ms and a cross-fade (default 5 ms) into the stretch. *Strength* scales the dry-head length to 0.
3. Drop the *Curve* control and the curve preview, and the plateau solve with it: they existed for the varying-rate drive, which gives nothing the dry head does not. (This changes [design-engine.md](design-engine.md); that file's owner decides.)
4. Still to prove, with ears and real loops: consecutive slices (the join between one slice's tail and the next slice's dry head), polyphonic material, stereo, and a ratio change mid-slice. The dsp-controller review the design requires is separate from this note.

## How to reproduce

```
cmake -S tests/prototypes/stretch -B build/stretch-proto
cmake --build build/stretch-proto --config Release
build/stretch-proto/Release/stretch_prototype.exe 256 0 0    # output block, splitComputation, window preset (0 default, 1 cheaper, 2 short)
```
