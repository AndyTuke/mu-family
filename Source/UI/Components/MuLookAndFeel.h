#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

class MuLookAndFeel : public juce::LookAndFeel_V4
{
public:
    enum ColourIds
    {
        // ── Backgrounds ──────────────────────────────────────────────────
        windowBackground        = 0x10000001,  // #1c1c1b  plugin root
        panelBackground         = 0x10000002,  // #232322  secondary panels
        sidebarBackground       = 0x10000003,  // #1a1a19  left sidebar
        sidebarItemBackground   = 0x10000004,  // #252524  sidebar item bg
        sidebarItemSelected     = 0x10000005,  // #2d2d2b  selected sidebar item bg
        overlayBackground       = 0x10000006,  // #111110  modal overlay bg

        // ── Knob category colours ─────────────────────────────────────────
        knobEuclidean           = 0x10000010,  // #7F77DD  purple: steps, hits, rotate, pitch
        knobPadding             = 0x10000011,  // #1D9E75  teal:   legacy alias (use knobPostPad)
        knobInsertPad           = 0x10000012,  // #D4537E  pink:   insert start/length, modulator controls
        knobLevel               = 0x10000013,  // #EF9F27  amber:  amp ADSR, accent, Euclid C controls
        knobFxSend              = 0x10000014,  // #D85A30  coral:  effect/delay/reverb sends, intra-FX routing
        knobReverb              = 0x10000015,  // #378ADD  blue:   reverb size, diffusion, damp, pre-delay
        knobPan                 = 0x10000016,  // #888780  grey:   pan
        knobModulation          = 0x10000017,  // #D4537E  pink:   (alias – same as knobInsertPad)
        knobPrePad              = 0x10000018,  // #2BB5C5  cyan-teal: pre pad
        knobPostPad             = 0x10000019,  // #1D9E75  teal:      post pad, filter cutoff/res, filter env

        // ── Ring colours (RhythmCircle) ──────────────────────────────────
        ringEuclidA             = 0x10000020,  // #7F77DD  purple, always outermost
        ringEuclidB             = 0x10000021,  // #D85A30  coral,  second ring
        ringEuclidC             = 0x10000022,  // #EF9F27  amber,  dashed accent ring
        ringModA                = 0x10000023,  // #1D9E75  teal
        ringModB                = 0x10000024,  // #EF9F27  amber
        ringModC                = 0x10000025,  // #D4537E  pink
        ringModD                = 0x10000026,  // #378ADD  blue
        ringInactive            = 0x10000027,  // #333332  unfired step dot
        ringPrePad              = 0x10000028,  // #2BB5C5  cyan-teal: pre-padded steps
        ringPostPad             = 0x10000029,  // #1D9E75  teal:      post-padded steps
        ringInsertPad           = 0x1000002A,  // #D4537E  pink:      insert-padded steps

        // ── Segment control states ────────────────────────────────────────
        segmentActiveBg         = 0x10000030,  // #3C3489  general active background
        segmentActiveBorder     = 0x10000031,  // #7F77DD  general active border/text
        segmentPositiveBg       = 0x10000032,  // #085041  positive active background
        segmentPositiveBorder   = 0x10000033,  // #1D9E75  positive active border/text
        segmentWarningBg        = 0x10000034,  // #854F0B  warning active background
        segmentWarningBorder    = 0x10000035,  // #EF9F27  warning active border/text
        segmentInactiveBg       = 0x10000036,  // #2a2a2a  inactive background
        segmentInactiveBorder   = 0x10000037,  // #444444  inactive border
        segmentInactiveText     = 0x10000038,  // #888888  inactive text

        // ── StepEditor ───────────────────────────────────────────────────
        stepEditorBar           = 0x10000040,  // #1D9E75  teal, all bars same colour
        stepEditorZeroLine      = 0x10000041,  // #555554  centre zero line
        stepEditorBackground    = 0x10000042,  // #1e1e1d  editor bg
        stepEditorGridLine      = 0x10000043,  // #333332  subtle grid dividers

        // ── LFOEditor ────────────────────────────────────────────────────
        lfoEditorBackground     = 0x10000050,  // #1e1e1d
        lfoEditorCurve          = 0x10000051,  // #1D9E75  teal curve line
        lfoEditorCurveFill      = 0x10000052,  // #1D9E7540 semi-transparent fill
        lfoEditorPoint          = 0x10000053,  // #ffffff  control point
        lfoEditorPointHover     = 0x10000054,  // #7F77DD  hovered point
        lfoEditorHandle         = 0x10000055,  // #888780  bezier handle
        lfoEditorZeroLine       = 0x10000056,  // #444444
        lfoEditorPlayhead       = 0x10000057,  // #D4537E  pink playhead

        // ── VU meter ─────────────────────────────────────────────────────
        vuMeterLow              = 0x10000060,  // #1D9E75  green zone
        vuMeterMid              = 0x10000061,  // #EF9F27  amber zone
        vuMeterClip             = 0x10000062,  // #E24B4A  red zone
        vuMeterPeakHold         = 0x10000063,  // #ffffff  peak hold line
        vuMeterBackground       = 0x10000064,  // #111110

        // ── Sample bar ───────────────────────────────────────────────────
        sampleBarNoSample       = 0x10000070,  // #444444  muted text
        sampleBarLoaded         = 0x10000071,  // #999999  loaded filename
        sampleBarMissing        = 0x10000072,  // #EF9F27  amber warning
        sampleBarBackground     = 0x10000073,  // #1a1a19

        // ── Status bar ───────────────────────────────────────────────────
        statusBarBackground     = 0x10000080,  // #141413
        statusBarText           = 0x10000081,  // #888780  grey
        statusBarValue          = 0x10000082,  // #cccccc  bright value text

        // ── General text ─────────────────────────────────────────────────
        labelText               = 0x10000090,  // #888780  secondary labels
        valueText               = 0x10000091,  // #cccccc  parameter values
        headingText             = 0x10000092,  // #e8e8e6  headings
        mutedText               = 0x10000093,  // #555554

        // ── Buttons ──────────────────────────────────────────────────────
        addButtonBorder         = 0x100000a0,  // #555554  dashed border
        addButtonText           = 0x100000a1,  // #888780
        addButtonHoverBg        = 0x100000a2,  // #2a2a28

        // ── Sidebar tab line ─────────────────────────────────────────────
        sidebarTabLine          = 0x100000b0,  // matches rhythm colour, drawn at runtime

        // ── #366: new tokens for the audit-flagged hardcoded colours ─────
        backgroundDialog        = 0x100000c0,  // modal dialog cards
        backgroundModalDim      = 0x100000c1,  // dim behind modal cards (semi-transparent)
        backgroundFxRowDim      = 0x100000c2,  // FX/Delay row disabled overlay
        backgroundMixerStripDim = 0x100000c3,  // inactive mixer-channel overlay
        textBright              = 0x100000c4,  // transport-btn active text
        textDisabledButton      = 0x100000c5,  // transport-btn disabled text
        transportWhileStoppedBg = 0x100000c6,  // green (press to play)
        transportWhilePlayingBg = 0x100000c7,  // red   (press to stop)
        indicatorModulationTint = 0x100000c8,  // soft cyan modulation ring
        indicatorGRTint         = 0x100000c9,  // orange GR arc
        indicatorGRMeterBg      = 0x100000ca,  // GRMeter strip background
        indicatorGRMeterBar     = 0x100000cb,  // GRMeter bar
        vuMeterGreen            = 0x100000cc,  // VU safe zone
        vuMeterYellow           = 0x100000cd,  // VU hot zone
        vuMeterRed              = 0x100000ce,  // VU near-clip/clip
        vuMeterClipFlash        = 0x100000cf,  // VU clip flash
        sampleBarMissingWarning = 0x100000d0,  // RhythmPanel sample-missing tint
        mixerInactiveNameBg     = 0x100000d1,  // inactive-rhythm name strip
    };

    MuLookAndFeel();

    // Rotary slider
    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h,
                          float sliderPos, float startAngle, float endAngle,
                          juce::Slider&) override;

    // Text buttons
    void drawButtonBackground(juce::Graphics&, juce::Button&,
                              const juce::Colour& bg, bool over, bool down) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool over, bool down) override;

    // ComboBox
    void drawComboBox(juce::Graphics&, int w, int h, bool down,
                      int bx, int by, int bw, int bh, juce::ComboBox&) override;
    void positionComboBoxText(juce::ComboBox&, juce::Label&) override;

    // Label
    void drawLabel(juce::Graphics&, juce::Label&) override;

    // Static palette helpers
    static juce::Colour colour(ColourIds id) noexcept;

    // Fixed 30-colour palette for rhythm colour picker
    static const juce::Colour rhythmPalette[30];

    // ──────────────────────────────────────────────────────────────────────
    // Medium-baseline sizing constants. Single source of truth — change the
    // value here, every consumer picks it up on rebuild. The plugin window
    // is fixed-size at these dimensions; the responsive `getWidth() / N`
    // layout formulas across the UI are being progressively replaced with
    // fixed PX values measured from this baseline. Large / Small variants
    // will arrive later as % scalings of these numbers.
    //
    // See docs/knob-size-audit.md for the per-bucket map of which controls
    // consume which constant.
    // ──────────────────────────────────────────────────────────────────────

    // Plugin window dimensions (the editor calls setSize with these).
    static constexpr int kWindowWidth  = 1170;
    static constexpr int kWindowHeight = 870;

    // ── Top-level chrome ──────────────────────────────────────────────────
    static constexpr int kTransportBarH = 36;
    static constexpr int kStatusBarH    = 20;
    static constexpr int kSidebarW      = 82;   // mirrored from RhythmSidebar::kWidth

    // ── RhythmPanel regions (derived from the window minus chrome) ────────
    // Main area: 1088 × 814 at default. RhythmPanel internal headers consume
    // 28 (rhythm header) + 22 (sample bar) + 144 (voice section row) = 194
    // px of vertical space before the dynamic top half / mod half split.
    static constexpr int kRhythmPanelW    = kWindowWidth - kSidebarW;                          // 1088
    static constexpr int kRhythmPanelH    = kWindowHeight - kTransportBarH - kStatusBarH;      // 814
    static constexpr int kRhythmHeaderH   = 28;
    static constexpr int kSampleBarH      = 22;
    static constexpr int kVoiceSectionH   = 144;
    // contentH = 814 - 28 - 22 - 144 = 620. Top half locks to 288 because
    // the modulator minimum of 332 px takes the rest (avail = contentH - 332 = 288;
    // raw 0.55 × contentH = 341 → clamped to avail).
    static constexpr int kRhythmTopH      = 288;
    static constexpr int kCircleSize      = kRhythmTopH;                                       // square
    static constexpr int kEuclidPanelW    = kRhythmPanelW - kCircleSize;                       // 800
    static constexpr int kEuclidPanelH    = kRhythmTopH;                                       // 288
    static constexpr int kVoiceSectionW   = kRhythmPanelW;                                     // 1088
    static constexpr int kModulatorPanelW = kRhythmPanelW;                                     // 1088
    static constexpr int kModulatorPanelH = kRhythmPanelH - kRhythmHeaderH
                                          - kSampleBarH - kRhythmTopH - kVoiceSectionH;        // 332

    // RhythmPanel applies a 7 px inset (kPanelPad + 1) when placing the four
    // big inner panels — these are the actual usable widths for layout math.
    static constexpr int kPanelPad        = 6;
    static constexpr int kRhythmInset     = kPanelPad + 1;                                     // 7
    static constexpr int kCircleInnerSize = kCircleSize     - 2 * kRhythmInset;                // 274
    static constexpr int kEuclidInnerW    = kEuclidPanelW   - 2 * kRhythmInset;                // 786
    static constexpr int kEuclidInnerH    = kEuclidPanelH   - 2 * kRhythmInset;                // 274
    static constexpr int kVoiceInnerW     = kVoiceSectionW  - 2 * kRhythmInset;                // 1074
    static constexpr int kVoiceInnerH     = kVoiceSectionH  - 2 * kRhythmInset;                // 130
    static constexpr int kModulatorInnerW = kModulatorPanelW - 2 * kRhythmInset;               // 1074
    static constexpr int kModulatorInnerH = kModulatorPanelH - 2 * kRhythmInset;               // 318

    // ── VoiceSection sub-panel widths ─────────────────────────────────────
    // Layout: pitch / filter / amp = 5 × Size2-knob each, insert = 4 × Size2,
    // separated by 3 × divW. Voice subsection cells are Size 2 — flowing from
    // kKnobSize2 below — so the canonical Size 2 knob width drives both the
    // per-knob cell and the parent sub-panel widths. Adjust kKnobSize2 to
    // grow/shrink every voice control + insert in lockstep.
    //
    // (kKnobSize2 is defined later in this class. Forward-referencing via
    // the alias keeps the section ordering "regions first, knob sizes next"
    // but means the cell math depends on the literal value 55 here. If
    // kKnobSize2 changes, update kVoiceUnitW to match.)
    static constexpr int kVoiceDivW       = 6;
    static constexpr int kVoiceUnitW      = 55;                                                // = kKnobSize2 (Size 2)
    static constexpr int kVoiceLabelH     = 14;
    static constexpr int kVoiceSubH       = kVoiceInnerH - kVoiceLabelH;                       // 116
    static constexpr int kVoicePitchW     = 5 * kVoiceUnitW;                                   // 275
    static constexpr int kVoiceFilterW    = 5 * kVoiceUnitW;
    static constexpr int kVoiceAmpW       = 5 * kVoiceUnitW;
    static constexpr int kVoiceInsertW    = 4 * kVoiceUnitW;                                   // 220

    // Per-knob cell dimensions inside each voice sub-panel.
    static constexpr int kVoiceGap        = 4;
    static constexpr int kVoiceKnobCellH  = (kVoiceSubH - kVoiceGap) / 2;                      // 56
    static constexpr int kVoicePFAKnobW   = kVoiceUnitW;                                       // Size 2
    static constexpr int kVoiceInsertKnobW = kVoiceUnitW;                                      // Size 2

    // ── MixerOverlay layout ───────────────────────────────────────────────
    // MixerOverlay occupies the same area as RhythmPanel (1088 × 814). The
    // FX rows live in the bottom 32 % of the height; channel strips fill the
    // rest. Channel widths are computed so 8 rhythm + 3 return strips share
    // the available horizontal space alongside the master + label panel.
    static constexpr int kMixerOverlayW    = kRhythmPanelW;                                    // 1088
    static constexpr int kMixerOverlayH    = kRhythmPanelH;                                    // 814
    static constexpr int kMixerHeaderH     = 22;
    static constexpr int kMixerFXGap       = 6;
    static constexpr int kMixerFXPad       = 6;
    static constexpr int kMixerDivW        = 4;
    static constexpr int kMixerChanGap     = 3;
    static constexpr int kMixerMasterW     = 80;
    static constexpr int kMixerLabelPanelW = 38;
    static constexpr int kMixerInsertPanelW = 130;   // mirrored from MixerChannel::kInsertPanelW
    // Derived heights/widths at the Medium baseline.
    static constexpr int kMixerFXAreaH     = 261;   // jmax(220, round(814 * 0.32)) = 261
    static constexpr int kMixerFXRowH      = (kMixerFXAreaH - kMixerFXGap * 2 - kMixerFXPad * 2) / 3;   // 79
    static constexpr int kMixerStripH      = kMixerOverlayH - kMixerFXAreaH - kMixerHeaderH;   // 531
    static constexpr int kMixerMasterTotalW = kMixerMasterW + kMixerInsertPanelW;              // 210
    static constexpr int kMixerNumChans    = 8 + 3;                                            // 8 rhythm + 3 returns
    static constexpr int kMixerChanW       = (kMixerOverlayW - kMixerLabelPanelW
                                              - 2 * kMixerDivW - kMixerMasterTotalW
                                              - (8 - 1) * kMixerChanGap) / kMixerNumChans;    // 73

    // ── Canonical knob sizes ──────────────────────────────────────────────
    // Four buckets — every knob in the plugin picks one.
    //
    //   Size 1 (largest) — Euclid Steps/Hits/Rotate; mixer FX-row knobs
    //                      (Effect / Delay / Reverb).
    //   Size 2           — Voice subsection (pitch/filter/amp/insert);
    //                      master insert effect controls.
    //   Size 3           — Euclid pad/insert controls (prePad/postPad/
    //                      insStart/insLen); mixer channel-strip knobs
    //                      (sends / pan / sidechain Amount) except envelopes.
    //   Size 4 (smallest)— Sidechain envelope knobs (Attack + Release).
    static constexpr int kKnobSize1 = 88;
    static constexpr int kKnobSize2 = 55;
    static constexpr int kKnobSize3 = 73;
    static constexpr int kKnobSize4 = 36;

    // Backward-compat alias — previous code referred to the Large bucket as
    // kKnobSizeLarge. Kept so existing call sites keep working until they're
    // moved to the numbered names.
    static constexpr int kKnobSizeLarge = kKnobSize1;
};
