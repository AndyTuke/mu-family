Perform a deep code audit across the whole codebase and log every finding to `backlog.md` as a `**[Audit]**` row in the Open section. **Do not fix anything** — this command produces a discussable list, not a diff. Fixes happen as a separate explicit step after the user triages.

This is the broader sibling of `/optimise`. Where `/optimise` audits changed files and auto-fixes them, this command sweeps the entire project source and surfaces findings for human decision.

## Optional argument

`$ARGUMENTS` may narrow scope to a domain or path: e.g. `/deepaudit hot-swap`, `/deepaudit Source/UI`, `/deepaudit concurrency`. If empty, sweep everything.

## What to look for

Walk the project source. For each file, ask: *would an expert reviewer flag something here?* Group findings by the seven tiers below and prioritise the higher tiers — if you'd produce more than ~20 findings, drop the LOW tier (6-7) and surface a note that more cleanup is available.

### Tier 1 — Real bugs / behavioural divergence (HIGH)
Behaviour that's wrong, silently differs by state, or is a latent crash. The signature pattern from prior audits: two code paths that handle the same conceptual operation but diverge in what they do (e.g. preset-load-while-stopped vs preset-load-while-playing dropping a feature on one side — see #389).

- Stopped-vs-playing path divergence — features dropped on one path.
- DAW-vs-standalone divergence.
- Edge cases the existing code doesn't guard (empty containers, `-1` sentinels, out-of-range indices).
- Off-by-one in step / sample / index arithmetic.
- APVTS parameter IDs read but never registered, or vice versa.
- Uninitialised members read before write.
- Loops with subtle exit conditions (`while (!flag)` where flag is never set in the loop body).
- **Headless / no-editor divergence** — behaviour that only works because the *editor* ran (seeded default state, cached `getRawParameterValue` pointers, populated a collection on first paint) and silently breaks in `--render` mode, a DAW with no editor open, or a preset loaded before the editor exists. Sibling of DAW-vs-standalone. (Finding: the editor seeds a ControlSequence's default Smooth curve points on display, so a headless render that never opens the editor evaluates that LFO as a flat zero.)
- **Silent no-op on plausible misconfiguration** — mode-selected / dual-representation state where a *well-formed but internally inconsistent* config yields silence with no diagnostic. Distinct from malformed-parse (Tier 3): the data parses fine, it just doesn't match the active mode. Example: a `ControlSequence` in `Smooth` mode carrying only `<Step>` values (no `<Point>`s) evaluates to a constant 0 — modulation that looks wired but does nothing. Such paths should fail safe (self-heal to whichever representation has data) or fail loud (log / surface a diagnostic), never silently nothing.
- **Structurally-inert exposed controls** — a parameter, modulation destination, or UI control that *cannot* affect output given the architecture, yet is offered to the user. Not "dead code" (it's live and user-facing) — it silently lies about having an effect. Example: an envelope `Release` exposed as a modulation target when one-shot step triggers never issue a note-off, so the release stage is never entered in normal playback; or a dropdown entry that resolves to no handler. Either wire it up or remove it from the surface.
- **Index-coupled registry integrity** — tables / enums whose *ordinal* index is persisted or cross-referenced (1-based dropdown IDs that equal table indices, snapshot-array enums, per-algo config rows). An insert or delete silently shifts every downstream index, scrambling saved references or array lookups. Check that a test pins the invariant (the `insert.p1..p4`-at-`kTable[10..13]` guard, #617) and that retired entries are replaced by reserved placeholders rather than deleted.

### Tier 2 — Audio-thread safety + concurrency (HIGH if exposed, MED otherwise)

Audio threading is the highest-stakes correctness axis in a JUCE plugin. The audio thread runs in real-time at strict deadlines (typically 1–10 ms per block). If it allocates, takes a long lock, or even sees a torn read, the user hears a click or dropout. Walk every function called from `processBlock` (directly or transitively) and check:

- **No allocation on the audio thread** — `new`, `delete`, `std::make_unique`, `std::vector::push_back` (when it reallocates), `std::string` construction, `juce::String` concatenation, `juce::ValueTree` mutations. Look for hidden allocs: `juce::Array::add` past capacity, `std::map`/`std::unordered_map` insertion, `std::function` assignment.
- **No blocking on the audio thread** — `std::mutex::lock`, `juce::CriticalSection::enter`, `juce::WaitableEvent::wait`. Try-locks (`tryEnter`, `compare_exchange_strong`) are OK; spin-locks are OK if held for nanoseconds; anything message-thread-held is not.
- **No system calls on the audio thread** — `time()`, `clock_gettime()`, `printf`, `std::cout`, `juce::Logger::writeToLog`, `OutputDebugString`. These trap into the kernel and/or acquire global I/O locks — millisecond stalls under load.
- **First-touch page faults** — memory `reserve()`d on the message thread but never written there will page-fault on first audio-thread write. Look for `std::vector::reserve` without a subsequent `assign` / `resize`-with-default; same for `AudioBuffer::setSize(..., clearExtraSpace=false)` followed by audio-thread first writes. Either touch the memory once on the prep path or accept the first-block glitch.
- **Race conditions** — when two threads read/write the same data without synchronisation, the order of operations can vary and the result is undefined. Look for: plain `bool` / `int` flags written by message thread + read by audio thread (or vice versa); non-atomic shared state; `parameterChanged` listeners that touch UI state (DAW automation may fire `parameterChanged` from the audio thread).
- **UAF risk on callbacks** — `std::function` on a long-lived owner (e.g. `PluginProcessor`) capturing `[this]` of a shorter-lived component (e.g. `PluginEditor`). The editor dtor must null the callback, or the lambda must use `Component::SafePointer`. Same for `AsyncUpdater` queues, MIDI callbacks, FileChooser completion lambdas.
- **`juce::Timer` dtor** — every class deriving from `juce::Timer` must call `stopTimer()` in its destructor; if a timer tick fires during object teardown, `timerCallback()` runs on a partially-destroyed object (UAF). Look for `class Foo : ..., private juce::Timer` without a matching `~Foo()` that calls `stopTimer()`. Three missing dtors were found in this project in 2026-05 audit (#483, #484, #485).
- **Non-RAII flag flips** — `flag = true; work(); flag = false;` — if `work()` throws, the flag latches true. Wrap in a `ScopedValueSetter` / RAII guard.
- **Lock-order / deadlock potential** — `suspendProcessing` + spin-locks + try-locks. Look for orderings that could deadlock or starve the UI thread. If `suspendProcessing(true)` is called inside a loop, that's N back-to-back audio stalls (#392).
- **Atomic memory ordering** — `relaxed` where `acquire`/`release` would matter for *publishing* other state. (Atomic reads on a single value with no other coordination → relaxed is fine.)
- **Move semantics on hot paths** — large structs copied where moved would do. `sequencer.getRhythm(r) = sw.pendingRhythm` is copy; `= std::move(sw.pendingRhythm)` is the right call (#392).

### Tier 3 — Memory safety + graceful degradation (HIGH if reachable, MED otherwise)
`/optimise` covers per-file memory issues. `/deepaudit` is for the cross-cutting ones — state that a single-file view can't see — plus *brittleness*: places where external failure isn't handled and the user sees a crash, silence, or nonsense instead of a clean degradation.

**Memory safety:**
- **Dangling pointers / references** stored as members that point into containers that may reallocate (`std::vector` element addresses; iterators kept past mutation).
- **Object ownership confusion** — `std::unique_ptr` released with `.get()` and the raw pointer stored elsewhere; `new` without an obvious `delete` or smart-pointer wrap.
- **Stale weak pointers** — `Component::SafePointer` checked once then dereferenced later without re-checking.
- **Outliving owners** — Components passing `&this` to a long-lived registry that doesn't have a corresponding deregister in dtor.
- **Buffer overruns** — `AudioBuffer` access beyond `getNumChannels()` / `getNumSamples()`; array/vector index without bounds check on data from external sources.
- **Stack overflow risk** — large stack-allocated arrays inside recursive or deeply-nested paths.

**Graceful degradation — what happens when external state fails:**
- **Missing files** — linked sample disappeared mid-session (#329 has the missing-indicator pattern); preset file deleted between dropdown population and click; content folder unmounted. Code must show a visible "missing" affordance, not silently swap in nothing.
- **Malformed input** — `juce::parseXML` returns `nullptr` (truncated/corrupted preset), `juce::ValueTree::fromXml` returns invalid tree, a property is the wrong type (`String` where `int` was expected). Every parse path should have a "if it's broken, don't apply" branch — never partial-apply garbage.
- **Out-of-band parameter values** — host sends `NaN` / `Inf` / negative-where-positive-expected. APVTS clamps the registered range but raw `getRawParameterValue` reads still need defensive checks if used in DSP. `juce::jlimit` / `std::isfinite` guards on anything fed to filter coefficients or buffer indices.
- **Sample-rate / block-size change** — `prepareToPlay` re-called mid-session. Every cached coefficient, time-based buffer size, and SR-derived constant must re-derive. Look for state that's computed in the ctor but read after `prepareToPlay` may have changed the SR.
- **File I/O failure** — `replaceWithText`, `createDirectory`, `readBinaryData` can all fail (disk full, permissions, antivirus). Failure paths should report to the user (status bar / alert), not silently no-op.
- **Silent catch blocks** — `catch (...) {}` or `catch (const std::exception&) { /* nothing */ }` — exceptions deserve at least a log line; better, a user-facing report.

### Tier 4 — Performance / paint efficiency / glitch sources (MED)

JUCE UI specific:
- **`paint()` doing heavy work each frame** — string formatting, path building, image decoding, `juce::AffineTransform` chains that could be cached. Cache shape geometry (#371 RingCache pattern).
- **`repaint()` called unconditionally from `Timer`** — should compare against last-known state and only repaint when it changed. The #370 SidebarItem signature pattern is a model.
- **Full-area `repaint()` when only a small region updated** — should use `repaint(Rectangle<int>)`.
- **Allocations in `paint()`** — `juce::String` concatenation, `std::vector` construction, fresh `juce::Path` per frame.
- **`getRawParameterValue` called per-block instead of cached once** — should cache the `std::atomic<float>*` pointer at panel bind time.

Audio path performance:
- **Per-block recompute that could be cached** — coefficients re-derived when `cutoff`/`resonance` haven't moved (#368 pattern), repeated `apvts.getRawParameterValue` calls in `processBlock`.
- **Repeated string / vector allocs on hot paths** — `juce::String::fromFirstOccurrenceOf` per parameter change, `std::vector` constructed per timer tick (#370).
- **Linear scans where a hash lookup fits** — >10 items, hot path (#401).
- **Callback ordering causing wasted work** — bulk push fires N listeners that re-run after a full rebuild already happened (#393).

### Tier 5 — Architecture (MED)
- **God classes / oversized files** — files >1500 lines (the #365 trigger was 2729), classes with >20 public methods, classes with two unrelated responsibilities glued together (e.g. file I/O + audio state + UI coordination in one class). Suggest a split with a sketch of the partial-class TU pattern.
- **God functions** — single functions that are themselves doing too much. Concrete thresholds: >100 lines, switch statements with >10 cases (often signals missing table-driven design — `syncFXParam` is a candidate), nesting >3 levels, mixed responsibility (one function that "loads a preset AND updates the UI AND fires callbacks AND reschedules a timer"). `processBlock` is the usual offender in JUCE plugins. Suggest extraction into private helpers or a state-machine refactor.
- **mu family reuse opportunities** — code in `Source/` that duplicates or parallels logic which lives in (or should live in) `mu-core`. Check `docs/design-plugin-family.md` for the canonical list of shared concerns (`ProcessorBase`, `VoiceSlot`, sidebar widgets, sample-bar widgets, APVTS bulk-push patterns, preset save/load wiring). When a μ-Clid-specific helper looks generic (no μ-Clid-specific param IDs or semantics), flag it as "candidate to lift into mu-core."
- **Singleton-ish anti-patterns** — global mutable state, static accumulators, files that hide a global.
- **Cross-layer leakage** — UI components that reach into the audio engine directly (bypassing APVTS), audio engine that knows about UI types.

### Tier 6 — Refactor / duplication / docs drift / magic numbers (LOW)
- **Duplication across 3+ call sites** that could be a helper (e.g. "select rhythm + refresh chrome" boilerplate #395, identical label setup blocks #397).
- **Magic numbers** that should be named `constexpr` — especially layout dimensions, timing constants, audio thresholds, retry counts.
- **Documentation drift** — a recurring category in this project. Look for:
  - `docs/design-*.md` claiming a structure / type / list that no longer matches `Source/` (prior hits: #373 lock type, #361 INSERT algo count, #362 filter list, #374 deleted widget still in spec).
  - `CLAUDE.md` referencing files / commands / stages that no longer exist or have moved (#364 missing design-doc entry).
  - Header docstrings describing parameters or behaviour the implementation no longer matches.
  - `// #NNN` comments referencing fixed issues whose context no longer applies.
  - `backlog.md` policy violations (rows out of descending order; status mismatched with content; the always-Open/On-Hold/Fixed grouping broken — #350).
- **Stale comments** in source — the comment describes old behaviour, the code has moved on (#399).
- **Dead code** — members always equal to their default; unused parameters; functions with no callers; flags toggled but never read.
- **Const-correctness** — getters that should be `const`; references that should be `const&`; member functions that don't mutate state.

### Tier 7 — Style: deprecated JUCE APIs, naming, consistency (LOW)
- **Deprecated JUCE APIs** — `juce::Atomic<T>` (use `std::atomic`), `juce::Font(name, size, style)` (use `juce::Font(juce::FontOptions{}...)`), `juce::ScopedPointer` (use `std::unique_ptr`), `juce::String(int).getCharPointer()` patterns that have modern equivalents. (Don't flag JUCE version itself — that's a `JUCE_PATH` / CI concern, not an audit one.)
- **Mixed APIs across the codebase** — two ways of doing the same thing co-existing (`juce::Atomic` next to `std::atomic` was the #396 example). Flag if it reads as mid-migration.
- **Naming outliers** — not bikeshedding camelCase, but flagging *deviations from the established codebase convention*: a `snake_case` member where the rest of the class uses camelCase, an `m_`-prefix where the rest is bare names, inconsistent suffixes (`Idx` in one place, `Index` in another for the same concept).
- **Lambda variable shadowing** that makes scanning harder (#390).
- **Inconsistent class member ordering** vs the declaration order in headers.

## What NOT to flag

- **Pure style preferences** — `auto` vs explicit types, brace placement, trailing whitespace. Only flag style if it has a concrete consequence (the atomic API mix made the codebase look mid-migration; that's worth flagging).
- **Third-party code** — JUCE, Signalsmith, Monocypher, clap-juce-extensions. Audit project source only (`Source/**`, `cmake/**`, `installer/**`).
- **JUCE version currency** — whether `JUCE_PATH` points at the latest JUCE is a CI question, not an audit question. *Deprecated APIs we're using* is fair game (Tier 7); "JUCE is N versions behind" is not.
- **Things `/optimise` would catch on a single file** — if the finding is single-file and would be caught by the changed-files-only `/optimise` skill, defer to that. This skill is for cross-file, cross-thread, architecture-level findings.
- **Hypothetical future requirements** — only flag what's wrong against current code, not speculation about features that might exist.

## How to surface findings

For each finding, write a Backlog Open row with this anatomy:

```
| <N> | **[Audit]** <SEVERITY if Tier 1-3> — <one-sentence summary>. <Trace through code with exact file:line refs>. <Concrete consequence — what breaks for users>. Fix sketch: <one-line proposed change>. | 🔴 Open | — |
```

- `<N>` = next sequential issue number above the current highest in `backlog.md`.
- Severity tag (`HIGH` / `MED`) only on Tier 1–3 findings. Tier 4–7 use plain `**[Audit]**`.
- File:line refs MUST use the project's markdown link format: `[Source/file.cpp:NN](Source/file.cpp#LNN)`. The IDE renders these clickable.
- Each row is ONE discrete finding. If two issues share a root cause but have different fixes, split into two consecutive numbers.
- Use the next sequential whole numbers — NEVER suffix notation (`#178a` / `#178b`).
- Insert in descending order at the top of the Open section (highest number first).
- Search the existing Open section before writing — if a finding is already logged under a different number, don't duplicate it.

## Output to the user

After writing the rows, summarise in chat:
- Total findings written, broken down by severity and tier (e.g. "2 HIGH (Tier 1), 3 HIGH (Tier 2), 1 MED (Tier 3), 4 MED (Tier 4-5), 6 LOW (Tier 6-7) = 16 total").
- The 1-3 findings most worth fixing first (with the issue numbers), and *why* — typically the Tier 1 bugs and any Tier 2 concurrency issue that's actually reachable.
- If you dropped LOW-tier items to stay under ~20 total, say so explicitly so the user can ask for another pass with `/deepaudit Tier 6` or `/deepaudit Tier 7`.

Do not start fixing. The user will pick which findings to address and in what order.

## Do not

- Do not fix anything. Even if a finding is one-line. Even if it's obvious. Log it and stop.
- Do not ask for confirmation before writing rows. Just write them.
- Do not write rows for fixes you've already applied this session.
- Do not flag findings that are already in the Open section under a different number — search first.
- Do not flag pure-style preferences with no concrete consequence (see "What NOT to flag").
