Perform a thorough code audit of the changed files in this JUCE/C++ audio plugin codebase. Work through each category below in order, fixing every problem you find without asking permission — except for architectural changes, which you should flag for discussion instead.

## 1. Memory safety and crash risks (highest priority)

Check for and fix:

- **Audio-thread allocation**: any `new`, `delete`, `std::make_unique`, `std::vector::push_back` (that may reallocate), `std::string` construction, or other heap ops inside `processBlock`, `processReturn`, `processSends`, or any function called from those paths.
- **Use-after-free**: raw pointers to JUCE Components or heap objects that may have been destroyed. Pay special attention to lambda captures, `juce::Component::SafePointer` usage, and modal callback lifetimes.
- **Dangling references**: references or raw pointers stored as member variables that point into containers that may reallocate (e.g. `std::vector` element addresses).
- **Buffer overruns**: `AudioBuffer` access beyond `getNumChannels()` or `getNumSamples()`; array/vector index without bounds check.
- **Double-free or leaked ownership**: anywhere `std::unique_ptr` is released with `.get()` and the raw pointer is also stored; any `new` without corresponding `delete` that isn't wrapped in a smart pointer.
- **Stack overflow in audio thread**: large stack-allocated arrays or deeply recursive calls inside the audio callback.
- **Race conditions**: shared state written on the message thread and read on the audio thread without atomic protection or a try-lock (check `modLock`, channel/return state, `numRhythms`, `loadedSamplePaths`).
- **JUCE-specific**: `AsyncUpdater` / `MessageManager::callAsync` called on the audio thread; `Timer` callbacks referencing destroyed components; `AlertWindow` raw `new` without `delete` guard.

## 2. Correctness errors

- Logic bugs: off-by-one in step indices, modular arithmetic, loop bounds.
- APVTS mismatches: parameter IDs used in UI that don't exist in `createParameterLayout()`; `convertTo0to1` applied to a value already in normalised range (double-normalisation).
- Uninitialized members: class members without default initialisers that are read before being set.
- Silent truncation: `float`→`int` casts that discard fractional values where rounding was intended; `int` division where `float` was intended.

## 3. Inefficiencies

- Repeated `apvts.getRawParameterValue()` calls inside loops or per-block paths — these should be cached as raw `std::atomic<float>*` pointers.
- Unnecessary copies of `juce::String`, `std::vector`, or `AudioBuffer` where a const-ref or move would suffice.
- `repaint()` called unconditionally from a `Timer` or `parameterChanged` when the relevant state hasn't changed.
- Component `paint()` doing non-trivial computation (string formatting, path building) that could be cached.

## 4. Code quality

- Dead code: unreachable branches, unused parameters, members that are always their default value.
- Overly complex expressions that can be simplified without changing behaviour.
- Missing `jassert` guards on public entry points where preconditions should be documented.

## Scope

Focus on files changed since the last commit (`git diff --name-only HEAD~1 HEAD`). If a changed file calls into an unchanged file and you spot a problem there, note it but don't fix it — keep the diff minimal.

After completing all fixes, build the project (`cmake --build build --config Debug`) and resolve any new warnings or errors introduced by your changes. Then summarise: what you fixed, what you flagged for discussion, and whether the build is clean.
