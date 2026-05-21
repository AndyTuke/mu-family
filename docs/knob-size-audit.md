# Knob-size audit

A walkthrough of every `KnobWithLabel` / slider site in `Source/UI/` and the dimensions each control resolves to at runtime. Compiled to feed a follow-up consolidation pass.

## Method

Each panel computes its knob bounds in `resized()` from container width/height. Sizes are proportional, not absolute, so the *bucket* a knob falls into depends on its container size at the moment it lays out — but the *formula* is fixed per knob and that is what this audit catalogues. Where a panel uses a fixed pixel constant (FXRow, DelayRow) that is noted directly.

## Buckets

### Bucket A — Voice section grid knob (Pitch / Filter / Amp)

Formula: `kW × rowH` where `kW = panelWidth / 5`, `rowH = (panelHeight - gap) / 2`.

At a typical 480-wide voice panel: **96 × ~75 px**.

Consumers (sites in [Source/UI/Voice/](../Source/UI/Voice/)):

- **PitchSubsection** — `pitchOctave`, `pitchSemi`, `pitchFine`, `pitchDepth`, `pitchAtk`, `pitchDec`, `pitchSus`, `pitchRel`.
- **FilterSubsection** — `filterCutoff`, `filterRes`, `filterDepth`, `filterAtk`, `filterDec`, `filterSus`, `filterRel`. (`filterType` is `2 * kW × rowH/2` — a dropdown, not a knob, but lives in this grid.)
- **AmpSubsection** — `ampLevel`, `ampSendEff`, `ampSendDly`, `ampSendRev`, `ampAccent`, `ampAtk`, `ampDec`, `ampSus`, `ampRel`.

### Bucket B — Voice section Insert knob

Formula: same as Bucket A but `kW = panelWidth / 4` (Insert has only 4 columns).

At a typical 480-wide voice panel: **120 × ~75 px**.

Consumers ([Source/UI/Voice/InsertSubsection.cpp](../Source/UI/Voice/InsertSubsection.cpp)):

- `insertDrive`, `insertOutput`, `insertDither`, `insertTone`.

Difference from Bucket A: ~25 % wider per knob due to 4-column instead of 5-column division.

### Bucket C — FXRow / DelayRow fixed-pixel knob

Formula: fixed `kKnobW = 72 px`, height = row height (passed in by parent).

Consumers:

- [FXRow.cpp](../Source/UI/FXRow.cpp) — every param knob added to the row.
- [DelayRow.cpp](../Source/UI/DelayRow.cpp) — `multipleKnob`, `feedbackKnob`, `spreadKnob`, `dirtKnob`, plus `msKnob` at `kMsW = 72` (declared separately but identical width).

### Bucket D — Euclidean Panel main knobs

Formula: `eW × ctrlH` where `eW = colW * 0.9`, `colW = innerW / 7`, `ctrlH = rowH - kLabelH`.

Consumers ([Source/UI/EuclideanPanel.cpp](../Source/UI/EuclideanPanel.cpp)):

- `stepsA/B/C`, `hitsA/B/C`, `rotA/B/C` — three identical knobs per row, three rows.

### Bucket E — Euclidean Panel pad / insert knobs

Formula: `pW × (knobH - mP)` where `pW = (innerW - eW*3) / 4`, `knobH = ctrlH - kSwitchH - 6`.

The "minus mP" on height makes these slightly shorter than Bucket D — the switch sits under them in the same column.

Consumers ([Source/UI/EuclideanPanel.cpp](../Source/UI/EuclideanPanel.cpp)):

- `prePadA/B/C`, `postPadA/B/C`, `insertStA/B/C`, `insertLenA/B/C`.

### Bucket F — Mixer channel strip knob

Formula: `stripW × variable-height` where the height depends on which row the knob is on. There are several sub-formulas:

- `scAmount` height = `scAmtH_l` (computed per-channel-type).
- `scAttack`, `scRelease` height = `scEnvH_l` (half-row, side-by-side).
- `sendEffect`, `sendDelay`, `sendReverb` height = `sendH`.
- `panKnob` height = `panH`.

Consumers ([Source/UI/MixerChannel.cpp](../Source/UI/MixerChannel.cpp)). All strip-full-width, vertically stacked.

### Bucket G — Mixer channel insert knob

Formula: dynamic — `ipW × min(60, availH / nRows)` for the 4-knob case, halved horizontally to `ipW/2` when laying out 2-up.

EQ mode lays out 4 knobs in 4 stacked single-column rows; non-EQ mode lays out the visible knobs in a 2-column grid. Different visible-knob counts (1, 3, or 4) produce different row heights, capped at 60 px.

Consumers ([Source/UI/MixerChannel.cpp:270-305](../Source/UI/MixerChannel.cpp#L270)) — `insDrive(2)`, `insOutput(2)`, `insTone(2)`, `insExtra(2)` for both insert slots.

### Bucket H — Misc fixed-size

- **GRMeter** ([Source/UI/Components/GRMeter.h](../Source/UI/Components/GRMeter.h)) — narrow strip beside the fader (`grW × faderH`). Not really a knob but bundled in the channel-strip controls.
- **EuclideanPanel logic/legato segments** — `kLegatoW × (kLogicH-6)` and `logicW × (kLogicH-6)`. Pills, not knobs.

## Summary

Eight distinct sizing formulas across the UI. The voice section has two (A: pitch/filter/amp grid at width/5; B: insert grid at width/4). Everything below the voice section uses either fixed-pixel widths (C — FXRow/DelayRow at 72 px) or proportional-but-different formulas (D/E — Euclidean main vs pad knobs; F/G — mixer strip vs insert; H — meters/pills).

## Recommendation for follow-up consolidation

A practical target is **three canonical knob sizes**:

1. **Standard** — used by voice-section grid (Pitch/Filter/Amp/Insert), set at a fixed pixel size matching FXRow's 72 px so they line up vertically when the mixer overlay opens on top of the voice section. Currently A and B differ purely because Insert has 4 columns vs the others' 5; making them the same width means the Insert row leaves slack at the end (already true after #546's Low Cut slot lands on Filter).
2. **Compact** — narrower variant for the mixer channel strip and Euclidean panel pad knobs (Buckets E, F, G). One target width matched to channel-strip width (~60–80 px depending on number of channels visible).
3. **Wide** — the Euclidean main knobs (Bucket D) which read as ~0.9 of a column out of 7 in a wide panel — distinct enough visually that merging them into Standard would shrink the most-used controls of the panel.

Bucket H (meters/pills) stays separate because they aren't knobs.

To implement consolidation, factor a `kStandardKnobW`, `kCompactKnobW`, `kWideKnobW` set of constants into [Source/UI/Components/MuClidLookAndFeel.h](../Source/UI/Components/MuClidLookAndFeel.h) and route every `setBounds` through them. This is a multi-file edit — keep it as a separate issue with one PR per bucket so regressions can be bisected.
