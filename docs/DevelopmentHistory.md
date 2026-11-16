# μ-Clid — Development History

Completed build stages with dates derived from git history.

---

| Stage | Date | Description | Key Files | Issues Closed | Build |
|---|---|---|---|---|---|
| Init | 2026-04-27 | Initial project setup — empty JUCE plugin builds successfully | CMakeLists.txt, PluginProcessor, PluginEditor | — | — |
| 1 & 2 | 2026-04-28 | Euclidean sequencer engine — EuclideanGenerator, HitGenerator, Rhythm, SequencerEngine, PluginProcessor | `Sequencer/EuclideanGenerator`, `HitGenerator`, `Rhythm`, `SequencerEngine` | — | — |
| 3 | 2026-04-28 | Sample playback engine + sequencer fixes — SamplePlayer, VoiceEngine skeleton, sequencer clock | `Audio/SamplePlayer`, `Audio/VoiceEngine` | — | — |
| 4 | 2026-04-28 | ControlSequence and ModulationMatrix (pure logic, no UI) | `Sequencer/ControlSequence`, `Modulation/ModulationMatrix` | — | — |
| 5 | 2026-04-28 | UI component library — MuClidLookAndFeel, KnobWithLabel, SegmentControl, NudgeInput, TimeSelector, DropdownSelect, StepEditor, LFOEditor, AddButton, StatusBar | `UI/Components/` | — | — |
| 6 | 2026-04-29 | UI panel layer + design doc reorganisation — RhythmCircle, SidebarItem, RhythmSidebar, EuclideanPanel, VoiceSection, RhythmPanel | `UI/RhythmCircle`, `UI/EuclideanPanel`, `UI/VoiceSection`, `UI/RhythmPanel` | — | — |
| 7 | 2026-04-29 | ModulatorPanel, ModMatrixPanel, MidiOutputEngine | `UI/ModulatorPanel`, `UI/ModMatrixPanel`, `Audio/MidiOutputEngine` | — | — |
| 7.5 | 2026-04-29 | TransportBar with play/stop and BPM for standalone mode | `UI/TransportBar` | — | — |
| 8 | 2026-04-30 | FX chain — FXSlotBase, OversampledProcessor, 8 EffectAlgorithms, EffectSlot, DelaySlot, ReverbSlot, FXChain, FXRow UI, DelayRow UI | `FX/`, `UI/FXRow`, `UI/DelayRow` | — | — |
| 9 | 2026-05-02 | MixerEngine, MixerOverlay, MixerChannel, VUMeter; bug fixes #1–7; 5 UI feature improvements; shared design system | `Audio/MixerEngine`, `UI/MixerChannel`, `UI/MixerOverlay`, `UI/Components/VUMeter` | #1–#7 | — |
| 9.5 | 2026-05-02 | VoiceParams, full VoiceEngine redesign (Amp/Filter/Pitch ADSR + filter chain), VoiceSection redesign into 4-column layout | `Audio/VoiceParams`, `Audio/VoiceEngine`, `UI/VoiceSection` | — | — |
| 9.6 | 2026-05-02 | Drive section in VoiceSection; Mixer always-8-channels with greyed inactive; FX sub-panel borders; fader height consistency; pan no-value display; Delay spread/dirt 0–100; Mixer button "Sequencer" label; ADSR/filter-res 0–100 display; Mod timing as DropdownSelect with labels | Various UI files | — | — |
| 10 | 2026-05-03 | TransportBar complete, AboutPanel, PresetBrowser, SaveDialog, SettingsOverlay; rhythm rename/delete with confirmation; EFX→Delay/Reverb sends; Echo=Delay algorithm; master loop length; APVTS wiring (getState/setState, preset save/load) | `UI/AboutPanel`, `UI/PresetBrowser`, `UI/SaveDialog`, `UI/SettingsOverlay` | — | — |
| 11 | 2026-05-03 | Ring rotation + hit arc pulses (RhythmCircle); sidebar flash on hit (SidebarItem); VU ballistics redesign; panel fade transitions; sidebar add/remove animations; modulator playhead (LFOEditor/StepEditor); font modernisation (FontOptions) | `UI/RhythmCircle`, `UI/SidebarItem`, `UI/Components/VUMeter`, `UI/ModulatorEditor` | #8–#11, #13, #18, #19 | — |
| Post-11 | 2026-05-03 | Optimisations: ModulationMatrix sort order cache, static FXAlgorithmDef registries; per-ring rotation speed fixes; sidebar blank-on-load fix | Various | — | — |
| 12 | 2026-05-04 | Fixes & cleanup — rhythm rename propagation fix; full label de-abbreviation (knob labels, segment mode labels, delay modifiers, status bar strings); dead TimeSelector removal; loop/sync dropdown design verified | `UI/RhythmPanel`, `UI/EuclideanPanel.h`, `UI/VoiceSection`, `UI/DelayRow`, `UI/Components/TimeSelector` (deleted) | #12, #15, #16, #20, #21 | 103 |
| 13 | 2026-05-04 | UI completions — Amp FX send knobs (Effect/Delay/Reverb) added to Voice Amp config row; intra-FX APVTS wiring verified end-to-end; Settings Overlay audited; signalsmith `windows.h` shadow bug fixed by removing premature includes (re-added in Stage 15 as SYSTEM) | `UI/VoiceSection`, `CMakeLists.txt` | #17, #22, #23 | 103 |
| 14 | 2026-05-04 | Euclid C accent system — `BlockResult` struct replaces bare `int` return from `SequencerEngine::processBlock`; Ring C pattern cached separately in `cachedCPatterns`; coincidence detection sets `accentMask` bit when Ring C fires on the same global step as an A+B hit; `VoiceEngine::trigger(bool isAccented)` stores `accentGain = decibelsToGain(accentDb)` and applies it as a post-ADSR level multiplier; `accentDb` added to `VoiceParams` and registered as an APVTS param per-rhythm; Accent knob added to VoiceSection Amp config row; VoiceSection layout expanded from 18 to 19 columns (Amp 4→5 cols) | `Audio/VoiceParams`, `Audio/VoiceEngine`, `Sequencer/SequencerEngine`, `PluginProcessor`, `UI/VoiceSection` | #14 | 104 |
| 15 | 2026-05-04 | Signalsmith FDN reverb — replaced `juce::Reverb` (Freeverb) with `signalsmith::basics::ReverbFloat` (Hadamard FDN with per-line modulation); pimpl pattern keeps Signalsmith headers out of ReverbSlot.h; include paths added as `SYSTEM PRIVATE` to avoid `signalsmith-dsp/windows.h` shadowing Windows SDK; wet work buffers pre-allocated in `prepare()` (no audio-thread allocation); Room/Hall/Plate/Spring presets retuned; `rt20` decay param set per-algorithm; user `size/damp/diffusion/mod` params mapped to `roomMs/highDampRate/early/detune` | `Source/FX/ReverbSlot.h`, `Source/FX/ReverbSlot.cpp`, `CMakeLists.txt` | #24 | 105 |
| 16 | 2026-05-04 | DSP quality pass — Delay: Hermite cubic interpolation replaces integer read pointer, 50ms one-pole LP smoothing on delay time (avoids clicks on BPM/LFO changes), `smoothCoeff = exp(-1/(0.05*sr))` computed in `prepare()`; Flanger: through-zero implementation — both dry and wet paths read from delay buffer at `baseSamp` and `baseSamp±lfoVal*baseSamp*depth` respectively, sweep can cross centre so comb passes through unity; Phaser: frequency-correct LFO mapping — notch sweeps 200–4000 Hz logarithmically, coefficient derived via bilinear transform `a=(1-tan(πf/sr))/(1+tan(πf/sr))` replacing the old linear coefficient sweep; TransportBar loop dropdown widened 68→100px; ModulatorEditor timing row dropdowns fixed to 110px and left-aligned | `Source/FX/DelaySlot.cpp`, `Source/FX/Effects/FlangerEffect.h`, `Source/FX/Effects/PhaserEffect.h`, `Source/UI/TransportBar.h`, `Source/UI/ModulatorEditor.cpp` | #25, #26, #27, #34, #35 | 109 |
| Post-16 | 2026-05-04 | Bug fixes — Flanger rate range widened 0.01–20Hz (was 0.1–8Hz) for full jet-flange effect; mixer channel level default lowered 0.75→0.5 (−6dB) to avoid VU red-lining on loud samples; sidebar hit indicator changed from full background flash to expanding pulse ring centred on miniCircle (quadratic ease-out over 200ms); VoiceSection Depth knob moved from envelope row to config row for both Pitch and Filter columns; Drive algorithm dropdown moved to full-width row 1, controls (Drive/Output/Tone) moved to row 2; Drive control labels update dynamically when algorithm changes (Bitcrusher shows Bits/Rate/Tone); Euclid A/B/C pre/post pad mode toggles relabelled from Ignore/Mute to Pad/Mute to match Insert section | `Source/FX/FXAlgorithmDef.h`, `Source/PluginProcessor.cpp`, `Source/UI/SidebarItem.cpp`, `Source/UI/VoiceSection.h`, `Source/UI/VoiceSection.cpp`, `Source/UI/EuclideanPanel.h` | #33, #37, #38, #39, #40, #41, #42 | 110 |
| 17 | 2026-05-04 | Voice chain + bitcrusher quality pass — Chorus: 4-point Catmull-Rom Hermite interpolation replaces linear, per-voice LFO rate detuning ±1.5% spread (evenly distributed so average = user rate, prevents fixed-period comb artefacts); Drive: ADAA for soft (tanh) and hard clip — antiderivatives `ln(cosh(x))` and `x²/2`/`|x|−0.5` with 1e-4 fallback to midpoint evaluation, cosh overflow guard at |x|>12 using asymptotic `|x|−log(2)`; Drive `prevDriveX[2]` state persists across blocks per channel; Bitcrusher: 1-pole LP pre-filter at new Nyquist (`exp(−π/rateThreshold)`) applied before sample-hold, TPDF triangular dither `(r1−r2)*lsb` before quantisation using `juce::Random`; Foldback and drive-bitcrush left as direct (ADAA impractical for those shapes) | `Source/FX/Effects/ChorusEffect.h`, `Source/Audio/VoiceEngine.h`, `Source/Audio/VoiceEngine.cpp`, `Source/FX/Effects/BitcrushEffect.h` | #28, #29, #30 | 112 |
| Post-17 | 2026-05-04 | Final issue sweep — Rhythm rename: `setRhythm()` now calls `finishEditingName(true)` before switching so an in-progress edit is always committed even if the focus-lost event fires late; Mixer FX knob parity: `DelayRow::kKnobW` and `kMsW` changed 64→72 to match `FXRow::kKnobW = 72`, giving identical knob sizes across all three rows; Mixer FX container panel: outer rounded-rect panel (darker fill, 6px corner radius, 1.5px border) added around all three FX rows in `MixerOverlay::paint()`; rows inset by `kFXPad = 6` on all sides and `kFXAreaH` updated accordingly | `Source/UI/RhythmPanel.cpp`, `Source/UI/DelayRow.h`, `Source/UI/MixerOverlay.h`, `Source/UI/MixerOverlay.cpp` | #12, #31, #32 | 113 |
| Post-Stage 17b | 2026-05-05 | Rhythm rename — final fix swapping the home-grown `juce::TextEditor` + `onFocusLost` editor for `juce::Label::setEditable(true, true, false)`. Previous approach was fragile because `onFocusLost` only fires when another component grabs keyboard focus, but most siblings in the panel don't; the editor stayed visible with `editingName` stuck `true`, so `paint()` (gated on `!editingName`) never drew `r.name`. Label commits on Enter/Escape/click-anywhere via its own internal mouse listener, fires `onTextChange` once, and `RhythmPanel::commitNameFromLabel` writes to `proc.getRhythm(currentRhythmIndex).name`. Default rhythm name changed to `<unnamed>` (Rhythm.h, PluginProcessor.cpp, PluginEditor.cpp), and a rhythm-colour border now traces the name box in the panel header | `Source/UI/RhythmPanel.h`, `Source/UI/RhythmPanel.cpp`, `Source/UI/RhythmSidebar.h`, `Source/UI/RhythmSidebar.cpp`, `Source/PluginEditor.cpp`, `Source/Sequencer/Rhythm.h`, `Source/PluginProcessor.cpp` | #12 | 119 |
| 18 | 2026-05-05 | VU meter recalibration to industry K-system convention — `VUMeter` rescaled so the visible bar maps `−60..0 dBFS` (full digital range) with 0 VU drawn as a tick mark at −18 dBFS (per AES/EBU; see [elysia.com](https://www.elysia.com/18dbfs-is-the-new-0dbu/)). Graduated colour zones replace the old single-colour bar: green below 0 VU (−18 dBFS), yellow between 0 VU and −6 dBFS, red above −6 dBFS. Clip LED still latches at ≥ 0 dBFS for 3 s, click to clear. Constants renamed for clarity (`kFloor`→`kFloorDb`, `linToDB`→`linToDb`); added `kZeroVuDb`, `kRedThreshDb`, `kClipDb`. Healthy mixed signals at −18 dBFS now sit at 70 % bar height with the 0 VU mark visible — the previous full-scale-at-top layout made any signal above −6 dBFS look like redlining even when audio was nowhere near clipping. Meter ballistics (instant attack, −1.5 dB/tick release at 30 Hz, 0.67 s peak hold, 0.3 dB/tick peak decay) preserved | `Source/UI/Components/VUMeter.h`, `Source/UI/Components/VUMeter.cpp` | (gain staging) | 121 |
| 19 | 2026-05-05 | Voice chain headroom + four issue fixes — **Stage 19 core**: `ampLevel` default lowered 1.0 → 0.5 (−6 dB) in `VoiceParams`, APVTS layout (`ampLvl` parameter default), and `VoiceSection`'s slider initial value, so a single voice with a 0 dBFS sample now peaks at −6 dBFS leaving 6 dB of summing headroom. Existing presets reload 6 dB quieter as documented trade-off. **Issue #43** (sidebar pulse not firing): replaced the one-shot `hitFired` bool with a monotonic `hitCount` atomic int — `RhythmCircle` and `SidebarItem` now edge-detect by tracking `lastHitCount`, removing the race where whichever timer ran second saw the flag already cleared. Audio thread sets both fields; old `hitFired` retained for backward compat. **Issue #44** (Effect return wet-only): added `setSendMode(bool)` to `EffectAlgorithmBase`; each algorithm's `processInner` now uses `(sendMode ? 1.0f : mix)` for wet and `(sendMode ? 0.0f : 1.0f - mix)` for dry. `EffectSlot::setAlgorithm` flips `sendMode=true` on every algorithm. UI: `FXRow::hideParameter("mix")` skips the knob in `MixerOverlay`'s effectRow setup. Updated Chorus/Flanger/Phaser/Echo/CombFilter algorithms. **Issue #45**: TransportBar loop-off label changed from "Loop Off" to ∞ (Unicode 0x221E). **Issue #48**: Euclid Pad/Mute toggle width cap raised 40→56 px so full text fits | `Source/Audio/VoiceParams.h`, `Source/PluginProcessor.cpp`, `Source/PluginProcessor.h`, `Source/UI/VoiceSection.cpp`, `Source/UI/RhythmCircle.{h,cpp}`, `Source/UI/SidebarItem.{h,cpp}`, `Source/FX/Effects/EffectAlgorithmBase.h`, `Source/FX/Effects/{Chorus,Flanger,Phaser,Echo,CombFilter}Effect.h`, `Source/FX/EffectSlot.cpp`, `Source/UI/FXRow.{h,cpp}`, `Source/UI/MixerOverlay.cpp`, `Source/UI/TransportBar.cpp`, `Source/UI/EuclideanPanel.cpp` | #43, #44, #45, #48 | 124 |
| 20 | 2026-05-05 | Pre-mixer summing trim — fixed −6 dB pre-fader headroom trim added to every channel input in `MixerEngine::processBlock`, applied between voice output and the (visible) channel fader. Implemented as `static constexpr float kHeadroomTrim = 0.5f` in `MixerEngine.h` and a single `buf.applyGain(kHeadroomTrim)` call after `voices[r].process(...)` and before `applyPanGain(...)`. The trim is invisible to the user (channel fader UI is unchanged) but absorbs +6 dB of summing across simultaneous voices and FX returns. Combined with Stage 19's voice-level −6 dB, a fresh project with a 0 dBFS sample now peaks ≈ −14 dBFS at the master output (≈ +4 dB on the calibrated VU), comfortably below clipping. Existing presets are 6 dB quieter on top of Stage 19's reduction (12 dB total) — users may need to push faders up. Triggered by user observation that build 124 still showed Master and Effect-return clipping with hot samples summing on the FX bus | `Source/Audio/MixerEngine.h`, `Source/Audio/MixerEngine.cpp` | (gain staging) | 125 |
| Post-20 | 2026-05-05 | Five issue fixes — **#47** (Euclid B/A rotation): `HitGenerator::getPattern()` and `getStepTypes()` only rotated when `rotate > 0`; fixed to `rotate != 0` with a `((r % n) + n) % n` normalisation so negative values rotate backwards correctly. **#51** (transport loop label): added `juce::Label loopLabel` to `TransportBar` drawn as "Loop:" to the left of the loop dropdown (36 px, right-aligned). **#55** (Drive None option): added "None" as the first drive algorithm (driveChar=0), shifting Soft→1, Hard→2, Fold→3, Bit→4; VoiceEngine short-circuits the entire drive stage when `driveChar == 0`, saving CPU; APVTS range extended 0–3 → 0–4. **#56** (Bitcrusher Bits display): when driveChar=Bit (id=5), `driveDrive` slider gets a `textFromValueFunction` that computes `max(2, 16 − v/100×12)` and displays as "N bits" (e.g. "16 bits" at 0 drive, "4 bits" at 100 drive). **#59** (envelope time display): all nine A/D/R ADSR knobs across Pitch/Filter/Amp sections get `textFromValueFunction` showing "N ms" for <1000 ms and "N.N s" for ≥1 s; sustain knobs show "N%" | `Source/Sequencer/HitGenerator.cpp`, `Source/UI/TransportBar.{h,cpp}`, `Source/Audio/VoiceParams.h`, `Source/PluginProcessor.cpp`, `Source/Audio/VoiceEngine.cpp`, `Source/UI/VoiceSection.cpp` | #47, #51, #55, #56, #59 | 127 |
| Post-20b | 2026-05-05 | Voice chain + UI polish — **#57** (Bitcrusher sample rate reduction): `driveRate` APVTS param (100–48000 Hz) added, wired through `VoiceParams.driveRate` and `syncRhythmParam`; VoiceEngine Bit case implements hold-and-sample pattern with per-channel `bitRateCounter`/`bitRateHeld` state; counter increments each sample, holds quantised value for `currentSampleRate / driveRate` samples. **#62** (LPF label + reusable filter): `OnePoleLP` 1-pole IIR struct extracted into `Source/Audio/AudioFilters.h` (coefficient `a = exp(−2πfc/sr)`, `y[n] = (1−a)x[n] + a·y[n−1]`); VoiceEngine tone filter migrated to `toneFilter[2]` array of `OnePoleLP`, called via `prepare(cutoff, sr)` + `process(x)` per block per channel, replacing inline state variable. **#63** (Rate knob in Bitcrusher mode): in Bitcrusher mode `driveOutput` knob dynamically switches label→"Rate", range→100–48000, Hz/kHz formatter, and `onValueChanged` routes to `drvRate` APVTS; non-Bit mode reverts to "Output"/ −24..0 dB / `drvOut`; switching is handled in `updateDriveLabels()` with null-callback guard to prevent spurious APVTS writes during range change. **#60** (INSERT label): VoiceSection paint() changed "DRIVE" → "INSERT". **#61** (Bitcrusher dropdown): drive algorithm dropdown item "Bit" renamed to "Bitcrusher". **#64** (double-click value entry): `KnobWithLabel::mouseDoubleClick` now shows an inline `juce::TextEditor` over the value dead zone; editor uses `slider.getValueFromText()` for round-trip parsing of any `textFromValueFunction`; commits on Return/focus-loss, cancels on Escape; `setDoubleClickReturnValue` disabled. **#65** (filter cutoff Hz/kHz): filterCutoff slider gets `textFromValueFunction` returning "<N> Hz" below 1000, "<N.N> kHz" above. **#66** (loop-point divider): `RhythmCircle::drawRing()` draws a 1.5 px radial line at `startOff` angle (between step N−1 and step 0) in a colour 60% brighter than `panelBackground` at 85% alpha | `Source/Audio/AudioFilters.h` (new), `Source/Audio/VoiceParams.h`, `Source/Audio/VoiceEngine.{h,cpp}`, `Source/PluginProcessor.cpp`, `Source/UI/VoiceSection.cpp`, `Source/UI/Components/KnobWithLabel.{h,cpp}`, `Source/UI/RhythmCircle.cpp` | #57, #60, #61, #62, #63, #64, #65, #66 | 128 |
| 21 | 2026-05-06 | Meter mode toggle — `VUMeter` gained a `MeterMode` enum (Peak / VU / K-12 / K-14) with `setMode()`. **Peak**: existing instant-attack + peak-hold behaviour unchanged. **VU / K modes**: symmetric first-order IIR ballistics, τ = 300 ms at 30 Hz (α ≈ 0.895), peak-hold tick hidden. **K-12 / K-14**: colour zones shift relative to the K reference — green→yellow at −12/−14 dBFS (0 K), yellow→red at −8/−10 dBFS (+4 K). A 22 px header strip added to `MixerOverlay` above the channel strips contains a `SegmentControl` "Peak \| VU \| K-12 \| K-14" that propagates the chosen mode to every channel, return, and master VU meter. | `Source/UI/Components/VUMeter.{h,cpp}`, `Source/UI/MixerChannel.h`, `Source/UI/MixerOverlay.{h,cpp}` | #114 | 173 |
| 22 | 2026-05-05 | Preset system — user content folder at `%USERPROFILE%\Documents\TDP\muClid` (Presets / Rhythms / Samples); path user-configurable in Settings overlay via `PropertiesFile`. Full preset extension changed `.mu` → `.muClid` (#87); rhythm preset `.muRhythm`. SaveDialog "Embed samples" checkbox encodes referenced samples as base64 inside the XML; load falls back to the Samples folder. `_default.muClid` loaded silently on startup; `_default.muRhythm` applied when a new rhythm is added. Preset file structure simplified to a container of 1–8 `Rhythm` subtrees (#88); legacy flat-format files still load via `restoreStateFromTree` fallback. | `Source/PluginProcessor.{h,cpp}`, `Source/UI/PresetBrowser.{h,cpp}`, `Source/UI/SaveDialog.{h,cpp}`, `Source/UI/SettingsOverlay.{h,cpp}` | #67, #68, #69, #70, #87, #88 | 142 |
| 23 | 2026-05-06 | Hot-swap rhythm while playing — `SequencerEngine::BlockResult` extended with `rhythmLoopWrapMask` and `masterLoopWrapped` flags; `PluginProcessor` gains a `PendingRhythmSwap` struct array, `AsyncUpdater` base, and `stageRhythmPreset` / `cancelStagedSwap` / `hasPendingSwap` methods. Audio thread detects the loop boundary and sets `boundaryReached`; message thread (via `handleAsyncUpdate`) does `suspendProcessing` + VoiceEngine swap + APVTS sync. UI: orange "SWP" badge in the sidebar item indicates a pending swap; clicking it cancels. "Hot-swap timing" dropdown in Settings selects "On master loop" vs "On rhythm loop". If not playing, applying a rhythm preset takes effect immediately. | `Source/Sequencer/SequencerEngine.{h,cpp}`, `Source/PluginProcessor.{h,cpp}`, `Source/UI/SidebarItem.{h,cpp}`, `Source/UI/RhythmPanel.{h,cpp}`, `Source/UI/SettingsOverlay.{h,cpp}` | #97 | 165 |
| 24 | 2026-05-06 | Sidechain ducking per mixer channel — each rhythm channel strip gained a sidechain section (104 px, placed above the send knobs): a compact dropdown listing "None" + other active channels by name, an Amount knob (0–100 % ducking depth), and two half-width knobs side-by-side labeled "/" (attack, 1–500 ms) and "\" (release, 10–2000 ms). DSP in `MixerEngine`: two-phase `processBlock` — Phase 1 fills all channel buffers; Phase 2 applies a per-channel envelope follower (`scEnv[r]`) driven by the source channel's post-fader audio, computing gain = `1 − amount × env` per sample using attack/release coefficients (`exp(−1/(ms×0.001×sr))`); Phase 3 pans, mixes, sends to FX. New `ChannelState` fields: `sidechainSource`, `sidechainAmount`, `sidechainAttackMs`, `sidechainReleaseMs`. Four new APVTS params per rhythm channel (`scSrc`, `scAmt`, `scAtk`, `scRel`), wired via `syncMixerParam`. `MixerOverlay::refreshSidechainSources()` rebuilds each channel's dropdown whenever rhythm count changes. | `Source/Audio/MixerEngine.{h,cpp}`, `Source/UI/MixerChannel.{h,cpp}`, `Source/UI/MixerOverlay.{h,cpp}`, `Source/PluginProcessor.cpp` | #115, #116, #117 | 176 |
| 26 | 2026-05-08 | Multimode SVF filter — `juce::dsp::StateVariableTPTFilter` in `VoiceEngine` provides LP / HP / BP / Notch modes. LP/HP/BP route directly through the TPT-SVF; Notch implemented as dry − bandpass using a pre-filter `notchBuffer` copy allocated in `prepareToPlay`. `syncFilter()` maps `filterType` 0/1/2 → lowpass/highpass/bandpass; type 3 sets bandpass internally and the subtract runs in `process()`. `VoiceSection` filter column: type dropdown (LP/HP/BP/Notch, IDs 1–4), Cutoff knob, Resonance knob (`filterRes`, 0–100% display, stored 0–0.99). APVTS: `fltType` (int 0–9, allows future expansion), `fltRes` (float 0–0.99), `fltCut` (float 20–20 kHz). Modulation destination `filter.resonance` wired in `ModDest` and `ModulationMatrix`. Implemented in base voice chain; Notch mode added in issue #160 (build 254). | `Source/Audio/VoiceEngine.{h,cpp}`, `Source/Audio/VoiceParams.h`, `Source/UI/VoiceSection.cpp`, `Source/Modulation/ModulationMatrix.cpp`, `Source/UI/ModulatorEditor.h` | #160 | 254 |
| 27 | 2026-05-08 | Ring Modulator insert — new `driveChar=9` algorithm in `InsertProcessor`. DSP: per-sample `data[i] *= (1 − mix + sin(phase) × mix)` where phase increments at `2π·freq/sr` per channel. Per-channel `ringPhase[2]` state in `InsertProcessor`. Parameters reuse existing APVTS slots: `drvDrv` (Mix 0–100%), `drvTon` (Freq 10–5000 Hz). UI: VoiceSection `configureInsertAlgorithm` case 9 (Mix + Freq knobs); MixerChannel `configureInsertAlgorithm` case 9 (same). APVTS `drvChar` range extended 0–8 → 0–10; jlimit guards updated in PluginProcessor. Skew applied to Freq knob for log feel. | `Source/Audio/InsertProcessor.{h,cpp}`, `Source/UI/VoiceSection.cpp`, `Source/UI/MixerChannel.cpp`, `Source/PluginProcessor.cpp` | #165 | 271 |
| 28 | 2026-05-08 | Tape Saturation insert — new `driveChar=10` algorithm in `InsertProcessor`. DSP: pre-gain (1–10× from Drive 0–100%) → `tanh(x·gain)` → DC block (1st-order HP at 20 Hz: `y=sat−dcIn+r·dcOut`, per-channel `dcBlockIn/Out[2]`) → 1-pole LP tone filter (reuses `toneFilter[2]`) → output gain (−24..0 dB). Parameters reuse existing APVTS: `drvDrv` (Drive), `drvTon` (Tone 200–20 kHz), `drvOut` (Output). UI: case 10 in both VoiceSection and MixerChannel `configureInsertAlgorithm` (Drive/Output/Tone labels). Insert panel knob height also reduced (jmin 80→40) and 4 px padding added inside panel. | `Source/Audio/InsertProcessor.{h,cpp}`, `Source/UI/VoiceSection.cpp`, `Source/UI/MixerChannel.cpp` | #164, #166 | 271 |
| 29 | 2026-05-09 | Extended voice filter bank — four new `filterType` values added without new APVTS params (pre-declared range 0–9 already covered). Types 4–6 (LP 24 / HP 24 / BP 24) use `juce::dsp::LadderFilter<float>` in LPF24/HPF24/BPF24 modes; `filterRes` (0–0.99) maps directly to ladder resonance; `ladderFilter` prepared alongside the SVF in `prepareToPlay`, drive fixed at 1.0. Type 7 (LP 6) uses `OnePoleLP` (two instances for stereo) from `AudioFilters.h`; coefficient computed from `modCutoff` each block, no `prepare` call needed. `syncFilter()` extended with a full switch to configure the correct object per type. `process()` filter section restructured: SVF+notch path for types 0–3, `ladderFilter.process(ctx)` for types 4–6, per-channel 1-pole loop for type 7. UI: four items appended to the `filterType` `DropdownSelect` in `VoiceSection` (LP 24 id=5, HP 24 id=6, BP 24 id=7, LP 6 id=8 → types 4–7). Existing types 0–3 (LP/HP/BP/Notch via SVF) untouched. | `Source/Audio/VoiceEngine.{h,cpp}`, `Source/Audio/VoiceParams.h`, `Source/UI/VoiceSection.cpp` | #169 | 282 |
| 30 | 2026-05-09 | Extended voice filter bank — six new filter types (AP12, Notch24, HP6, Peak, Lo Shelf, Hi Shelf). AP12 (type 9): 2nd-order all-pass biquad using `BiquadFilter::setAllPass()` (this JUCE build's `StateVariableTPTFilterType` has no `allPass` member). Notch24 (type 10): `LadderFilter` in BPF24 mode with `dry − BP` subtraction reusing `notchBuffer`, mirroring the Notch12 pattern. HP6 (type 11): `OnePoleHP` struct added to `AudioFilters.h` (HP = input − LP(input), same coefficient formula as `OnePoleLP`). Peak/Lo Shelf/Hi Shelf (types 12–14): allocation-free `BiquadFilter` struct (transposed direct-form-II, Audio EQ Cookbook formulas); `res` → Q (0.1–9.9), gain fixed +12 dB; coefficients recomputed per-block from `modCutoff` so filter envelope sweeps the EQ frequency. `eqFilter[2]` and `hp6Filter[2]` members added to `VoiceEngine`; both reset in `prepareToPlay()`. `fltType` APVTS max extended 9 → 14; `jlimit` guard updated. `VoiceSection` dropdown gains 6 items in logical order (HP 6, Notch 24, AP 12, Peak, Lo Shf, Hi Shf). | `Source/Audio/AudioFilters.h`, `Source/Audio/VoiceEngine.{h,cpp}`, `Source/Audio/VoiceParams.h`, `Source/UI/VoiceSection.cpp`, `Source/PluginProcessor.cpp` | #180, #300, #301, #181, #182 | 299 |
| Post-29 | 2026-05-09 | Filter dropdown reorder + feedback comb filter — `filterType` dropdown in `VoiceSection` reordered to LP 6 / LP 12 / LP 24 / BP 12 / BP 24 / HP 12 / HP 24 / Notch / Comb (IDs non-contiguous to preserve APVTS round-trip). New feedback comb filter (type 8): circular buffer per channel (max delay = `sr / 20 Hz + 4` samples, allocated in `prepareToPlay`), linear interpolation on fractional read pointer (`readF = writePos − sr/cutoff`), feedback gain from `filterRes` (0–0.99). Cutoff controls resonant peak frequency. `VoiceParams.h` comment updated to document type 8=Comb. | `Source/Audio/VoiceEngine.{h,cpp}`, `Source/Audio/VoiceParams.h`, `Source/UI/VoiceSection.cpp` | #175 | 284 |
| 31 | 2026-05-10 | Filter extraction — per-voice multi-mode filter dispatch + state moved out of `VoiceEngine` into a new reusable `MultiModeFilter` (`Source/Audio/MultiModeFilter.{h,cpp}`). The class owns `juce::dsp::StateVariableTPTFilter`, `juce::dsp::LadderFilter`, `OnePoleLP[2]`, `OnePoleHP[2]`, `BiquadFilter[2]`, two `std::vector<float>` comb buffers + write positions, and a `notchScratch` `AudioBuffer`. Exposes `prepare(sr, blockSize, numChannels) / reset / setType(int) / setCutoff(Hz) / setResonance(0..0.99) / process(buf, ns, nCh)`. Type codes match the existing APVTS `fltType` integers (0–14: LP12/HP12/BP12/Notch12, LP24/HP24/BP24, LP6, Comb, AP12, Notch24, HP6, Peak, LoShelf, HiShelf), so APVTS wiring and presets round-trip unchanged. `VoiceEngine` keeps the modulated cutoff calculation (depends on filter envelope) and now delegates the entire ~100-line dispatch ladder to a single `voiceFilter.process()` call. The notch dry-subtract trick and feedback comb state are now encapsulated in `MultiModeFilter`. Behaviour is bit-identical — pure code movement. | `Source/Audio/MultiModeFilter.{h,cpp}` (new), `Source/Audio/VoiceEngine.{h,cpp}`, `CMakeLists.txt` | #184 | 303 |
| 32 | 2026-05-13 | Mixer Responsive Layout — all three mixer areas now scale with the window. **#255** MixerOverlay horizontal: `kChanW` static constant removed; `chanW = jmax(44, (w − 2×kDivW − masterTotalW − 7×kChanGap) / 11)` computed each `resized()` call; divider positions cached as `lastDivX1/lastDivX2` for `paint()`; master channel fills any remaining horizontal space. **#256** MixerChannel vertical: `kSendH`, `kPanH`, `kTopAreaH` removed as static constants; sidechain section `scH = jmax(kSidechainH, round(h × 0.20))` with internal proportional split; sends+pan `spH = jmax(144, round(h × 0.35))` divided as `sendH = spH/4`, `panH = spH − 3×sendH`; fader fills remainder with 40 px floor. **#257** FX section + FXRow: `kFXAreaH`/`kFXRowH` replaced by `fxAreaH = jmax(220, round(h × 0.32))` and `fxRowH = (fxAreaH − gaps − pads) / 3`; `FXRow::resized()` distributes remaining row width evenly across knobs: `knobW = jmax(48, (rowW − fixedW) / numKnobs)`, `kKnobW` constant removed. | `Source/UI/MixerOverlay.{h,cpp}`, `Source/UI/MixerChannel.{h,cpp}`, `Source/UI/FXRow.{h,cpp}` | #255, #256, #257 | 360 |
| 25 | 2026-05-08 | Chorus effect — `ChorusEffect` (algorithm 0 in `EffectSlot`) with custom 2–4 voice delay-line implementation. 4-point Catmull-Rom Hermite interpolation for smooth, artefact-free modulation; per-voice LFO rate detuning ±1.5% evenly distributed across voices (all average to user rate — eliminates fixed-period comb filtering). 30 ms base delay per voice. Stereo spread via per-odd-voice delay asymmetry controlled by Spread (%). Parameters: Rate (Hz), Depth (%), Voices, Spread (%), Mix (%). Integrated with `FXSlotBase`/`EffectSlot`/`FXAlgorithmDef`; params routed via the shared `eff_p0..p4` APVTS system consistent with Flanger, Phaser, Echo. Foundation built in Stage 8 (initial chorus); quality improvements in Stage 17 (Hermite interpolation + detuning). Stage 25 confirms the feature complete and documents the architecture decision to use the generic param system rather than dedicated `eff_cho*` params. | `Source/FX/Effects/ChorusEffect.h`, `Source/FX/EffectSlot.{h,cpp}`, `Source/FX/FXAlgorithmDef.h` | — | 264 |
| 33 | 2026-05-13 | Plugin Family Architecture (mu-core) — CMake `mu-core` INTERFACE library extracting shared audio/FX/modulation/UI source files so future mu-family plugins (mu-tant etc.) share the voice/FX/mixer stack without code duplication. INTERFACE chosen over STATIC: JUCE's CMake module initialisation macros (`JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED`, `JUCE_MODULE_AVAILABLE_*`) are propagated only via `juce_add_plugin`'s INTERFACE targets; a STATIC library that doesn't link them fails with "No global header file was included!". `VoiceSlot` base struct extracted from `Rhythm` carrying all non-Euclidean fields (`voiceParams`, `controlSequences`, `modulationMatrix`, `modLock`, `name`, `colourIndex`); explicit copy constructor/assignment required because `std::atomic<bool>` is non-copyable. `Rhythm : public VoiceSlot` retains only Euclidean fields. `ProcessorBase` abstract base class owns `fxChain` and `mixerEngine` (both `public` for direct UI panel access) and provides `processCoreBlock()` which calls `fxChain.setHostBpm()` then `mixerEngine.processBlock()`; `PluginProcessor` now inherits `ProcessorBase`. `MuClidLookAndFeel` → `MuLookAndFeel` rename with backward-compat shim (`using MuClidLookAndFeel = MuLookAndFeel`) so all 58 existing include sites compile unchanged. | `CMakeLists.txt`, `Source/ProcessorBase.{h,cpp}` (new), `Source/Sequencer/VoiceSlot.h` (new), `Source/Sequencer/Rhythm.h`, `Source/UI/Components/MuLookAndFeel.{h,cpp}` (new), `Source/UI/Components/MuClidLookAndFeel.{h,cpp}` (shim), `Source/PluginProcessor.{h,cpp}` | #258, #259, #260, #261 | 368 |

| 35 | 2026-05-13 | Robust preset format — (1) Renamed `VoiceParams::drive*`/`drv*` C++ fields → `insert*` (#452); APVTS IDs/XML keys unchanged for DAW back-compat. (2) De-normalised save format via `writeKindedProperty`: `r0_stepsA="16"`, `r0_fltCut="234.7"` instead of JUCE-normalised 0..1. (3) Algorithm selectors persisted as stable string names (`r0_drvChar="Bitcrusher"`, `r0_fltType="LP24"`, `eff_algo="chorus"`, modulator `mode="Smooth"`) via [AlgorithmNames.h](../mu-core/Audio/AlgorithmNames.h) lookup tables — reorder-safe. Covers per-rhythm params (#448), global FX/mixer state (#451), modulator enums (#436). (4) `presetVersion` bumped to 2; v0/v1 preset files rejected at gate with clear error; `readKindedPropertyAsActualV2` handles v2 parsing (#449). (5) `docs/preset-format.md` documenting XML schema, ParamKind tags, algorithm-name contracts (#450). | [AlgorithmNames.h](../mu-core/Audio/AlgorithmNames.h), [RhythmParamTable.h](../mu-core/Persistence/RhythmParamTable.h), [PluginProcessor_Internal.h](../mu-clid/Source/Plugin/PluginProcessor_Internal.h), PluginProcessor_Preset.cpp, [docs/mu-clid/preset-format.md](mu-clid/preset-format.md) | #436, #447–#452 | 520 |
| 36 | 2026-05-21 | Stage 36 (insertParam[4]) + UI scale picker + audit fix-forward — 9 named VoiceParams insert fields collapsed to a single `insertParam[4]` array driven by per-algorithm config table ([InsertSlotConfig.h](../mu-core/Audio/InsertSlotConfig.h)); insert mod destinations collapsed from 11 to 4 generic slots. UI Size picker (Medium / Large) in Settings. Multiple audit fixes. | `Source/Audio/InsertSlotConfig.h`, `Source/Audio/VoiceParams.h`, multiple insert + UI files | #594, #597, #602, #603, #604, #606, #608, #609, #610, #611, #612, #613, #614 | 588 |
| 34 | 2026-05-25 | Seamless hot-swap with polyphonic voice tail — old `VoiceEngine` keeps rendering its in-flight sample / amp envelope after swap, drains naturally, gets cleaned up off the audio thread. Steps 1 + 2 + 3 landed in #413 / #414 / #415; #416 forced filter / insert reset on retire to kill the "comb-filter overlay" symptom (engines never drained because of ADSR.isActive() semantics + filter-self-feed past env). #417 fixed a pre-existing ampRelToEnd envelope bug masked by the never-drain bug. **Stage closed (#627 in #644 at v1.0.606)**: VoiceEngine signal flow reworked to FILTER-THEN-AMP — per-voice `ampEnvs[MaxVoices]` collapsed to a single engine-level `ampEnv` that gates the post-filter + post-insert output. Filter resonance / comb feedback / reverb-style insert tails now ring naturally through hot-swap (markRetired no longer resets filter / insert state). Trade-off: per-voice envelope independence on overlapping voices is gone — fine for drum-style triggers, pattern legato (#419) handles pad cases. Step 4 (atomic-pointer swap) remains deferred — current suspend gap (~1.33 ms at 48k/64) inaudible on percussive material. | [docs/mu-clid/archive/design-seamless-hotswap.md](mu-clid/archive/design-seamless-hotswap.md), [docs/mu-clid/design-627-filter-then-amp.md](mu-clid/design-627-filter-then-amp.md), [Source/Audio/VoiceEngine.{h,cpp}](../mu-core/Audio/VoiceEngine.cpp), [Source/Plugin/HotSwapStager.cpp](../mu-clid/Source/Plugin/HotSwapStager.cpp), [Source/Audio/MixerEngine.cpp](../mu-core/Audio/MixerEngine.cpp) | #413, #414, #415, #416, #417, #627, #644 | 606 |

---

## Issue History

Issues resolved during development — see [backlog.md](../backlog.md) for the full table.

Issues #1–#11 were raised and resolved during Stages 9–11. Issues #13 and #18 were resolved as design decisions on 2026-05-04 (no code change required — Euclid C intentionally inherits the full A/B padding parameter set).

Issues #12, #15, #16, #20 were fixed in Stage 12. Issue #21 was resolved as a design decision — the loop dropdown correctly controls master loop length; Host Sync mode is a separate future feature. All Stage 12 + 13 changes reached first successful build at build 103 (builds 94–102 were failed attempts blocked by signalsmith-dsp's `windows.h` shadowing the Windows SDK header).

Issue #17 was fixed in Stage 13 — three FX send knobs added to VoiceSection Amp config row, wired to the same `ch{i}_sendEff/Dly/Rev` APVTS params used by MixerChannel strips. Issues #22 and #23 were verified/audited with no code changes needed.

Issue #14 was fixed in Stage 14 — full accent signal path implemented. Note: Advance Mode and Accent Velocity (originally listed in the stage description) were descoped from v1 per issue #19 (design resolved in Stage 11).

## Plugin-family architecture: stage logs moved from design-plugin-family.md (2026-11-16)

These sections were finished stage logs. They are kept verbatim as they stood, including the pre-2026-11-14 wording (per-binary `kMaxLayers`, the INTERFACE `mu-core`, the Linux release); `docs/design-plugin-family.md` holds the current rules and a short summary of each.

### Licensing: original 2026-10-11 ruling (deleted / kept table, placement, testing, staged order)

**Owner decision.** The source is GPLv3. mu-Clid, mu-Tant, mu-Toni and mu-On are pay-what-you-want on Lemon Squeezy (minimum £0, suggested £20; a key is issued even at £0). mu-Clid MIDI and mu-Link are freeware. An unlicensed copy is **fully functional**: no track, step, save or preset limits, no demo banner, and no "Lite" free tier (the former mu-Clid Lite is now a separate MIDI-effect product, see "The MIDI-effect product" below). Instead it shows one polite support dialog per session. A licence key still exists and still activates (a key marks a supporter, removes the prompt, and unlocks nothing). The four Pay What You Want products call `initLicensing`; the freeware mu-Clid MIDI and mu-Link never do, so they never show the dialog.

### What is deleted, what is kept

| Delete | Keep |
|---|---|
| `ProcessorBase::demoMaxChannels`, `demoMaxSteps`, `canAddChannel`, `canSaveLayerPreset`, `maxSteps` (and their callers: `ChannelSidebar`, `MasterLoopSection`, `LayerPresetHeader`, both `PluginEditor` add-channel checks, `EuclideanPanel`, `RhythmPanel`, `VoicePanel`, `GatingDesigner::setMaxCells` use, the tant `limitToCells` demo calls, the preset-load clamps in `PresetIO*` and `PluginProcessor_Preset`, the sequencer `setStepCap`) | `LicenseManager`, `OnlineActivation`, `LemonSqueezyClient`, `ActivationStore`, `MachineFingerprint`, `initLicensing`, `ActivationOverlay`, `activateOnlineFn`, the `.lic` offline path, the license tool |
| `EditorShellBase::demoBanner`, `kDemoBannerH`, the banner click route, the banner layout rows | the update check (`StatusBar` update link, `UpdateChecker`) |
| The save gate in `transportBar.onSavePreset`, `setSaveEnabled(licensed)` calls | The `StatusBar` is no longer given "Demo limited ..." messages (delete those two) |
| `MUFAMILY_REQUIRE_LICENSE` as a feature gate (see below) | |
| The mu-Clid *demo-limit* paths. mu-Clid Lite is **not** deleted: as of 2026-10-11 it is a product in its own right and leaves mu-Clid by the staged plan in "The MIDI-effect product" below, not by deletion. | |

`isLicensed()` goes too. It is replaced by `bool isSupporter() const` (offline `.lic` valid OR online-activated), which is true or false identically in Debug and Release. Nothing in the audio or editor path may branch on it except the support prompt and the About/Activation text.

### Where it lives

- **`mu-core/UI/SupportOverlay.{h,cpp}`**, a sibling of `ActivationOverlay` (same non-blocking card, click outside dismisses, same `MuLookAndFeel` tokens; the `ux-controller` rules the look and wording). The shell owns it as a member, wired in `wireOverlays()`, shown through a `showSupport(bool)` that joins the overlay state machine. Card content: a short thank-you and what the money does, the pay-what-you-want line, a **Support (opens the checkout page)** button, an **Enter a key** button that calls `showActivation(true)`, a **Not now** button, and a **Don't show this again** tick box.
- **`mu-core/License/SupportPromptPolicy.h`**, juce_core-only and header-only, the testable decision: `shouldShow(hasLicensing, isSupporter, hiddenForever, shownThisProcess)`. It is true only for a licensing product, not a supporter, not hidden for ever, and not yet shown in this process.
- **Once per session** means once per process: a function-local `inline std::atomic<bool>` in the policy header, set when the card is shown. A DAW project with twenty instances shows it once. The shell shows it from `parentHierarchyChanged()` the first time the editor is on screen (so a headless instance never prompts), deferred by a short `callAfterDelay` so it never competes with plugin load.
- **Persistence of "don't show again":** the product's own settings file (`ProcessorBase::appSettings`, key `supportPromptHidden`, bool), through two small `ProcessorBase` accessors in the style of `setUiScale`: `bool isSupportPromptHidden() const` and `void setSupportPromptHidden(bool)` (writes and `saveIfNeeded()`). Per user and per product, not in APVTS, because it is a user preference and must not travel inside a preset or a project. A supporter never sees the prompt, so the key is only written by the tick box.
- **Checkout URL plumbing:** a field on `ProcessorBase::LicensingConfig`, `const char* checkoutUrl`, supplied by each licensed product from its `Source/License/LicenseKey.h` (`kCheckoutUrl`), exposed as `ProcessorBase::getCheckoutUrl()`. This sits with the rest of the product's licensing identity and spares every editor an extra setter call; `ActivationOverlay` gets a "Get a key" link from the same accessor. The values live only in each product's `LicenseKey.h` and match the website's buy links; mu-core names none of them.
- **Link, not a payment form.** The button opens the checkout in the browser (`juce::URL::launchInDefaultBrowser`). The plugin handles no money and no card data.
- A permanent entry point stays after the banner goes: the About panel shows "Support mu-X" and "Enter a key" buttons (supporters see "Thank you"), so the dialog is never the only route.

### `MUFAMILY_REQUIRE_LICENSE` after this change

It no longer gates features, so its Debug-bypass meaning disappears. Remove it from the root `CMakeLists.txt`. Debug and Release behave the same, which is what makes the dialog testable in a Debug build (below). The `LicenseManager.h` header comment is rewritten with the macro.

### Testing

- **Unit (mu-core-tests):** `SupportPromptPolicyTests.cpp` covers every input combination of `shouldShow` and the once-per-process flag (the flag is reset through a test-only `resetForTests()`). A second test builds `TestProcessorCore` over a temp `PropertiesFile` and checks `setSupportPromptHidden` round-trips through the settings file, and that `isSupporter()` is true after `onlineActivated` is set and false otherwise.
- **Manual / Debug:** with no key a Debug build now shows the prompt on first open (the Debug bypass is gone), so it is exercised on every test run. To see it again after "don't show again", delete the `supportPromptHidden` line from `%APPDATA%\TDP\<app>.xml`, or run with the env var `MU_SUPPORT_PROMPT=reset` (read once in `initLicensing`, Debug-only, clears the key). To see the supporter path, activate with the verified £0 key.
- **Regression:** the existing unit and round-trip tests must show no behavioural change in a licensed state; the full-preset save and an 8-track, 64-step preset must work with no key present (add one such case to each of mu-clid-tests and mu-tant-tests, replacing any test that asserts the demo clamp, for example `limitToCells`, which can stay as a pure `GatePattern` function test or go with the feature).

### Staged order (each stage buildable and independently shippable)

1. **Policy and persistence (additive).** `SupportPromptPolicy.h`, `isSupporter()`, `isSupportPromptHidden/Set`, `checkoutUrl` in `LicensingConfig` and both `LicenseKey.h` files, `mu-core-tests` for the above. Nothing visible changes.
2. **SupportOverlay and shell wiring.** The overlay, `showSupport`, first-show trigger, About buttons, "Get a key" link. The old demo banner and caps still exist, so this stage is visible but harmless. `ux-controller` rules the card first.
3. **Remove caps.** Delete the `ProcessorBase` cap API and every caller listed above, the demo status messages, the save gate, the preset-load clamps, the tant demo `limitToCells`; replace `isLicensed()` calls with nothing (or `isSupporter()` in About). Update tests. This is the one large edit, still product by product if wanted: mu-Tant, then mu-Clid.
4. **Remove the banner and the macro.** `demoBanner`, its layout, click route; `MUFAMILY_REQUIRE_LICENSE`; header comments.
5. **(Superseded 2026-10-11: Lite is not retired.)** The owner made mu-Clid Lite a separate MIDI-effect product. Its split from mu-Clid follows "The MIDI-effect product" below and is independent of stages 1 to 4 (it needs none of them).
6. **Docs and site.** Rewrite the licensing lines of both manuals (`user-documentation-author`), `/notes`, the site buy buttons and pricing (separate site task), and the CLAUDE.md wording below.

Each stage ends in a Debug build and the unit tests; stages 3 and 4 also run the round-trip listening tests and pluginval on the build PC.

### The MIDI-effect product: staged plan and the shared-sequencer move in detail (done)

### Ruling: a product, reached in stages (not a target forever)

Today it is a second `juce_add_plugin` target inside `mu-clid/`, compiling about seventeen of mu-Clid's files a second time under `MUCLID_LITE_BUILD=1`, with roughly sixty `#if` sites in `PluginProcessor.{h,cpp}`, `_APVTS`, `_Modulation`, `PresetIO` and `StandaloneApp`. That breaks the family shape (one product, one folder, one namespace, eight `Source/` folders) and the "no per-product copy of shared behaviour" rule in both directions: mu-Clid carries MIDI-only branches, and a standalone MIDI product would otherwise need its own copy of the Euclidean sequencer. It therefore gets its own folder, namespace, docs and CLAUDE.md. It must **not** be done as a fork: the Euclidean sequencing both products share moves to `mu-core` first (the "second product needing a behaviour" rule).

Stages (each ends in a Debug build and the unit tests; stages 2 and 3 also run the round-trip listening tests and pluginval on the build PC):

1. **Identity, no code move (cheap, do first).** Rename every user-visible and release-facing name to the final name while keeping the CMake target, `MUCLID_LITE_BUILD` and every plugin ID untouched: `PRODUCT_NAME` / `PLUGIN_NAME`, `getName()`, the `.iss` and installer filename, `PackageRelease.cmake` slug, `DeployReleaseLite.cmake` filenames, `release.yml` staging and zip names, `mu_build_artifacts.ps1`, `check-build-artifacts.ps1`, `run-pluginval.ps1`, the site. Add the Windows `[InstallDelete]` and the uninstall of the old `mu-Clid Lite` bundle (below). Docs: a product CLAUDE.md stub and `docs/<slug>/`.
2. **Extract the shared sequencing into `mu-core/Sequencer/`.** The files that Lite and mu-Clid both compile (`EuclideanGenerator`, `HitGenerator`, and whatever of `Rhythm` / `RhythmSequencer` is free of sample, voice and sidechain concepts) move to `mu-core` under neutral names and the `mu_core` namespace, mu-Clid being the second consumer that makes it shared. The `Rhythm`-into-`Layer` container work (design-naming §5 step 5.7) rewrites the same classes; **sequence this stage after that one**, or hand the extraction to it, so the code moves once. **Owner chose the earlier split (2026-10-11):** extract only the pure generators (`EuclideanGenerator`, `HitGenerator`) now and leave `Rhythm` / `RhythmSequencer` for step 5.7; the MIDI product carries a thin `Rhythm` of its own until then, recorded as a known exception. **Superseded 2026-10-11 (architecture-steward, on the as-built tree):** the MIDI product does not carry a `Rhythm` of its own; it compiles mu-Clid's `Rhythm.cpp`, `RhythmSequencer.cpp`, `EuclideanPanel.cpp` and `RhythmCircle.cpp` from `mu-clid/Source` with that folder on its include path, which is the per-product copy and the product-to-product dependency this section forbids. The `Rhythm` that exists today is already pure (generators, logic, reset steps, legato, a `Layer`; no sample, voice or sidechain member), so it moves **now**, before stage 7, in the stages under "Stage 2b" below; stage 7 then no longer adds engine members to the shared class.

### Stage 2b: the shared Euclidean sequencer moves to mu-core (ruled 2026-10-11; done 2026-10-12, stages 1 to 5)

The shared pieces and their destination (the moves are `git mv`; a header keeps its `Folder/Name.h` include path, so `#include "Sequencer/Rhythm.h"` in mu-Clid and the MIDI product resolves unchanged through the `mu-core` include root):

| From `mu-clid/Source/` | To `mu-core/` | Namespace |
|---|---|---|
| `Sequencer/Rhythm.{h,cpp}` | `Sequencer/Rhythm.{h,cpp}` | `mu_core` (`Rhythm`, `Logic`, `EuclidOverrides`) |
| `Sequencer/SequencerEngine.{h,cpp}` (now `RhythmSequencer`) | `Sequencer/RhythmSequencer.{h,cpp}` | `mu_core` |
| `Sequencer/RhythmPlayState.h` | `Sequencer/RhythmPlayState.h` | `mu_core` |
| `Persistence/RhythmParamTable.h` | `Persistence/RhythmParamTable.h` | `mu_core` (`ParamKind`, `RhythmParamDef`, `kRhythmParamDefs`, `kRhythmParamCount`) |
| `Persistence/PresetHelpers.h` | `Persistence/RhythmPresetHelpers.h` (renamed: it is the kinded-property reader/writer for that table, not a product's preset I/O) | `mu_core` |
| `UI/EuclideanPanel.{h,cpp}` | `UI/EuclideanPanel.{h,cpp}` | `mu_ui` |
| `UI/RhythmCircle.{h,cpp}` | `UI/RhythmCircle.{h,cpp}` | `mu_ui` |

`Sequencer/GeneratorAliases.h` stays in mu-Clid and becomes the alias header for the sequencer and persistence set
(`using mu_core::Rhythm;` and so on inside `mu_clid`; the two panels get their own UI alias header, see the stage rules below), the pattern it already uses for the generators, so
mu-Clid's roughly 1580 uses need no edit. Rules for the moved code: **no sample, voice-engine, sidechain,
licence or APVTS-id knowledge in `mu_core::Rhythm`**; the euclid modulation-snapshot indices that
`EuclideanPanel` reads become a panel-local enum in `EuclideanPanel.h` (`EuclidSnap`; mu-Clid's `kSnapEuc*`
values are pinned to it by a `static_assert`, see the stage rules below), so the panel no longer includes
`Modulation/ModulationSnapshot.h`. **Stage 7 of the layer plan** (mu-Clid into the container) does **not**
derive a `mu_clid::Rhythm` (amended 2026-10-12; see "Stage 7 implementation plan"): mu-Clid and the MIDI
product both use `mu_core::Rhythm`, and the engine, retired engines, MIDI-out engine, sample path and
mod-previous-state flags stay processor-side keyed by slot. After stage 2b (done) the MIDI product's CMake has no
`${MU_CLID_SOURCE}` include path and compiles nothing from `mu-clid/`.

**The five stages (ruled 2026-10-11; supersedes an earlier split that created the `EuclidSnap` mapping first and moved Rhythm, RhythmPlayState, RhythmSequencer, RhythmParamTable and PresetHelpers in one step).** The backlog split is the correct one, backlog rows 1489 to 1493 (umbrella 1411), because it follows the include graph so each stage leaves both products building:

- `Rhythm.h` includes `Layer.h` (already mu-core) and `GeneratorAliases.h` (a `mu_clid` alias header, so it must be replaced by direct `Sequencer/EuclideanGenerator.h` / `HitGenerator.h` includes and `mu_core::` names when the file moves). `RhythmPlayState.h` includes only `<atomic>`. Both are leaves: **stage 1**.
- `RhythmSequencer.h` includes `Rhythm.h` (relative; must become `Sequencer/Rhythm.h`), `MuLimits.h` and `Audio/SpinLock.h` (both mu-core). It needs stage 1 only: **stage 2**.
- `RhythmParamTable.h` includes `Sequencer/Rhythm.h` and mu-core `Audio/AlgorithmNames.h`; `PresetHelpers.h` includes only `RhythmParamTable.h` and `AlgorithmNames.h`. Neither touches the engine, so they need stage 1 only, and they move together because the helper cannot compile without the table: **stage 3**.
- `RhythmCircle.h` includes `RhythmPlayState.h` (stage 1) and `GeneratorAliases.h`; `EuclideanPanel.h` includes `Rhythm.h` (stage 1), `GeneratorAliases.h`, mu-core components and `Plugin/ProcessorBase.h`, and its `.cpp` includes `Modulation/ModulationSnapshot.h` (mu-Clid) for the `kSnapEuc*` indices. Only that one include blocks the move, so the panel-local enum is part of the panel's own stage, not a stage of its own: **stage 4**.
- The MIDI product's include path and compile-ins can only go once all of the above has moved: **stage 5**.

Stages 2, 3 and 4 are independent of each other once stage 1 is in; run them in the backlog order. Rules that apply to every stage:

- **Each stage moves its own users.** Moving a file out of `mu-clid/Source` breaks the relative includes of what is left behind and the `mu_clid::` qualified uses in the MIDI product. Stage 1 therefore also: makes `GeneratorAliases.h` include `Sequencer/Rhythm.h` and `Sequencer/RhythmPlayState.h` and add `using mu_core::Rhythm; using mu_core::Logic; using mu_core::EuclidOverrides; using mu_core::RhythmPlayState;`; adds `#include "GeneratorAliases.h"` to the not-yet-moved `RhythmSequencer.h`, `RhythmParamTable.h`, `EuclideanPanel.h` and `RhythmCircle.h` (and changes `RhythmSequencer.h`'s `"Rhythm.h"` to `"Sequencer/Rhythm.h"`) so their unqualified `Rhythm` still resolves inside `mu_clid`; and in the MIDI product changes `using mu_clid::Rhythm; using mu_clid::RhythmPlayState;` in `MidiRhythmCore.h` and `mu_clid::Rhythm` in `PluginEditor.cpp` to `mu_core::`. Each later stage does the same for its own files (stage 2: `MidiRhythmCore.h` `RhythmSequencer`; stage 3: `mu_clid::kRhythmParamCount` / `kRhythmParamDefs` / `readKindedPropertyAsActualV2` in `MidiRhythmCore.{h,cpp}`, `SessionState.cpp`, `SessionStateTests.cpp`; stage 4: `mu_clid::EuclideanPanel` / `RhythmCircle` in the MIDI `PluginEditor.{h,cpp}`).
- **Alias headers.** `GeneratorAliases.h` carries the sequencer and persistence aliases. The two panels are GUI classes and must not be pulled into audio translation units through it, so stage 4 adds a small `mu-clid/Source/UI/EuclidPanelAliases.h` (`using mu_ui::EuclideanPanel; using mu_ui::RhythmCircle;`) included by the mu-Clid editor files. Stage 3 adds `using mu_core::ParamKind; ...` for the table to `GeneratorAliases.h` (or the files that use it name `mu_core::` directly where there are few).
- **CMake.** The moved `.cpp` files go into `mu-core/CMakeLists.txt` (the library source list; the headless test targets that list mu-core sources individually, `mu-clid/CMakeLists.txt` near `Source/Sequencer/Rhythm.cpp` and `mu-clid-midi/CMakeLists.txt` near its `HitGenerator.cpp` block, get the same line and lose the `${MU_CLID_SOURCE}` or `Source/` one). A header-only move changes no CMake list.
- **Panel mod snapshots (stage 4).** The panel-local enum is `EuclidSnap` in `EuclideanPanel.h` (`kEucAHits ... kEucCInsLen`, 18 entries, same order as the `kSnapEuc*` run in `ModulationSnapshot.h`); mu-Clid's `ModulationSnapshot.h` keeps its names and adds `static_assert(kSnapEucAHits + n == EuclidSnap::...)` for the first and last entry so the order is pinned. The MIDI product passes no `modulationSnapshot` callback (it has no modulation), so it needs nothing.
- Each stage ends with the checks named in its backlog row, and `check-core-boundary.ps1` clean.
3. **Create `mu-<slug>/`.** Own `CMakeLists.txt`, own thin `PluginProcessor` (a `ProcessorBase` subclass, MIDI-effect bus layout, `lite_midiNote`-style parameters renamed in the product only through a migration), own editor (`LiteEditor` becomes `<Product>Editor`, in `UI/`), the eight `Source/` folders with `.gitkeep` in the empty ones (`License`, `Modulation`, `Audio` are expected empty). `mu-clid` loses all `MUCLID_LITE_BUILD` code, the `mu-clid-lite` target, `LiteEditor.*` and the Lite members of `PluginProcessor`. Add the new directory to the root list in `check-core-boundary.ps1` and `check-mod-targets.ps1`.
4. **Retire the old paths.** Remove `BuildInstallerLite.cmake` / `DeployReleaseLite.cmake` in favour of the shared product-parameterised scripts (or renamed copies), the Lite lines in `PackageRelease.cmake`, and the `mu-clid/installer/mu-Clid-Lite.iss` (moves to `mu-<slug>/installer/`).

### The MIDI-effect product: migration touch list and owner decisions

### What the migration touches

| Area | Files | Stage |
|---|---|---|
| Installer | `mu-clid/installer/mu-Clid-Lite.iss` (rename, new output filename, `[InstallDelete]`, keep `AppId`), `cmake/BuildInstallerLite.cmake` | 1, 4 |
| Release workflow | `.github/workflows/release.yml` staging dirs, copy paths and the three zip names (Windows, macOS on hold, Linux): `mu-Clid-Lite-*.zip` | 1, 3 (paths) |
| Packaging and deploy | `cmake/PackageRelease.cmake` (`-DPRODUCT=`), `cmake/DeployReleaseLite.cmake`, the deploy blocks in `mu-clid/CMakeLists.txt` | 1, 3, 4 |
| Build tooling and hooks | `scripts/mu_build_artifacts.ps1` (artefact list that the post-build hook prints), `tests/scripts/check-build-artifacts.ps1`, `tests/scripts/run-pluginval.ps1`, `tests/scripts/check-core-boundary.ps1`, `check-mod-targets.ps1` (product root list), `/build` and `/release` skills | 1, 3 |
| CMake | root `CMakeLists.txt` `add_subdirectory`, `mu-clid/CMakeLists.txt` Lite block, new `mu-<slug>/CMakeLists.txt`, `increment_build_number` dependency, `versioninfo-lite.rc.in`, `resources/` icon and the shared `mu-clid-resources` target | 3 |
| Source | `MUCLID_LITE_BUILD` sites, `LiteEditor`, `computeLiteTransport` and the Lite processBlock helpers, `lite_midiNote` parameters | 3 |
| Website | `site/download.html` (card, file names, the analytics label `μ-clid LITE download`), a product page, a release-notes page, the `/notes` command's product list, `web-designer` brief | 1 |
| Manuals | a short `docs/<slug>/create_manual.ps1` and `.docx` (`user-documentation-author`); the mu-Clid manual drops any Lite mention and copy to the OneDrive root | 3 |
| Tests | a `<slug>-tests` target (the generator and processor tests move with the code); `tests.md` rows; pluginval path | 3 |
| Presets and extension | none: `.muRhythm` and `MuClidState` are unchanged; a new full-preset extension only if the product ever gains full presets | none |
| Docs | this section, design-naming §3 / §4 / §5, `CLAUDE.md` monorepo layout and product list, a `mu-<slug>/CLAUDE.md` | 1, 3 |

### Owner decisions (settled 2026-10-11)

1. **Name:** display name **mu-Clid MIDI** (the descriptive-suffix exception for a deliberate sibling, design-naming §3). Slug **`mu-clid-midi`**, namespace **`mu_clid_midi`**, source folder `mu-clid-midi/`, docs folder `docs/mu-clid-midi/`, test target `mu-clid-midi-tests`, installer/zip names `mu-Clid-MIDI-*`. The names are display and layout only; the next item does not change.
2. **Plugin IDs kept:** `MCLt`, `TDP1`, `com.transwarp.mu-clid-lite`, `MuClidState` all stay (the CMake target may be renamed in stage 3; the IDs inside it may not).
3. **`.muRhythm` / `Rhythm` shared with mu-Clid:** yes (rule above).
4. **Licensing:** no support prompt and no licensing glue. It is freeware.
5. **Formats:** VST3 + CLAP only (no AU, no Standalone). Free, no price on the site.
6. **Identity:** it keeps its own icon and its own site product page and release-notes page.
7. **Extraction timing (stage 2):** extract the pure generators now (`EuclideanGenerator`, `HitGenerator` into `mu-core/Sequencer/`, namespace `mu_core`); `Rhythm` and `RhythmSequencer` follow with the layer-container stage (design-naming §5 step 5.7). Until then mu-Clid MIDI carries a thin `Rhythm` of its own, a recorded known exception, not to be extended. **Resequenced 2026-10-11:** they move now, as Stage 2b above, because the class is already pure and stage 7 is the last and riskiest of the plan; the owner is asked to confirm the new order.

### Shared layer sidebar and layer panel: migration order and design-future check

**Migration order (smallest blast radius first; each numbered step builds on its own).**
1. Tokens in `MuLookAndFeel` plus the two `ProcessorBase` changes above, with mu-Clid's overrides. No UI
   change. Unit test: `swapLayers` virtual default still gated by `canEditLayers`.
2. Sidebar: `git mv` `ChannelSidebar` to `LayerSidebar`, new hook signature, fix the four `PluginEditor`s and
   `mu-core`, `mu-clid`, `mu-tant` CMake lists (full Debug build, `mu-core` touched). Then drop the subclasses:
   mu-Toni (placeholder visual; `ArpMiniVisual`), mu-On, mu-Tant (delete `VoiceSidebar.*`, new
   `PatternMiniVisual`), mu-Clid last (delete `RhythmSidebar.*`; the editor uses `onLayerSelected` etc. in
   place of the `onRhythm*` aliases). The 30 Hz glyph timers must keep their `stopTimer()` in the destructor.
   Verify drag-reorder and the staged-swap badge in mu-Clid and mu-Tant by hand.
3. `LayerPanel` built and adopted in mu-Toni first (smallest `EnginePanel`: it already has header, modulator
   and `VoiceBand`), then mu-Tant (`VoicePanel` becomes `PatternContent` inside `LayerPanel`, which also
   delivers the `EnginePanel` family name through `LayerPanel` plus a content class, so the separate rename
   is not done), then mu-Clid (`RhythmPanel` keeps its drag-and-drop target and circle refresh as the content
   component; `VoiceSection` stays as the content's voice band). UI only: no `dsp-controller` pass or listening
   tests; run the unit tests and get the `ux-controller` sign-off on the three bands.
4. mu-On: sidebar only now. Its per-lane `GroovePanel` shows all lanes at once and is rewritten by the
   per-layer engine selector work (#1241); it adopts `LayerPanel` there, not before.

**Design-future check.** Easier: this is the one-framework direction (per-product sidebar and panel copies
become one class; product input is a mini-visual factory, a content component and a destination provider,
all keyed by layer index, so a combined instance can answer them per layer type without a change to either
class). Nothing here closes off the controller work (`mu-control` reads the same `ProcessorBase`
layer API) or the standard UI sizing (the three tokens are that standard). Risk to name: step 2 touches
`mu-core` and all four products at once, so it is one mechanical commit with no behaviour change; do not mix
the accessor rename (`getNumChannels` family to `getNumLayers`, done in its own commit 0338a05) into it.

### Stage 7 implementation plan (#1318) and Stage 8 owner decision (#1319)

### Stage 7 implementation plan (#1318; ruled 2026-10-12, Architecture Steward)

Builds on stage 6 as committed (29bb55d): mu-Tant, mu-Toni and mu-On run on `ProcessorBase` `addLayer` / `removeLayer` /
`swapLayers` / `resetLayer`, `LayerEditController`, `numVoices` / `voiceColours`. mu-Clid is the last product with its own
edit routine (`RhythmManager`, `proc.rhythms.*`), its own storage (`RhythmSequencer` holds `std::vector<Rhythm>`),
its own count (`numActiveRhythms`) and its own lock (`rhythmsLock`).

**Status (built, build 1407).** Steps 0 to 4 are done; step 5 is decided against (2026-10-13, see the step 5 row). As built: `RhythmManager` is gone; mu-Clid's
rhythms are `mu_core::Rhythm` layers on the shared container (`keepSpareLayers`, `numLayers`, `layersLock`); `RhythmSequencer` binds `Rhythm*` per slot and
keeps only per-slot audio state; the processor-side per-slot state is moved by the hooks in `mu-clid/Source/Plugin/PluginProcessor_Layers.cpp`
(`createLayer`, `layersReordered`, `layersMoved`, `syncSlotParams`); `numActiveRhythms`, `rhythmStore` and the `swapLayers` / `resetLayer` /
`canReorderLayers` overrides are deleted. `kAutomatedRhythms` stays: it is the count of rhythms with full "Rhythm N" automation names, not a live count.

**Amended 2026-10-19 (E6).** Everything above that keeps engines, play state and retired tails in processor-side arrays keyed by slot was superseded by the layer extraction (extraction-plan E5.3 to E5.7): those now live in `mu_clid::SampleRhythm` and the shared `RetireBin`, the processor-side engine, tail and play-state arrays are gone (`RhythmSequencer` stays in `mu-core`, shared with mu-Clid MIDI, as the pattern bookkeeping), `PresetIO` is split into `Persistence/RhythmPresetTree` plus `PluginProcessor_Preset.cpp`, and the processor holds no per-rhythm render glue. The structural-edit hooks, `layersLock` and the stager decision above are unchanged.
The MIDI product keeps its own `rhythmsLock` for its one fixed rhythm (ruling 4). The text below is the plan as ruled and is kept for the reasoning.

**Rulings that change the earlier stage 7 text.**

1. **No `mu_clid::Rhythm`.** mu-Clid uses `mu_core::Rhythm` (the `GeneratorAliases.h` alias stays), as the MIDI product does.
   The earlier plan put the voice engine, MIDI engine, retired tails, play state, sample path and previous-state flags into a
   derived layer. They stay processor-side, keyed by slot, as "Slot-keyed audio state stays processor-side" already rules for
   retired tails and UI-read atomics, because: `processCoreBlock` takes the engines as one contiguous
   `unique_ptr<VoiceEngine>` array; `installRhythm` retires the outgoing engine into a per-slot tail that must outlive the
   layer; `rhythmPlayState` and `modSnapshot` are read by the UI at 30 Hz by index and need a stable address (a mu-On
   `GrooveVoices` precedent). Putting an engine in the layer is exactly the `Layer::render` question of stage 8, which is not
   decided; stage 7 must not decide it by accident.
2. **`Rhythm` stays a copyable value** (the earlier plan made it non-copyable). `PendingRhythm`, `PreparedFullPreset`,
   `installRhythm`'s swap-in and `PresetIO`'s prepared rhythms all rely on that; it is only non-copyable if it holds an engine.
3. **`RhythmSequencer` stops owning rhythms.** It binds `Rhythm*` per slot (`bindRhythm(slot, Rhythm*)`, null = unbound), keeps
   `getRhythm(i)` / `getNumRhythms()` (so about 100 call sites do not change), keeps all its audio-private per-slot state
   (phase, step history, pending hand-over, pattern caches), and loses `addRhythm(const Rhythm&)`. `setNumRhythms(n)` sets the
   live count and clears the state of grown slots; it no longer resizes anything. `removeRhythm(i)` / `swapRhythmSlots(i, j)` become
   `removeSlotState(i)` / `swapSlotState(i, j)`: they move the per-slot state only (the pointer moves belong to
   `ProcessorCore`), and keep the lock contract (caller holds the layer lock with processing suspended). The slot state
   follows the rhythm on a reorder and on a remove, exactly as today; this is why `layersMoved` alone is not enough (below).
4. **The MIDI product owns its one `Rhythm` as a member of `MidiRhythmCore` and binds it to slot 0.** It does not adopt the layer container
   (one fixed layer, no edits, no `createLayer`); its `getLayer(0)` / `getRhythm()` return the same object as now. Its whole change is that
   constructor line and the CMake line for the new test, so "keeping the MIDI product working" is a step 1 property, proven before mu-Clid is touched.
5. **Two additions to `ProcessorBase`**, both additive with defaults that leave mu-Tant, mu-Toni and mu-On untouched:
   - `virtual void layersReordered(LayerEdit kind, int a, int b)` with `LayerEdit { Remove, Swap }`, called under `layersLock`
     immediately before `layersMoved` (remove: `a` = index removed; swap: `a`, `b`). It carries what `layersMoved(first, last)`
     cannot: whether the data at a slot moved from elsewhere. mu-Clid moves its per-slot audio state in it (the locked section
     of today's `RhythmManager::remove` / `swap`).
   - The parameter phase of the edits (phase 3: `copyLayerParams`, `swapLayerParams`, `resetLayerParams`) becomes a
     `virtual bool layerHoldsParamTruth() const` (default false) plus `virtual void syncSlotParams(int slot)`. When true the base
     skips the APVTS copy and calls `syncSlotParams(s)` for every slot the edit touched (the shifted ones, the vacated top slot,
     both exchanged slots, the reset slot), message thread, still suspended, not under `layersLock`. mu-Clid's override runs in its
     `apvtsLoading` guard: `pushRhythmToApvts(s)` and `pushMixerChannelToApvts(s)`, `sequencer.updatePattern(s)`, `voiceEngines[s]->setParams`,
     and frees engines parked under the lock. This is the "one allowed difference" of contract item 6.5, now a real hook.
6. **`Rhythm` gains `adoptDefaults(Layer&)`** (in `mu-core/Sequencer/Rhythm.cpp`, shared with the MIDI product, which does not call it):
   exchange `genA / genB / genC`, `logic`, `resetSteps`, `patternLegato` and the `RelaxedInt` output settings, then the base. Without it
   `resetLayer` would reset only the voice parameters and modulators. `createLayer(slot)` returns a default `Rhythm` named
   "Rhythm N+1" with the default genA; `layerAdded(index)` applies `_default.muRhythm` (what `applyDefaultRhythm` does today).
7. **`keepSpareLayers = true`**: all 8 `Rhythm`s are built in the constructor (the restore path writes data into slots above the live
   count before it sets the count, `PresetIO_HostState` does that today with `setNumRhythms(MaxRhythms)`), `canEditLayers()` true,
   `numLayers` replaces `numActiveRhythms`, `getNumActiveLayers` and `kAutomatedRhythms` read it, `rhythmsLock` is `layersLock`.
   mu-Clid does **not** call `captureComposedState` / `applyComposedState`, so it does not start saving `numVoices` / `voiceColours`:
   its saved data does not change (`r0_` prefixes, `PresetIO` shapes, `MuClidState`, `.muClid`, `.muRhythm` all stay).
8. **`HotSwapStager` stays as it is** (it keeps its own `mu_hotswap::Stager<PendingRhythm, ...>`). Moving it onto
   `layerSwapper` (step 5) was **decided against, 2026-10-13**: see the step 5 row. `installRhythm` changes only in that the swap-in lands in `*layers[r]`
   (an address that no longer moves) instead of the vector element.

**Steps.** Each is one commit, ends with a Debug build, the suites named, and a byte comparison of the listening renders against
a baseline taken before step 1. A step that fails its proof is reverted, not carried into the next.

| Step | Kind | What | Files | Proof |
|---|---|---|---|---|
| 0 | gate | Start from a clean tree: the in-flight `SequencerEngine` to `RhythmSequencer` rename (and `ChannelHeaderBar` to `LayerHeaderBar`) must be committed first, because step 1 edits the same files. Build `mu-clid-tests` and `mu-clid-midi-tests` explicitly. Render the baselines: `CLID_roundtrip`, the `swap` and `slot swap` hot-swap listening tests, and the retire-tail test. | none | all green; baseline WAVs kept for byte comparison |
| 1 | M | Sequencer storage decoupled, no behaviour change. `RhythmSequencer` binds `Rhythm*` per slot (ruling 3). mu-Clid temporarily owns `std::array<Rhythm, kMaxLayers> rhythmStore` and `RhythmManager` moves data between stored elements exactly as the vector did; the MIDI product owns and binds its one `Rhythm` (ruling 4). | `mu-core/Sequencer/RhythmSequencer.{h,cpp}`, `mu-clid/Source/Plugin/PluginProcessor.{h,cpp}`, `mu-clid/Source/Sequencer/RhythmManager.cpp`, `mu-clid/Source/Persistence/PresetIO.cpp`, `PresetIO_HostState.cpp` (the `setNumRhythms` sites), `mu-clid-midi/Source/Sequencer/MidiRhythmCore.{h,cpp}`, `mu-clid-midi/Source/Tests/MidiRenderTests.cpp` (construction sites), `mu-clid/Source/Tests/RhythmSequencerWrapTests.cpp`, `SwapCommitTimingTests.cpp`, `MidiOutputModeTests.cpp`, `NoDemoCapsTests.cpp` (they construct sequencers), a new `RhythmSequencerSlotTests.cpp` | `RhythmSequencerSlotTests` (bind; grown slot clean; `removeSlotState` and `swapSlotState` carry phase, step history and pending start with the rhythm); every existing sequencer test unchanged; `mu-clid-midi-tests` including `SessionStateTests` and `MidiRenderTests` (the MIDI product's whole proof); `CLID_roundtrip` byte-identical |
| 2 | M | `mu-core` additions, no product uses them yet: `layersReordered`, `layerHoldsParamTruth` / `syncSlotParams` (ruling 5), `Rhythm::adoptDefaults` (6). `LayerEdit` lives in `ProcessorBase.h`. | `mu-core/Plugin/ProcessorBase.{h,cpp}`, `mu-core/Sequencer/Rhythm.{h,cpp}` | `LayerOwnershipTests` / `LayerStructureTests`: a fake product that sets `layerHoldsParamTruth`, asserts `layersReordered` fires once per edit under the lock with the right arguments and before `layersMoved`, and that `syncSlotParams` visits exactly the touched slots; a `Rhythm::adoptDefaults` case (all euclid data exchanged, name and colour kept, no `ControlSequence` address changes); full Debug build of all five products (`mu-core` touched); `mu-core`, mu-Tant, mu-Toni and mu-On suites and their `*_roundtrip` unchanged |
| 3 | S | mu-Clid adopts the container. Constructor builds 8 `Rhythm`s through `createLayer`, `keepSpareLayers`, `canEditLayers`, `numLayers`; `rhythmsLock` to `layersLock` with the same try-lock plus the `layerEditInFlight` skip; the locked sections of `RhythmManager::add / remove / swap / reset` move into `layersReordered`, `layersMoved`, `layerAdded` and `syncSlotParams` with identical order (engines, MIDI engines, `parkPendingMidiOff`, retired tails and their drained flags, `setSlotIndex` on moved engines, `samples.swapPaths` / `clearPath`, `resetPlayState`, previous-state flags and euclid overrides reset); `swapLayers` / `resetLayer` / `canReorderLayers` overrides go; `PluginEditor` `onAddLayer` / `onRhythmDeleted` and the `RhythmContent` rename / reset calls move onto `LayerEditController` (hooks: unbind the modulator panel and `RhythmContent` pointers, then show the layer). `RhythmManager` is deleted; `rename` becomes a short processor method taking `layersLock`. | `mu-clid/Source/Plugin/PluginProcessor.{h,cpp}`, `PluginProcessor_APVTS.cpp`, `PluginProcessor_Modulation.cpp`, `PluginEditor.cpp`, `mu-clid/Source/Sequencer/RhythmManager.{h,cpp}` (deleted), `mu-clid/Source/Persistence/PresetIO.cpp`, `PresetIO_HostState.cpp`, `mu-clid/Source/Plugin/HotSwapStager.cpp` (`installRhythm` target, `checkBoundaries` count), `mu-clid/Source/Audio/SampleLibrary.{h,cpp}`, `mu-clid/Source/UI/RhythmContent.cpp`, `RhythmMiniVisual.cpp`, `mu-clid/CMakeLists.txt`, tests `ProcessorSeamTests.cpp`, `SwapCommitTimingTests.cpp`, a new `ClidLayerStructureTests.cpp` | `ClidLayerStructureTests` (fill to 8; remove the middle; swap ends; reset; assert for each: the rhythm's data, parameters, colour, mixer strip, sidechain source, sample path, engine pointer identity, retired tail and pending-swap cancel all follow the right slot; a removed slot's owed MIDI note-off still sounds; the last layer cannot be removed); the two-thread stress test from the shared `ProcessorSeamHelpers.h` (message thread edits while a loop calls `processBlock`: no crash, no allocation); every `mu-clid-tests` suite; `CLID_roundtrip` byte-identical; `swap`, `slot swap`, retire-tail renders byte-identical; a new scripted render with add, reorder and delete mid-play compared against the same script at the step 1 commit (audio equal except the intended difference, nothing at all if none); `mu-clid-midi-tests` |
| 4 | M | Tidy and docs. Delete `numActiveRhythms`, `rhythmStore`, `kAutomatedRhythms` if unused, the dead `RhythmManager` references in comments; `GeneratorAliases.h` keeps its aliases; update this doc, design-naming 4 / 5, `mu-clid/CLAUDE.md`. | the same files | `check-core-boundary`, `check-comment-refs`, full Debug build of all products |
| 5 | **Decided against, 2026-10-13** | Was: `HotSwapStager` onto `layerSwapper` (`PendingRhythm` as the payload, mu-Clid's swap-mode boundary rule as the callable). Not done and not planned. Reasoning: the shared behaviour is already shared, because `HotSwapStager` is built on `mu_hotswap::Stager` (the handshake and the loop-wrap predicates), so a move removes no second copy of anything. mu-Clid's payload is a prebuilt `Rhythm` + `VoiceEngine` (sample I/O done at stage time) plus a full-preset slot and engine retire tails, while `layerSwapper` is the `ValueTree` / APVTS-apply driver the other three products use; forcing the engine install through it would only move code and risk the swap timing that `SwapCommitTimingTests` and the hot-swap listening renders pin. Revisit only if a second product needs engine-carrying payloads (then generalise `LayerSwapper` first). | none | none |

**What stays per product (mu-Clid):** `VoiceEngine` / `MidiOutputEngine` arrays, retired tails and drain flags, `rhythmPlayState`,
`modSnapshot`, `lastEuclidOverrides` / `prevEuclidOverrides` / `prevMatrixHadAssignments`, the pending-parameter drain
(`pendingDirty`, `rawParams`, `suffixIndex`: keyed by APVTS slot, not by layer), `orphanOffs`, `SampleLibrary` paths, `HotSwapStager`,
`PresetIO`, the MIDI output modes, the euclid mod-target table. What goes: `RhythmManager`, `numActiveRhythms`, `rhythmsLock`, the `swapLayers` /
`resetLayer` / `canReorderLayers` overrides, `rhythmStore` (step 1 scaffold).

**Risks, and the answer to each.**
- *Address stability changes.* A `Rhythm` used to sit at a fixed vector index with data copied through it; now the object moves with the pointer, and a removed
  layer is destroyed after the unlock. Anything holding a `Rhythm*` / `ControlSequence*` across an edit dangles. Audit: `ModulatorPanel` and its
  bound `ControlSequence` pointers (the `LayerEditController` unbind hook), `RhythmContent`, `MidiOutSection`, the voice subsections, `EuclideanPanel::currentRhythm`
  (fetches per call, safe), `RhythmMiniVisual` (reads by index per tick, safe). `adoptDefaults` exchanges elements one by one for the same reason.
- *Allocation under the lock.* The locked section may move pointers and engines but never construct or free one: parked engines (removed slot, dropped tails) are released in
  `syncSlotParams` after the unlock; `updatePattern` also runs there, not in `layersMoved`.
- *Lock miss.* On a failed try-lock the block renders silence and must still advance the transport and boundary detection (hot-swap principle 4). Today mu-Clid's
  transport derivation sits before the lock; keep that order and confirm the sequencer's wrap detector does not need the block.
- *CLAP does not honour `suspendProcessing`.* `layersLock` plus `layerEditInFlight` is the barrier; the audio path must test both.
- *Colour choice differs slightly.* The base picks the first unused palette entry; mu-Clid's `onAddLayer` probed from `count % palette`. The family rule wins (contract item 6.6),
  a visible difference in some add / delete sequences: `ux-controller` to confirm.
- *The MIDI product breaks if step 1 or 2 changes `Rhythm` semantics.* It shares `mu_core::Rhythm` and `RhythmSequencer`; `mu-clid-midi-tests` runs after every step, and the product has no layer edits to exercise.
- *Not in the default build.* `mu-clid-tests` and `mu-clid-midi-tests` are built on demand: build them by target name, or a green full build proves nothing about them.

**Unattended or supervised.** Not one pass. Steps 0 to 2 are safe unattended: they are mechanical, additive and proven by existing byte-identical renders and
unit tests, and nothing in mu-Clid's audio thread changes behaviour. Step 3 changes the audio thread's lock, the order of an edit that moves audio state and the
retire-tail; it can be attempted unattended only as a local, unpushed commit with `Refs` (never `Closes`), and only if every proof listed passes, otherwise reverted to
the step 2 commit. Steps 4 and 5 follow a supervised look. **`dsp-controller` reviews after step 3 and before anything is pushed or closed**, and must be given: the
`layersLock` try-lock and `layerEditInFlight` skip in `processBlock`; `RhythmSequencer` slot-state moves in `layersReordered` (phase, `pendingKind`, `patternLock`
order against `layersLock`); engine, MIDI engine, retired-tail and drain-flag shifts, and the owed note-off path (`parkPendingMidiOff`, `orphanOffs`); that nothing is
constructed or destroyed under the lock; the `Rhythm*` rebinding in `bindRhythm` is done under the lock and the audio thread reads the slot table only inside it; the
transport and wrap detection on a lock miss; `installRhythm` landing in `*layers[r]` (the swap must not allocate).

**Design-future check.** Easier: mu-Clid on the same container makes every product's edits one routine, which the combined instance needs; the sequencer no
longer owning its layers is the shape a registry wants (a layer type supplies data, the host owns it). Not closed off: the engine stays where stage 8 can still decide
whether it moves into the layer. Harder: nothing. The new hooks are generic (`layersReordered`, `syncSlotParams`) and have no mu-Clid names.

### Stage 8: what the owner decides (#1319; 2026-10-12, Architecture Steward)

**Answered 2026-10-15:** the combined instance is scheduled as the fifth product mu-Tronic, so option C is taken, staged product by product; see
[mu-Tronic: the combined instance is scheduled](#mu-tronic-the-combined-instance-is-scheduled-ruled-2026-10-15) and [mu-tronic/design.md](mu-tronic/design.md).
The text below is kept for the reasoning.

The row waited for the saved-data renames (#1263); those are done (closed at build 1322), so that dependency is gone. What remains is not a technical question. The
registry (`LayerTypeInfo`: id, name, prefix, preset extension, mod-target table, `create`) and `Layer::render` (the layer draws itself into the mixer, so a session can mix
layer types) have exactly one customer: the combined instance of [design-future.md](design-future.md) ("One framework, one instance"), which is still unscheduled and
whose own open questions are the owner's. The docs already settle the technical order (registry only when two layer types can share an instance; the registry
constructs and describes and never dispatches; dispatch stays virtual). They do not settle whether to build it.

Options:
- **A. Park until the combined instance is scheduled (recommended).** Move the row to On Hold with a one-line note. Costs nothing, loses nothing: stage 7 deliberately keeps
  engines processor-side so `Layer::render` can still take them either way. A `typeId` and `LayerTypeInfo` built now would have one type per product and no way to be tested
  against a second, so the shape would be guessed.
- **B. Build only the describing half now:** `Layer::typeId()` virtual plus a registry that constructs and lists types, with the saved `Slot` node gaining a `type` attribute
  (absent = the product's own). Small and additive, no audio-thread change. Trade-off: dead code until a second type exists, and the risk of choosing the wrong fields.
- **C. Full stage 8:** B plus `Layer::render`, moving each product's engine into its layers and replacing the mixer's `RenderChannelFn`. Large, touches the audio thread of every
  product, reverses the processor-side engine ruling above, and cannot be finished honestly without the owner's answers on licensing and product shape.

Question for Andy (one paragraph, for the morning): do you want the combined instance scheduled, and if so is it a fifth product or a mode of an existing one, and how do
layers of the Pay What You Want products (mu-Toni, mu-On) and the freeware mu-Clid MIDI rhythm sit inside one licensed instance, and whose key covers it? Until you answer, #1319 should stay parked (option A); the answer decides where the registry
lives (which product's processor hosts it), whether the plugin ID and licence of the combined instance are new, and so whether option B or C is next. No work on it is useful
before that, and nothing that is done on stages 6 and 7 makes the answer harder.

### Order of work for the variable-layer adoption (#1240, #1241) and what was left under #1265 / #1264

#### Order of work for #1240 and #1241 (ruled 2026-10-12)

Each step ends with a Debug build, that product's unit suites, `mu-core` suites, and the `*_roundtrip` listening
tests; mu-Clid is not edited (its `canEditLayers()` stays false and it does not call `captureComposedState`), but
`mu-clid-tests` is built and run after steps 1 and 2 because they touch `ProcessorBase`.

1. **`mu-core` + mu-Tant (no behaviour change):** the count / colour persistence above, mu-Tant switched over (its
   `numVoices` / `voiceColours` lines go; `NoDemoCapsTests`, `PresetRoundTripTests`, `TANT_roundtrip` unchanged and
   green is the proof); `remapLayerReferences` with a `LayerOwnershipTests` case (fake product, three layers, remove the
   middle, assert the references follow and the gone one maps to -1); `unsavedLayerCount` default 1.
2. **`LayerEditController`** (the editor glue, above), mu-Tant moved onto it. Owned by the UX ruling; mu-Toni and mu-On
   cannot ship the add / delete UI without it.
3. **mu-Toni (#1240)**, then **4.** **mu-On (#1241)**. mu-Toni first: it has no engine change, so it proves the adoption
   path (8 layers of parameters, try-lock in `processBlock`, `layersMoved`, count persistence) alone. mu-On then adds
   only what is its own (engine selector, engine-qualified parameters, per-slot step rows, Rumble source).

**mu-Toni recipe (the same eight points mu-Tant satisfies; nothing else is new):**
(a) `kNumChannels` is deleted; parameters (`a{N}_*`, `ch{N}_*`), `vp` / `modDestAtoms` pointers and mixer strips are
declared and cached for all `mu_limits::kMaxLayers` slots; the `aud = (i == 0)` default (layers 2 and up silent) goes,
because a layer added by the user must be audible at its defaults (to `ux-controller` if it objects);
(b) `keepSpareLayers = true`, all 8 `Arp`s built by `createLayer(slot)` (which sets the shared `bank` on the runner,
`insert->setSlotIndex(slot)` and prepares it), `canEditLayers()` true, `numLayers` 1, `unsavedLayerCount` 4, colour
`i` for slot `i`; `getLayerColourIndex` is the base default over `Layer::colourIndex`; `getMaxLayers` override goes;
(c) `processBlock` takes `ScopedTryLock(layersLock)` once, skips the render while `layerEditInFlight`, loops
`readVoice` / `render` over `numLayers` only, and still runs `flagSwapBoundaries` when the lock misses (mu-Tant's shape);
(d) `layersMoved(first, last)` resets the runner, insert and any held-note or accent state of each moved slot, no
allocation; (e) `prepareToPlay` prepares all 8 runners and inserts under `layersLock`; (f) `setupLayerState` uses
`kMaxLayers` prefixes and `legacyLayerParamPrefixes(kMaxLayers)`; (g) the editor wires `onAddLayer` / delete through
`LayerEditController` and calls `layerPanel.setFixedLayers(false)`; (h) tests: Toni `ProcessorSeamTests` (count now 1 fresh),
a `LayerStructureTests`-style case (fill to 8, remove middle, swap, parameters / colour / mixer strip follow), the
two-thread stress test copied from mu-Tant's seam tests into the shared `ProcessorSeamHelpers.h` rather than a second
copy, an old 4-layer composed state and an old APVTS dump both loading as 4 layers, a state with `numVoices` 6 round trip.

### What is left under #1265 and #1264 (ruled 2026-10-12)

State of the tree: stages 1 to 3 and 5 are done for mu-Tant, mu-Toni and mu-On, stage 4 for those three; the
`Slot` to `Layer` renames are done (`voiceSlots`, `kMaxChannels`, `kMaxVoices` no longer exist). Open:

1. **#1265 closes after three small steps** (it is the hoisting umbrella): (a) `SlotBound<T>` and
   `Layer::insertGainReduction()` in `Layer.h`; (b) mu-Tant's `inserts` array becomes `Pattern::insert`
   (`SlotBound<InsertProcessor>`), mu-Toni's `Arp::insert` is wrapped the same way and both override
   `insertGainReduction()`, the editors reach the meter through one `ProcessorBase::getInsertGR(i)`; (c) `slotLayer`
   is replaced by the base default (`keepSpareLayers ? layers[i].get() : getLayer(i)`) and mu-Tant's override goes.
   Not in scope, by ruling: `typeId` and `Layer::render` (stage 8, #1319), `resetToDefaults` (the shipped
   `adoptDefaults` does this job), the clip bank (#1277), hot-swap staging state (done: `LayerSwapper`), the mixer
   binding (slot index is the binding; nothing to hoist).
2. **#1264 closes after:** mu-Tant's remaining per-slot arrays (`voices`, `voiceSnap`, `retiring`) are **not**
   moved into `Pattern` (ruled 2026-10-12; "part A as written" is dropped, see "Slot-keyed audio state stays
   processor-side" below). They are gathered into one processor-side `std::array<SlotState, kMaxLayers>`
   (`engine`, `retiring`, `snap`), a tidy-up that is optional and needs no review beyond the DSP check
   (`blkPatternBeats` is a per-block audio-thread cache and stays in the
   processor); the sequencer role names `Arpeggiator` to `ArpSequencer` (done), and `SequencerEngine` to
   `RhythmSequencer` (done 2026-10-12, ahead of stage 7, as a pure rename);
   `GrooveSequencer` already conforms and mu-Tant has no sequencer class to rename (`GatePattern` is data and stays);
   the Tant `voice` vocabulary: non-UI identifiers (`currentVoice`, `numVoices`, locals) now, `VoicePanel` /
   `VoiceSidebar` with the shared `LayerSidebar` / `LayerPanel` work (#1505 / #1506; the per-layer sound panel is
   now `<Type>Content` inside `LayerPanel`, superseding the earlier `EnginePanel` choice; see the ruling below);
   mu-On's lane / channel wording to `Track` is **folded into #1241**, which rewrites the same code (233 uses),
   not done on its own. The step grid and `GrooveVoices` stay in the processor until #1241 decides the per-layer engine selector.
3. Stage 6 no longer waits on Andy: confirmed 2026-10-12, a fresh mu-Toni instance has 1 layer, old 4-layer states load
   as 4, no demo cap (the demo cap is retired; mu-Toni is a Pay What You Want product). Order of work: "Order of work for #1240 and #1241" above.

### Layer ownership: stages 1 to 8 table and owner decisions for stage 6

### Stages (each: one product at a time, Debug build, unit tests, round-trip listening tests; macOS untouched)

M = mechanical (unattended is safe), S = supervised (audio thread / lock change: run the listening tests
on the build PC), O = needs the owner.

| # | Kind | What | Exact content | Guard |
|---|---|---|---|---|
| 1 | M | Derived types as value members, no behaviour change | **Tant**: `Pattern : Layer` holds `gate`, `filter`, `pitch` (`GatePattern`), `ring` (`VoiceRingBuffer`), `snap[]`, `InsertStage`, the retiring state, the user-wavetable path / index per oscillator, the voice engine pointer; `std::array<Pattern, 8> patterns` replaces `voiceSlots`, `gatePatterns`, `filterPatterns`, `pitchPatterns`, `voiceRingBuffers`, `voiceSnap`, `inserts`, `retiring`, `voiceColourIndex`, `osc*User*`. `Pattern::copyFrom(const Pattern&)` replaces the `copyDataFrom` calls. **Toni**: `Arp : Layer` holds `ArpVoiceRunner`, `InsertStage`, `vp` pointers, `modDestAtoms`; replaces `runners`, `inserts`, `insCfg`, `vp`, `modDestAtoms`, `voiceSlots`. **On** (built 2026-10-12): `Track : Layer` holds the trigger counter, its `StepRow` and its Rumble envelope (data the layer owns, so it moves with the pointer; `StepPattern` only binds each slot to the track's row, rebound in `layersMoved`); `GrooveVoices` (engines per slot, one object, heap-allocated) stays in the processor. **Clid**: nothing; `Rhythm` already derives. | Each product's unit tests; its `*_roundtrip` (sample-identical); `check-core-boundary` |
| 2 | M | `ProcessorBase::getLayer(int)` accessor (virtual, returns the product's value-array slot) | Default `getLayerName` / `getLayerColourIndex` come from `Layer::name` / `colourIndex`, built with the names the products show today ("Voice N" for mu-Tant, "Layer N" for mu-Toni, lane names for mu-On, the rhythm name for mu-Clid), so nothing visible changes; the editors' `voiceSlots[sel]` / `voiceSlot(lane)` become `getLayer(sel)`; the GR-meter pointer helpers (`getInsertGRPtr`, `getInsertGRReductionPtr`) become `getLayer(i)->insertGainReduction()` | unit tests; boundary check; build all four |
| 3 | M | Persistence virtuals | `Layer::writeExtras` / `applyExtras` (base: modulators); each product's `LayerExtras` lambdas become the virtual overrides on `Pattern` / `Arp` / `Track`; `initLayerState(prefixes)` builds the `LayerExtras` that calls `getLayer(slot)`. mu-Clid keeps `PresetIO` shapes and gains nothing here. | `MuTantPersistTests`, `ModulatorSerialiseTests`, `PresetRoundTripTests`, `PresetXMLRoundTripTests`, `TONI_roundtrip`, `ON_roundtrip`, `TANT_roundtrip` |
| 4 | S | Ownership moves to `ProcessorBase` | The `layers` array, `numLayers`, `layersLock`, `createLayer`, `layerAs<T>`; Tant first (its `numVoices` / `voicesLock` become these, `bankLock` split out), then Toni and On at a constant count (4 / 5) with the try-lock added so a later edit is safe; the product's `addVoice` etc. still exist and now move pointers instead of copying data. | as stage 3, plus a new `LayerOwnershipTests` (construct, publish, destroy off-lock) and a listening render with structural edits scripted mid-play |
| 5 | S | Central edits and the generic stager. **Part 1 done (commit a41c5fd):** `addLayer` / `removeLayer` / `swapLayers` / `resetLayer` in `ProcessorBase` with the hooks `createLayer`, `canEditLayers`, `cancelPendingSwap`, `Layer::onMoved`; mu-Tant adopted (its `addVoice` / `removeVoice` / `swapVoices` / `resetVoice` are gone). Deviations: mu-Tant keeps all 8 `Pattern`s allocated (add / remove reset and re-use a slot rather than creating / destroying one); no `typeId` yet (stage 8); no `onLayersChanged` hook; mu-Clid adopts with stage 7 (`RhythmManager` stays until then). **Part 2 done (commit 8a8be28, ruled 2026-10-11):** the stager storage `mu_hotswap::Stager` stays a template in `mu-core/Plugin/HotSwap.h`; the generic piece is `mu_hotswap::LayerSwapper<Payload, N>` (the old `BarLineSwapper` generalised: the boundary rule is a callable giving the loop length in beats per slot), held by `ProcessorBase` as `layerSwapper` (a `ValueTree` instance, `N = kMaxLayers = 8`) with default `hasPendingSwap` / `hasPendingFullPreset` / `cancelPendingSwap` over it and `flagSwapBoundaries` for the audio thread. mu-Tant's `VoiceHotSwapStager` is deleted and the Toni / On `BarLineSwapper` members are dropped (they now call `flagSwapBoundaries` with the default bar rule); mu-Tant's boundary rule is `mu_tant::swapLoopBeats`; mu-Clid keeps its `PendingRhythm` stager until stage 7. Deviations: (a) N is 8 in Toni and On too (was 4 / 5), so the stager is one size family-wide; (b) `onPresetSwapCommitted` now fires once, after all applies in a commit, rather than between them; (c) mu-Tant decided stage-or-apply itself on `isInternalPlaying()` at first; it now follows the family rule through `useFull` / `useSlot` (see the Hot-swap section). || new `LayerStructureTests`: fill to 8, remove the middle, swap ends, reset; assert parameter values, colour, mixer channel, sidechain source and pending-swap cancel all follow; a two-thread stress test (message thread edits while a loop calls `processBlock`, no allocation, no crash); every `*_roundtrip` and the hot-swap listening tests (`swap`, `slot swap`) |
| 6 | O | Variable layers in mu-Toni (#1240) and mu-On (#1241); the clip bank (#1277) | See owner decisions below. The clip bank is `Layer::clips`: a clip is a stored layer-preset tree, launching it is a staged layer swap (Stage 5), and a pad never writes a preset. Ruled and staged in "Clip bank" below. | tests per feature, written with the decisions |
| 7 | S | mu-Clid into the container | **Amended 2026-10-12 (Architecture Steward): the ordered plan is "Stage 7 implementation plan (#1318)" below; read that, not this cell.** **Amended again 2026-10-19 (E6, layer extraction): the "engines stay processor-side" ruling is reversed.** `mu_clid::SampleRhythm : mu_core::Rhythm` now owns its `VoiceEngine`, `MidiOutputEngine`, play state, modulation snapshot, sample path and retire tail; mu-Clid renders only through `renderAndMix`, retired engines sound out as `mu_clid::EngineTail` in the shared `RetireBin`, and `mu-clid-layer` is an INTERFACE library. What Stage 7 built and still stands: `mu_core::Rhythm` used directly, `RhythmManager` deleted, `layersLock`, `HotSwapStager` kept for the standalone. Detail: extraction-plan 2.5 and the E5 notes. Original Stage 7 scope that stands: `RhythmSequencer` stops owning the rhythms and binds `Rhythm*` per slot; `rhythmsLock` becomes `layersLock`; `RhythmManager` is deleted. Highest risk of the plan (about 1580 uses, the retire-tail), so four steps, each its own commit. | `mu-clid-tests` (build the target explicitly, it is not in ALL), `mu-clid-midi-tests`, `CLID_roundtrip`, the retire-tail and hot-swap listening tests, a scripted structural-edit render |
| 8 | O | Registry and `Layer::render` | With the combined instance. The saved-data renames it waited for (#1263) are done. Until the owner schedules the combined instance the mixer's `RenderChannelFn` stays product-side, and nothing is built for this stage; see "Stage 8: what the owner decides (#1319)". | with that work |

Stages 1-3 are order-independent between products (any product can go first). From stage 4 on, do Tant first
(it already has the dynamic count and the lock), then Toni, On, and Clid last.

### Owner decisions for Stage 6

- **mu-On (#1241):** its parameters were prefixed by *lane type* (`k_ b_ h_ s_ r_`); since 2026-10-12 they are
  index-based (`t0_` ... `t4_`, #1263 step 2, with legacy-state and MIDI-map migration), which a variable
  count with a per-layer engine selector needs. **Settled by Andy 2026-10-11: (b), a variable lane count on the
  index prefixes** (the alternative, five fixed lanes, is dropped). Open sub-points from #1241 that need a
  design before the work: which kick a Rumble layer processes (Andy 2026-10-08: a Source selector, default the first
  Kick), per-layer params (each layer keeps every engine's settings), step rows per layer, and how 5-lane
  presets map to layers (they load as their 5 layers).
- **mu-Toni (#1240):** `kNumChannels` is 4 today. Going to 8 adds layers 5-8 parameters (additive, old sessions
  load with the new layers at defaults) and changes the host's automation list. Default count and demo cap
  **confirmed by Andy 2026-10-12:** fresh instance 1 layer, no cap.
- **Both:** the add / delete / reorder UI (it is the shared `ChannelSidebar`, so it is a visible change for the
  freeware and licensed products alike) and whether delete fades or cuts (item 7 says cut).

### Stage 33 (complete, build 368) and the mu-tant plugin structure plan

## Stage 33 (complete — build 368)

> **Historical snapshot.** The paths below are the original single-tree (`Source/…`) layout as Stage 33 left it. They have since been relocated to the `mu-core/` + `<product>/Source/` monorepo layout (see "Current shared / product-specific split" above for the authoritative current locations) and the `MuClidLookAndFeel` shim was removed. This section is kept for the decision history.

### #258 — `mu-core` CMake INTERFACE library

`add_library(mu-core INTERFACE)` in `CMakeLists.txt` propagates shared source files via `target_sources(mu-core INTERFACE ...)`. Both `mu-clid` and `mu-clid-lite` call `target_link_libraries(... mu-core)`. INTERFACE chosen over STATIC because JUCE's CMake module-initialisation macros are propagated only via `juce_add_plugin`'s INTERFACE targets — a STATIC library that doesn't link them would fail with "No global header file was included!".

**Shared (mu-core):**
```
Source/Audio/VoiceEngine.{h,cpp}
Source/Audio/VoiceParams.{h,cpp}
Source/Audio/InsertProcessor.{h,cpp}
Source/Audio/MixerEngine.{h,cpp}
Source/Audio/MultiModeFilter.{h,cpp}
Source/Audio/SamplePlayer.{h,cpp}
Source/FX/**
Source/Modulation/**
Source/UI/Components/**   (includes MuLookAndFeel.h/.cpp)
Source/UI/FXRow.{h,cpp}
Source/UI/MixerChannel.{h,cpp}
Source/UI/MixerOverlay.{h,cpp}
```

**mu-clid only:**
```
Source/Sequencer/RhythmSequencer.{h,cpp}
Source/Sequencer/Layer.h              ← base struct for shared voice data
Source/Sequencer/Rhythm.{h,cpp}
Source/Sequencer/HitGenerator.{h,cpp}
Source/UI/EuclideanPanel.{h,cpp}
Source/UI/RhythmPanel.{h,cpp}
Source/UI/RhythmCircle.{h,cpp}
Source/UI/RhythmSidebar.{h,cpp}
Source/UI/ModMatrixPanel.{h,cpp}
Source/UI/ModulatorEditor.{h,cpp}
Source/PluginProcessor.{h,cpp}
Source/PluginEditor.{h,cpp}
```

### #259 — `Layer` base struct (done)

Non-Euclidean members extracted from `Rhythm` into `Source/Sequencer/Layer.h`. Explicit copy constructor and assignment operator provided because `std::atomic<bool>` is non-copyable:

```cpp
// Sequencer/Layer.h
struct Layer {
    static constexpr int MaxControlSequences = 8;
    VoiceParams                   voiceParams;
    std::vector<ControlSequence>  controlSequences;
    ModulationMatrix              modulationMatrix;
    mutable std::atomic<bool>     modLock { false };  // audio thread try-locks; message thread spins
    juce::String                  name;
    int                           colourIndex = 0;
    // explicit copy ctor/assignment defined (atomic members are non-copyable)
};

// Sequencer/Rhythm.h
struct Rhythm : public Layer {
    HitGenerator genA, genB, genC;
    // ...
};
```

`RhythmSequencer` stores `std::vector<Rhythm>`. mu-tant's engine will store `std::vector<Layer>` or a derived type.

### #260 — `ProcessorBase` shared skeleton (done)

`Source/ProcessorBase.{h,cpp}` — `PluginProcessor` now inherits `ProcessorBase` instead of `AudioProcessor` directly. `ProcessorBase` owns `fxChain` and `mixerEngine` (both `public`) and provides `processCoreBlock()` which calls `fxChain.setHostBpm()` then `mixerEngine.processBlock()`. mu-tant's future processor will extend the same base.

### #261 — `MuClidLookAndFeel` → `MuLookAndFeel` (done)

`Source/UI/Components/MuLookAndFeel.{h,cpp}` holds the renamed class. `MuClidLookAndFeel.h` is a backward-compat shim:

```cpp
#include "MuLookAndFeel.h"
using MuClidLookAndFeel = MuLookAndFeel;
```

All 58 existing `#include "MuClidLookAndFeel.h"` sites compile unchanged via the shim. `CMakeLists.txt` compiles `MuLookAndFeel.cpp` via `mu-core`.

---

## mu-tant plugin structure (Stage 34+)

A new `mu-tant/` plugin directory can be created. It will:

1. Have its own `CMakeLists.txt` that adds a JUCE plugin target and links `mu-core`
2. Provide its own `PluginProcessor` (extending `ProcessorBase`)
3. Provide its own `PluginEditor` and voice-engine-specific UI panels
4. Reuse `MixerOverlay`, `FXRow`, `ModulatorEditor`, `ModMatrixPanel` directly from mu-core

The shared visual identity (`MuLookAndFeel`) ensures a consistent look across all μ plugins without duplicating any styling code.
