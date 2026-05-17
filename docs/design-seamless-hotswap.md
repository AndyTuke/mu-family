# Stage 34 — Seamless Hot-Swap (Polyphonic Voice Tail)

## Goal

When a rhythm is hot-swapped during playback, the old rhythm's audio continues playing its current sample / envelope tail naturally instead of being cut off. The new rhythm starts playing in parallel as its pattern steps land. Result: no click on swap, smooth musical transition.

## Why now

Hot-swap is functionally complete (#385/#386/#389/#392) but the old `VoiceEngine` is destroyed at swap commit, cutting any in-flight audio at non-zero level. On sustained material (pads, long-tail percussion, comb filters mid-decay) this is an audible click. The fix isn't a tweak — it's an architectural extension of the swap model. Worth doing now while the swap path is fresh.

## Architecture

Extend the existing `voiceEngines[r]` from a single owner to an "active + N retired tails" model:

- **Active engine** — the one new hits trigger on. One per rhythm slot, just like today.
- **Retired engines** — N engines per rhythm slot (default N=4) that no longer receive triggers but keep rendering whatever audio they had in flight. Move there at swap commit instead of being destroyed.
- **Drain** — when a retired engine has no active sample voices AND its amp envelope is idle, it's "drained" and safe to destroy.
- **Cleanup** — audio thread sets a per-slot atomic flag when a retired engine drains; message thread polls the flag and destroys the engine off the RT thread.

This is the same pattern as polyphonic voice stealing in samplers — old notes ring out into their natural tail while new notes start.

## Sequencing — incremental steps with test gates

Each step builds cleanly on its own and is testable. If a test fails, the step before is the rollback point.

---

### Step 1 — Drain detection (foundation, zero behaviour change)

**Change:** Add `bool VoiceEngine::isFullyDrained() const noexcept`. Returns `true` when:
- All `SamplePlayer` voices have `playPos < 0` (no sample mid-playback), AND
- `ampEnv.getState()` is JUCE's idle state OR amp envelope is below a tiny threshold (~1e-5 = -100 dB).

Header declaration only — no caller yet. No behaviour change.

**Build:** Should compile clean with zero warnings.

**Test 1.1 — Build only**
- `cmake --build build --config Debug` succeeds with no errors.
- `cmake --build build --config Release` succeeds with no errors.
- Plugin loads in standalone, plays default rhythm. No audible difference vs prior build.

**Risk if test fails:** Compile error. Trivial to roll back (delete the method).

---

### Step 2 — Retired-engine infrastructure (slots + render loop, no retire-on-swap yet)

**Change:** Add to `PluginProcessor`:

```cpp
static constexpr int kMaxRetiredEngines = 4;

// Per-rhythm slots for retired-but-still-draining engines.
// All start nullptr — populated by Step 3 when retire-on-swap is wired.
std::array<std::array<std::unique_ptr<VoiceEngine>, kMaxRetiredEngines>,
           SequencerEngine::MaxRhythms> retiredVoiceEngines;

// Audio thread sets true when isFullyDrained() returns on the matching slot.
// Message thread (handleAsyncUpdate) clears the slot when flagged.
std::array<std::array<std::atomic<bool>, kMaxRetiredEngines>,
           SequencerEngine::MaxRhythms> retiredReadyForCleanup;
```

In the voice-rendering path (currently `voiceEngines[r]->process(channelBuf, ns)` inside `processCoreBlock`), after rendering the active engine, iterate the retired array for that rhythm and call `process()` on each non-null engine, mixing into the same channel buffer. After each retired render, check `isFullyDrained()` — if true, set the cleanup flag.

In `handleAsyncUpdate`, drain the cleanup flags: for each set flag, destroy the corresponding retired engine (move out of unique_ptr → message-thread destruction, no RT thread alloc/free).

All retired slots start nullptr — nothing populates them in Step 2, so the iteration is always over null pointers. Zero behaviour change.

**Build:** Should compile clean.

**Test 2.1 — Behaviour unchanged**
- Plugin loads, plays default rhythm.
- Hot-swap a rhythm preset while playing. **Should sound exactly the same as before** (old voice still cuts off — Step 3 changes that).
- Multi-rhythm play (load preset with 2+ rhythms). Verify each rhythm sounds correct.
- Stop and restart. Verify no orphaned audio leaks.

**Risk if test fails:** Likely a CPU regression from iterating empty retired arrays, or a memory-layout issue. Roll back the rendering loop change.

---

### Step 3 — Retire-on-swap (the actual feature)

**Change:** In `handleAsyncUpdate`'s swap commit block, replace the destroy-by-overwrite with a retire-then-swap:

```cpp
// BEFORE Step 3:
voiceEngines[r] = std::move(sw.pendingVoice);   // old engine destroyed

// AFTER Step 3:
auto oldEngine = std::move(voiceEngines[r]);    // old engine survives
voiceEngines[r] = std::move(sw.pendingVoice);

// Find an empty retired slot for the old engine.
bool retired = false;
for (auto& slot : retiredVoiceEngines[r])
{
    if (!slot)
    {
        slot = std::move(oldEngine);
        retired = true;
        break;
    }
}
if (!retired)
{
    // All 4 retired slots full — back-pressure. Force-cut the oldest by
    // destroying it here (same as pre-Stage-34 behaviour for this one engine).
    // Pick slot 0 as "oldest" — simplest; could track timestamps if needed.
    retiredVoiceEngines[r][0] = std::move(oldEngine);
}
```

The old engine now lives in a retired slot. Audio thread continues rendering it each block (Step 2 wired this). It tails out naturally; when its amp envelope finishes and all sample voices complete, audio thread flags it for cleanup; message thread destroys it.

`suspendProcessing` is still used for the swap commit itself — Step 4 would replace that with atomic-pointer; Step 3 is fully functional without it. The slight suspend-gap silence (~1.33 ms at 48k/64) creates a tiny pause IN the retired engine's mid-sample playback. Subtle, usually imperceptible. Step 4 eliminates it.

**Build:** Should compile clean.

**Test 3.1 — Long-sample swap during playback (the key user-facing test)**
- Load a preset whose sample is long and sustained (pad, drone, long crash cymbal). At minimum 2 seconds of sustain.
- Play, wait for a hit. While the sample is mid-playback, **immediately** click a different preset in the rhythm dropdown.
- **Expected:** the old sample continues playing audibly while the new rhythm begins. No abrupt cut, no click.
- **Failure mode A** — old sample still cuts off: retire-on-swap didn't fire. Check that `oldEngine` move actually went into a retired slot and that the rendering loop is iterating retired slots.
- **Failure mode B** — old sample plays but glitches/clicks: drain detection is firing too early (cutting off mid-envelope), OR retired engine's parameters changed (it should be frozen — parameters only update on the active slot).
- **Failure mode C** — UAF crash on retired engine: cleanup queue raced with audio-thread rendering. Verify the cleanup flag is set BEFORE the message thread can destroy the engine, and that the destruction happens via move-out (audio thread sees nullptr next block).

**Test 3.2 — Spam-swap (back-pressure)**
- Queue 6 swaps in rapid succession on the same rhythm slot. (Click different presets as fast as you can.)
- **Expected:** Works correctly. The oldest retired engines get force-cut as the slot cap (4) is exceeded — those get clicks individually, but the system stays stable.
- **Failure mode:** crash, hang, or growing memory.

**Test 3.3 — Stop during retire**
- Play a long-sample preset. Mid-playback, swap to another preset. While the old engine is mid-tail-out (you can hear it), press Stop.
- **Expected:** all audio stops cleanly. No leftover audio after Stop. Retired engines should still exist in their slots — they just don't get triggered or rendered (or get rendered to silence). On next Play, the new active engine plays the new rhythm; old retired engines eventually drain (since amp envelope ran out) and get cleaned up.
- **Failure mode:** audio continues after Stop, OR Stop crashes, OR Play after Stop produces wrong audio.

**Test 3.4 — Stress: 8 rhythms each retiring**
- Load a preset with all 8 rhythms active, each playing long samples.
- Swap each rhythm to a different preset in rapid succession (within 1 second).
- **Expected:** all 8 retired tails play in parallel with the 8 new active engines. CPU goes up significantly but audio doesn't glitch. Eventually retired engines drain and CPU returns to baseline.
- **Failure mode:** CPU spike causes dropouts, OR a retired engine doesn't drain (memory growth), OR audio glitches.

---

### Step 4 — Atomic-pointer for the active engine (optional optimization)

**Change:** Replace `voiceEngines[r]` from `std::unique_ptr<VoiceEngine>` to `std::atomic<VoiceEngine*>`. Move storage to a separate `std::vector<std::unique_ptr<VoiceEngine>> voiceEngineStorage` (or per-slot std::unique_ptr in a separate array).

Audio thread reads `active.load(std::memory_order_acquire)` once per block. Swap commit:
1. Append new engine to storage (or assign to per-slot storage).
2. `active.store(newPtr, std::memory_order_release)`.
3. Retire old pointer to retired slot (same as Step 3, but now without suspendProcessing).

`handleAsyncUpdate` no longer calls `suspendProcessing(true/false)` for the swap commit.

**Why this is optional:** Step 3 already gives seamless tail-out. The remaining win from Step 4 is eliminating a single ~1.33 ms silence gap in the OLD retired engine's mid-sample playback during swap. Likely imperceptible without specific A/B testing.

**Trade-off:** Adds complexity (separate storage container, atomic-pointer lifetime management, memory-order discipline). Only worth doing if Step 3's residual artifact is audible.

**Build/test:** Same suite as Step 3, plus listen specifically for the difference at the moment of swap. If you can't hear a difference between Step 3 and Step 4, leave Step 4 unimplemented — the complexity isn't paying for itself.

---

## Risks and rollback

- **Step 1**: trivial; revert if compile fails.
- **Step 2**: should be invisible. If CPU spikes or audio glitches, the rendering-loop change is the suspect — revert it. The slot members can stay (unused).
- **Step 3**: the meaningful change. Risk vectors:
  - *UAF on retired engine*: message thread destroys while audio thread still rendering. Mitigated by audio-thread-sets-flag-first, message-thread-destroys-after pattern. Test 3.3 exercises this.
  - *Memory growth*: retired engines never drain. Detector: log when a retired engine sits >30s without draining. Mitigation: check `isFullyDrained()` definition — is amp envelope idle reachable?
  - *Wrong audio after swap*: retired engine somehow getting triggered or parameter-updated. It should be FROZEN — no parameter writes, no triggers, no APVTS sync. Verify in code review.
- **Step 4**: optional; only attempt if Step 3 lands stably. Atomic-pointer races are subtle.

## Out of scope for Stage 34

- Crossfade tail curve customization (currently natural envelope shape only — fine for v1).
- User-visible "retired engine count" indicator.
- Per-rhythm retired-engine cap configuration (hardcoded N=4 in v1).
- Saving retired-engine state across plugin destroy/restore (they just drain on shutdown — no persistence).

## Backlog entries to log per step

Each step lands as its own commit + backlog entry. Numbering starts at the next available issue (currently 413 or higher). Entries should describe what changed, what tests passed, and any deviations from this plan.
