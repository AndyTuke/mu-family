# mu-On parameter locks (p-locks): design note, 2026-11-10

Backlog row #1798. Andy's rulings are in the row; this note turns them into a design. Status: **design only, nothing built**. Needs the reviews listed at the end before code.

## What we want (Andy, 2026-11-10)

A step can carry **locks**: absolute values for chosen engine parameters, played only on that step, like Elektron's parameter locks. In short:

- A lock sets the **absolute value** of one parameter on one step. Any engine knob that has a modulation destination can be locked, **Tune included**.
- No held button on a laptop: an **Add p-Lock toggle** at the bottom of the Step panel. While it is on, turning an engine knob locks that knob for the selected step.
- **Tails like Elektron:** a lock takes effect when its step triggers and holds until the next step that triggers, then the track's own values return.
- One **per-step toggle, Mod On | Off** (Andy, 2026-11-11, replacing Replace | Add): *Mod On* means modulation still moves a locked parameter, starting from the locked value; *Mod Off* means modulation is ignored for the locked parameters of that step. Default Off would be the Elektron-like absolute lock; default to be ruled by `ux-controller`.
- **Slide is not part of this.** The Bass already has ties and glide and is the only voice that needs it.
- **Limit 8 locks per step**, from one named constant that can be raised later; storage must be dynamic in use but fixed in size (no allocation on the audio thread).
- A step with locks shows a small **red LED** at its bottom-left. Selecting a locked step: every locked knob shows its **locked value**, carries a **red LED in its centre**, and a **ghost line on the outside of the knob** marks the track's own value.
- Locks exist only where there is a step sequencer: **mu-On and the mu-On layers in mu-Tronic**. The code stays generic so a future app with a step row can bolt it in.

## What Elektron does (research summary)

A p-lock is a value stored on one trig; that trig plays the parameter at the stored value and every other trig plays the track's value. Made by holding a trig and turning a knob, removed by holding the trig and pressing that knob (or clearing the trig). Locks can sit on steps with no note ("trigless locks") and do not retrigger envelopes. Elektron's pool is limited (72 distinct parameters per pattern on older machines). A "slide trig" glides locked values to the next lock. Locks are per trig, not continuous automation.

## Design

### 1. Generic pieces (mu-core, plugin-agnostic)

New files under `mu-core/Sequencing/` (name for the architecture-steward; the rule is: no product ids, no `mu_on::`):

- **`StepLocks.h`: `StepLockSet`**: a fixed array of `kMaxStepLocks` (8) slots. A slot is one `std::atomic<uint32_t>`: 16 bits **key** (an opaque id the host app assigns to a parameter) and 16 bits **value** (a proportion 0..1 in 1/65535 steps), empty = all ones. One atomic per slot keeps key and value consistent for the audio thread. Operations: `find(key)`, `set(key, value)` (adds, or replaces; returns false when full and the key is new), `remove(key)`, `clear()`, `count()`, `forEach`. No locks, no allocation.
- **`LockedValues`**: the audio-thread side: given a `StepLockSet`, a table mapping keys to this block's parameter slots, and the seed array, it overwrites the seed of each locked parameter (`applyAsSeed`) and reports which ones it overwrote, so *Mod Off* mode can pin them after the matrix (`pinAfterMatrix`). Pure functions over arrays; unit-testable without a processor.
- **Knob decoration** (UI): `LockedKnobMarks` (a mu-core UI helper used by `KnobWithLabel`): `setLock(optional<double> lockedValue, double trackValue)` draws the red centre LED and the ghost line, all colours and sizes from `MuTheme` / `MuLookAndFeel` (new fields, nothing hard-coded). A knob with no lock set draws as it does today.

A new app supplies: its own key scheme, one `StepLockSet` per step, and the three hook calls below. It needs nothing from mu-On.

### 2. mu-On side

- **Key** = engine-qualified destination: `engineIndex * 256 + destIndexInThatEngine's MuOnModDest table`. Reusing the destination tables means every continuous parameter (Tune included) is lockable with no new list, and a new destination is lockable the moment it is added. Locks on an engine other than the track's current one are kept and inert (same rule as the step row's other values when the engine changes).
- **Storage:** `StepRow` cell gains a `StepLockSet` and a `lockMode` atomic (Mod Off / Mod On). 64 steps x 8 slots x 4 bytes = 2 KB per track. Shared with mu-Tronic through `mu-on-layer`, so both products get it together (the mirror rule).
- **Value space:** a lock stores the knob's **proportion** (0..1), the space the modulation seed already uses, so a lock is an absolute position on the knob, independent of the unit or the range.

### 3. Audio thread

Today `Track::render` does: Phase 2 `resolve` (params through the modulation matrix, once per block, at the block's start beat), then Phase 4 `clockRow` (the steps in the block fire). A lock must be known **before** `resolve`, so:

1. **Peek.** A new `StepClock::firstFiringStep(row, startBeat, n, ...)` looks at the block's step edges without changing the clock and applies the same pure checks `stepPlays` already makes (loop, condition, probability hash). It returns the step that will fire, or none. Because `stepPlays` is deterministic in (pass, step), peek and fire agree.
2. **Hold.** The track keeps `activeLockStep` (an int, audio-thread only). If a step fires in this block, `activeLockStep` becomes that step (even one with no locks: that is how the tail ends, the track's values return). If none fires, it holds.
3. **Apply.** After the seed is filled and before the matrix, `LockedValues::applyAsSeed` overwrites the seeds of the active step's locks. Then:
   - **Mod On:** the matrix runs on the locked seed, so modulation moves the parameter around the lock.
   - **Mod Off:** after the matrix, the locked parameters are set back to the lock value (modulation ignored for them).
4. **Transport stop / seek / start** reset `activeLockStep` to none (the track's own values).
5. The existing limit stays: parameters are per block, and an engine takes one trigger per block (design-step-conditions "Known limit"), so a lock applies from the block its step fires in. **This is the one point for the `dsp-controller`**: is block-rate application acceptable for fast steps at large buffers, or must a lock split the block at the trig (the drum filter already renders in pieces)? Recommendation: block-rate, since that matches how every other per-step value behaves today.

Nothing allocates and no lock is taken; the cells are atomics (the same pattern as the other step values), and the key table is built in `prepare`.

### 4. UI

- **Step panel, bottom:** a toggle button **P-Lock** (an LED toggle in the family style) and a **Mod: On | Off** two-way control that is enabled only while the selected step has locks. Both report to the status bar ("P-Lock: On", "Step Lock Mod: On").
- **While P-Lock is on and a step is selected:** turning an engine knob writes a lock for that step instead of changing the track's knob. The knob shows the lock (value, red centre LED) and the track value as a ghost line outside the knob. Turning it back to the ghost value does not remove the lock; removal is explicit.
- **Removing a lock:** double-click a locked knob (P-Lock on), or **Clear locks** in the Step panel (all of the step's locks). Needs `ux-controller` confirmation; Elektron's hold-and-press has no laptop equivalent.
- **Full step:** with 8 locks used, a knob not yet locked ignores the turn and the status bar says "Step has 8 locks".
- **Step LED:** a small red LED on the step cell when the step has any lock. **Conflict to settle:** the step cell's bottom-left already holds the step number on the beat steps (5, 9, 13 in the current screenshots). Options: the LED left of the number, or the bottom-right corner. `ux-controller` rules.
- **Selecting a step with locks (P-Lock off too):** the knobs show the locks, so the user sees what the step does; editing a locked knob with P-Lock off changes the track value, not the lock.
- **Status bar rule** applies to every new control (toggle, mode control, the lock knob marks).

### 5. Saved data

In the step row's saved node, **sparse**: `lock` is a comma list with one entry per step that has locks, `step:key=value|key=value`, and `lockMode` the steps with Mod On. Absent = no locks. Old states load unchanged. Presets (`.muTrack`, `.muOn`) carry locks because they carry the row.

### 6. Out of scope (for now)

Trigless locks (a lock on a step with no note), slide between locks, locks on Rumble (it has no row), locks on the engine choice rows (Bass wave, FM algorithm, filter type: choices, not continuous), live-recording locks, locking Level/Pan/sends is allowed (they are destinations) but Fill is not. Each can be added later without changing the data model: trigless locks need only the peek to treat a locked empty step as an event that does not trigger.

## Interaction with what exists

- **Octave and Note** per step stay as they are; they add to the Tune after the lock, so a locked Tune plus a step Note is `lock + semitones`.
- **Modulation destinations** `.prop`: the lock is proportion-space, so a full-depth modulator with Mod On sweeps from the lock value, clamped at the knob's ends.
- **Live arcs on the knobs** (`modLive`) keep showing the resolved value; the lock marks are separate and only appear for the selected step.
- **Conditions and probability:** a step that does not play (condition, bar, probability) does not apply its locks. That follows from step 3.

## Tests

`StepLockSet`: set / replace / remove / full / empty, key and value round trip. `LockedValues`: seed override, Mod Off pins after the matrix, Mod On leaves the matrix on, unlocked parameters untouched. `Track`: a locked step changes the value in the block it fires, the value holds until the next firing step, a step that does not play leaves the previous lock holding, the tail ends on an unlocked firing step, stop/seek reset. Persistence: a row with locks round-trips; an old state loads with none. A mu-Tronic layer shows the same behaviour. The status-bar audit (`run-ui-audits.ps1`) for the new controls.

## Review needed before code

- `ux-controller`: the LED placement, the lock marks on the knob, the removal gesture, the Mod toggle wording.
- `architecture-steward`: file placement in mu-core, names (`StepLockSet`, `LockedValues`), the key scheme.
- `dsp-controller`: the peek-before-resolve order and block-rate application (point 5 in section 3).
- `test-steward`: the test list.

## Staging

1. mu-core `StepLockSet` + `LockedValues` + tests (no UI).
2. `StepRow` storage and persistence; `Track` peek, hold, apply; tests.
3. The Step panel toggle, Mod control, knob marks and the step LED; the audit.
4. mu-Tronic check, manual, release notes.
