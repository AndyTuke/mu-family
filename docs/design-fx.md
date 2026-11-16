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

The text below is the ruling as made; where it says "today" or "old" it describes the code before the merge. Built state: `Algorithms/` holds the three `Fx` + `Core` pairs, `FxUse.h`, `FxFactory.h` and `ModDelayLine.h`; `Engines/` holds `SpringReverb.h` and `TapeSpringEcho.h`; the insert-only algorithms still live in `Insert/` and `Send/` still holds `EffectAlgorithmBase.h` until step 6; `Tests/FxMergeTests.cpp` exists.

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
`Send/` and `Insert/` disappear once step 6 has moved the remaining insert-only algorithms and `EffectAlgorithmBase.h`; until then `Insert/` holds the insert-only algorithms and `Send/` holds `EffectAlgorithmBase.h`. Classes live in namespace `mu_audio` (the area namespace for audio in mu-core; the old global names are left alone). Class `ChorusFx` etc. = the algorithm used by the slots; `ChorusCore` etc. = the DSP, nothing else. Insert-only algorithms keep their `...Insert` names (renamed to `...Fx` only when touched for another reason, never in a bulk rename).

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
6. **Move the rest (open).** `git mv` the remaining `Insert/*.h` (including `InsertAlgorithmBase.h`, `NoneInsert.h`) and `Send/EffectAlgorithmBase.h` into `Algorithms/`, add `kUse = FxUse::Insert` to each insert-only class, fix includes (`InsertProcessor.*`, `EffectSlot.h`, tests); `Send/` and `Insert/` are now empty and gone. Mechanical, and may be done later or by itself; steps 1-5 do not depend on it.
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
- `Insert/TapeSatInsert.h` is rewritten in place (class name, id 10 and dispatch unchanged; insert-only, so it keeps its `...Insert` name and moves to `Algorithms/` with the rest in the merge's last step).
- New shared helpers in `mu-core/Audio/FX/Algorithms/` (namespace `mu_audio`):
  - `Adaa1Tanh.h`: `struct Adaa1Tanh { float process(float x) noexcept; void reset() noexcept; }`, the ADAA tanh with its guards, lifted from `OverdriveInsert.h` (a second user, so it moves to a shared header before a second copy exists). `OverdriveInsert` switches to it in the same step (identical maths; bit-identical output is the check).
  - `TapeEmphasis.h`: `struct TapeEmphasis { void prepare(double sr); float pre(float) noexcept; float de(float) noexcept; void reset() noexcept; }`, the first-order shelf pair.
- Reuses `BiquadFilter` and `OnePoleLP` (`Audio/AudioFilters.h`) and `InsertSlotConfig.h`. Nothing new in `mu-core/UI`. No change to design-naming (class names follow the existing `...Insert` rule; helpers are plain nouns).

### Build steps (each builds and runs its tests)
1. Add `Algorithms/Adaa1Tanh.h`; switch `OverdriveInsert.h` to it; existing Overdrive tests unchanged.
2. Add `Algorithms/TapeEmphasis.h`.
3. Rewrite `Insert/TapeSatInsert.h` to the path above (built); `InsertProcessor.cpp` dispatch untouched. Step 3b (amendment): add the 2x polyphase-IIR oversampling around the saturating stage, adjust the latency test entry and the aliasing and droop tests.
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
