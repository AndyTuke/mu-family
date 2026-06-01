#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>

namespace mu_ui
{
    // Global UI scale factor. 1.0 = Medium baseline (the values stored in
    // MuLookAndFeel are Medium); 0.85 = Small; 1.15 = Large. Phase 3 will
    // surface a settings picker that writes this; Phase 1+2 leave it at 1.0
    // so the visual output is identical to the pre-scaling code.
    //
    // Mutable so a future settings overlay can write to it. Audio code never
    // reads this — it's UI-only.
    inline float scale = 1.0f;

    // Scale an integer pixel value (Medium baseline) to the current UI scale,
    // rounded to the nearest integer. Wrap every literal and every constant
    // reference inside setBounds with this so changing `scale` (Small / Large
    // toggle) propagates uniformly without recompiling.
    //
    // `mu_ui::s(N)` is intentionally short — call sites are heavy. Drop a
    // `using mu_ui::s;` at the top of resized() / paint() to use `s(N)`.
    inline int s(int medium) noexcept
    {
        return (int) std::round((float) medium * scale);
    }

    // Float overload — for font heights, line widths, etc. where the int
    // round-trip would lose precision.
    inline float sf(float medium) noexcept { return medium * scale; }
}

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
        globalAccent            = 0x100000d2,  // purple — borders / accents for views
                                               // that aren't per-rhythm (mixer overlay,
                                               // global FX rows). Sequencer page uses the
                                               // current rhythm colour for the same role.

        // ── Modulator label colours A–H ───────────────────────────────────
        // Named tokens for the per-modulator label palette so that all colour
        // values live here — no inline `juce::Colour(0xFF...)` in component code.
        modLabelA               = 0x100000e0,  // teal
        modLabelB               = 0x100000e1,  // amber
        modLabelC               = 0x100000e2,  // pink
        modLabelD               = 0x100000e3,  // blue
        modLabelE               = 0x100000e4,  // purple
        modLabelF               = 0x100000e5,  // coral
        modLabelG               = 0x100000e6,  // cyan-teal
        modLabelH               = 0x100000e7,  // grey
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

    // Fixed 8-colour palette for the 8 rhythm slots — Green / Red / Blue /
    // Yellow / Brown / Orange / Cyan / Silver. Index matches creation order
    // (rhythm 0 → palette[0]). Global / mixer accents use `globalAccent`
    // (purple) instead — it's reserved out of this palette deliberately so a
    // purple rhythm and a purple mixer border never collide visually.
    static constexpr int kChannelPaletteSize = 8;
    static const juce::Colour channelPalette[kChannelPaletteSize];

    // ──────────────────────────────────────────────────────────────────────
    // Medium-baseline sizing constants. Single source of truth — change the
    // value here, every consumer picks it up on rebuild. The plugin window
    // is fixed-size at these dimensions; the responsive `getWidth() / N`
    // layout formulas across the UI are being progressively replaced with
    // fixed PX values measured from this baseline. Large / Small variants
    // will arrive later as % scalings of these numbers.
    //
    // See docs/mu-clid/archive/knob-size-audit.md for the per-bucket map of which controls
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
    static constexpr int kChannelPanelW    = kWindowWidth - kSidebarW;                          // 1088
    static constexpr int kChannelPanelH    = kWindowHeight - kTransportBarH - kStatusBarH;      // 814
    static constexpr int kChannelHeaderH   = 28;
    static constexpr int kSampleBarH      = 22;
    static constexpr int kVoiceSectionH   = 144;
    // contentH = 814 - 28 - 22 - 144 = 620. Top half locks to 288 because
    // the modulator minimum of 332 px takes the rest (avail = contentH - 332 = 288;
    // raw 0.55 × contentH = 341 → clamped to avail).
    static constexpr int kChannelTopH      = 288;
    static constexpr int kCircleSize      = kChannelTopH;                                       // square
    static constexpr int kEuclidPanelW    = kChannelPanelW - kCircleSize;                       // 800
    static constexpr int kEuclidPanelH    = kChannelTopH;                                       // 288
    static constexpr int kVoiceSectionW   = kChannelPanelW;                                     // 1088
    static constexpr int kModulatorPanelW = kChannelPanelW;                                     // 1088
    static constexpr int kModulatorPanelH = kChannelPanelH - kChannelHeaderH
                                          - kSampleBarH - kChannelTopH - kVoiceSectionH;        // 332

    // RhythmPanel applies a 7 px inset (kPanelPad + 1) when placing the four
    // big inner panels — these are the actual usable widths for layout math.
    static constexpr int kPanelPad        = 6;
    static constexpr int kChannelInset     = kPanelPad + 1;                                     // 7
    static constexpr int kCircleInnerSize = kCircleSize     - 2 * kChannelInset;                // 274
    static constexpr int kEuclidInnerW    = kEuclidPanelW   - 2 * kChannelInset;                // 786
    static constexpr int kEuclidInnerH    = kEuclidPanelH   - 2 * kChannelInset;                // 274
    static constexpr int kVoiceInnerW     = kVoiceSectionW  - 2 * kChannelInset;                // 1074
    static constexpr int kVoiceInnerH     = kVoiceSectionH  - 2 * kChannelInset;                // 130
    static constexpr int kModulatorInnerW = kModulatorPanelW - 2 * kChannelInset;               // 1074
    static constexpr int kModulatorInnerH = kModulatorPanelH - 2 * kChannelInset;               // 318

    // ── VoiceSection sub-panel widths ─────────────────────────────────────
    // Layout: pitch / filter / amp = 5 × Size2-knob each, insert = 4 × Size2,
    // separated by 3 × divW. Voice subsection cells are Size 2 — flowing from
    // kKnobSize2 below — so the canonical Size 2 knob width drives both the
    // per-knob cell and the parent sub-panel widths. Adjust kKnobSize2 to
    // grow/shrink every voice control + insert in lockstep.
    //
    // kVoiceUnitW is linked further down — the voice subsection column width
    // tracks the Size 2 bucket width so adjusting kKnobCellPaddingX (or any
    // Size 2 dimension) propagates through both the knob cell AND the parent
    // sub-panel allocation in one step. Forward-declared as a literal here
    // and then redefined via the alias at the bottom of this class.
    static constexpr int kVoiceDivW       = 6;
    // The actual value comes from kVoiceUnitW_ at the bottom of the class —
    // declared here as a forward stub equal to the Size 2 width literal so
    // dependent constants (kVoicePitchW = 5 * kVoiceUnitW etc.) work.
    static constexpr int kVoiceUnitW      = 54;   // = kKnobSize2W (Size 2 cell width)
    // Filter sub-section uses narrower columns (6 cols × 50 = 300 px) to make
    // room for the Drive knob while keeping pitch/amp/insert at the standard 54.
    static constexpr int kVoiceFilterColW = 50;
    static constexpr int kVoiceLabelH     = 14;
    static constexpr int kVoiceSubH       = kVoiceInnerH - kVoiceLabelH;                       // 116
    static constexpr int kVoicePitchW     = 5 * kVoiceUnitW;                                   // 270
    static constexpr int kVoiceFilterW    = 6 * kVoiceFilterColW;                              // 300 (6 cols)
    static constexpr int kVoiceAmpW       = 5 * kVoiceUnitW;                                   // 270
    static constexpr int kVoiceInsertW    = 4 * kVoiceUnitW;                                   // 216

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
    static constexpr int kMixerOverlayW    = kChannelPanelW;                                    // 1088
    static constexpr int kMixerOverlayH    = kChannelPanelH;                                    // 814
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
    //
    // Each bucket specifies BOTH width and height. KnobWithLabel draws the
    // visible knob circle at `jmin(width, height - labelArea) / 2 - 2` — so
    // a single "Size" constant (width only) lets cells of the same width
    // but different heights render visibly different knobs. The W/H pair
    // pins both dimensions across every consumer of a bucket.
    //
    // Heights were chosen to match the smallest naturally-occurring cell
    // height in each bucket. Widths are now derived: cell width = visible
    // knob circle diameter + 2 × per-bucket padding. KnobWithLabel reserves
    // 14 px for the bottom label + 4 px topPad, so the slider area height
    // = H − 18 and the visible circle diameter (when H-limited) = H − 22.
    // Each bucket has its own X-padding constant — tune one bucket without
    // affecting the others. Bigger knobs (Size 1) usually want a touch more
    // horizontal breathing room for the longer labels that sit underneath.
    static constexpr int kKnobCellPadding1X = 10;
    static constexpr int kKnobCellPadding2X = 10;
    static constexpr int kKnobCellPadding3X = 6;
    static constexpr int kKnobCellPadding4X = 6;

    static constexpr int kKnobSize1H = 70;
    static constexpr int kKnobSize2H = 56;
    static constexpr int kKnobSize3H = 46;
    static constexpr int kKnobSize4H = 39;

    static constexpr int kKnobSize1W = (kKnobSize1H - 22) + 2 * kKnobCellPadding1X;   // 68
    static constexpr int kKnobSize2W = (kKnobSize2H - 22) + 2 * kKnobCellPadding2X;   // 54
    static constexpr int kKnobSize3W = (kKnobSize3H - 22) + 2 * kKnobCellPadding3X;   // 36
    static constexpr int kKnobSize4W = (kKnobSize4H - 22) + 2 * kKnobCellPadding4X;   // 29

    // KnobWithLabel internal chrome — these constants are used by every knob's
    // resized() + paint() so a single edit here changes the label band size /
    // top padding / font sizes uniformly. They're consumed via mu_ui::s() /
    // sf() at the call site so Phase 2 scale propagates.
    static constexpr int   kKnobLabelH    = 14;   // height of the bottom label band
    static constexpr int   kKnobTopPad    = 4;    // pad above the rotary (room for mod ring)
    static constexpr float kKnobLabelFont = 10.0f;
    static constexpr float kKnobValueFont = 8.0f;
    static constexpr int   kKnobValueH    = 11;   // value-text box height inside the rotary dead zone

    // Voice subsection column width MUST equal Size 2 W so adjusting
    // kKnobCellPaddingX rescales both the cell AND the voice section unit in
    // lockstep. The constant is declared earlier in the class (so the
    // dependent kVoicePitchW = 5 × kVoiceUnitW works at that point) — this
    // assert holds the contract.
    static_assert(kVoiceUnitW == kKnobSize2W,
                  "kVoiceUnitW must equal kKnobSize2W — update both together");

    // Width-only legacy names — equal to the W component of each bucket.
    // Existing call sites that set bounds with `(x, y, kKnobSize1, panelH)`
    // keep their old layout meaning until they're moved to the W/H pair.
    static constexpr int kKnobSize1     = kKnobSize1W;
    static constexpr int kKnobSize2     = kKnobSize2W;
    static constexpr int kKnobSize3     = kKnobSize3W;
    static constexpr int kKnobSize4     = kKnobSize4W;
    static constexpr int kKnobSizeLarge = kKnobSize1W;

    // Mixer channel-strip knob width — pinned to the strip width so the
    // visible knob fills the strip without overhang. Size 3 was bumped to 75
    // but kMixerChanW remains 73 (a function of how 11 strips pack into the
    // overlay), so the mixer-strip uses its own width-matched constant rather
    // than the Size 3 bucket. Height stays at Size 3 H.
    static constexpr int kMixerStripKnobW = kMixerChanW;   // 73
    static constexpr int kMixerStripKnobH = kKnobSize3H;   // 46
};
