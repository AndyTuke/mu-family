Perform a thorough code audit of the changed files in this JUCE/C++ audio plugin codebase. Work through each category below in order, fixing every problem you find without asking permission — except for architectural changes, which you should flag for discussion instead.

## 1. Memory safety and crash risks (highest priority)

Check for and fix:

- **Use-after-free**: raw pointers to JUCE Components or heap objects that may have been destroyed. Pay special attention to lambda captures, `juce::Component::SafePointer` usage, and modal callback lifetimes.
- **Dangling references**: references or raw pointers stored as member variables that point into containers that may reallocate (e.g. `std::vector` element addresses).
- **Buffer overruns**: `AudioBuffer` access beyond `getNumChannels()` or `getNumSamples()`; array/vector index without bounds check.
- **Double-free or leaked ownership**: anywhere `std::unique_ptr` is released with `.get()` and the raw pointer is also stored; any `new` without corresponding `delete` that isn't wrapped in a smart pointer.
- **JUCE-specific**: `AsyncUpdater` / `MessageManager::callAsync` called on the audio thread; `Timer` callbacks referencing destroyed components; `AlertWindow` raw `new` without `delete` guard.

## 2. Correctness errors

- Logic bugs: off-by-one in step indices, modular arithmetic, loop bounds.
- APVTS mismatches: parameter IDs used in UI that don't exist in `createParameterLayout()`; `convertTo0to1` applied to a value already in normalised range (double-normalisation).
- Uninitialized members: class members without default initialisers that are read before being set.
- Silent truncation: `float`→`int` casts that discard fractional values where rounding was intended; `int` division where `float` was intended.

## 3. Inefficiencies

- Unnecessary copies of `juce::String`, `std::vector`, or `AudioBuffer` where a const-ref or move would suffice.
- `repaint()` called unconditionally from a `Timer` or `parameterChanged` when the relevant state hasn't changed.
- Component `paint()` doing non-trivial computation (string formatting, path building) that could be cached.

## 4. Code quality

- Dead code: unreachable branches, unused parameters, members that are always their default value.
- Overly complex expressions that can be simplified without changing behaviour.
- Missing `jassert` guards on public entry points where preconditions should be documented.

## 5. Audio-path code (delegated)

Audio-thread safety and DSP efficiency are not checked here. If any changed file is on the audio path (`mu-core/Audio/**`, a product's `Source/Audio/**` or `Source/Sequencer/**` engine code, `ProcessorBase`, a `processBlock` path, or state shared with the audio thread), run the `dsp-controller` agent (Agent tool, subagent_type `dsp-controller`) on those files before building. It covers audio-thread allocation and locking, races and thread hand-off, stack use, cached parameter pointers, denormals, aliasing and per-sample cost, and it is read-only. Fix every **Blocker** and **Major** finding it returns without asking, apart from architectural changes, which you flag for discussion. Report **Minor** and **Risk** findings in the summary. Re-run the agent on the fixed files if you changed audio-path code to fix a Blocker.

## Scope

Focus on files changed since the last commit (`git diff --name-only HEAD~1 HEAD`). If a changed file calls into an unchanged file and you spot a problem there, note it but don't fix it — keep the diff minimal.

After completing all fixes, build the project (`cmake --build build --config Debug`) and resolve any new warnings or errors introduced by your changes. Then summarise: what you fixed, what you flagged for discussion, the `dsp-controller` verdict (if it ran), and whether the build is clean. Do not edit `backlog.md`; hand anything that needs logging to the `backlog-administrator` agent.
