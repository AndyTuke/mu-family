# μ-Tant — Hot-Swap Design Reference

How μ-Tant loads a preset *while playing* and switches it at a musical loop
boundary with no audible glitch (no transport hitch, no silence gap). Read this
before touching `VoiceHotSwapStager`, the staging/commit paths in
`PluginProcessor`, or the wavetable bank load path.

This is the **product-side** sibling of mu-clid's hot-swap. The shared *pattern*
and the cross-product lessons live in
[../design-plugin-family.md](../design-plugin-family.md#hot-swap-staged-preset--layer-swaps-family-pattern);
mu-clid's deeper mechanics (retire-then-swap voice tail, `suspendProcessing`) are
in [../mu-clid/design-hotswap.md](../mu-clid/design-hotswap.md). This doc covers
**what mu-tant does and why it differs**.

---

## 1. Scope

| Flavour | Trigger | Boundary | Entry point |
|---|---|---|---|
| **Full preset** (`.muTant`) | preset dropdown / MIDI PC ch 9 | **voice 0's** gate-pattern wrap | `PluginProcessor::loadPreset` |
| **Per-voice** (`.muPattern`) | `ChannelHeaderBar` / MIDI PC ch 1-8 | **that voice's own** gate-pattern wrap | `PluginProcessor::loadVoicePreset` |

Stopped → applied immediately. Playing → staged + committed at the boundary.
Same apply code for both triggers (`applyFullPresetTree` / `applyVoicePresetTree`).

---

## 2. How mu-tant differs from mu-clid (important)

mu-tant is **APVTS-centric**, not engine-object-centric:

| | mu-clid | mu-tant |
|---|---|---|
| Payload | pre-built `Rhythm` + `VoiceEngine` (+ sample) | parsed `juce::ValueTree` (the APVTS state / `.muPattern`) |
| Commit mechanism | `std::move` engine into slot + retire old | `apvts.replaceState` + `readVoiceDataFromState` |
| Commit isolation | `suspendProcessing` + `rhythmsLock` (µs, work pre-built) | **no blanket lock** — relies on the audio render's existing fine-grained locks |
| Outgoing-voice tail | **retire-then-swap**: old `VoiceEngine` plays out its sample/release tail | **none** — oscillators are continuous (free-running drones), there is no note tail to preserve, so the new params simply take over |
| Boundary | master loop / per-rhythm wrap (`swapMode`) | full → voice 0's gate wrap; per-voice → that voice's gate wrap |

**Why no `suspendProcessing` / blanket lock in mu-tant.** mu-clid suspends for a
microsecond because its commit is a pointer swap. mu-tant's commit mutates APVTS
params + per-voice gate/modulator data; each of those is *already* guarded by a
lock the audio render respects, so a blanket lock is unnecessary — and holding one
made the render bail to silence (and froze the transport) for the whole commit
(#885). See §6.

**No retire-tail needed.** mu-clid retires the old `VoiceEngine` so a sample
one-shot / amp-release rings out across the swap. mu-tant's voices are continuous
oscillators with no per-note tail, so the new config taking over at the boundary is
already seamless. *If* a future mu-tant feature adds an amp-release that should ring
across a swap, mu-clid's retire-then-swap (see its doc §8) is the pattern to port.

---

## 3. Components

| File | Role |
|---|---|
| [Source/Plugin/HotSwapBoundary.h](../../mu-tant/Source/Plugin/HotSwapBoundary.h) | Pure boundary predicates (`mu_tant::hotswap::patternWrapped` / `swapBoundaryReached`) — unit-testable without a processor. |
| [Source/Plugin/VoiceHotSwapStager.h](../../mu-tant/Source/Plugin/VoiceHotSwapStager.h) | Header-only staging state machine: per-voice pending slots + one full-preset slot, the store-release/load-acquire handshake, `checkBoundaries` (audio) + `take*` (message). `ValueTree` payload, **no `PluginProcessor` coupling**. |
| [Source/Plugin/PluginProcessor.cpp](../../mu-tant/Source/Plugin/PluginProcessor.cpp) | `loadPreset`/`loadVoicePreset` (stage-or-apply), `applyFullPresetTree`/`applyVoicePresetTree` (commit bodies), `preloadWavetablesFrom*`, `handleAsyncUpdate` (commit drain), boundary check in `processBlock`. |
| [Source/Audio/WavetableBank.{h,cpp}](../../mu-tant/Source/Audio/WavetableBank.h) | `findByPath` (lock-free resolve), `decodeFile` (off-lock decode) + `appendTable` (locked append) — the two-phase load that keeps the swap real-time-safe. |
| [Source/Tests/HotSwapBoundaryTests.cpp](../../mu-tant/Source/Tests/HotSwapBoundaryTests.cpp) | Predicate + stager handshake tests. |

`PluginProcessor` is a `juce::AsyncUpdater`; the audio thread `triggerAsyncUpdate()`s
and `handleAsyncUpdate()` commits on the message thread.

---

## 4. Threading contract & the readiness gate

Identical handshake to mu-clid: the payload `ValueTree` is touched **only on the
message thread** (stage writes it, `take*` moves it out — the message loop
serialises those); the audio thread touches **only the two atomic flags**
(`isReady`, `boundaryReached`) in `checkBoundaries`.

- `stage*()` sets `isReady` store-release **last**, after the payload is fully built
  (parse + wavetable decode/append). `checkBoundaries` only flags `isReady` swaps.
  → **Readiness gate:** if a loop boundary arrives before the build finishes, it is
  skipped and the swap commits at the *next* loop point. A half-built payload can
  never commit, and the commit never blocks the audio thread waiting on a build.
- Per-voice payload writes (`stageVoice` / `takeVoice` / `cancelVoice`) and the
  full payload (`stageFull` / `takeFull`) are all message-thread; no lock needed on
  the payload itself.

---

## 5. Boundary detection (`processBlock`)

Runs **outside** the render try-lock so the transport never freezes during a commit:

1. Compute the transport snapshot (`blkPlaying`, `blkBeatStart`, `blkBeatsPerSample`).
2. Render under `ScopedTryLock(voicesLock)` — on contention leave the block silent
   (a voice add/remove is the *only* thing that takes that lock; a hot-swap does not).
3. **Always** advance `internalBeatPos` (atomic) and run the boundary check.

`internalBeatPos` wraps at a fixed 64-beat ceiling (float-precision guard, *not* a
musical loop). The boundary is computed on the **raw pre-ceiling** advanced position
so the loop-index test holds for pattern lengths that don't divide 64:

```cpp
patternWrapped(oldPos, newPos, patBeats)   // floor(newPos/patBeats) != floor(oldPos/patBeats)
swapBoundaryReached(playing, wasPlaying, oldPos, newPos, patBeats)
//   playing            → commit on a reference-pattern wrap
//   playing→stopped    → commit immediately (apply-on-stop; gate closed, glitch-free)
//   stopped (no edge)  → never (a stopped stage is applied immediately at stage time)
```

`checkBoundaries` is given each voice's `patBeats` (`gatePatterns[v].patternLengthBars*4`)
and uses voice *v*'s own length for a per-voice swap, voice 0's for the full preset.

---

## 6. Why the commit takes no blanket lock (the lock discipline)

`applyFullPresetTree` / `applyVoicePresetTree` run on the message thread
**without** `voicesLock`, concurrently with the audio render. This is safe because
every structure they touch is independently guarded by a lock the render respects:

| Structure mutated at commit | Guard | Audio render respects it via |
|---|---|---|
| APVTS params (`replaceState`, `setValueNotifyingHost`) | per-param `std::atomic<float>` | cached `getRawParameterValue` atomic reads |
| Gate / filter / pitch patterns (`deserialiseGate`) | `GatePattern.editLock` (spin) | `applyGateBlock` tryLock → passthrough on contention |
| Modulators (`deserialise/clearModulators`) | `VoiceSlot.modLock` (spin) | `applyModulation` tryLock → skip on contention |
| Wavetable index (`findByPath` resolve) | none needed — **lock-free** | preloaded at stage; commit only reads |
| `numVoices` | `std::atomic<int>` | read once per block |
| Mixer / FX (`syncAllFxParams`) | same path as live automation | already concurrent-safe |

A concurrent block therefore sees *old-or-new per structure* (≤1 block) — inaudible —
instead of the render being silenced for the whole commit.

> **Rule:** never hold a lock around slow work in the swap path. Two corollaries
> from real bugs:
> - **#886:** resolving a wavetable via `bank.addOrLoadFile()` (locked) *at commit*
>   silences the render for the coincident block (intermittent pause). Resolve with
>   the lock-free `findByPath` instead; the table was pre-loaded at stage.
> - **#888:** even at *stage*, don't decode under the lock. `preloadWavetablesFromVoiceTree`
>   `findByPath`-skips already-loaded tables, calls `WavetableBank::decodeFile` (file
>   read + WAV decode + FFT mip build) **off-lock**, then takes `voicesLock` only for
>   the microsecond `appendTable` push.

The only thing that still takes `voicesLock` briefly is `appendTable` (a `push_back`
that may realloc `tables`, which the audio render reads by index) — held for
microseconds at *stage*, never at commit.

---

## 7. Stage → commit walk-through

**Stage** (`loadPreset`, message thread):
1. Parse XML → `ValueTree` (accept a `MuTantPreset` wrapper or a bare state element).
2. If `isInternalPlaying()`: `preloadWavetablesFromState` (decode off-lock + append)
   then `hotSwapStager.stageFull(move(tree))`. Else `applyFullPresetTree(tree)` now.

`loadVoicePreset` is the same shape for one voice (`preloadWavetablesFromVoiceTree`
+ `stageVoice`, else `applyVoicePresetTree`).

**Commit** (`handleAsyncUpdate`, message thread, triggered from `processBlock`):
1. `takeFull` → `applyFullPresetTree` (`replaceState` + `numVoices` + colours +
   `readVoiceDataFromState` + `syncAllFxParams`) → fire `onPresetSwapCommitted`.
2. `takeVoice(v)` for each flagged voice → `applyVoicePresetTree` → fire
   `onVoiceHotSwapCommitted(v)`.
3. `drainPendingMidiProgramChanges()` (ch 1-8 → `loadVoicePreset`, ch 9 →
   `loadPreset` — themselves stage-or-apply).

`stageFull` cancels all pending per-voice swaps (a full preset replaces every voice).

---

## 8. Editor refresh

The shell calls `onPresetLoaded` **synchronously** right after `loadPreset`
([EditorShellBase.cpp](../../mu-core/UI/EditorShellBase.cpp) `onPresetSelected`),
which for a *staged* swap runs against pre-swap state. So the commit re-triggers the
refresh: `onPresetSwapCommitted` → editor re-runs `onPresetLoaded({})`;
`onVoiceHotSwapCommitted(v)` → refresh that voice's sidebar glyph + (if shown) re-bind
the panel (knobs + wavetable dropdowns, the #879 class of stale-UI bug). **The editor
clears both callbacks in its destructor** — the processor can outlive the editor
(DAW close-window-keep-plugin), and a commit firing into a dead editor is a UAF.

---

## 9. Staging badges

Shared, automatic — mu-tant only wires them:
- `TransportBar` "SWP" pill ← `hasPendingFullPreset()` → `hotSwapStager.hasFullPending()`.
- `ChannelSidebar` per-voice badge ← `isPendingSwap(v)` → `hasPendingSwap(v)`; cancel
  via `onCancelPendingSwap(v)` → `cancelStagedSwap(v)` → `hotSwapStager.cancelVoice(v)`.

---

## 10. Interaction with structural voice edits

`addVoice` / `removeVoice` / `swapVoices` / `resetVoice` shift/swap/reset per-voice
data by **index**, so they **cancel pending per-voice staged swaps** that the renumber
would misdirect:
- `removeVoice` / (down-shift renumbers everything from `idx`) → cancel **all** voice swaps.
- `swapVoices(a,b)` → cancel `a` + `b`.
- `resetVoice(idx)` / `addVoice` (new slot) → cancel that slot.

A staged **full** preset is *index-independent* (it replaces the whole state at
commit), so it is left pending and simply wins. (Same invariant + fix in mu-clid —
see its doc.)

---

## 11. Invariants / edge cases

- **Readiness gate** (§4): no half-built commit; not-ready-at-boundary waits for the next loop.
- **Stop-mid-stage:** the playing→stopped edge commits all ready swaps (gate closed → glitch-free).
- **Supersede:** re-staging a voice overwrites its slot; a full preset supersedes all pending voice swaps.
- **MIDI PC while stopped:** drained in `handleAsyncUpdate`, `isInternalPlaying()` re-checked → applied immediately.
- **No editor:** commit callbacks are null-checked.
- **Wavetable bank reads vs append:** audio reads by index under the render `voicesLock`; `appendTable` pushes under `voicesLock`; `findByPath` is a read-only scan (no concurrent append during a commit — all appends are message-thread). 

---

## 12. Tests

[HotSwapBoundaryTests.cpp](../../mu-tant/Source/Tests/HotSwapBoundaryTests.cpp) — the
pure predicates (wrap incl. non-divisor lengths, stop-edge, stopped-immediate) and the
stager handshake (full vs per-voice boundary, supersede, full-supersedes-voice,
stop-edge commit). Runs in `mu-tant-tests` (built every build).
