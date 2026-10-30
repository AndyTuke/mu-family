# mu-Tilate: engine, detection and tempo

Status: design only, nothing built (seeded 2026-10-30 from the first wireframe). Product overview: [design.md](design.md); panels: [design-ui.md](design-ui.md). Shared pieces named here are built in `mu-core` (see design.md, "Shared pieces").

### Transient detection ★

**Research summary (to be re-checked at design time).**
- *Detection function:* spectral flux (Bello et al. 2005, "A Tutorial on Onset Detection in Music Signals"; Dixon 2006, "Onset Detection Revisited") is the standard general-purpose choice. **SuperFlux** (Böck and Widmer 2013) adds a max-filter over the frequency axis and a lag, which suppresses false onsets from vibrato and tonal material. An optional cheap **energy-envelope** mode suits pure drum loops.
- *Adaptive threshold:* a moving median or mean of the detection function plus an offset, so quiet and loud passages are judged relatively.
- *Peak picking:* local maxima above the threshold, with a minimum gap between picks.
- *Slice start:* placed slightly before the peak (pre-roll) and snapped to the nearest zero crossing, so the attack is kept and the cut does not click.

**Controls** (all shared knobs / segment controls):
- *Method:* Transients (spectral flux), Soft-tonal (SuperFlux), Even divisions of N (no analysis); manual editing is always available on top.
- *Sensitivity:* the threshold offset.
- *Min gap:* no slices closer than this.
- *Low-cut / High-cut:* the frequency band the detector looks at (for example ignore sub bass, or listen to hats only).
- *Pre-roll:* how far before the peak each slice starts.

**Behaviour.**
- Detection runs on a **worker thread** (never on the audio thread) when the loop loads or a control changes.
- The **detection curve is cached**: moving Sensitivity (and Min gap) only re-picks peaks from the cached curve, so markers update live while the knob moves. Changing Method, band or loop re-computes the curve.
- Manually moved or added markers are pinned and stay put on re-detect; auto markers are replaced.
- **Implementation plan:** write the detector in-house on `juce::dsp::FFT` (about 200 lines), no new dependency.

### Tempo matching and time-stretch
- **Modes (per layer):** Off, Re-pitch (varispeed, cheap, pitch moves with tempo), Stretch (pitch preserved).
- **Library for Stretch: Signalsmith Stretch (MIT, header-only), decided (Andy, 2026-10-30).** Rubber Band (GPL / commercial) and SoundTouch (LGPL, weaker on drums) are rejected. The only licence obligation is to include Signalsmith Stretch's copyright and MIT notice. The family already ships third-party notices in `THIRD_PARTY_NOTICES.txt` (copied into every release package and installer) and credits them in the About overlay; the Signalsmith reverb headers live in `ThirdParty/signalsmith-basics/`. Add Signalsmith Stretch to `THIRD_PARTY_NOTICES.txt` and the About credits, and keep its headers beside the others in `ThirdParty/`. Andy's own licence is unaffected. The stretch engine is product-independent and lives in `mu-core`.
- **Scaling:** whatever the mode, slice start / end positions are multiplied by the tempo ratio so slices remain on the grid. For Stretch each slice is stretched as its own short segment, never the whole loop in one go, and inside the segment the stretch is transient-weighted (next item).
- **Transient-weighted stretch (modelled on Bitwig).** The stretch is not applied evenly across a slice. At the slice's start (its transient) the audio plays essentially unstretched, so the attack stays sharp, and the stretch grows with distance from the transient and settles into an even plateau for the rest of the slice. Per slice this is a **time-warp map from output time to input position**:
  1. **Hold:** local rate 1.0 for the *Transient hold* period (a few ms to a few tens of ms).
  2. **Ramp:** a smooth transition whose shape is the *Curve* control (linear to eased, for example an ease-in-out) from rate 1.0 to the plateau rate.
  3. **Plateau:** a constant rate for the rest of the slice.
  The plateau rate is **solved** so the slice's total output length equals the tempo-matched length (output length = input length x tempo ratio): the hold and ramp consume known output time and input span, and the plateau takes the remainder. Edge cases: a slice shorter than the hold plays at rate 1.0 for the hold's worth and the remaining difference is absorbed by the plateau or, if there is no room, by a plain uniform rate; ratio 1.0 is a pass-through (all rates 1.0); **compression** (ratio below 1) works the same way, the plateau rate then being above 1.0; if the solve has no positive plateau (extreme compression of a short slice) the hold is shortened until it does.
- **Controls (per layer, shown under Stretch, in the Source area beside Tempo):** *Transient hold* (ms), *Curve* (shape), and *Strength* (0 = uniform stretch, 1 = full transient protection; it scales the hold and ramp so the user can fade the effect out; judged useful because slices with soft starts, such as pads and vocals, want it low). A small **curve preview** (local rate against slice time) sits beside the knobs, drawn with the shared curve-drawing look and `MuLookAndFeel` colours. All are shared `KnobWithLabel` controls and APVTS-backed layer parameters; Tempo mode, Hold, Curve and Strength are `ModTarget` candidates.
- **How the stretcher is driven (unproven, prototype required).** Signalsmith Stretch's process call takes an input sample count and an output sample count per call, so a varying local rate should be reachable by feeding it a varying input count per output block, following the warp map (about rate 1.0 in the hold, then the ramp, then the plateau). This is **not proven**: it must be confirmed in a prototype that the stretcher's internal analysis follows such a rate change without artefacts (smeared attacks, phase flutter at the hold-to-ramp join), and what block size works. If it does not, fall back to a uniform plateau stretch of the tail with the first few ms played unstretched and cross-faded in. The `dsp-controller` must review it before adoption.
- **Real time:** Stretch runs per voice on the audio thread only with preallocated state (no allocation in `processBlock`); the warp map is computed on the message thread when a slice or a control changes and read by the audio thread as a finished table.

## Real-time and engine notes

- Transient analysis (detection function, cache and peak picking, as in "Transient detection") runs on a worker thread; never on the audio thread. The audio thread only reads the finished slice list.
- Slice playback reuses `SamplePlayer` and the shared retire-tail mechanism (so a slice that is cut off by the next one fades, no click), on top of the automatic few-millisecond declick fade.
- Boundary edits (moving a marker while playing) use the same hot-swap approach as other products: staged on the message thread, applied at a loop boundary.

## Tempo analysis ★ (research, 2026-10-30; decided: metadata first, then analysis)

Used for library rows, for loading a loop into a layer, and for the Source `Set` control.

**Order of trust.**
1. **File metadata:** WAV `acid` chunk (beats, tempo) and `smpl` chunk, and AIFF / Apple Loops chunks. If present and plausible (inside the range prior), use it with full confidence.
2. **Analysis** ("Method" below).
3. **Filename hint** such as `120bpm`, lowest priority, used only when analysis confidence is low.

**Why loops are a special case.** A loop is normally an exact whole number of beats (usually bars), so the tempo is `60 x beats / duration_seconds`. The problem reduces to finding the beat count, not tracking a drifting tempo.

**Method.**
1. Trim leading and trailing silence first (length must be the musical length).
2. For each candidate beat count (for example 1 to 64), compute the implied BPM and **discard anything outside about 60 to 200 BPM**.
3. Score each remaining candidate by how well the **onset detection curve** (the same cached spectral-flux curve as transient detection, so nothing is analysed twice) fits a beat grid of that tempo, using a comb-filter / autocorrelation / tempogram score. References: Scheirer 1998 (comb filters), Ellis 2007 (dynamic-programming beat tracking), Davies and Plumbley (beat tracking and tempo), and librosa's `beat_track` as a readable reference implementation.
4. Pick the best score. The **range prior plus the loop length** resolves half-tempo and double-tempo errors (both candidates are whole beat counts; the range and the grid fit choose).
5. Find the **downbeat offset** by correlating the curve with the beat grid, so beat 1 lines up with the first strong hit.
6. Keep a **confidence value** (best score against the runner-up) and show it next to the tempo. Low confidence is shown plainly, never hidden.

**Override.** The existing `Set` control in Source lets the user correct tempo and length; the override is **saved with the layer** and beats any later analysis.

**Implementation.** Written in-house on the existing detection curve, about 150 lines, no new dependency. aubio is GPL and Essentia is AGPL, which do not suit a closed product. Runs on the same worker thread as detection, never on the audio thread; results are cached per file for the library list.
