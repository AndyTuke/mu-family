# μ-Clid — FX Chain Design Reference

## FX Slot Overview

| Slot | Name | Type | Send behaviour |
|---|---|---|---|
| 1 | Effect | Insert-style | 0–50% blends wet in. 50–100% fades dry out. |
| 2 | Delay | Insert-style | 0–50% blends wet in. 50–100% fades dry out. |
| 3 | Reverb | Send-style | Pure send. Dry signal unaffected. |

**Critical:** Global FX parameters are NOT valid modulation destinations. FX are global — modulating from a rhythm would break rhythm independence.

## FXSlotBase Interface (all FX must implement this)

```cpp
class FXSlotBase {
    virtual void prepare(double sampleRate, int blockSize) = 0;
    virtual void process(juce::AudioBuffer<float>&) = 0;
    virtual juce::String getName() = 0;
    virtual juce::String getCategory() = 0;
    virtual juce::Component* createEditor() = 0;
    virtual void getStateInformation(juce::MemoryBlock&) = 0;
    virtual void setStateInformation(const void*, int) = 0;
};
```

## Intra-FX Routing

Send knobs route signal between FX return channels in the mixer:
- Effect return: Effect→Delay send, Effect→Reverb send
- Delay return: Delay→Reverb send
- Reverb return: no outgoing sends (end of chain)

UI placement: routing knobs in FX return channel strip in mixer, above pan.

## Effect Unit Algorithms (v1)

All distortion algorithms (Soft Clip, Hard Clip, Foldback, Bitcrush) and filter algorithms
(Ladder Filter, Comb Filter) have been moved out of the shared Effect slot. Per-rhythm Drive
is now part of the voice chain — see [mu-clid/design-voice.md](mu-clid/design-voice.md).

The Effect slot hosts modulation and time-based algorithms only:

| Algorithm | Category | DSP Approach | Key Parameters | Oversampling |
|---|---|---|---|---|
| Chorus | Modulation | Manual delay buffer + per-voice sine LFO | Rate, depth, voices (2–4), spread, mix | None |
| Flanger | Modulation | Short delay line (0.5–10ms) + sine LFO + feedback | Rate, depth, feedback, mix | None |
| Phaser | Modulation | First-order allpass chain + sine LFO + feedback | Rate, depth, stages (up to 12), feedback, mix | None |
| Echo | Time | Stereo delay line, no sync, ping-pong spread | Time, feedback, spread, mix | None |

**Chorus** — multi-voice: 2–4 delay taps each with their own LFO phase offset, panned across the stereo field by spread. Depth controls LFO modulation amount (± delay time). Mix is wet/dry blend.

**Flanger** — single delay line modulated into very short times (0.5–10ms). Feedback creates the characteristic notch comb. Feedback is bipolar: positive flanges, negative produces a softer phasing character. Mix is wet/dry blend.

**Phaser** — all-pass chain (2–12 stages in pairs). LFO sweeps the all-pass hinge frequency. Feedback controls resonance intensity. Mix is wet/dry blend.

**Echo** — simple stereo echo without BPM sync (use the dedicated Delay slot for sync'd delay). R delay = L delay × (1 + spread × 0.1) for stereo widening via ping-pong feel. Feedback controls decay. Mix is wet/dry blend. Intended for short slapback and room-simulation echoes.

**Algorithm switch thread safety:** `EffectSlot::setAlgorithm()` calls `prepare()` internally. It must be called from the **message thread only** — calling it from the audio thread during playback is not safe.

**No oversampling required:** All four Effect algorithms are linear or operate on delay-line modulation; there is no nonlinear waveshaping that generates out-of-band harmonics.

## Delay Parameters (dedicated Delay slot — unchanged)

| Section | Parameters |
|---|---|
| Time — sync mode | DropdownSelect (1, 1/2, 1/4, 1/8, 1/16, 1/32 + triplet/dotted) + ms display at current BPM |
| Time — free mode | ms with NudgeInput (step sizes 1, 5, 10ms) |
| Repeats | Feedback, spread, dirt (saturation on feedback path) |
| Intra-FX | Delay→Reverb send knob on Delay return channel in mixer |

**Spread implementation:** R delay = L delay × (1 + spread × 0.1). Maximum spread is +10% longer on the right channel. No phase inversion — purely time-based stereo widening.

**Dirt implementation:** Soft saturation on the feedback path (`tanh` based), scaled by dirt amount. At dirt = 0 the feedback path is linear.

**Buffer sizing:** `DelaySlot` pre-allocates `4 × 192000` samples (~4 seconds at 192kHz) for L and R. No reallocation during playback.

**BPM source:** `DelaySlot::setHostBpm()` is called each `processBlock` from `FXChain`. In DAW mode the host BPM is used; in standalone mode `PluginProcessor::internalBpm` is passed. Sync mode recalculates delay samples on each BPM change.

## Reverb Algorithms (v1)

| Algorithm | Library | Character | Parameters |
|---|---|---|---|
| Room | Signalsmith FDN | Tight natural space (decay 0.6 s) | Size, decay, pre-delay, diffusion, damp, mod, dirt |
| Hall | Signalsmith FDN | Long lush decay (decay 2.5 s) | Size, decay, pre-delay, diffusion, damp, mod, dirt |
| Plate | Signalsmith FDN | Dense metallic (decay 1.8 s) | Size, decay, pre-delay, diffusion, damp, mod, dirt |
| Spring | Signalsmith FDN | Lo-fi mechanical (decay 0.35 s) | Size, decay, pre-delay, diffusion, damp, mod, dirt |

Reverb has **no mix knob** — it is always a pure send. Shimmer removed from v1 (requires pitch shifting in feedback loop — v2 feature).

**Current implementation (Stage 15+):** All four reverb algorithms use the Signalsmith FDN reverb (integrated Stage 15). Algorithm selection applies parameter presets that bias `roomMs`/decay/`early`/damping for different characters.

**Decay (`rev_decay`):** 0.1–10 s, default 2.5, placed right after Size on the Reverb row. It is the RT60 in seconds (time to −60 dB). `ReverbSlot::setParam("decay")` passes `decay / 3` to Signalsmith's `rt20`, because the library defines `rt20` as the time to −20 dB (its own comment, "decay to -60dB", is `rt20` × 3). The per-algorithm preset decays (Room 0.6, Hall 2.5, Plate 1.8, Spring 0.35 s) are RT60 values; they were once passed straight to `rt20`, which made the old Hall tail about 7.5 s. Size sets only the room length and is independent of decay (the Signalsmith feedback is normalised per loop).

**Presets and the algorithm change:** choosing an algorithm from the editor menu pushes that algorithm's preset values, including decay, back into the knobs (APVTS). A host automating the algorithm parameter instead pushes the current knob values into the slot, so the knobs stay the source of truth.

**Parameter manifests:** adding `rev_decay` changed the frozen parameter manifests (`tests/manifests`); they are regenerated with `tests/scripts/regenerate-frozen-baselines.ps1 -Reason`. Projects and presets saved before it do not carry a decay value, so they load with the knob's current value (the default 2.5 s on a fresh load), not the decay of their reverb type; a Spring, Room or Plate project from before plays with a different tail until the knob is set. A hook that sets the decay from the preset when the value is missing is a possible follow-up.

### Stage 15: Signalsmith Reverb (complete)

Integrated the Signalsmith FDN reverb (`signalsmith-audio/basics`) as a pimpl in `ReverbSlot`. Headers in `ThirdParty/signalsmith-basics/include/` added as `SYSTEM PRIVATE` in `CMakeLists.txt` to avoid `signalsmith-dsp/windows.h` shadowing the Windows SDK.

**Vendored-library patches (record here; ThirdParty has no patch list, 2026-10-12):** `ThirdParty/signalsmith-basics/.../reverb.h` `ReverbSTFX::reset()` carries one local line, `delay4.reset();`, which upstream omits so a reset left its last output behind; `ReverbSlot::reset()` relies on it. Re-apply after any re-vendoring (grep for `mu patch`).

**Implementation:** `signalsmith::basics::ReverbFloat` wrapped behind a pimpl to keep Signalsmith headers out of `ReverbSlot.h`. Pre-delay buffer and `dirt` (tanh saturation) remain before the reverb call. Algorithm presets bias `roomMs`/`rt20` (from decay)/`early`/`detune`/`highCutHz`/`highDampRate` for each character. UI params (Size, Decay, Diffusion, Damp, Mod) map to those member fields.

**Pre-delay buffer:** ~100ms pre-allocated (`MaxPreDelaySamples = 192000 / 10`), applied before the reverb. Wet scratch buffers also pre-allocated in `prepare()` — no audio-thread allocation.

## FXAlgorithmDef Data Class

Stores: algorithm ID, name, category, params (array of `FXParamDef`: id, name, min, max, default, units), and `oversamplingFactor`. Drives both the UI (FXRow populates knobs from `params`) and the DSP (EffectSlot selects oversampling factor from def). A static `FXAlgorithmRegistry` provides the canonical ordered lists for both effect algorithms and reverb algorithms.

The oversampling field is retained in the struct for forward compatibility; all current Effect algorithms set it to 1 (bypass).

**Intra-FX routing:** `FXChain` exposes `setEffectToDelaySend()`, `setEffectToReverbSend()`, `setDelayToReverbSend()`. Wired to Mixer return-channel send knobs. Default 0.0 — no routing unless explicitly set.

## Effect Algorithm Files (current)

Distortion and filter effect files (`SoftClipEffect`, `HardClipEffect`, `FoldbackEffect`,
`BitcrushEffect`, `LadderFilterEffect`, `CombFilterEffect`) remain on disk but are no longer
registered in `FXAlgorithmRegistry::effectAlgorithms()` and are not instantiated — these
algorithms live in `InsertProcessor` for the voice insert slot instead.

Chorus, Flanger and Phaser are registered and active in the Effect slot (as `mu-core/Audio/FX/Algorithms/{Chorus,Flanger,Phaser}Fx.h`, each with its `...Core.h`; see the merge section below).
Echo has no algorithm class: the Effect slot's Echo index runs its embedded `DelaySlot` (the old `EchoEffect.h` was removed as dead code).

## One algorithm, two roles: Chorus, Flanger, Phaser merge (ruled 2026-11-16, built 2026-11-16, steps 1-5 and 7; step 6 open)

The text below is the ruling as made; where it says "today" or "old" it describes the code before the merge. Built state: `Algorithms/` holds the three `Fx` + `Core` pairs, `FxUse.h`, `FxFactory.h` and `ModDelayLine.h`; `Engines/` holds `SpringReverb.h` and `TapeSpringEcho.h`; every algorithm, the insert-only ones and `InsertAlgorithmBase.h` / `EffectAlgorithmBase.h` included, now lives in `Algorithms/` (step 6 done 2026-11-22; `Insert/` and `Send/` are gone); `Tests/FxMergeTests.cpp` exists.

**Why.** Each of the three existed twice (a send class and an insert class) with its own LFO, delay line and smoothing code. Andy's decisions: one folder `FX/Algorithms/`, one class per effect marked insert / send / both, one shared DSP core per effect with thin adapters; the insert has four knobs, so the send parameters that do not fit are fixed in insert mode while the send keeps all of them; the richer send sound wins; no preset migration (saved presets may sound different; the insert APVTS and param ids do not change, so they still load).

**Facts the plan rests on (read from the code).**
- Every Effect-slot algorithm runs at factor 1: `EffectSlot::prepare` builds `OversampledProcessor(1)` and every `FXAlgorithmDef.oversamplingFactor` is 1. So "send is oversampled" is not true today; `processInner` runs at the host rate. The cores must still take their rate from `prepare(sr)` and size lines from it, so a later factor > 1 works unchanged (the send adapter hands over the rate `prepareInner` receives).
- `EffectSlot` sets send mode on all algorithms, so the send's internal dry path (`dry * in`, and the flanger's delayed dry tap) is never used. The send is always wet-only into the return bus, where the dry signal is undelayed. A flanger insert with an undelayed dry therefore has the same topology as the send in the bus.
- Phaser: the two all-pass sections and the sweep (200 Hz..4 kHz, log) are the same filter; Chorus and Flanger differ in structure, listed below.
- Old inserts have no smoothing on rate / depth / centre; the send smooths everything for 15 ms. The core smooths every continuous value for 15 ms; the adapters own only the mix (the insert snaps on first use, see `started` today).

### Folder, files, names (design-naming.md "Algorithm files")
```
mu-core/Audio/FX/
  Algorithms/   FxUse.h  ChorusCore.h ChorusFx.h  FlangerCore.h FlangerFx.h  PhaserCore.h PhaserFx.h
                ModDelayLine.h            (was Insert/InsertDelayLine.h)
                <every other insert algorithm, moved unchanged in the last step: EqInsert.h ... TremoloInsert.h, InsertAlgorithmBase.h, EffectAlgorithmBase.h>
  Engines/      SpringReverb.h  TapeSpringEcho.h   (were at the FX root; engines wrapped by an algorithm)
  Slots/        unchanged (EffectSlot, DelaySlot, ReverbSlot, FXChain, FXSlotBase, FXAlgorithmDef, OversampledProcessor)
```
`Send/` and `Insert/` are gone: step 6 (2026-11-22) moved the insert-only algorithms, `InsertAlgorithmBase.h` and `EffectAlgorithmBase.h` into `Algorithms/`. Classes live in namespace `mu_audio` (the area namespace for audio in mu-core; the old global names are left alone). Class `ChorusFx` etc. = the algorithm used by the slots; `ChorusCore` etc. = the DSP, nothing else. Insert-only algorithms keep their `...Insert` names (renamed to `...Fx` only when touched for another reason, never in a bulk rename).

### The flag
`Algorithms/FxUse.h`:
```cpp
namespace mu_audio {
enum class FxUse : unsigned { Insert = 1, Send = 2, Both = 3 };
constexpr bool supports(FxUse have, FxUse want) noexcept { return (static_cast<unsigned>(have) & static_cast<unsigned>(want)) == static_cast<unsigned>(want); }
}
```
Each algorithm class carries `static constexpr mu_audio::FxUse kUse = FxUse::Both;` (insert-only classes: `Insert`). It is the only place the role is stated. The registries read it at the two registration points, at compile time: `EffectSlot.cpp` builds its array with a helper `makeSend<A>()` and `InsertProcessor.cpp` fills `dispatch[]` with `makeInsert<A>()`; each helper has `static_assert(supports(A::kUse, FxUse::Send))` / `(..., FxUse::Insert)`. `FXAlgorithmDef` gets no new field (it would need the class header and form an include cycle). A unit test (below) additionally checks that `effectAlgorithms()[0..2]` and `dispatch[17..19]` are the same three classes.

### Class shapes
- `XCore` (one per effect, header-only, no juce::AudioBuffer, no VoiceParams, no atomics, no strings):
  - `struct Params { ... }` the plain-number parameters in real units (Hz, 0..1 fractions, ms).
  - `void prepare(double sampleRate)` (allocates; guards `sr > 0`), `void reset()` (silences state, marks "snap next params"), `void setParams(const Params&)` (sets smoothing targets; the first call after reset snaps), and `void tick(float inL, float inR, float& wetL, float& wetR)` (one stereo frame; advances smoothers and the LFO; **returns the wet signal only**, never blends dry). A mono caller passes `inR = inL` and ignores `wetR`; cost is a few flops.
  - No allocation outside `prepare`; no virtual calls.
- `XFx final : public EffectAlgorithmBase, public InsertAlgorithmBase`, one per effect, members: `XCore core;` the send front end (relaxed `std::atomic<float>` for the UI/host-written params, same as today) and the insert front end (reads `insertSlot(p, 0..3)`). Overrides: `getDef()` returns `FXAlgorithmRegistry::effectAlgorithms()[i]` by reference (no member copy of the def, which also stops each insert instance copying a string vector), `prepareInner(sr, n)` and `prepare(sr, n)` both call `core.prepare`, one `reset()` overriding both bases, `setParam` (send), `processInner(block)` (send) and `process(buf, ns, nCh, p, grOut)` (insert). The adapters are only these overrides: map real params to `Core::Params`, loop the samples calling `tick`, blend, write. An object serves one role in practice (EffectSlot owns one, InsertProcessor another); the other role's front end is dead weight of a few bytes.
- Blending (adapters, not the core): send: `out = wet` (wet-only; the dry is on the bus). If a future caller clears send mode the adapter blends `out = in*(1-mix) + wet*mix` with the old `mix` param. Insert: `out = in*(1-m) + wet*m` with `m` the Mix knob smoothed 20 ms; Phaser insert uses the notch blend `out = in*(1-m) + 0.5*(in + wet)*m` (adding the dry to the all-passed signal is what makes the notches; on the send bus the main mix does that). Mix 0 leaves the insert bit-exact untouched (a test requires it).
- Delay: both modulated-delay cores use `ModDelayLine` (the cubic Hermite line, with taps read before the push). One line per channel; Chorus reads all its voices as taps of that one line (the old send kept four identical lines per channel; the content was the same, the memory was 4x). Line length is `ceil(0.07 * sr) + 8` for Chorus (30 ms centre + 20 ms swing x 1.5 spread = 60 ms worst case) and `ceil(0.012 * sr) + 8` for Flanger.
- Sample rate: cores only use the `sr` they were prepared with. The send adapter calls `prepare` with the rate `prepareInner` receives (the oversampled rate when a factor is ever > 1); the insert adapter with the host/voice rate. No core knows about oversampling; since both roles run at factor 1 today nothing else changes.

### Per effect: core parameters, send map, insert map, fixed values
Send params and ids are unchanged (`FXAlgorithmRegistry`). Insert slot ranges and ids 17 / 18 / 19 are unchanged (`InsertSlotConfig.h`); only what they drive changes.

**Chorus** (`insertAlgo` 17). `ChorusCore::Params`: `rateHz`, `depth` 0..1, `centreMs`, `swingMs` (swing at depth 1), `voices` 2..4, `spread` 0..1. Core: voice `v` has LFO phase offset `v/4`, rate detune `1 + (t-0.5)*0.03` with `t = v/(voices-1)`, left delay `centre + lfo*depth*swing`, right delay with `swing` multiplied by `1 + spread*0.5` on odd voices, floor 2 samples, output summed and scaled `1/voices` (all as the old send class did).
- Send: rate = Rate, depth = Depth/100, voices = round(Voices) clamped 2..4, spread = Spread/100, `centreMs = 30`, `swingMs = 20`, `mix` kept for the unused non-send path.
- Insert: Slot 0 Rate 0.05..8 Hz log -> `rateHz`; Slot 1 Depth 0..100 % -> `depth`; Slot 2 Delay 5..30 ms -> `centreMs` with `swingMs = 0.4 * centreMs` (the swing share the insert has today); Slot 3 Mix 0..100 %. **Fixed hidden: `voices = 3`, `spread = 0.5`.** Three voices because the old insert was one voice with a quarter-turn stereo offset and three is the first count that sounds like an ensemble chorus; spread 0.5 is the send default and replaces the old R quarter-phase as the stereo widener. The insert loses nothing it exposed and gains thickness.

**Flanger** (`insertAlgo` 18). `FlangerCore::Params`: `rateHz`, `depth` 0..1, `feedback` -0.95..0.95, `stereoOffset` (turns of LFO). Core: write `in + tanh(feedState * feedback)` into the line, wet = line read at `5.25 ms * (1 + depth * sin)` (floor 1 sample; the wet delay sweeps 0..10.5 ms and so crosses the undelayed dry), `feedState = wet`. The tanh is new in the send (the insert had it); it is linear at small signal so the send barely changes and 0.95 cannot run away.
- Send: Rate 0.01..20 Hz, Depth/100, `feedback = Feedback/100 * 0.95`, `stereoOffset = 0` (as today).
- Insert: Slot 0 Rate 0.05..5 Hz log -> `rateHz`; Slot 1 Depth; Slot 2 Feedback -95..95 % -> `feedback = pct*0.01`; Slot 3 Mix. **Fixed hidden: `stereoOffset = 0.25`** (right LFO a quarter turn behind, as the insert has today); centre 5.25 ms is a constant of the core. The default row `{0.3 Hz, 70 %, 50 %, 50 %}` stays; the sound at those values changes (wider 0..8.9 ms sweep, deliberate).

**Phaser** (`insertAlgo` 19). `PhaserCore::Params`: `rateHz`, `depth` 0..1, `stages` (even, 2..12), `feedback` 0..1, `stereoOffset`. Core: `freq = 200 Hz * exp(lfoNorm * ln 20)`, `lfoNorm = 0.5 + 0.5*sin*depth` (equal to the insert's centre / octave-span form; the two are the same sweep), clamp `[20, 0.45 sr]`, all-pass `a = (t-1)/(t+1)`, `t = tan(pi f/sr)`, chain input `in + tanh(fbState * feedback * 0.99)`, `fbState` = chain output, wet = chain output.
- Send: Rate 0.1..8, Depth/100, Stages rounded to even 2..12, Feedback/100, `stereoOffset = 0`.
- Insert: Slot 0 Rate 0.05..5 Hz log; Slot 1 Depth; Slot 2 Feedback 0..90 % -> `feedback = pct*0.01`; Slot 3 Mix (notch blend). **Fixed hidden: `stages = 6`, `stereoOffset = 0.25`.**

### Mod targets and APVTS
Nothing changes: send FX params keep their APVTS ids and mod rows, insert params stay the generic `insertParam[0..3]` with the same ids and the table in `InsertSlotConfig.h` (`kInsertSyncSlot` stays -1 for all three). No new continuous parameter is added, so no new mod-target row is required; the fixed values are not parameters. `mu_mod::checks` and `check-mod-targets.ps1` must stay green.

### Tests
Port (include and type names only): `AuditMinorsTests.cpp` (`ChorusEffect`/`PhaserEffect` smoothing and zero-delay tests, lines ~153-215), `MixerBlockValueTests.cpp` (line ~1085 `ChorusInsert a17; FlangerInsert a18; PhaserInsert a19` and its includes), `InsertDSPSmokeTests.cpp`, `StereoParityTests.cpp`, `SpectralBehaviourTests.cpp` (only if they include the moved headers; the other `InsertDelayLine` / `FX/Insert` includers just change the path). `NewInsertTests.cpp`, `FxAlgoSwitchTests.cpp` and both `SendFXSmokeTests.cpp` go through `InsertProcessor` / `EffectSlot` and need no change; they are the regression net (Mix 0 untouched, finite at corners, algorithm switch mapping, 30 ms + 20 ms chorus read bound). New `Tests/FxMergeTests.cpp` (added to `Tests/CMakeLists.txt`): (1) role flags: `ChorusFx/FlangerFx/PhaserFx::kUse == Both`, and the three registry rows / dispatch slots instantiate those classes; (2) core equivalence: for each effect, the insert path at Mix 100 % (Phaser: derived from the notch blend) and the send path with the mapped parameters produce the same wet within 1e-6 on a noise input; (3) chorus insert uses 3 voices with spread 0.5 (a regression guard on the hidden values); (4) finite and bounded at 44.1 / 96 / 192 kHz and block sizes 1..2048 for both roles including mono (`nCh = 1`); (5) reset: output after `reset()` equals a fresh object bit for bit. Goldens stay stale on purpose until the sounds settle.

### Build steps (each one builds and runs the tests on its own)
1. **Engines move (done).** `git mv` `FX/SpringReverb.h`, `FX/TapeSpringEcho.h` to `FX/Engines/`; fix includes in `Insert/TapeSpringInsert.h`, `TapeSpringEcho.h` (-> `Engines/SpringReverb.h`), `Tests/SpringReverbTests.cpp`, `Tests/TapeSpringEchoTests.cpp`, the comment paths in `Tests/CMakeLists.txt`. Pure move.
2. **Flag and shared line (done).** Add `Algorithms/FxUse.h`; `git mv Insert/InsertDelayLine.h Algorithms/ModDelayLine.h` and rename the class `ModDelayLine` (in namespace `mu_audio`); update `Insert/PitchDelayInsert.h`, `ChorusInsert.h`, `FlangerInsert.h` and the other `InsertDelayLine` includers (`InsertProcessor.h`, `mu-link/Source/Server/ServerEngine.h` and tests: grep first). No behaviour change.
3. **Chorus (done).** Add `ChorusCore.h`, `ChorusFx.h`; add `makeSend` / `makeInsert` helpers (in `FxUse.h`'s neighbour `Algorithms/FxFactory.h`) and use them for slot 0 in `EffectSlot.cpp` and `dispatch[17]` in `InsertProcessor.cpp`; delete `Send/ChorusEffect.h`, `Insert/ChorusInsert.h`; port tests; add the Chorus cases of `FxMergeTests.cpp`.
4. **Flanger (done).** Same for `FlangerCore.h`, `FlangerFx.h`, slot 1, `dispatch[18]`, delete `FlangerEffect.h`, `FlangerInsert.h`.
5. **Phaser (done).** Same for slot 2, `dispatch[19]`.
6. **Move the rest (done 2026-11-22).** `git mv` the remaining `Insert/*.h` (including `InsertAlgorithmBase.h`, `NoneInsert.h`) and `Send/EffectAlgorithmBase.h` into `Algorithms/`, add `kUse = FxUse::Insert` to each insert-only class, fix includes (`InsertProcessor.*`, `EffectSlot.h`, tests); `Send/` and `Insert/` are now empty and gone. Mechanical; done as one move plus include rewrite, every insert-only class marked `kUse = FxUse::Insert` and registered through `makeInsert` (Vocoder, which takes a constructor argument, stays a `make_unique`).
7. **Docs (done).** Updated `docs/design-plugin-family.md` paths if it names `FX/Send` or `FX/Insert`, `tests.md` (new `FxMergeTests`), the manuals only if the insert Chorus/Flanger/Phaser descriptions change (Frank decides; the default-row sounds changed).

Order matters: 1, 2, then 3-5 in any order (3 first as the template), 6 last.

**Do not:** make the core blend dry and wet (the phaser insert and send need different blends); give `FXAlgorithmDef` a role field (include cycle); keep the old per-voice delay lines in the core; add the insert's missing send parameters as new insert knobs (four slots is the contract); rename the insert-only classes in bulk.

## Tape saturation rework: insertAlgo 10 TapeSat (ruled 2026-11-16, Ted; built as row 1818, amended 2026-11-16 after measurement: 2x oversampled)

**Amendment 2026-11-16 (Ted), after the first build measured four deviations.** Rulings: (1) aliasing at 9 kHz, Drive 100 was only 3.4 dB under plain tanh (target 6): **changed**, insert 10 runs its saturating stage 2x oversampled, see "Aliasing" below; (2) HF droop of about 2 dB at 10 kHz from the ADAA step average: **fixed by the same change** (at twice the rate the droop at 10 kHz falls to about 0.5 dB); (3) level matching peaks +3.9 dB at Drive 50 with square-root makeup: **accepted**, the family-standard makeup stays and the bound is 4.5 dB (below); (4) Bump 0: **changed as built**, the head-bump biquad now always runs (0 dB gain at Bump 0, an identity), so its state is warm and a Bump jump from or to 0 is only a coefficient step, not a click; nothing is skipped. Review notes folded in: the ADAA state resets to the fixed offset (silence into the shelf gives exactly the offset, so the first sample has no dip), the input is clamped to +-1e4 in TapeSat and Overdrive (a NaN or Inf becomes 0, a huge finite sample cannot overflow the shelf state), and the bump and Tone coefficients are cached and recomputed only when the knob moves. Measured results are listed under "Measured (built, row 1818)" below.

**Why.** `TapeSatInsert` today is gain, `tanh`, DC block, one-pole low-pass: a plain soft clip with a tone knob, the same sound as SoftClip and Overdrive (insertAlgo 14, biased-tanh ADAA, the tube-style stage; it stays as it is and this ruling does not copy it). Andy approved a convincing tape model for insert 10. Slot 2 is unused today.

### What real tape does, and what is modelled
| Behaviour | Real tape | Here |
|---|---|---|
| Level-dependent HF saturation: highs run out of headroom first, so loud highs compress and dull | record pre-emphasis (NAB/IEC shelf) before the tape, de-emphasis after | **modelled**: a first-order high shelf before the saturator and its exact inverse after it |
| Soft, gradual knee, odd harmonics dominate | hysteresis loop | **modelled** by a `tanh` curve, first-order ADAA |
| Mild even harmonics | bias and magnetisation asymmetry | **modelled**: small fixed offset into the curve, removed again after (0.08 knee units; not a knob) |
| Low "head bump", a soft rise around 50 to 120 Hz | playback-head gap and wavelength effects | **modelled**, Slot 2, optional |
| Gentle HF loss | gap, spacing and thickness loss | **emerges** from the emphasis pair around the saturator (more drive, more HF loss), plus the Tone low-pass; no extra fixed filter |
| Path-dependent hysteresis memory, wow/flutter, noise, crosstalk | | **not modelled** |

**Model chosen: emphasis, saturate, de-emphasis (the classic cheap tape stage).** Jiles-Atherton hysteresis (Chowdhury, "Real-time physical modelling for analog tape machines", DAFx 2019, as in ChowTape, which is GPLv3 and so not reusable here) needs an iterative solver per sample and 2x to 8x oversampling to stay stable and alias-free: roughly 10 to 30 times the cost of this model (estimate, not measured), too heavy for a per-voice insert on every layer. The memoryless saturator between a shelf and its inverse reproduces the audible part (small signals pass flat; big signals lose highs first), adds only a few multiply-adds beyond the curve, and is unconditionally stable.

### Signal path (per channel; state for two channels, as the other inserts)
1. `x`, a non-finite value replaced by 0 and a finite one clamped to +-1e4 (the same guard in TapeSat and Overdrive).
2. **Pre-emphasis**: first-order high shelf, +9 dB at high frequencies (zero 1.5 kHz, pole 4.2 kHz, bilinear with pre-warp, coefficients set in `prepare` for the rate; valid at 44.1, 96 and 192 kHz). Unity at DC.
3. Gain `preGain` (smoothed), then add the fixed offset `b = 0.08`.
4. **Saturator**: `tanh`, first-order ADAA (antiderivative `log(cosh x)`, the large-`|x|` form, small-`dx` fallback, flat-top guard), then subtract the resting offset `tanh(b)`.
5. **De-emphasis**: the exact inverse of step 2 (numerator and denominator swapped; stable because the shelf zero is inside the unit circle). Small-signal response is therefore flat: the tilt shows only when the saturator is driven.
6. **Head bump** (always runs; at Slot 2 = 0 its gain is 0 dB, an identity): the existing `BiquadFilter::setPeak`, 75 Hz, Q 0.9, 0 to +4 dB. Coefficients are cached and recomputed only when the knob moves.
7. DC block (one-pole high-pass at 20 Hz, as today and in Overdrive).
8. **Tone**: the existing `OnePoleLP`, bypassed (its state follows the signal) at 19 kHz and above, exactly as Overdrive.
9. Output gain.

**Drive and level matching.** `preGain = 10^(Drive/100 * 24/20)`, 1x to about 16x (0 to 24 dB), 15 ms linear smoothing. `outGain = 10^(Output/20) / sqrt(preGain)`: the square-root makeup the family already uses (SoftClip, Overdrive). For a tape-level signal (peak 0.25) this keeps the output within about 3 dB over the whole Drive range, so more drive gives denser and duller sound rather than just louder. The test measures it.

**Aliasing (amended: 2x oversampled, ADAA kept).** The first build (ADAA only, as Overdrive and SoftClip) measured the strongest alias at -17.1 dB against -13.7 dB for plain tanh at 9 kHz, Drive 100, 44.1 kHz: 3.4 dB better, not the 6 dB ruled. The cause is built in: the pre-emphasis lifts the tone about 1.8 dB further into the curve than a bare ADAA tanh sees, and ADAA alone cannot cure that. A cheaper fix does not reach the target: lowering the emphasis gain (+9 to +6 dB) gains about 1 dB and flattens the tape character, and a pre-filter before the shelf cuts the very highs the emphasis exists to saturate. Andy wants a good tape sound, so the ruling is to pay for quality:
- The **saturating stage runs at 2x**: pre-emphasis, Drive gain and offset, ADAA tanh, de-emphasis. Head bump, DC block, Tone and Output stay at the base rate.
- Oversampler: `juce::dsp::Oversampling<float>`, 1 stage (2x), `filterHalfBandPolyphaseIIR`, owned by `TapeSatInsert` and created in `prepare` (the second argument, the block size, is already passed; it is currently unnamed). **Not** the FIR equiripple filter that `OversampledProcessor` uses: the FIR adds many samples of latency per voice, which the insert chain has nowhere to compensate; the polyphase IIR halfband costs a few samples (measured: the impulse peaks 4 samples after the input). `OversampledProcessor` is not reused because it wraps a whole `AudioBuffer` and fixes the FIR; the insert needs the raw object over the channel subset. Do not change `OversampledProcessor` for this.
- Shape in `process`: take an `AudioBlock` over the first `nChState` channels, subblock `ns`, `processSamplesUp`, run the stage in the oversampled block, `processSamplesDown`. If `ns` exceeds the prepared block size, loop in chunks; never allocate. Extra channels beyond two pass through, as now.
- The emphasis pair is prepared at `2 x sampleRate` (its coefficients are rate-aware already), and the Drive smoother runs at the oversampled rate (prepared at `2 x sampleRate`, advanced once per oversampled sample), so the gain ramp keeps its 15 ms time.
- **Latency.** Insert 10 now adds the halfband group delay (measured: the impulse response peaks 4 samples after the input, about 0.09 ms at 44.1 kHz), not reported and not compensated: inserts are per voice, mixed with other voices and the dry path, and a 4-sample offset on one insert type is inaudible. `InsertLatencyTests` in `MixerBlockValueTests.cpp` ("Insert algorithms report zero latency") must therefore not assert zero for `TapeSatInsert`: change that one entry to a measured bound (impulse response peak within 4 samples of the input), keep the zero assertions for the others, and keep the registry check that no algorithm grows a `getLatencySamples()`. Do not add reporting.
- The oversampler state is part of `reset()` (`oversampling->reset()`), so the reset-equals-fresh test holds.
- Expected result, to be confirmed by the test: the 9 kHz alias margin over plain tanh grows from 3.4 dB to well above the 6 dB threshold (the halfband removes folded energy that lands in the passband), and the 10 kHz droop falls to within 0.5 dB.

**CPU (audio thread).** Per sample per channel: the shelf pair and one ADAA step (one `log(cosh)` and a divide, or `tanh` when `dx` is tiny) at twice the sample rate, the halfband up and down filters, the DC block, one one-pole, and the bump biquad (always on): about 5 to 6 times the original gain-tanh-low-pass insert (estimate, not measured; the built ADAA-only version was about 2 to 3 times), paid only on a voice that selects TapeSat, and still below the Compressor or Vocoder. Andy accepts the CPU for sound quality here. The oversampler is allocated in `prepare` per instance (a few kB), nothing on the audio thread. No allocation (all state is a handful of floats), nothing outside `prepare`. Shelf coefficients are set in `prepare`; bump and Tone at block rate, as Tone is today. Keep the `isfinite` guard.

### Knob map (slot ids unchanged, so saved presets load)
| Slot | Label | Range | Skew | Unit | Role | Default |
|---|---|---|---|---|---|---|
| 0 | Drive | 0..100 | Linear | "" | `preGain` above | 30 |
| 1 | Output | -24..0 | Linear | dB | trim | 0 |
| 2 | **Bump** | 0..100 | Linear | % | head-bump gain, 0 to +4 dB at 75 Hz; 0 is 0 dB (the biquad still runs) | 30 |
| 3 | Tone | 200..20000 | Log, dynamic unit | Hz | plain low-pass, open at 19 kHz and up; stays a plain low-pass | 20000 |

`kInsertAlgoSlots[10]` slot 2 becomes `{ "Bump", 0.0f, 100.0f, SkewMode::Linear, "%" }`; `kInsertAlgoDefaults[10]` becomes `{ 30, 0, 30, 20000 }` (first visit only; a saved preset keeps its own values, and old presets store slot 2 as 0, which is Bump 0). The old default Drive 0 showed nothing on first selection. Tone keeps its label, range and unit.

**Mod targets: no new row.** Insert knobs are the generic `insert.p1..p4` rows already in every product's table, labelled per algorithm from `kInsertAlgoSlots` (`mu_mod::populateDropdown`), so Bump becomes a target the moment its label exists. Depth stays a percentage of the knob's range. `check-mod-targets.ps1` and `mu_mod::checks` need no edit.

### Class shape and files
- `Algorithms/TapeSatInsert.h` (it was `Insert/TapeSatInsert.h` until the 2026-11-22 folder move) is rewritten in place (class name, id 10 and dispatch unchanged; insert-only, so it keeps its `...Insert` name and moves to `Algorithms/` with the rest in the merge's last step).
- New shared helpers in `mu-core/Audio/FX/Algorithms/` (namespace `mu_audio`):
  - `Adaa1Tanh.h`: `struct Adaa1Tanh { float process(float x) noexcept; void reset() noexcept; }`, the ADAA tanh with its guards, lifted from `OverdriveInsert.h` (a second user, so it moves to a shared header before a second copy exists). `OverdriveInsert` switches to it in the same step (identical maths; bit-identical output is the check).
  - `TapeEmphasis.h`: `struct TapeEmphasis { void prepare(double sr); float pre(float) noexcept; float de(float) noexcept; void reset() noexcept; }`, the first-order shelf pair.
- Reuses `BiquadFilter` and `OnePoleLP` (`Audio/AudioFilters.h`) and `InsertSlotConfig.h`. Nothing new in `mu-core/UI`. No change to design-naming (class names follow the existing `...Insert` rule; helpers are plain nouns).

### Build steps (each builds and runs its tests)
1. Add `Algorithms/Adaa1Tanh.h`; switch `OverdriveInsert.h` to it; existing Overdrive tests unchanged.
2. Add `Algorithms/TapeEmphasis.h`.
3. Rewrite `TapeSatInsert.h` (then in `Insert/`, now `Algorithms/`) to the path above (built); `InsertProcessor.cpp` dispatch untouched. Step 3b (amendment): add the 2x polyphase-IIR oversampling around the saturating stage, adjust the latency test entry and the aliasing and droop tests.
4. `InsertSlotConfig.h`: Bump label on row 10 slot 2, defaults row 10, row comment ("Drive / Output / Bump / Tone").
5. Tests: new `mu-core/Tests/TapeSatTests.cpp`, listed in `mu-core/Tests/CMakeLists.txt`. Product golden renders that use insert 10 are stale on purpose until Andy allows a re-record.
6. Docs: manual wording (Frank), `tests.md` (Fred), release note.

### Tests (offline, no listening)
- Even harmonics: 1 kHz sine at Drive 60, Bump 0: 2nd harmonic above the plain-`tanh` reference (the old class) and below the 3rd; odd harmonics still dominate.
- HF compresses first: 200 Hz and 8 kHz at the same input level: the 8 kHz gain falls at least 3 dB more than the 200 Hz gain between Drive 0 and Drive 100; at -40 dBFS and Drive 30 the response is flat within 0.5 dB from 100 Hz to 10 kHz (the first build was flat only to 3 kHz and about 2 dB down at 10 kHz; the 2x amendment must restore this).
- Bump: Bump 100 raises 75 Hz by 3 to 5 dB over Bump 0; Bump 0 adds no lift (0 dB gain; the biquad always runs, so there is no bit-identity test).
- Aliasing: 9 kHz sine at Drive 100, 44.1 kHz: the strongest folded component is at least 6 dB below the plain-`tanh` reference (first build: 3.4 dB, failed; expected to pass with the 2x amendment; record the measured margin in `tests.md`).
- Level matching: RMS of a 0.25-peak 1 kHz sine at Output 0 within **4.5 dB** across Drive 0, 25, 50, 75, 100 (ruled 3 dB; the built stage peaks +3.9 dB at Drive 50 with the square-root makeup, exponents 0.6 and 0.65 are no better; accepted).
- Stability: finite output, no latched state after a NaN or Inf burst, at 44.1, 96 and 192 kHz, block sizes 1, 7, 512 giving the same result within float noise.

### Measured (built, row 1818, 2x oversampled)
- Alias margin at 9 kHz, Drive 100, 44.1 kHz: strongest folded component -45.6 dB, against -13.7 dB for plain tanh (the target was 6 dB better; far exceeded).
- HF droop at 10 kHz (-40 dBFS, Drive 30): -0.47 dB, inside the 0.5 dB bound.
- Level matching: peak +3.9 dB at Drive 50 (bound 4.5 dB).
- Harmonics at Drive 60, 1 kHz: H2 -32.5 dB, H3 -22.1 dB (odd dominates, even present).
- Latency: impulse peaks 4 samples after the input (about 0.09 ms at 44.1 kHz).
- Latency: the impulse response of insert 10 peaks within 2 samples of the input (replaces the zero-latency assertion for this algorithm only).
- Reset: after `reset()` a render is bit-identical to a fresh instance's; settled Drive 0 output has no DC (mean below 1e-4).
- `InsertDSPSmokeTests` and `InsertAlgoTableTests` (labels present, defaults in range) stay green.

**Do not:** copy Overdrive's variable bias knob (Slot 2 is not bias; the asymmetry is fixed and small); add a Jiles-Atherton solver; use the FIR `OversampledProcessor` for insert 10, or report its latency; put the helpers in `Insert/`; add a mod-target row by hand; change the slot ids or the Tone range.

**Status:** built (row 1818, commit 49339805) without oversampling; the 2x amendment above is the open code change, owned by the main conversation.

**Andy decides:** Slot 2 as head Bump (ruled here) or as an asymmetry amount ("Warmth"); first-visit defaults Drive 30 / Bump 30.

## Three new inserts: Auto Pan (22), Gate (23), Waveshaper (24) (ruled 2026-11-16, Ted)

**Why.** Andy approved three more per-voice inserts. All are insert-only (no send role). This section settles Auto Pan versus Tremolo, placement, numbering, knobs and the real-time rules, so a coder needs no further decisions.

### 1. Auto Pan is its own insert, not a Tremolo mode
Tremolo (21) uses all four slots (Rate / Depth / Shape / Phase), so a Pan mode would have to take over a slot's meaning (Phase and Pan are different things) or add a hidden mode switch. Tremolo at Phase 100 % already pans, but on a linear law (the two gains always sum to 1, so the centre is 6 dB down) and with no way to place the centre. Auto Pan uses a constant-power law and a Centre knob. Tremolo is **not changed** (its Phase 100 % note stays true). Auto Pan reuses `kTremoloShapeNames` for Shape and the same sync convention, so the two families match; they do not share a class (different law, different state).

### 2. Files and names
New work goes in `mu-core/Audio/FX/Algorithms/` (the folder design-naming's `Fx` row and the merge section fix for algorithms); nothing new goes in `Insert/`. Insert-only classes keep the `...Insert` suffix and carry `static constexpr mu_audio::FxUse kUse = mu_audio::FxUse::Insert;`. They are registered with `mu_audio::makeInsert<A>()`.
```
mu-core/Audio/FX/Algorithms/
  AutoPanInsert.h     class mu_audio::AutoPanInsert    insertAlgo 22
  GateInsert.h        class mu_audio::GateInsert       insertAlgo 23
  WaveshaperInsert.h  class mu_audio::WaveshaperInsert insertAlgo 24
  WaveshaperCurve.h   (optional) the triode curve as free inline functions, so a test can call the curve alone
mu-core/Tests/NewInsertTests.cpp (extend), new mu-core/Tests/ThreeInsertsTests.cpp (listed in Tests/CMakeLists.txt)
```
Each includes `Audio/FX/Algorithms/InsertAlgorithmBase.h` and `Audio/FX/Algorithms/FxUse.h` (as `ChorusFx.h` does). The old `Insert/` folder no longer exists.

### 3. Numbers and the edits that must agree
`kInsertAlgoCount` and `InsertProcessor::kNumInsertAlgos` go 22 to 25; two `static_assert`s (InsertProcessor.h, AlgorithmNames.h) and the `kInsertMenu` assert fail the build if any table is missed. Edits:
1. `Audio/InsertSlotConfig.h`: `kInsertAlgoCount = 25`; append rows 22, 23, 24 to `kInsertAlgoSlots`, `kInsertAlgoDefaults` and `kInsertSyncSlot` (22 -> `0`, 23 -> `-1`, 24 -> `-1`); update the "22 rows" comments.
2. `Audio/AlgorithmNames.h`: append `"AutoPan"` (22), `"Gate"` (23), `"Waveshaper"` (24) to `kInsertAlgorithmNames` (the names are the preset keys: **never rename later**); menu rows `{ 22, "Auto Pan", 25 }`, `{ 23, "Gate", 65 }`, `{ 24, "Waveshaper", 160 }` (alphabetical: after 3-Band EQ, between Fold and Hard Clip, after Vocoder St).
3. `Audio/InsertProcessor.h`: `kNumInsertAlgos = 25`; `InsertProcessor.cpp`: `dispatch[22] = add(mu_audio::makeInsert<mu_audio::AutoPanInsert>());` and likewise 23 and 24, plus the three includes.
4. Nothing per product. The names table, the menu (`populateInsertMenu`), the knob driver (`InsertSlotUi` reads `kInsertAlgoSlots`), the master insert panels, mu-link's master FX (`ServerEngine` clamps with `kNumInsertAlgos`), mu-Tant's param range and the preset name lookup all read these tables. All of it is in mu-core, so `check-core-boundary.ps1` is unaffected.
5. **Mod targets: no new row.** The insert knobs are the generic `insert.p1..p4` rows in every product, labelled per algorithm from `kInsertAlgoSlots`, so each new knob is a target the moment its label exists. Depth stays a percentage of the knob's range. The hidden fixed values are constants, not parameters, so `check-mod-targets.ps1` and `mu_mod::checks` need no edit.
6. Tests: `NewInsertTests.cpp` `expectEquals(kInsertAlgoCount, 25)`, loops run to `Waveshaper`, the "has a Mix knob" exemption becomes Tremolo, AutoPan, Gate, Waveshaper. `MixerBlockValueTests.cpp` `InsertLatencyTests` (instances near line 1085, count check near 1100, zero-delay assertion near 1139): add the three; AutoPan and Gate assert zero latency; Waveshaper gets the same measured-bound entry as TapeSat (impulse peak within 4 samples). `AlgorithmNameTableTests`, `InsertAlgoTableTests` and `BusInsertTests` iterate the tables and need no edit. `tests.md` (Fred), manuals (Frank), release note (`/notes`) once for the batch.
7. No goldens use these indices yet; none go stale.

### 4. Knob maps (slot ids fixed from first release; presets store the normalised value, so ranges and skews must not change later)
Status-bar names are the knob labels (the shell already reports label and value for every insert knob; nothing extra to wire).

**22 Auto Pan** (stereo constant-power pan swept by a wave; zero latency)
| Slot | Label | Range | Skew | Unit | Role | Default |
|---|---|---|---|---|---|---|
| 0 | Rate | 0.1..20 | Log | Hz | LFO rate; with the insert's Sync on, 1..16 sixteenths (`kInsertSyncSlot[22] = 0`, same helpers as Tremolo) | 0.5 |
| 1 | Depth | 0..100 | Linear | % | how far the wave swings the pan from Centre (100 % reaches full left and right) | 60 |
| 2 | Shape | 0..2 | IntStep, `kTremoloShapeNames` | "" | Sine / Triangle / Square (square rounded over about 2 ms, as Tremolo) | 0 (Sine) |
| 3 | Centre | -100..100 | Linear | % | static pan offset; also a plain pan control at Depth 0 | 0 |

DSP: `pan = clamp(centre + depth * w, -1, 1)` with `w` in -1..1 and `centre = Centre/100`. Angle `a = (pan + 1) * pi/4`; `gL = sqrt(2) * cos(a)`, `gR = sqrt(2) * sin(a)`. At pan 0 both gains are exactly 1 (so Depth 0 with Centre 0 leaves the signal bit-exact untouched; a test requires it); full left gives `gL = sqrt(2)`, `gR = 0`. The gains apply to the left and right channels of the stereo input (a balance, not a mono-to-stereo spread); a mono layer (`nCh = 1`) passes through unchanged. One LFO phase for both channels (no Phase knob); the rounded-square smoother is per insert. The LFO phase, sync maths (`insertSyncStepFromNorm`, `insertSixteenthSeconds`, `tempoBpm`) and square rounding follow `TremoloInsert` (a shared `LfoWave.h` helper is welcome the next time either is touched, not required now). Gains are computed per sample from the wave, so the sweep is continuous; the 2 ms square rounding is the only smoothing needed.

**23 Gate** (downward gate, internal stereo-linked detector, no look-ahead; zero latency)
| Slot | Label | Range | Skew | Unit | Role | Default |
|---|---|---|---|---|---|---|
| 0 | Threshold | -80..0 | Linear | dB | level above which the gate opens | -40 |
| 1 | Range | -80..0 | Linear | dB | how far the closed gate cuts; 0 = no cut (bit-exact untouched), -80 = fully shut (gain exactly 0) | -60 |
| 2 | Attack | 0.1..50 | Log | ms | gain rise time on opening | 1 |
| 3 | Release | 5..2000 | Log, dynamic unit | ms | gain fall time after hold | 150 |

DSP: detector `d = max(|L|, |R|)` through a peak follower (instant rise, 5 ms release; hidden). The gate opens when `d > thr` and closes when `d < thr - 4 dB` (hysteresis, hidden) and a hold of 20 ms (hidden) has run out. `floor = (Range <= -79.9) ? 0 : 10^(Range/20)`; Range 0 skips the multiply entirely. Target gain is 1 (open) or `floor` (closed); the gain moves by a one-pole in the linear domain, Attack time going up and Release time going down, one gain for both channels (stereo-linked, so the image does not shift). Time constants come from the sample rate in `prepare`, at least one sample long. No Mix, no sidechain, no gain-reduction meter (`insertAlgoShowsGainReduction` stays as it is; a Gate meter would be a separate UX decision). Threshold and Range are dB in a Linear skew on purpose (dB is already perceptual, as Output on the other inserts).

**24 Waveshaper** (valve-style: biased triode curve, asymmetric, even harmonics; 2x oversampled)
| Slot | Label | Range | Skew | Unit | Role | Default |
|---|---|---|---|---|---|---|
| 0 | Drive | 0..100 | Linear | "" | input gain, 0 to +30 dB | 30 |
| 1 | Output | -24..0 | Linear | dB | trim | 0 |
| 2 | Bias | 0..100 | Linear | % | grid bias: moves the operating point along the curve, more asymmetry and more even harmonics | 30 |
| 3 | Sag | 0..100 | Linear | % | supply sag: loud passages pull the stage's gain down and it recovers slowly (valve power-supply compression) | 30 |

Why Sag and not Tone in slot 3: Overdrive (14) is already the biased-tanh stage with Drive / Output / Bias / Tone. The Waveshaper's difference is the curve and the dynamics, so it spends its fourth knob on Sag, and does not repeat Overdrive's four labels. Tone can come from the EQ or a second insert.

**Transfer curve (the triode model).** With `v` the biased input:
- `f(v) = v / (1 + v)` for `v >= 0` (grid-conduction side, soft, limit +1.0)
- `f(v) = v / (1 - v / 1.5)` for `v < 0` (cut-off side, limit -1.5, reached later)

Slope 1 at `v = 0` on both sides (small signals pass at unity) while the second derivative differs, so there is 2nd harmonic even at Bias 0, the valve signature Overdrive's symmetric tanh lacks. This is a simple Child-Langmuir-style soft-knee rational model, not a Koren fit. Antiderivative if ADAA is needed (see Aliasing): `F(v) = v - ln(1 + v)` for `v >= 0`; `F(v) = -1.5 v - 2.25 ln(1.5 - v) + 2.25 ln 1.5` for `v < 0` (continuous at 0).

**Signal path (per channel).** `x` (non-finite replaced by 0, finite clamped to +-1e4, the TapeSat guard) -> times `preGain = 10^(Drive/100 * 30/20) / (1 + 0.8 * Sag/100 * env)` -> add `bias = Bias/100 * 0.6` -> **2x oversampled**: `f` -> subtract the resting offset `f(bias)` -> back to base rate -> DC block (20 Hz one-pole, as Overdrive and TapeSat) -> times `outGain = 10^(Output/20) / sqrt(preGainNoSag)`. `env` is a follower of `max(|L|,|R|)` times the drive gain, clipped to 4, attack 10 ms, release 150 ms (hidden starting values; the measured tests may retune them); stereo-linked, advanced at the base rate. At Sag 0 the gain is level-independent (a test requires it).

**Aliasing (2x oversampled).** Same ruling as TapeSat (amended 2026-11-16): `juce::dsp::Oversampling<float>`, 1 stage, `filterHalfBandPolyphaseIIR`, owned by the insert and created in `prepare` with the block size it is given (never the FIR `OversampledProcessor`, whose latency the insert chain cannot compensate); `reset()` resets it; long blocks run in chunks; channels beyond two pass through; nothing allocated on the audio thread. Target: 9 kHz sine at Drive 100, 44.1 kHz, strongest folded component at least 6 dB below plain `tanh` at the same gain. The rational curve is less smooth than tanh, so if 2x alone misses the target, add first-order ADAA with the antiderivative above (guards as `Adaa1Tanh.h`) before considering 4x; record the margin in `tests.md`.

**Latency.** Waveshaper adds the halfband group delay (about 4 samples), not reported and not compensated, exactly as TapeSat. Auto Pan and Gate add none and keep the zero-latency assertion; Gate has no look-ahead for that reason.

### 5. Real-time rules for all three
- No allocation outside `prepare`; no locks; no virtual calls beyond the base-class `process`. State for two channels, fixed-size arrays, as the other inserts.
- `prepare(sr, block)` guards `sr > 0` (falls back to 44100) and fills every coefficient from the rate; `reset()` returns all state to fresh (LFO phase, gate gain and hold counter, followers, oversampler), so reset-equals-fresh is bit-exact.
- NaN and Inf: Waveshaper clamps as above; Gate and Auto Pan treat a non-finite sample as 0 before the detector or the gain stage, so a burst cannot latch a follower or the gate. Every `insertSlot(p, n)` value is clamped to its table range inside the algorithm (as Tremolo does).
- Denormals: the callback runs under `juce::ScopedNoDenormals`; the followers, one-poles and the DC block still flush values below 1e-20 to 0 where they decay to silence.
- Smoothing: Drive, Output and Bias 15 ms (`juce::SmoothedValue`, snapped at `prepare` and `reset`, as Overdrive); Auto Pan's Centre and Depth 15 ms; Gate's gain smoothing is its own envelope.
- Finite and bounded output at all knob corners with and without Sync, at 44.1, 96 and 192 kHz and block sizes 1, 7, 512 (extends the existing corner test; peak bound 8 as the others).

### 6. Tests (offline, no listening; `ThreeInsertsTests.cpp`)
- **Auto Pan:** Depth 0 and Centre 0 untouched (error below 1e-5); constant power (`gL^2 + gR^2 = 2` within 1e-4 across pan); Centre +100 with Depth 0 puts everything on the right and leaves the left silent; left and right levels swing in opposition over one measured LFO period; the square shape has no sample-to-sample step above a bound; Sync gives the expected period at 120 bpm; mono passes through unchanged.
- **Gate:** a tone 20 dB above Threshold passes within 0.1 dB once the attack has settled; a tone 20 dB below is cut by Range (+-0.5 dB); Range 0 untouched; Range -80 gives exact zeros after release; a signal between Threshold and Threshold - 4 dB does not chatter (state changes bounded); Hold keeps the gate open 20 ms after the level drops; attack and release times within 20 % of the knob on a step; a hard-panned input gets the same gain on both channels (stereo-linked).
- **Waveshaper:** H2 above the Overdrive reference at the same Drive with Bias 0 (1 kHz sine, Drive 60) and H2 grows with Bias; odd harmonics still present; the curve alone has slope 1 at 0, is continuous across 0 and reaches the stated limits; Sag 100 pulls a loud burst's gain down and it recovers, Sag 0 is level-independent; DC after silence below 1e-4; alias margin as above; latency bound as above; level matching within 4.5 dB across Drive 0 to 100 for a 0.25-peak 1 kHz sine (family standard, as TapeSat); reset equals fresh.

**Do not:** add a Pan mode or a fifth slot to Tremolo; put the new files in `Insert/`; give any of the three a Mix knob (Depth, Range and Drive are their own dry/wet); reuse Overdrive's tanh or its Tone knob in the Waveshaper; add look-ahead to the Gate (latency); rename `"AutoPan"`, `"Gate"` or `"Waveshaper"` once released (they are preset keys); add a mod-target row by hand; change a range or skew after release.

**Andy decides:** the preset key "Waveshaper" for the valve stage (the menu text could say "Valve" with no other change, but the key must be chosen before release); Sag as the fourth knob (ruled) versus Tone.

**Status:** ruled, not built. Code owned by the main conversation: the numbered edits in section 3, then the tests in section 6.

## Five more inserts: Comb (25), FreqShift (26), Formant (27), Stutter (28), LoFi (29) (ruled 2026-11-16, Ted)

**Why.** Andy approved five more per-voice inserts. All are insert-only. This section settles placement, numbering, every table, the four-knob maps, the sync and no-transport behaviour of Stutter, the real-time rules and the tests, so a coder needs no further decisions. Sections 1, 2 and 5 of "Three new inserts" (own class per effect, `Algorithms/` with `FxUse::Insert`, `makeInsert<A>()`, `...Insert` suffix, finite-input guard, clamped slot reads, 15 ms smoothing snapped at `prepare` and `reset`, no allocation outside `prepare`, denormal flush, reset equals fresh) apply unchanged and are not repeated.

### 1. Files, names, numbers
```
mu-core/Audio/FX/Algorithms/
  CombInsert.h        class mu_audio::CombInsert       insertAlgo 25   preset key "Comb"
  FreqShiftInsert.h   class mu_audio::FreqShiftInsert  insertAlgo 26   preset key "FreqShift"
  FormantInsert.h     class mu_audio::FormantInsert    insertAlgo 27   preset key "Formant"
  StutterInsert.h     class mu_audio::StutterInsert    insertAlgo 28   preset key "Stutter"
  LoFiInsert.h        class mu_audio::LoFiInsert       insertAlgo 29   preset key "LoFi"
  HilbertIir.h        struct mu_audio::HilbertIir      shared helper: the all-pass-pair 90 degree network (FreqShift)
mu-core/Tests/FiveInsertsTests.cpp   (new, listed in Tests/CMakeLists.txt)
```
Preset keys are ASCII, no spaces, append-only, never renamed after release. "Comb" does not clash with the filter names `CombPlus` / `CombMinus`: those live in `kFilterTypeNames`, a different table. Menu text may differ from the key and may change freely.

**Edits that must agree** (the `static_assert`s on `kInsertAlgoCount`, `kNumInsertAlgos` and `kInsertMenu` fail the build if one is missed):
1. `Audio/InsertSlotConfig.h`: `kInsertAlgoCount = 30`; append rows 25..29 to `kInsertAlgoSlots` and `kInsertAlgoDefaults` (section 3); `kInsertSyncSlot`: 25 -> `-1`, 26 -> `-1`, 27 -> `-1`, **28 -> `0`**, 29 -> `-1`; update the "25 rows" comments to 30. Add one name table beside the others: `kStutterDirNames[3] = { "Fwd", "Rev", "Alt" }`.
2. `Audio/AlgorithmNames.h`: append `"Comb"`, `"FreqShift"`, `"Formant"`, `"Stutter"`, `"LoFi"` (25..29) to `kInsertAlgorithmNames`; menu rows (alphabetical, gaps kept): `{ 25, "Comb", 45 }` (after Clipper 40, before Compressor 50), `{ 27, "Formant", 62 }` (after Fold 60), `{ 26, "Freq Shift", 64 }` (before Gate 65), `{ 29, "Lo-Fi", 95 }` (after Limiter 90), `{ 28, "Stutter", 122 }` (after Soft Clip 120, before Tape Delay 125). Menu names stay short (`MasterColumnFitTests.cpp` measures them).
3. `Audio/InsertProcessor.h`: `kNumInsertAlgos = 30`; `InsertProcessor.cpp`: five includes and `dispatch[25..29] = add(mu_audio::makeInsert<mu_audio::XInsert>());`.
4. Nothing per product: the knob driver, menu, master panels, mu-link master FX (`ServerEngine` clamps with `kNumInsertAlgos`), mu-Tant's parameter range and the preset name lookup all read the tables (all mu-core, so `check-core-boundary.ps1` is unaffected).
5. **Mod targets: no new row.** The knobs are the generic `insert.p1..p4` rows, labelled per algorithm; depth stays a percentage of the knob's range. `check-mod-targets.ps1` and `mu_mod::checks` need no edit.
6. Tests (section 6). No goldens use these indices; none go stale. `tests.md` (Fred), manuals (Frank), release note (`/notes`) once for the batch.

### 2. Decisions common to the five
- **No oversampling in any of them.** Comb, FreqShift and Formant are linear (all-pass or low-pass bounded loops); Stutter is a copy; LoFi has no non-linearity. So there is no TapeSat-style halfband latency.
- **Latency: none reported, none compensated.** Comb, Formant and Stutter add none (Stutter replays audio it already passed; it never looks ahead). FreqShift's all-pass network has phase delay but no look-ahead. LoFi adds up to 2 ms at Wobble 100 and exactly 0 at Wobble 0. Latency test entries (`MixerBlockValueTests.cpp`): Comb, Formant, Stutter and LoFi at Wobble 0: zero; FreqShift: impulse peak within 8 samples; LoFi at Wobble 100: peak within 2.5 ms. Keep the registry check that no algorithm grows `getLatencySamples()`.
- **Mix.** Comb, FreqShift, Formant and Stutter have Mix in slot 3 (`dry*(1-m) + wet*m`, bit-exact dry at Mix 0, the wet branch skipped). **LoFi has no Mix**: each of its four knobs is an amount that is zero at 0 (Tone is open at 20 kHz), so it is dry/wet by construction. The "has a Mix knob" exemption list in `NewInsertTests.cpp` becomes Tremolo, AutoPan, Gate, Waveshaper, **LoFi**.
- **Random sources** (Stutter's hit roll, LoFi's drift, hiss and crackle): one xorshift32 per insert, seeded from `setSlotIndex` as Bitcrusher does, restarted in `reset()`, advanced per sample (so output does not depend on block size). Two layers do not produce the same noise; reset equals fresh.
- **Memory.** Every layer's `InsertProcessor` builds all algorithms in `prepare`, so each buffer is paid by every layer whether or not the algorithm is selected. Allocate for the prepared rate only (never a blind 192 kHz; `prepare` runs again on a rate change). At 192 kHz, two channels: Comb 50 ms (77 kB), LoFi 4 ms (6 kB), Stutter 2 s (3 MB; 0.8 MB at 48 kHz), FreqShift and Formant none. Stutter is the only one worth watching; it is capped at 2 s on purpose and is the first candidate for lazy allocation if the layer count ever makes it matter (design-runtime-counts).
- **Block size and rate.** Results match within float noise for block sizes 1, 7, 512 at 44.1, 96 and 192 kHz; an algorithm with a buffer shorter than `ns` loops in chunks.

### 3. Knob maps (slot ids fixed from first release; presets store the normalised value, so ranges and skews must not change later)
Status-bar names are the labels (the shell already reports label and value for every insert knob).

**25 Comb** (input-driven feedback comb resonator; zero latency)
| Slot | Label | Range | Skew | Unit | Role | Default |
|---|---|---|---|---|---|---|
| 0 | Freq | 20..5000 | Log, dynamic unit | Hz | resonant pitch (delay = sr / Freq) | 220 |
| 1 | Feedback | -95..95 | Linear | % | loop gain; negative inverts it (odd harmonics only, hollow, an octave lower) | 70 |
| 2 | Damping | 0..100 | Linear | % | one-pole low-pass in the loop: 0 open (20 kHz), 100 about 800 Hz (exponential map); darker, shorter ring | 30 |
| 3 | Mix | 0..100 | Linear | % | dry / wet | 50 |

DSP: `y[n] = x[n] + g * damp(y[n-D])`, `wet = y * sqrt(1 - g^2)` (power-normalised: a broadband input keeps about its level whatever the feedback; at resonance the peak rises, bounded at about +16 dB at 95 %). `D = sr/Freq` minus the loop's own fixed delay (one sample plus the damping filter's group delay at the current cutoff), so the pitch is true; read by 3rd-order Lagrange interpolation (`juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Lagrange3rd>` or an equivalent own class), line length `ceil(sr/20) + 8`. Freq (smoothed in the log domain, so a sweep glides) and Feedback are smoothed 15 ms; the damping coefficient is recomputed at block rate. `|g| <= 0.95` and a damping gain <= 1 keep the loop stable at every corner. Loop denormals flushed below 1e-20.
**Why this is not Karplus (11):** Karplus is a note-and-octave string excited by the input's attack; Comb is a continuous-Hz filter that colours whatever passes through, with negative feedback and a Mix. Karplus is **not changed**.
**Pitch tracking is not built.** `VoiceParams` carries no note frequency and an on-line detector adds latency and glitches. Tuning to a note is done with a sequence on the Freq knob (every knob is a mod target). "Follow the played note" would be a separate feature (a note-frequency field fed into `InsertProcessor` the way `setTempo` is); it changes the contract, so it goes through design-future first.

**26 FreqShift** (single-sideband frequency shifter; zero latency, not compensated)
| Slot | Label | Range | Skew | Unit | Role | Default |
|---|---|---|---|---|---|---|
| 0 | Shift | -1000..1000 | Linear | Hz | coarse shift; every partial moves by the same Hz (inharmonic, metallic) | 60 |
| 1 | Fine | -20..20 | Linear | Hz | added to Shift; slow beating and barber-pole phasing at small values | 0 |
| 2 | Feedback | 0..80 | Linear | % | wet output back into the input through a fixed 8 kHz one-pole low-pass (rising or falling spiral of repeats) | 0 |
| 3 | Mix | 0..100 | Linear | % | dry / wet | 50 |

DSP: analytic signal from `HilbertIir` (two all-pass chains of four second-order sections with the published coefficients, Niemitalo, "Hilbert transform with IIR all-pass filters", the 12-pole design, including its one-sample delay on the second chain; the coder follows the reference and the test checks the 90 degree difference). The pair `I`, `Q` is rotated by a quadrature oscillator: `wet = I*cos(phi) - Q*sin(phi)` shifts up; the sign of the `sin` term flips for a negative shift. The phase accumulator is a `double`, wrapped each sample; `cos` and `sin` come from a rotating complex multiplier renormalised each block (no `sin` per sample). Shift + Fine is smoothed 15 ms. A shifter preserves power, so no makeup gain. Feedback <= 0.8 keeps the loop stable. Independent network per channel, one shared oscillator phase.
**Limits to state in the manual (Frank):** the network is accurate from about 0.002 of the sample rate (about 90 Hz at 44.1 kHz, about 380 Hz at 192 kHz) up to near Nyquist; content below that edge leaks a faint opposite sideband, and content within the shift of Nyquist folds back (cannot be avoided without oversampling; accepted, the inputs are synth voices already filtered).
**Why this is not RingMod (9):** a ring modulator makes both sum and difference sidebands (symmetric, dissonant); the shifter makes one, so a harmonic note becomes a single inharmonic stack that moves. RingMod is not changed.

**27 Formant** (vowel filter; zero latency)
| Slot | Label | Range | Skew | Unit | Role | Default |
|---|---|---|---|---|---|---|
| 0 | Vowel | 0..100 | Linear | % | morph across A (0), E (25), I (50), O (75), U (100); in between, the neighbouring vowels' formant frequencies are blended on a log scale | 0 |
| 1 | Shift | -12..12 | Linear | st | moves all three formants together: up is a smaller, brighter throat, down a larger one | 0 |
| 2 | Resonance | 0..100 | Linear | % | narrowness of the formant bands (Q 3 at 0, Q 40 at 100, exponential map): breathy and open to nasal and ringing | 40 |
| 3 | Mix | 0..100 | Linear | % | dry / wet | 100 |

DSP: three parallel band-pass sections (TPT state-variable form, stable under modulation) at the interpolated formants F1, F2, F3, summed with fixed relative gains `{1.0, 0.5, 0.2}`. Table (adult male, Hz; the coder may tune by ear within these neighbourhoods, never the structure): A 800 / 1150 / 2900, E 400 / 1600 / 2700, I 350 / 1700 / 2700, O 450 / 800 / 2830, U 325 / 700 / 2530. `Shift` multiplies all three by `2^(st/12)`; each is then clamped to 60 Hz .. 0.45 * sr. Level: wet scaled by `sqrt(Q / 4) / sqrt(1.0 + 0.25 + 0.04)` so narrow bands do not vanish and wide ones do not boom (the test bounds the pink-noise level change at 6 dB over the knob corners). Vowel, Shift and Resonance are smoothed 15 ms; coefficients are recomputed every 32 samples while any of them moves, at block rate when none does.
**Why this is not the filter section:** every product's filter is one or two resonant poles; Formant is three peaks in a fixed relationship that move together and morph between named vowels, which no single filter does, and it is the character of the effect (a voice) rather than a tone control. It suits the drone synth and sample layers.
**Display note for Sid (optional):** Vowel shows as a plain % because the knob display has no letter readout for a Linear knob, and an IntStep with a name table would stop the morph being continuous. If Andy wants the letters shown, that is a small UI addition (a name-by-range table on `SlotConfig`), not a change to this ruling.

**28 Stutter** (tempo-locked repeater; zero latency)
| Slot | Label | Range | Skew | Unit | Role | Default |
|---|---|---|---|---|---|---|
| 0 | Size | 5..500 | Log, dynamic unit | ms | slice length; with the insert's Sync on, 1..16 sixteenth notes (`kInsertSyncSlot[28] = 0`) | 125 |
| 1 | Repeat | 0..100 | Linear | % | chance that a slice boundary starts a stutter | 50 |
| 2 | Direction | 0..2 | IntStep, `kStutterDirNames` | "" | Fwd, Rev, Alt (reverses on every other pass) | 0 (Fwd) |
| 3 | Mix | 0..100 | Linear | % | dry / wet | 100 |

**How sync works with the existing mechanism, and with no transport.** An insert has only `tempoBpm` (set per block from `InsertProcessor::setTempo`); it has **no transport position and no play flag**, and this ruling does not add them. So Stutter is *tempo-locked, not bar-locked*: with Sync on, the slice length is `n * insertSixteenthSeconds(tempoBpm)` (the same helpers and slot convention as Tremolo and Tape Delay); the clock is a free-running sample counter restarted at `reset()` (layer reset, new preset); a tempo change takes effect at the next slice boundary. With the transport stopped, or in a standalone with no host, the tempo is whatever `setTempo` last received (120 bpm if never set) and the clock keeps running, so Stutter behaves identically with or without a transport; it simply cannot start on a bar line. **Andy decides** whether bar-aligned phase is worth a family change (a transport-position atomic beside `tempo` in `InsertProcessor`, fed by each product's engine, read by Stutter, Tremolo, Auto Pan and Tape Delay): it is cross-product and belongs in design-future first; nothing here makes it harder, because the slice length is one helper (`sliceSamples()`).
**Slice cap.** At most 2 s (the buffer length), so a long synced step at a slow tempo clamps rather than overruns; the value display is unchanged.

DSP: both channels record continuously into a circular buffer (`ceil(2 * sr) + 8` samples, allocated in `prepare`). A slice boundary comes every `L` samples (`L` = current slice length, rounded, at least 1 ms). At each boundary, **if not already holding**, a seeded roll against `Repeat/100` decides: on a hit, the last `L` samples are frozen as the slice (a read window `[w - L, w)` in the ring, which keeps recording; the window is read only while it is within the buffer) and replayed for **4 periods** (hidden constant), then the insert returns to the live signal and the next boundary rolls again. Repeat 100 % therefore stutters continuously, 0 % never (bit-exact dry, the branch skipped). Direction: Fwd reads the slice forwards each pass, Rev backwards, Alt forwards then backwards. Each pass edge has a 2 ms linear fade (`L/4` for tiny slices) so the loop point does not click. The replay mixes with the dry by Mix; the live signal is replaced only inside a hit. A change of Size or Sync mid-hit takes effect at the next boundary. The decision and the clock are shared by both channels (stereo-linked, so the image does not shift).
**Why Direction and not Pitch:** a pitch change reads the slice at another speed, so its period no longer matches the grid and the repeat drifts off the tempo; Direction keeps every pass exactly one slice long. **Why no Gate knob:** the edge fades and the fixed 4-period hold already give the gated rhythm, and Repeat is the one probabilistic control; a fifth role does not fit four slots.

**29 LoFi** (vinyl and tape age: wobble, hiss, crackle, tone loss)
| Slot | Label | Range | Skew | Unit | Role | Default |
|---|---|---|---|---|---|---|
| 0 | Wobble | 0..100 | Linear | % | wow and flutter together: pitch drift; 0 is bit-exact dry with no delay | 25 |
| 1 | Noise | 0..100 | Linear | % | steady hiss level, 0 to about -30 dBFS (quadratic map: `0.03 * (n/100)^2`) | 0 |
| 2 | Crackle | 0..100 | Linear | % | random vinyl pops, 0 to about 40 a second (quadratic map on the rate), size random per pop | 0 |
| 3 | Tone | 500..20000 | Log, dynamic unit | Hz | high-frequency loss (two one-poles, 12 dB/oct); 20 kHz and up bypasses | 6000 |

Order: wobble delay, then Tone, then add hiss and crackle (so Tone dulls the music, not the added noise, as on a worn record). **Wobble** is delay-line pitch modulation: `D = w * 2 ms + w * (1.2 ms * wow(t) + 0.15 ms * flutter(t))` with `w = Wobble/100`; wow is a 0.5 Hz sine summed with a slow seeded random drift (about 0.15 Hz smoothed noise), flutter a 7 Hz sine plus small seeded jitter; the 8 : 1 ratio and the rates are fixed (a separate Wow and Flutter knob needs a fifth slot; **Andy decides** if he would rather give up Crackle or Tone for it). `D` is linearly interpolated (the mild HF loss is on theme), `w` smoothed 15 ms, `D` at least 1 sample, line `ceil(0.004 * sr) + 8`. At `Wobble = 0` the delay is skipped: bit-exact and zero latency; at 100 the centre delay is 2 ms. Pitch deviation at 100 is about +-0.4 % from wow and +-0.6 % from flutter; the coder tunes inside this by ear. **Hiss:** white noise from the seeded generator through a one-pole high-pass at 1 kHz, an independent stream per channel, **added** (level-independent). **Crackle:** per sample a Poisson test (`rand < rate / sr`) fires a pop of random sign and size (0.05..0.5 peak, cube of a uniform so most are small) shaped as a 1.5 ms exponential decay; both channels fire together on 70 % of pops, the rest on one channel (hidden). The noise sources run only while the layer's insert chain runs; they do not self-gate. Peak with all four knobs at maximum stays below 2 for a 1.0 input (tested).

### 4. Real-time rules (additions to the earlier section)
- No allocation or locks outside `prepare`; buffers are `std::vector<float>` sized in `prepare`; ring indices wrap by comparison, never modulo of a negative.
- Every `insertSlot(p, n)` read is clamped to the table range; the sync step comes from `insertSyncStepFromNorm`; a non-finite tempo reads as 120 bpm through `insertSixteenthSeconds`.
- A non-finite input sample becomes 0 before any state; a finite one is clamped to +-1e4 in the recursive stages (Comb, FreqShift feedback, Formant).
- Denormals: loop and filter states flushed below 1e-20 where they decay to silence (Comb loop, Hilbert chains, formant bands, noise high-pass).
- `prepare` guards `sr > 0` (falls back to 44100).

### 5. Class shapes
All five derive `InsertAlgorithmBase`, carry `static constexpr FxUse kUse = FxUse::Insert;`, hold two-channel state in fixed arrays and keep tunables as named `constexpr` values at the top of the class (the hidden numbers above). `HilbertIir` is a plain struct (`prepare(sr)`, `process(x)` returning the pair, `reset()`); the oscillator stays inside `FreqShiftInsert`. Nothing else is shared yet, so nothing goes in `Insert/` and no `mu-core/UI` code is needed (labels, ranges and the Direction names come from `InsertSlotConfig.h`).

### 6. Tests (offline, no listening; `FiveInsertsTests.cpp`)
Existing lists: `NewInsertTests.cpp` count 30, loops run to `LoFi`, exemption list as in section 2; `MixerBlockValueTests.cpp` latency list (section 2 numbers); the shared corner test (finite, peak bound 8, every knob corner, with and without Sync, 44.1/96/192 kHz, blocks 1/7/512) covers all five. `AlgorithmNameTableTests`, `InsertAlgoTableTests` and `BusInsertTests` need no edit.
- **Comb:** pitch: white noise through Freq 220 and 1000 Hz at 44.1 and 96 kHz, Feedback 80, Damping 0: the autocorrelation peak sits within 10 cents (a one-sample error would fail it); Feedback -80 resonates at odd multiples only (even partials 12 dB below the odd); Damping 100 shortens the ring (decay time under half of that at 0); Mix 0 untouched; broadband level within 3 dB of the input across Feedback 0..95; Feedback 95 at Freq 20 stays stable (no growth over 10 s of silence after a burst).
- **FreqShift:** a 1 kHz sine, Shift +200 Hz, Mix 100, at 44.1 and 96 kHz: strongest output at 1200 Hz, the opposite sideband (800 Hz) and the original (1000 Hz) at least 35 dB below it; a negative shift mirrors it; the Hilbert pair's phase difference within 1 degree at 1 kHz and 5 kHz; Mix 0 untouched; Feedback 80 stable and bounded; level preserved within 1 dB; reset equals fresh.
- **Formant:** the spectrum of white noise shows three peaks within 15 % of the table's F1, F2, F3 for each of A, E, I, O, U (stops at 0, 25, 50, 75, 100 %); 12.5 % lies between A and E; Shift +12 doubles them; Resonance 100 gives a narrower -3 dB width than 0 at F1; pink-noise level change within 6 dB over all vowels and Resonance corners; Mix 0 untouched; sweeping Vowel across its range in 100 ms gives no click (sample-to-sample step bounded).
- **Stutter:** Repeat 0 untouched; Repeat 100, Fwd, a ramp-then-silence input: after the first boundary the output repeats the last slice four times, then returns to live; slice length matches Size within one sample (free) and `n` sixteenths at 120 bpm (Sync); Rev plays the slice backwards (sample-exact against a reference); Alt alternates; pass edges have no step above the fade bound; with `setTempo` never called and no transport the behaviour is that of 120 bpm; a tempo change takes effect only at the next boundary; the 2 s cap clamps; over 2000 boundaries at Repeat 50 the hit count is within 5 % of half; reset equals fresh; Mix 50 equals the average of dry and Mix 100.
- **LoFi:** all knobs at 0 with Tone 20000: untouched and zero latency; Wobble 100: impulse peak within 2.5 ms and a 1 kHz sine's measured frequency swings by 0.3 to 1.2 % peak; Noise 100 on silence gives RMS within 2 dB of -34 dBFS; Noise 0 and Crackle 0 on silence is exact zero; Crackle 100 on silence has 25 to 55 pops a second over 20 s (threshold-detected); Tone 1000 cuts an 8 kHz sine by at least 20 dB and leaves 100 Hz within 0.5 dB; output identical across block sizes 1, 7, 512; two slot indices give different noise, the same index the same; reset equals fresh; peak bound as above.

**Do not:** add a Mix to LoFi; add Pitch to Stutter or read a transport inside an insert; build a pitch tracker for Comb; change Karplus, RingMod or Tremolo; put the new files in `Insert/`; give Formant a hidden mode; allocate on the audio thread; rename a preset key once released; add a mod-target row by hand; change a range or skew after release.

**Andy decides:** (1) names: the keys `Comb`, `FreqShift`, `Formant`, `Stutter`, `LoFi` are ruled here ("Vowel" would be a more telling key than "Formant"); the key must be chosen before release; (2) LoFi's single Wobble knob (ruled) versus separate Wow and Flutter by dropping another knob; (3) whether Stutter should be bar-aligned (needs a transport feed, see Stutter); (4) whether Comb should track the played note (a separate feature); (5) a vowel letter readout for Formant (Sid).

**Status:** ruled, not built. Code owned by the main conversation: the numbered edits in section 1, then the classes, then the tests in section 6. Build one at a time in the order 25..29, each with its own test.
