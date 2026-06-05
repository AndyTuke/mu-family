# μ-Clid — Hot-Swap Design Reference

Authoritative reference for **seamless preset / rhythm hot-swap** in μ-Clid: how a
preset loaded *while the sequencer is playing* is staged off the audio thread and
committed at a musical loop boundary with no audible glitch. Read this before
touching `HotSwapStager`, `PresetIO`, the boundary detection in `PluginProcessor`,
or the retired-voice tail in `MixerEngine`.

For the family-wide pattern + the deliberate "keep it product-side" decision (and
how mu-tant's APVTS-centric variant differs), see
[../design-plugin-family.md](../design-plugin-family.md#hot-swap-staged-preset--layer-swaps-family-pattern).

---

## 1. What hot-swap is

Two flavours, both "switch at the next musical boundary, seamlessly":

| Flavour | Trigger | Scope | Entry point |
|---|---|---|---|
| **Per-rhythm swap** | load a `.muRhythm` into one slot (dropdown / MIDI PC ch 1-8 / `RhythmSidebar`) | one rhythm slot | `PresetIO::stageRhythmPreset(r, file)` |
| **Full-preset swap** | load a `.muClid` (dropdown / MIDI PC ch 9) | every slot + mixer + globals | `PresetIO::loadPreset(file)` |

**Stopped vs playing — one unified path (#666).** When the sequencer is *stopped*
the preset is applied immediately; when *playing* it is staged and committed at a
loop boundary. Crucially both use the **same build code and the same commit code** —
only the *trigger* differs — so a stopped load and a playing load can never diverge.

---

## 2. The core principle: heavy at stage, trivial at commit

The swap is glitch-free because **all expensive work happens at stage time, off the
audio thread**, and the **commit is just in-memory `std::move`s** under a single
short `suspendProcessing` window.

- **Stage time** (message thread, sequencer still rendering the *old* preset):
  parse XML → `ValueTree`, build each `Rhythm`, build each `VoiceEngine`
  **including sample disk I/O**, deserialise modulators. Nothing the audio thread
  touches is mutated yet.
- **Commit** (message thread, at the boundary, under `suspendProcessing` +
  `rhythmsLock`): move the pre-built `Rhythm` + `VoiceEngine` into the live slots,
  update the pattern cache. Held for *microseconds*.

> **Never do parse / disk I/O / allocation inside the commit.** That is the single
> rule that keeps the swap inaudible. If you find yourself adding work under the
> commit's suspend, move it to the stage-time build instead.

---

## 3. Components

| File | Role |
|---|---|
| [mu-clid/Source/Plugin/HotSwapStager.h/.cpp](../../mu-clid/Source/Plugin/HotSwapStager.h) | The staging state machine: per-rhythm pending slots + one full-preset pending slot, the store-release/load-acquire handshake, `checkBoundaries` (audio thread) and `processSwaps` (message thread). |
| [mu-clid/Source/Plugin/HotSwapBoundary.h](../../mu-clid/Source/Plugin/HotSwapBoundary.h) | **Pure** loop-boundary predicates (no processor dependency) so the defer decision is unit-testable. |
| [mu-clid/Source/Plugin/PresetIO.cpp](../../mu-clid/Source/Plugin/PresetIO.cpp) | `stageRhythmPreset`, `loadPreset`, `buildPreparedFullPreset`, `commitStagedFullPreset` — the build + apply bodies. |
| [mu-clid/Source/Plugin/PluginProcessor.cpp](../../mu-clid/Source/Plugin/PluginProcessor.cpp) | Boundary detection in `advanceSequencer` → `checkBoundaries` → `triggerAsyncUpdate`; `handleAsyncUpdate` → `processSwaps`. |
| [mu-clid/Source/Sequencer/SequencerEngine.h](../../mu-clid/Source/Sequencer/SequencerEngine.h) | Emits `BlockResult { rhythmLoopWrapMask, masterLoopWrapped }` and owns the wrap detector. |
| [mu-core/Audio/MixerEngine.cpp](../../mu-core/Audio/MixerEngine.cpp) | Renders **retired** voice engines (the swap tail) + flags them drained. |

---

## 4. Threading contract

```
message thread                         audio thread
──────────────                         ────────────
stage() / stageFullPreset()
  build payload (parse, VoiceEngine,
  sample load) — NO lock
  isReady.store(true, release) ───────► checkBoundaries() (per block)
                                          isReady.load(acquire)
                                          + loop wrapped this block?
                                            boundaryReached.store(true, release)
                                            return true
                              ◄───────── triggerAsyncUpdate()
handleAsyncUpdate()
  processSwaps()
    suspendProcessing(true) + rhythmsLock
    std::move pending → live slots
    suspendProcessing(false)
    pushRhythmToAPVTS + onCommitted cb
```

- `pendingSwaps[]` / `pendingPreset` are written on the message thread (stage /
  cancel) and read by the audio thread **only** through the two atomic flags
  (`isReady`, `boundaryReached`). The *payloads* are never touched by the audio
  thread.
- **No lock is held during `stage()`** — the `isReady` store-release is the barrier.
- `processSwaps()` is the only place the live slots change, under
  `suspendProcessing` (stops future blocks) **+** `rhythmsLock` (serialises with any
  in-flight block — suspend alone does not block an already-running `processBlock`,
  #663).

---

## 5. Boundary detection

`SequencerEngine::processBlock(beatPos)` returns a `BlockResult`:

- `rhythmLoopWrapMask` — bit *N* set = rhythm *N*'s step index wrapped to 0 this block.
- `masterLoopWrapped` — the master-loop counter reset this block.

`PluginProcessor::advanceSequencer` feeds those to
`hotSwapStager.checkBoundaries(numRhythms, masterLoopWrapped, rhythmLoopWrapMask)`,
which consults the **pure predicates** in `HotSwapBoundary.h`:

```cpp
// Per-rhythm swap. swapMode 0 (master mode) → every rhythm defers to the master
// loop point; otherwise each rhythm defers to its OWN loop wrap (bit r).
perRhythmBoundaryReached(swapMode, r, masterLoopWrapped, rhythmLoopWrapMask)

// Full preset. Defers to the master loop when one is defined (a preset spans every
// rhythm, so the master loop is the musical boundary). FREE-RUNNING (mstrLoop = 0,
// the default) has no master wrap, so fall back to rhythm 0's loop — without this
// the swap waits forever for a boundary that never comes (#653).
fullPresetBoundaryReached(hasMasterLoop, masterLoopWrapped, rhythmLoopWrapMask)
```

`swapMode` is user-settable (`PluginProcessor::setSwapMode`, a `SwapMode` atomic) and
decides whether per-rhythm swaps wait for the master loop or each rhythm's own loop.

**Wrap-detector resets** (all guard against a *false* wrap firing a premature commit):
- transport **stop→start** edge → `resetWrapDetector()` (`lastEffectiveStep = -1`) so the first block after restart can't emit a stale wrap.
- master-loop-length change → `setMasterLoopSteps` resets `lastEffectiveStep`.
- **after a commit** → `resetStepTrackingForSwap(r)` so the new pattern fires its first hit at the commit step instead of dropping it.

---

## 6. Per-rhythm swap walk-through (`stageRhythmPreset`)

1. Not playing → `applyRhythmPreset(file, r)` immediately, return.
2. Parse + validate the `.muRhythm` (`requireSupportedPresetVersion` — **v2 only**;
   legacy v0/v1 are refused with a clear `onLoadError`).
3. Migrate in place: `migrateInsertSlotsV3` (named insert fields → `insP1..4`),
   `migrateModAssignmentsV3` (old destination IDs → `insert.p1..4`).
4. Build `newRhythm` from the *current* rhythm with the preset's params applied on
   top (`kRhythmParamDefs` loop), then name / colour, then `deserialiseModulators`.
5. Build the new `VoiceEngine` (sample disk load) — the expensive part, done here.
6. `cancelPendingIfAny(r)` then `stage(r, move(rhythm), move(voice), samplePath)`.

---

## 7. Full-preset swap walk-through (`loadPreset`)

1. Parse → `root`. A **non-`MuClidPreset`** root is host/project state (the
   `getStateInformation` format) → `restoreStateFromTree` (not a hot-swap).
2. `buildPreparedFullPreset(root, …)` — build **every** `Rhythm` + `VoiceEngine` +
   sample off the audio thread into a `PreparedFullPreset { rhythms[], voices[],
   samplePaths[], tree }`.
3. Playing → `stageFullPreset(move(prepared))` (which first **cancels all pending
   per-rhythm swaps** — a full preset overwrites every slot). Stopped →
   `commitStagedFullPreset(prepared)` immediately.

`commitStagedFullPreset` (the commit, identical for stopped + boundary paths):
- `suspendProcessing(true)` + `rhythmsLock`.
- `setNumRhythms(n)`. **Shrink → drop the active count first; grow → publish the
  count last (after the new slots are populated).**
- Per slot: **retire-then-swap** (§8), move in the pre-built `Rhythm`, set sample
  path, prepare MIDI engines for newly-grown slots, `updatePattern`,
  `resetStepTrackingForSwap`.
- Tear down slots no longer active (shrink).
- Release `rhythmsLock`, `suspendProcessing(false)`.
- **APVTS finalize** outside the suspend, under `ScopedApvtsLoading` (so the
  `parameterChanged` listener skips the engine re-sync and can't clobber the
  freshly-installed voice): `pushRhythmToAPVTS` per rhythm + channel / global params
  from the parsed tree.

---

## 8. Retire-then-swap — the seamless voice tail (Stage 34 Step 3)

The reason a swap doesn't *click*: the **outgoing `VoiceEngine` is not destroyed at
the swap point.** Instead:

1. At commit, the old engine is `markRetired()` (enters its released / filter-reset
   state) and parked in a `retiredVoiceEngines[r]` slot; the new engine takes the
   live slot.
2. The audio thread keeps rendering each retired engine into the **same channel
   buffer** as the active one — so it rides the same fader / pan / sends / inserts —
   until `VoiceEngine::isFullyDrained()` (its in-flight sample + amp-envelope release
   have played out). `MixerEngine` then store-releases the parallel
   `retiredReadyForCleanup[r][i]` flag.
3. The message thread (`processSwaps`, under `suspendProcessing`) moves the drained
   engine out into a local that **destructs off the RT thread**.

`kMaxRetiredVoiceEngines = 4` per slot ([mu-core/MuLimits.h](../../mu-core/MuLimits.h)).
**Spam-swap back-pressure:** if all retired slots are full, slot 0 is force-cut
(hard stop) to make room.

---

## 9. `processSwaps()` (runs in `handleAsyncUpdate`)

1. **Drain retired-engine cleanup.** Empty-fast-path: scan the cleanup flags; only if
   any is set, `suspendProcessing` once, move out every flagged engine into a local
   `orphans` array, clear the flags, resume — `orphans` destructs off-RT at scope exit.
2. **Per-rhythm commit (two-pass, one suspend for the batch).** Collect every slot
   whose `boundaryReached` is set (skip any whose `isReady` was cleared by
   `cancelStagedSwap` between the audio thread flagging it and this handler running);
   then under **one** `suspendProcessing` retire-then-swap each; then, outside the
   suspend and under one `ScopedApvtsLoading` guard, `pushRhythmToAPVTS(r)` +
   `onRhythmHotSwapCommitted(r)`.
3. **Full-preset commit.** If `presetBoundaryReached`, clear the flags,
   `commitStagedFullPreset(pendingPreset)`, release the payload, fire
   `onPresetSwapCommitted()`.

---

## 10. UI: staging badges + commit callbacks

- **`TransportBar`** shows an orange **"SWP"** pill on the preset dropdown while a
  full-preset swap is queued — it polls `ProcessorBase::hasPendingFullPreset()` (5 Hz).
- **`ChannelSidebar`** shows a per-slot staging badge — it polls the product's
  `isPendingSwap(r)` (→ `hasPendingSwap(r)`) and offers cancel via
  `onCancelPendingSwap(r)` (→ `cancelStagedSwap(r)`).
- After a commit, `onRhythmHotSwapCommitted(r)` / `onPresetSwapCommitted()` fire on
  the message thread so the editor can refresh **non-APVTS** UI state (name label,
  sample bar, colour). The editor **must clear these callbacks in its destructor** —
  the processor can outlive the editor (DAW close-window-keep-plugin), and a commit
  firing into a dead editor is a use-after-free.

---

## 11. Offline render (`RenderMode`)

The offline render loop never yields to JUCE's message loop, so `triggerAsyncUpdate`
would never be serviced. `RenderMode` calls `proc.flushPendingAsyncUpdates()`
(`handleUpdateNowIfNeeded`) after each `processBlock` so a staged swap commits
synchronously on the render thread. (Live standalone/plugin pump the message loop
normally, so they don't need this.)

---

## 12. Edge cases / invariants

- **Cancel race:** a swap cancelled between the audio thread setting `boundaryReached`
  and `processSwaps` running is skipped (the `isReady`-cleared check).
- **Full supersedes per-rhythm:** `stageFullPreset` cancels all pending per-rhythm
  swaps so they can't commit onto slots the preset is about to overwrite.
- **Free-running** (no master loop): the full-preset swap falls back to rhythm 0's
  loop so it doesn't hang (#653).
- **Spam-swap:** retired slots full → force-cut slot 0.
- **v2-only presets:** legacy versions are refused at the stage entry point.
