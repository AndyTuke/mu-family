# #627 — Filter-then-Amp signal-flow refactor

## Goal

Re-order the VoiceEngine signal flow so the amp envelope sits at the **end** of the chain (after filter + insert) instead of at the front (per-voice, pre-filter). This naturally gates ALL post-DSP audio by the amp envelope, fixing two related issues:

1. **#627 — FX tail truncation on hot-swap.** Currently `markRetired()` resets `voiceFilter` + `insertProc` state to kill the "comb-filter / bitcrusher overlay" symptom from #416. The reset also kills the legitimate ringing of resonant filters, comb feedback, and reverb-style inserts. With filter-then-amp, no reset is needed: filter+insert ring into the envelope's release, gated to silence when the envelope hits idle.

2. **Latent bug in active engines.** Even WITHOUT retiring, the current `output.addFrom(tempBuffer, ampLevel * accentGain)` skips the amp envelope entirely. A high-feedback filter or sustained insert continues producing audio after the voice's envelope reaches idle, until masked by the next trigger. The new chain gates this too.

## Current signal flow (pre-refactor)

```
trigger() → ampEnvs[claimedIdx].reset() + noteOn()

process() per voice:
    SamplePlayer.process(buffer, ratio, voiceBuffers[vi], ns)   // raw sample
    ampEnvs[vi].applyEnvelopeToBuffer(voiceBuffers[vi])         // env shapes voice
    tempBuffer += voiceBuffers[vi]                              // mix env-shaped audio

process() post-mix:
    voiceFilter.process(tempBuffer)                             // shared filter
    lowCutFilter.process(tempBuffer)                            // shared HPF
    insertProc.process(tempBuffer)                              // shared insert FX
    output.addFrom(tempBuffer, ampLevel * accentGain)           // static scale only — env NOT re-applied
```

**Problem**: amp env is per-voice but applied BEFORE the shared filter+insert. Post-filter audio can outlast the envelope (filter resonance, insert feedback) and produce output indefinitely. The `output.addFrom` step doesn't re-gate by env.

## Proposed signal flow (post-refactor)

```
trigger() → ampEnv.reset() + noteOn()    [single engine-level env, not per-voice]

process():
    tempBuffer.clear()
    for each voice slot vi:
        if voices[vi].isActive():
            SamplePlayer.process(buffer, ratio, voiceScratch, ns)
            tempBuffer += voiceScratch          // mix raw samples (no env yet)
    voiceFilter.process(tempBuffer)             // shared filter (state rings naturally)
    lowCutFilter.process(tempBuffer)
    insertProc.process(tempBuffer)              // insert FX
    ampEnv.applyEnvelopeToBuffer(tempBuffer)    // single engine-level env gates the mix
    output.addFrom(tempBuffer, ampLevel * accentGain)
```

**Effect**: when `ampEnv` hits idle (envelope release complete), `applyEnvelopeToBuffer` multiplies by 0 → output is silent regardless of what filter/insert internal state is doing. Drain detection works the same as today.

## Architectural choice — engine-level vs per-voice amp envelope

The current code has **per-voice amp envelopes** (`ampEnvs[MaxVoices]`) but shared filter + shared insert + shared pitch env + shared filter env. This is a hybrid that's been the source of behaviour quirks. The proposal is to collapse to **engine-level amp envelope** (`ampEnv`) to align with the rest of the engine's already-shared state.

**Per-voice considered and rejected** — would require per-voice filter + insert chains (4× state, 4× DSP cost). The filter+insert pair has substantial state (comb buffers, ladder coefficients, compressor envelope follower, FFT windows in vocoder). Multiplying by 4 voices × 8 rhythm slots = 32 filter+insert instances would be a significant CPU + memory hit for a feature most patterns don't exercise (μ-Clid is primarily a sample-trigger drum/percussion sequencer, not a polyphonic pad synth).

### Behaviour trade-off

With engine-level amp env, **per-voice envelope shaping is lost** in overlap scenarios:

- **Today** — voice 0 mid-release (env at 0.3) + voice 1 attacks (env at 1.0). Mix = sample0×0.3 + sample1×1.0. Each voice independently shaped.
- **After** — voice 0 still mid-playback (sample data audible) + voice 1 attacks → engine env resets, ALL voices now multiplied by re-attacking env. Voice 0's "release" is gone; it re-attacks alongside voice 1.

**Where this matters**:
- **Percussion / drum patterns** — fine. Each hit is a fresh sample, overlap is rare, retrigger feels percussive anyway.
- **Pad-style overlapping sustain** — without intervention, would sound pulsed (every hit re-attacks the whole sound). Mitigated by **Pattern Legato (#419)** — tied hits already skip the envelope retrigger, so a held pad with adjacent tied steps gets exactly one attack and the envelope rides through.
- **Long-decay percussion with overlapping tails** — affected. Voice 0's natural decay would be cut short by voice 1's attack. May need a polyphony rethink later if this becomes a complaint.

## Step-by-step plan

Each step builds incrementally and is testable on its own.

### Step 1 — Collapse `ampEnvs[MaxVoices]` to single `ampEnv`

**[Source/Audio/VoiceEngine.h](../Source/Audio/VoiceEngine.h)**
- Replace `std::array<juce::ADSR, MaxVoices> ampEnvs` with `juce::ADSR ampEnv`
- Keep `voiceBuffers` for now (Step 2 removes it)

**[Source/Audio/VoiceEngine.cpp](../Source/Audio/VoiceEngine.cpp)**
- `prepareToPlay`: replace the per-voice loop with `ampEnv.setSampleRate(sampleRate)`
- `trigger`: replace `auto& ampEnv = ampEnvs[claimedIdx]; ampEnv.reset(); ampEnv.noteOn();` with `ampEnv.reset(); ampEnv.noteOn();` (no slot indexing)
- `syncEnvelopes`: configure single `ampEnv` with `activeParams.ampEnv*`
- `markRetired`: replace `for (auto& env : ampEnvs) env.noteOff()` with `ampEnv.noteOff()`
- `isFullyDrained`: replace per-voice env loop with `if (ampEnv.isActive()) return false`
- `process`: NO behaviour change yet — keep applying per-voice env (use `ampEnv` for all four voice slots, so each voice still gets the same env application as before, just from a shared env state). This step preserves drum-trigger behaviour and unblocks Step 2 cleanly.

**Build:** clean.

**Test 1.1 — Behaviour unchanged**
- Plays a standard pattern, sounds the same.
- Multi-voice overlap may differ slightly (since ampEnv state is now shared) but typically imperceptible on drum patterns.

### Step 2 — Move envelope application to end of chain

**[Source/Audio/VoiceEngine.cpp](../Source/Audio/VoiceEngine.cpp)** `process()`:
- Remove the per-voice `ampEnv.applyEnvelopeToBuffer(voiceBuffers[vi])` calls.
- Remove `voiceBuffers[vi]` accumulator pattern; voices render directly into `tempBuffer` via `vb.process(...)` with `tempBuffer` as the destination. Could also drop `voiceBuffers` member entirely.
- After `insertProc.process(tempBuffer)`, add `ampEnv.applyEnvelopeToBuffer(tempBuffer, 0, ns)`.
- `output.addFrom` unchanged.

**[Source/Audio/VoiceEngine.h](../Source/Audio/VoiceEngine.h)**:
- Remove `std::array<juce::AudioBuffer<float>, MaxVoices> voiceBuffers` member.
- Remove its `prepareToPlay` setSize loop.

**Build:** clean.

**Test 2.1 — Filter/insert tail rings through env release**
- Load a preset with strong filter resonance (LP24, cutoff ~800 Hz, resonance ~85%) and a short-decay envelope (Decay=300ms, Sustain=0).
- Trigger a hit. **Expected** (post-refactor): the filter "ping" rings through the envelope's release phase, fading to silence as env hits idle.
- **Compare to pre-refactor**: the ring was cut at envelope idle (the actual ring was happening in the filter but never reaching output past the per-voice env's zero state).

**Test 2.2 — Karplus / comb-filter natural decay**
- Load a Karplus preset (algorithm 11) with default Feedback ≈ 80%.
- Trigger and let it decay. **Expected**: the comb feedback rings musically and fades naturally with the envelope.

**Test 2.3 — Hot-swap FX tail (Stage 34 follow-up)**
- Long-sustain preset with resonant filter playing → swap to clean kick mid-decay.
- **Expected**: filter resonance now continues past swap (along with sample + amp env tail). Pre-refactor the resonance was cut by `markRetired`'s `voiceFilter.reset()`.

### Step 3 — Remove markRetired's filter+insert reset

**[Source/Audio/VoiceEngine.cpp](../Source/Audio/VoiceEngine.cpp)** `markRetired`:
- Remove `voiceFilter.reset()`, `lowCutFilter.reset()`, `insertProc.reset()`.
- Keep `ampEnv.noteOff()` + `filterEnv.noteOff()` + `pitchEnv.noteOff()` and the `retired = true` flag.

**[Source/Audio/VoiceEngine.h](../Source/Audio/VoiceEngine.h)**:
- Update the `markRetired` doc-comment to remove the "voiceFilter.reset() + insertProc.reset()" bullet and note that the env-end gate now handles drain.

**Build:** clean.

**Test 3.1 — Stage 34 #416 regression check**
- Load comb-filter or bitcrusher preset, play, swap to clean kick.
- **Expected**: NO "effect overlay" on the new kick. The retired engine's filter/insert state continues ringing but only outputs through its own engine's `ampEnv`, which is in release. As env approaches zero, the retired engine's output approaches silence. The new active engine has its own clean filter/insert state.
- **If failure**: the retired engine's filter ringing leaks into the new active engine's audio (would indicate the channel buffer is being shared without isolation — needs deeper investigation).

### Step 4 — Listening regression sweep

Run T1–T9 from the Tests section + the new Test 2.1 / 2.2 / 3.1 from Steps 2 / 3 above. Listen to a broad set of presets to gauge whether the polyphony trade-off (engine-level env) is acceptable in practice.

## Risks and rollback

- **Step 1**: nearly zero-risk (single env behaves equivalently to four identical-config per-voice envs for typical use). Revert = restore `ampEnvs[MaxVoices]`.
- **Step 2**: meaningful behaviour change for resonant filter / comb feedback (FX tail extended). Revert = move `applyEnvelopeToBuffer` back into the per-voice loop. Restore `voiceBuffers`.
- **Step 3**: completes the #627 fix. Revert = restore `voiceFilter.reset() + insertProc.reset()` in `markRetired`. Stage 34 fall-back to truncated tails.

## Out of scope

- Per-voice filter/insert chains (the "proper" polyphonic synth model). 4× CPU cost not justified for μ-Clid's drum-trigger use case.
- Crossfade tail curve customisation (natural envelope shape only — fine for v1).
- Per-rhythm "tail mix" buffer that gates differently (over-engineering for current symptom).

## Decisions for Andy

1. **Polyphony trade-off acceptable?** Engine-level amp env loses per-voice release shaping on overlapping voices. Pattern legato (#419) mitigates pad cases. OK to proceed?
2. **Implement now or after the listening tests on the current build?** The Tests section (T1–T9) is testing the current behaviour at v1.0.595. Worth verifying T1–T4 (Stage 34) at the current build first, so we have a "before" baseline against the post-refactor "after"?
3. **Drop `voiceBuffers` member entirely?** Step 2 makes it unused. Removing saves 4 × blockSize × channelCount floats per engine. Cheap to drop; no reason to keep unless we anticipate going back to per-voice in v2.

## Estimated effort

- Steps 1–3 code edits: ~80 LOC total across 2 files
- Build + smoke test: 5–10 min
- Listening regression on representative presets: 20–30 min
- Total: ~1 hour engineer time + Andy's listening verification
