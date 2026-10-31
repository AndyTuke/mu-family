# mu-Tilate build plan

Staged so every stage compiles, is tested without listening where it can be, and leaves the root build green. Started 2026-10-31 on Andy's go-ahead. Design: [design.md](design.md), [design-engine.md](design-engine.md), [design-ui.md](design-ui.md), [design-library.md](design-library.md). Stretch decisions: [stretch-prototype-results.md](stretch-prototype-results.md).

| Stage | Content | Checked by |
|---|---|---|
| 1 | `mu-tilate-layer` INTERFACE library and `mu-tilate-tests` console target in the root build. `SlicePlayer`: one monophonic slice at a time, native / re-pitch / reverse, fade in and out, cut fade into the next slice. **Done 2026-10-31.** | `mu-tilate-tests` (8 checks) |
| 2 | Slice model (`SliceList`, markers, pin flag) and the transient detector in `mu-core/Analysis` (spectral flux, cached curve, peak picking by Sensitivity / Min gap); worker pool. | unit tests on synthetic loops |
| 3 | Stretch voice: dry head, cross-fade into a constant-rate Signalsmith Stretch, stretcher primed after the head (never `outputSeek` inline), zero-padded input view at the slice end. | allocation probe, onset and level checks |
| 4 | Timeline model (blocks as ValueTree, quantise, hit-test, staging at the loop boundary) and the `Phrase` layer (`Layer` subclass, `PhraseParams`, ModTarget table). | layer tests with hand-built `RenderContext` |
| 5 | Plugin target (`juce_add_plugin`, VST3 + CLAP + Standalone), processor and editor shell, state, presets (`.muPhrase`, `.muTilate`). | headless processor tests, pluginval |
| 6 | UI: source waveform, slice markers, slice player, timeline, on the shared `mu-core` waveform, ruler and viewport. | screenshots |
| 7 | Sample library and factory loops, tempo analysis, mu-Tronic registry row, check-script and release wiring, manual. | per item |

Rules in force: shared code goes in `mu-core`; product symbols under `mu_tilate::`; no allocation in the audio thread; `check-layer-boundary.ps1` clean for this library.
