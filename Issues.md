# Issues

## All Issues

| # | Description | Status | Fixed Build |
|---|---|---|---|
| 1 | Delay time — both sync and free modes do not appear to work | ✅ Fixed | ≤80 |
| 2 | Delay feedback does not appear to work | ✅ Fixed | ≤80 |
| 3 | While playing, turning the Steps knob in Euclid B triggers the sample | ✅ Fixed | ≤80 |
| 4 | Delay feedback knob range should be 0–100 (not 0–0.95) | ✅ Fixed | ≤80 |
| 5 | EFX knob ranges are not user-friendly (e.g. Resonance shows 0–1 instead of a meaningful scale) | ✅ Fixed | ≤80 |
| 6 | EFX output is very loud — gain staging needs to be corrected to produce a usable output level | ✅ Fixed | ≤80 |
| 7 | Hits should fire when the step passes the 12 o'clock position | ✅ Fixed | ≤80 |
| 8 | On the sequencer page, the drive insert effect doesn't do anything | ✅ Fixed | ≤81 |
| 9 | Accent ring (Ring C) steps are drawn as lots of lines instead of filled blocks | ✅ Fixed | 82 |
| 10 | High-pass filter produces no sound when resonance is set to 0 | ✅ Fixed | ≤81 |
| 11 | VU meters are very slow to respond | ✅ Fixed | 82 |
| 12 | Rhythm rename does not propagate — name updates in panel header but not sidebar item or control bar; switching rhythms shows stale name in header | ✅ Fixed | 103 |
| 13 | Euclid C padding/insert controls — design updated; C intentionally shares the full A/B parameter set | ✅ Design resolved | 82 |
| 14 | Accent system not implemented — Ring C coincidence detection missing in SequencerEngine; `isAccented` flag not passed to VoiceEngine; Accent knob (0–+12 dB) not present in VoiceSection Amp config row; `accentDb` APVTS param not wired | ✅ Fixed | 104 |
| 15 | Label abbreviations violate "full words everywhere" design principle — `"Res"` → `"Resonance"`, `"Semi"` → `"Semitone"`, segment modes `"I"/"M"` → `"Ignore"/"Mute"` / `"Pad"/"Mute"`, Delay modifiers `"Str"/"Dot"/"Tri"` → `"Straight"/"Dotted"/"Triplet"` | ✅ Fixed | 103 |
| 16 | VoiceSection status bar labels use abbreviations throughout — "Pitch Atk/Dec/Sus/Rel", "Filt Env Atk/Dec/Sus/Rel/Dep", "Amp Atk/Dec/Sus/Rel", "Filter Res", "Drive Out" | ✅ Fixed | 103 |
| 17 | Amp FX send knobs (Effect, Delay, Reverb) on the Voice Amp column row not implemented — specified in Stage 10 design | ✅ Fixed | 103 |
| 18 | Euclid C padding controls decision — resolved: design updated to include full A/B padding set on C | ✅ Design resolved | 82 |
| 19 | Euclid C accent controls — design resolved: accent = Ring C coincidence with A+B hit, boosted by `accentDb` knob in Amp panel. Advance Mode and Accent Velocity descoped from v1. Tracked under #14. | ✅ Design resolved | 82 |
| 20 | TimeSelector component in `Source/UI/Components/` appears to be dead code (replaced by DropdownSelect) — confirm unused and remove | ✅ Fixed | 103 |
| 21 | TransportBar Loop/Sync dropdown — verified: current dropdown correctly controls master loop length (`mstrLoop` param, "Loop Off" / "N steps"). Design intent "Host Sync / Reset on Play / Free Running" describes a separate sync mode control not yet implemented — deferred to a later stage | ✅ Design resolved | 103 |
| 22 | Intra-FX return send knobs (Effect→Delay, Effect→Reverb, Delay→Reverb) — wiring to APVTS params `"eff2dly"`, `"eff2rev"`, `"dly2rev"` not verified end-to-end | ✅ Verified | 103 |
| 23 | Settings Overlay controls not audited — review all panels (Visual, Sequencer, Performance, Voice, Gain, Presets, Standalone) against design | ✅ Audited | 103 |
| 24 | Reverb: replace `juce::Reverb` (Freeverb/Schroeder) with Signalsmith FDN reverb — Freeverb produces metallic flutter on percussive sources; Signalsmith uses rotated-Hadamard FDN with per-line modulation | ✅ Fixed | 105 |
| 25 | Delay: add fractional interpolation and smooth delay time changes — current integer read pointer produces clicks/artefacts on BPM change or LFO modulation; Hermite cubic interpolation + 50ms parameter smoothing required | ✅ Fixed | 109 |
| 26 | Flanger: implement through-zero flanging — current min delay is 0.5ms so the zero-crossing never occurs; fix: delay dry signal by `baseSamp`, sweep wet path from 0 to `2*baseSamp` | ✅ Fixed | 109 |
| 27 | Phaser: fix LFO-to-allpass-coefficient mapping — current linear sweep clusters notches at high frequencies; correct mapping: convert LFO → Hz, then `a = (1 - tan(π*f/sr)) / (1 + tan(π*f/sr))` | ✅ Fixed | 109 |
| 28 | Chorus: upgrade from linear to Hermite cubic interpolation, add per-voice LFO rate detuning (±0.5–2%) for organic evolution vs. fixed-period modulation | 🔴 Open | — |
| 29 | Drive/Waveshaper: implement ADAA (Antiderivative Anti-Aliasing) — current `tanh` aliases without oversampling; ADAA formula: `(ln(cosh(x[n])) - ln(cosh(x[n-1]))) / (x[n] - x[n-1])` gives 8–16× equivalent at near-zero CPU cost | 🔴 Open | — |
| 30 | Bitcrusher: add pre-filter anti-aliasing and TPDF dither — add LP at new Nyquist before sample-hold, add triangular dither `x + (r1-r2)*lsb` before quantisation | 🔴 Open | — |
| 36 | Mixer VU meters read pre-fader — master, Effect, Delay, and Reverb return VUs captured before their respective gain stages so faders had no effect on the meters; inactive channel peaks not cleared when rhythms removed | ✅ Fixed | 108 |
| 31 | Mixer page FX knob sizing — Reverb row knobs are larger than Effect and Delay row knobs; all three rows must use the same knob size and be vertically aligned with each other | 🔴 Open | — |
| 32 | Mixer page FX section panel — the three effect rows (Effect/Echo, Delay, Reverb) should sit inside a dedicated container panel, with each row inside its own sub-panel | 🔴 Open | — |
| 33 | Sequencer page Pitch/Filter envelope depth — the Depth knob currently sits in the envelope (bottom) row; move it to the config (top) row for both Pitch and Filter columns so the envelope row only contains ADSR | 🔴 Open | — |
| 34 | Sequencer page master loop dropdown too narrow — the TransportBar loop/length dropdown clips its text; it should be wide enough to always display the full selected option without truncation | ✅ Fixed | 109 |
| 35 | Sequencer page modulation section dropdowns too wide — the Step and Loop dropdowns in the modulator timing row are over-sized; shrink to fit their content and left-align all controls on that row | ✅ Fixed | 109 |
