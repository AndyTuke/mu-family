# #598 — Migrate manual APVTS wiring to JUCE attachments

## Goal

Replace the hand-rolled `apvtsSet` + `parameterChanged` listener pattern that currently wires every UI control to its APVTS parameter with JUCE's standard `SliderAttachment` / `ComboBoxAttachment` / `ButtonAttachment` system (plus thin wrapper attachments for the project's custom controls).

End-state benefits:
- ~30 lambdas per editor disappear (× 5 subsections per rhythm × N rhythms).
- Automatic host-side undo / parameter-thread sync.
- Standard tooltip + double-click-to-default behaviour everywhere (currently we re-implement double-click in `KnobWithLabel`).
- New parameter additions become 2 lines (add to APVTS, attach to control) instead of 5 (apvtsSet write, refreshSuffix branch, kVoiceSuffixes entry, ParamPtr cache, range setup).

## Why this is deferred

Per the original #598 audit, the migration is mechanical refactor with significant test surface. There's no functional bug — the current pattern works at v1.0.606 with the listener gaps closed in #602 / #603. This plan is a roadmap; pulling the trigger needs a clear listening-regression budget.

---

## Current pattern (the manual model)

Every UI control follows the same five-step manual wiring. Example — `ampDec` in [AmpSubsection.cpp](../Source/UI/Voice/AmpSubsection.cpp):

```cpp
// (1) Slider setup — range, skew, default
ampDec.setRange(0.0, 10.0, 0.001);
ampDec.setValue(0.3);
ampDec.getSlider().setSkewFactor(0.3);

// (2) UI → APVTS — onValueChanged lambda writes via apvtsSet
ampDec.onValueChanged = [this](double v) {
    apvtsSet("aEnvDec", (float) v);
    ampDec.setLabel(adsrLabelStr("D", v));
};

// (3) apvtsSet — string-suffix lookup + setValueNotifyingHost
void apvtsSet(const char* suffix, float v) {
    auto* p = paramPtrCache.find(suffix)->second;
    p->setValueNotifyingHost(p->convertTo0to1(v));
}

// (4) APVTS → UI — parameterChanged listener routed through refreshSuffix
void parameterChanged(const String& id, float) {
    if (id.startsWith("r0_aEnvDec")) refreshSuffix("aEnvDec");
}
void refreshSuffix(const String& suffix) {
    if (suffix == "aEnvDec") {
        ampDec.setValue(p.ampEnvDec, dn);
        ampDec.setLabel(adsrLabelStr("D", p.ampEnvDec));
    }
}

// (5) Per-rhythm rebind — paramPtrCache cleared in setRhythm(int) so the
//     next apvtsSet call composes the new "r{N}_" prefix.
```

**Why it grew this shape**: each rhythm slot has its own APVTS parameter prefix (`r0_aEnvDec`, `r1_aEnvDec`, …). When the user selects a different rhythm in the sidebar, EVERY control on the editor needs to point at the new rhythm's parameters. The manual model dodges this by parameterising `apvtsSet` on the current rhythm index — a single cache flush moves the whole editor in one step.

JUCE's `SliderAttachment` cannot do this — its constructor takes a fixed `parameterID` string and holds a `juce::ParameterAttachment` that binds for the lifetime of the attachment object.

---

## Target pattern (attachments)

Standard JUCE wiring:

```cpp
// Decorate the slider with its range + display formatter ONCE, in the constructor.
ampDec.setRange(0.0, 10.0, 0.001);
ampDec.setSkewFactor(0.3);
ampDec.setTextValueSuffix(" s");
ampDec.textFromValueFunction = adsrLabelFromSeconds;

// Bind to APVTS in setRhythm — destroy old attachment, construct new one with new prefix.
ampDecAttachment.reset();
ampDecAttachment = std::make_unique<juce::SliderAttachment>(apvts, "r0_aEnvDec", ampDec.getSlider());
```

The attachment internally:
- Listens for APVTS changes and pushes them into the slider (replaces our `parameterChanged` + `refreshSuffix` branch).
- Listens for slider drags and writes back via `setValueNotifyingHost` (replaces our `apvtsSet`).
- Handles gesture begin/end for proper undo + DAW automation lanes.

---

## Migration scope — file inventory

Counted from current build at v1.0.606:

| Subsection | Controls | Custom widgets | Notes |
|---|---|---|---|
| [PitchSubsection](../Source/UI/Voice/PitchSubsection.cpp) | 9 knobs | — | All `KnobWithLabel`, all on JUCE Sliders |
| [FilterSubsection](../Source/UI/Voice/FilterSubsection.cpp) | 8 knobs, 1 dropdown | filterType uses `DropdownSelect` | Cutoff has unit Hz↔kHz switch via `textFromValueFunction` |
| [AmpSubsection](../Source/UI/Voice/AmpSubsection.cpp) | 9 knobs | — | Level knob has dB↔gain conversion in lambda |
| [InsertSubsection](../Source/UI/Voice/InsertSubsection.cpp) | 4 generic knobs, 1 dropdown | algo uses `DropdownSelect` | Per-algo skew + label come from `kInsertAlgoSlots` — already done via `mu_ui::configureKnobFromSlot` |
| [EuclideanPanel](../Source/UI/EuclideanPanel.cpp) | 21 knobs, 5 segments, 1 dropdown | `SegmentControl`, `DropdownSelect`, dynamic ranges (hits/rotate depend on stepsX) | Most complex — the dynamic range case forces an attachment-rebuild on stepsX change |
| [ModulatorPanel / ModulatorEditor](../Source/UI/ModulatorEditor.cpp) | 2 dropdowns, 1 segment, depth/curve slider pair per assignment row | All custom widgets; `NudgeInput` is custom too | Assignment rows are dynamic — created/destroyed as user adds/removes |

Total: **~50 controls per rhythm editor instance**, of which **~10 are not native JUCE controls** (SegmentControl, DropdownSelect, NudgeInput).

---

## Per-rhythm lifetime strategy

The crux of the refactor: attachments are constructed with a static parameter ID, but the editor needs to swap parameter prefixes on `setRhythm()`.

### Approach: tear-down + rebuild

Each subsection holds a `std::array<std::unique_ptr<juce::SliderAttachment>, N>` (and similar for combobox / button attachments). In `setRhythm(int newIdx)`:
1. `attachments[i].reset()` for all i — detaches all controls from the previous rhythm's parameters.
2. `attachments[i] = std::make_unique<...>(apvts, makeId("aEnvDec", newIdx), slider)`

Cost analysis:
- Tear-down + rebuild for ~50 controls. Each `SliderAttachment` constructor walks the APVTS to find the param + adds itself as a listener. Order of microseconds total — comparable to the existing `paramPtrCache.clear()` + lazy re-cache cost.
- Done on the message thread inside `setRhythm()`, not in `processBlock`. No real-time concern.

Edge case — UNDO history: rebuilding attachments resets the slider's undo identity. We don't currently use undo, so no practical issue. If undo support arrives later, the rebuild needs to suppress undo recording during the swap.

### Helper to make this less verbose

A `RhythmAttachmentBundle` template + a small registration helper:

```cpp
// In a per-subsection helper:
struct AttachmentBundle {
    juce::AudioProcessorValueTreeState& apvts;
    juce::OwnedArray<juce::SliderAttachment>   sliders;
    juce::OwnedArray<juce::ComboBoxAttachment> combos;
    juce::OwnedArray<juce::ButtonAttachment>   buttons;

    void clear() { sliders.clear(); combos.clear(); buttons.clear(); }

    void attachSlider(juce::Slider& s, const juce::String& id) {
        sliders.add(new juce::SliderAttachment(apvts, id, s));
    }
    // etc.
};
```

Each subsection then has a `setRhythm` that calls `bundle.clear()` then re-attaches every control with the new prefix.

---

## Conversion lambdas — the hardest part

The current `onValueChanged` lambdas do **value-domain conversion** between the slider's display units and the APVTS storage units. Examples:

| Control | Display | APVTS | Conversion |
|---|---|---|---|
| `ampLevel` | dB (-60..+6) | gain (0..2) | `dbToGain` / `gainToDb` |
| `ampAccent` | display % (0..100) | dB (0..12) | `v / 100 * 12` |
| `ampSus` | display % (0..100) | gain (0..1) | `v / 100` |
| `pitchDepth` | display % (0..100) | semitones (0..24) | `v / 100 * 24` |
| `filterDepth` | display % (0..100) | semitones (0..48) | `v / 100 * 48` |
| `filterCutoff` | Hz / kHz | Hz | identity, but `textFromValueFunction` formats |
| `sendEff/Dly/Rev` | display % (0..100) | 0..1 | `v / 100` |

**The problem**: `SliderAttachment` writes the slider's *raw* value directly to APVTS via `convertTo0to1`. There's no hook to apply a value-domain transform between the two. So either:
- (a) **Move the conversion into the APVTS parameter range itself** — e.g. register `ampLvl` as a dB-domain parameter (-60..+6 range) instead of a gain-domain one. The slider then directly displays dB; consumers (engine) call `juce::Decibels::decibelsToGain` to convert at read time.
- (b) **Move the conversion into `NormalisableRange`** — provide custom `convertFrom0to1` / `convertTo0to1` lambdas in the APVTS param construction so the parameter STORES the gain but DISPLAYS the dB.
- (c) **Restructure UI controls to display the APVTS unit directly** — change `ampAccent` slider range from 0..100 to 0..12, etc. Re-tune every slider's `textFromValueFunction` to format the new unit.

Option (a) is cleanest but breaks existing presets unless we extend the migration code (which we just discussed removing in #643). Option (b) is technically supported by JUCE but the `convertFrom0to1` API is poorly documented and skew composes weirdly. Option (c) is the most pragmatic — make every slider's display = the APVTS unit, restructure the `textFromValueFunction` to format display correctly.

**Recommended**: option (c). Sliders display the APVTS unit. The engine + tests already operate on the APVTS unit. The lambdas disappear because there's nothing to convert.

---

## Custom controls need wrapper attachments

`SegmentControl`, `DropdownSelect`, `NudgeInput` don't derive from `juce::Slider` / `juce::ComboBox` / `juce::Button`, so they need wrapper attachment classes:

```cpp
// In Source/UI/Components/SegmentControlAttachment.h
class SegmentControlAttachment : private juce::ParameterAttachment {
public:
    SegmentControlAttachment(juce::AudioProcessorValueTreeState& v,
                             const juce::String& paramID,
                             SegmentControl& sc);
private:
    SegmentControl& control_;
    void parameterChanged(float newValue) override {
        control_.setSelectedIndex((int) newValue);
    }
    void controlChanged(int idx) {
        beginGesture();
        setValueAsCompleteGesture((float) idx);
    }
};
```

Three wrapper classes total: `SegmentControlAttachment`, `DropdownSelectAttachment`, `NudgeInputAttachment`. Each is ~40 LOC, follows JUCE's `ParameterAttachment` pattern.

The `KnobWithLabel` already exposes `getSlider()` returning a `juce::Slider&` — so it works with `SliderAttachment` directly. No wrapper needed for the dominant control type.

---

## Step-by-step plan

Each step is independently testable. Tests gate progression.

### Step 0 — Preparation (no behaviour change)
- Move all value-domain conversions from `onValueChanged` lambdas into the APVTS parameter's `NormalisableRange` + slider range (option c above). This means changing the slider display range in every subsection.
- Adjust `apvtsSet` / `refreshSuffix` callers to remove the now-redundant `* multiplier` / `Decibels::*` calls.
- **Behaviour-preserving** — UI looks identical, audio identical. Just removes the conversion lambdas.
- **Tests**: full test suite (current 125) — should all still pass. Plus manual visual check of every knob's displayed value range.

### Step 1 — Add attachment wrappers for custom controls
- Add `SegmentControlAttachment`, `DropdownSelectAttachment`, `NudgeInputAttachment` classes to `Source/UI/Components/`.
- Compile + add a smoke unit test that constructs each wrapper, simulates a parameter change, and verifies the control's selected index moves.
- Keep the existing `onChange` callbacks intact for now — the wrappers will be used in Step 2 onward.
- **Tests**: 3 new unit tests for the wrapper classes.

### Step 2 — Migrate one subsection (AmpSubsection)
- Replace `apvtsSet` lambdas with `SliderAttachment` constructions in `setRhythm()`.
- Remove `AmpSubsection`'s contribution to `kVoiceSuffixes` and the `refreshSuffix` branch (parameter changes now route via attachments).
- `paramPtrCache` member can be deleted from this subsection.
- **Tests**: existing tests; PLUS manual A/B — load a preset, change every Amp knob via DAW automation lane, verify UI updates; change every Amp knob in UI, verify APVTS / preset save reflects.

### Step 3 — Migrate remaining voice subsections (Pitch, Filter, Insert)
- Same pattern as Step 2.
- Filter dropdown uses `DropdownSelectAttachment`.
- Insert dropdown + 4 knobs use the same plus the `mu_ui::configureKnobFromSlot` re-binding on algorithm change.
- **Tests**: existing + manual A/B per subsection.

### Step 4 — Migrate EuclideanPanel
- Most complex due to dynamic `hits`/`rotate`/`insSt` ranges that depend on `stepsX`. The `updateRangesA/B/C` callback that adjusts slider ranges runs BEFORE the attachment tries to push a new value to APVTS — so the attachment sees the resized range.
- `SegmentControl` instances (Trig/Leg, Poly/Mono) use the new `SegmentControlAttachment`.
- Logic `DropdownSelect` uses `DropdownSelectAttachment`.
- **Tests**: existing + manual A/B; specifically test changing `stepsA` mid-play with active modulation on `euclid.a.hits` to verify the dynamic-range attachment still re-clamps cleanly.

### Step 5 — Migrate ModulatorEditor + ModMatrixPanel
- Assignment rows are dynamic (added/removed by user). Each row holds its own attachment to the meta-parameters for that assignment (depth, curve).
- These already use a slightly different model — assignments aren't pure APVTS, they're stored in `ModulationMatrix` directly. Migration here probably means leaving them as-is and only migrating the per-modulator `mode` / `polarity` / `loop` controls.
- **Tests**: manual — verify mod editor state survives a DAW save/restore cycle.

### Step 6 — Final cleanup
- Delete `apvtsSet` helpers, `paramPtrCache` members, `kVoiceSuffixes` array (or shrink it to just euclid suffixes that haven't migrated).
- `parameterChanged` listener gets simpler — only handles APVTS-loading-flag gating and rhythm-index changes; per-parameter routing is gone.
- Update [docs/design-presets.md](design-presets.md) to note the attachment pattern as the canonical wiring.

---

## Risk + rollback

| Step | Risk | Rollback |
|---|---|---|
| 0 | Slider range changes could shift displayed values if the formatter doesn't match the new unit | Restore the previous slider ranges + conversion lambdas |
| 1 | Wrapper attachments may not handle gesture begin/end correctly → undo glitches | Wrappers are independent; revert the file |
| 2 | Listener gap during the migration window — value changes between deleting old wiring and adding new attachment | Step 2 is atomic per subsection (within one `setRhythm` call). Revert the file |
| 3 | Same as 2, multiplied. Filter cutoff `textFromValueFunction` is unique and needs porting | Per-subsection rollback |
| 4 | EuclideanPanel dynamic ranges + attachment race when `stepsX` changes mid-play | Most complex; consider keeping the existing manual model for euclid and migrating only voice subsections |
| 5 | Modulator editor's assignment rows are highly dynamic; attachment lifetime management here is genuine risk | Leave as-is; only voice + euclid migrate |
| 6 | None — cleanup only | Per-file revert |

**Recommendation**: ship Step 0 first (value-unit cleanup) regardless of whether the rest of the refactor proceeds. It's a win on its own and is the largest source of complexity in the current pattern.

---

## Out of scope

- `ModMatrixPanel` assignment rows — their attachments aren't APVTS-backed.
- Mixer overlay (`MixerChannel`, `MixerOverlay`) — already mostly uses raw `juce::Slider` listeners; conversion is similar but the per-rhythm pattern is different (channel index, not rhythm). Could follow voice once that's done.
- Settings overlay, transport bar — not per-rhythm, attachment lifetime is trivial. Migrate opportunistically.

---

## Decisions for Andy before starting

1. **Step 0 only, or the full refactor?** Step 0 alone (move conversions from lambdas to NormalisableRange/slider range) is a worthwhile cleanup with low risk. The full Steps 1–6 is a multi-day refactor with broad test surface.

2. **EuclideanPanel — migrate or skip?** It's the most complex subsection due to dynamic step-count ranges. Leaving it as-is means the manual pattern survives in one place; migrating it means committing to attachment-rebuild-on-stepsX-change machinery.

3. **`ampLevel` storage unit — dB or gain?** Step 0's option (c) wants every slider to display the APVTS unit. For amp level, the natural choice is dB. But the engine reads `voiceParams.ampLevel` as a linear gain multiplier. Either move the dB→gain conversion into the engine read path, or store dB in voiceParams and convert at every audio-thread read. Andy's call.

4. **Undo support?** JUCE attachments give undo for free (`setValueAsCompleteGesture` records); the current pattern doesn't. Worth turning on (one line per attachment) but exposes a previously-absent feature surface.

---

## Estimated effort

- **Step 0**: ~4 hours including verifying every knob's displayed range still matches expectations. Low risk.
- **Steps 1–3** (voice subsections): ~6 hours total.
- **Step 4** (Euclid): ~4 hours, more if dynamic-range corner cases bite.
- **Step 5** (Modulator panel): ~2 hours, scope-permitting.
- **Step 6** (cleanup): ~1 hour.
- **Manual A/B regression listening**: ~2 hours across all subsections.

Total: ~1.5–2 engineer-days for the full migration; ~half a day for Step 0 only.
