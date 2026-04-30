#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

class MuClidLookAndFeel : public juce::LookAndFeel_V4
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
    };

    MuClidLookAndFeel();

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
};
