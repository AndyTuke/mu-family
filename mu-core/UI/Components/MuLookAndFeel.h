#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "MuTheme.h"
#include "MuLimits.h"   // mu_limits::kMaxBuses: the mixer's Buses page
#include <cmath>
#include <vector>

namespace mu_ui
{
    // Shared UI repaint cadence (Hz) for animated components — VU/GR meters,
    // transport, sidebars, panels. One place to tune the family's frame rate.
    inline constexpr int kUiRefreshHz = 30;

    // Cached small-integer label strings for paint() loops (step / bar numbers, etc.).
    // juce::String always heap-allocates, so building one per number per frame churns the
    // heap on every repaint; this returns a reference into a lazily-built table instead.
    // Out-of-range (rare) returns an empty string rather than dangling.
    inline const juce::String& cachedIntLabel(int n)
    {
        static const std::vector<juce::String> table = []
        {
            std::vector<juce::String> t;
            t.reserve(1000);
            for (int i = 0; i < 1000; ++i) t.emplace_back(juce::String(i));
            return t;
        }();
        static const juce::String empty;
        return (n >= 0 && n < (int) table.size()) ? table[(std::size_t) n] : empty;
    }

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

        // ── Extra colour tokens replacing previously hardcoded values ────
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
                                               // that aren't per-channel (mixer overlay,
                                               // global FX rows). Sequencer page uses the
                                               // current rhythm colour for the same role.
        // App primary colours — each app passes one to setMetalStyle.
        appPurple               = 0x100000d3,
        appGreen                = 0x100000d4,
        appBlue                 = 0x100000d5,
        appYellow               = 0x100000d6,
        appIce                  = 0x100000da,   // mu-Tronic's near-white (the host of the other four)

        // Status lamps: a fault (e.g. MIDI clock lost) — its own red, not the VU clip red.
        indicatorFault          = 0x100000d7,
        // Destructive dialog button (Warning-icon ModalDialog primary): dark red fill;
        // its border and text use indicatorFault.
        buttonDangerBg          = 0x100000d8,
        // Lettering of a name that is queued, not yet playing (the header's preset box while a clip waits).
        queuedText              = 0x100000d9,

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

    // Metal style (mu-Clid): every ComboBox under this LookAndFeel is drawn as an LCD and
    // text labels are engraved into the metal. Off keeps the flat family look.
    void setMetalStyle(bool m) noexcept { metalStyle = m; }
    bool isMetalStyle() const noexcept  { return metalStyle; }
    static bool isMetal(const juce::Component& c);   // c's LookAndFeel is a MuLookAndFeel in metal style

    // The product's own colour (mu-Clid purple, mu-Tant green): panel borders, raised-box
    // outlines and LCD lettering use it. Unset = globalAccent, with LCDs in segmentActiveBorder.
    // Also makes it the app's selection colour: the theme's active segment / tab / button
    // colours and the global (mixer) accent follow it, so nothing stays in the family purple.
    void setAppAccent(juce::Colour c);
    static juce::Colour appAccent(juce::Component& c);
    // A host of several layer types paints the main area of the shown layer in that type's colour while its
    // chrome keeps the app's own: `root` and everything under it read `colour` from appAccent, from the main-page
    // knobs and from lit LCDs instead of the LookAndFeel's app colour. clearAccentOverride restores it.
    static void setAccentOverride(juce::Component& root, juce::Colour colour);
    static void clearAccentOverride(juce::Component& root);
    // An app's main page: every knob under `root` is drawn in the app colour (metal style with
    // an app colour set) instead of its own category colour. The editor shell marks the main panel.
    static void useAppColourKnobs(juce::Component& root);
    // `accent` as it looks painted on the metal: dull, dark and washed out (Lighting::paint*).
    static juce::Colour paintColour(juce::Colour accent);
    static juce::Colour lcdLitColour(juce::Component& c);

    // Screws style: small cross-head screws in the corners of panels and sub-panels.
    // Opt-in per app (mu-Clid first); shared components check hasScrews(component).
    void setScrews(bool on) noexcept { screws = on; }
    static bool hasScrews(const juce::Component& c);

    // A cross-head (Phillips) screw of diameter `d` at `centre`, lit from the top right.
    // `angle` turns the cross so a row of screws doesn't look stamped.
    static void drawScrew(juce::Graphics&, juce::Point<float> centre, float d, float angle);
    // A screw in each corner of a panel / raised sub-panel.
    static void drawPanelScrews(juce::Graphics&, juce::Rectangle<float> panel);
    static void drawSubPanelScrews(juce::Graphics&, juce::Rectangle<float> subPanel);
    // A thin strip panel (too short for four): one screw at each end, vertically centred.
    static void drawStripScrews(juce::Graphics&, juce::Rectangle<float> strip);

    // An engraved groove of `length` pixels from `start`: a dark line on the start edge and a light
    // one a pixel to the right (vertical) or below (horizontal). The one definition of a groove.
    static void drawGroove(juce::Graphics&, juce::Point<int> start, int length, bool vertical);

    // Lamps: a lit indicator behind a dark lens. lampColour mixes `clr` into the opaque lamp
    // base by `lit` (Lighting lampOff / lampDim / lampOn); drawLamp fills `shape` with that lens,
    // brighter at `centre` where the light sits behind it.
    static juce::Colour lampBase();
    // The accent halo behind a lit control (Lighting::performanceGlow), `radius` px wide.
    static void drawLitGlow(juce::Graphics& g, juce::Rectangle<int> r, juce::Colour accent, int radius);
    static juce::Colour lampColour(juce::Colour clr, float lit);
    static void drawLamp(juce::Graphics&, const juce::Path& shape, const juce::AffineTransform&,
                         juce::Colour lens, juce::Point<float> centre, float hotRadius);

    // Text cut into the metal: a dark cut offset down-left, the lettering over it in `ink`.
    static void drawEngravedText(juce::Graphics&, const juce::String& text, juce::Rectangle<int> area,
                                 juce::Justification just, juce::Colour ink, bool ellipsis = true);

    // Width a name plate needs for `text` at plate height `h` (text plus room for the screws).
    static float namePlateWidth(const juce::String& text, float h);

    // A metal panel (drawAccentPanel) with its title on a name plate in the top-left corner.
    static void drawTitledPanel(juce::Graphics&, juce::Rectangle<float> bounds,
                                const juce::String& title, juce::Colour accent);
    // A name plate sized to `text`, centred in `span` (e.g. a section's label row).
    static void drawCentredNamePlate(juce::Graphics&, juce::Rectangle<float> span, const juce::String& text);

    // Rotary slider
    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h,
                          float sliderPos, float startAngle, float endAngle,
                          juce::Slider&) override;

    // Linear sliders (mixer faders, depth slider): a rectangular cap lit like the knob disc,
    // on a recessed groove with an accent fill. The thumb radius is half the cap length so
    // the slider's travel keeps the cap inside its bounds.
    void drawLinearSlider(juce::Graphics&, int x, int y, int width, int height,
                          float sliderPos, float minSliderPos, float maxSliderPos,
                          juce::Slider::SliderStyle, juce::Slider&) override;
    int getSliderThumbRadius(juce::Slider&) override;

    // Vertical two-position slide switch (SlideSwitch). position runs 0 = top to
    // 1 = bottom and may sit between while the disc animates; selected is the end the
    // switch is committed to, whose label lights in the accent.
    virtual void drawSlideSwitch(juce::Graphics&, juce::Rectangle<float> bounds,
                                 float position, juce::Colour accent,
                                 const juce::String& topLabel, const juce::String& bottomLabel,
                                 int selected, bool highlighted);

    // A panel tinted with a product's accent colour: a faint wash of the accent, a soft
    // highlight from the top-right corner (the knobs' light direction), and a 2 px accent
    // outline. Products pass their own accent (mu-Clid: globalAccent).
    static void drawAccentPanel(juce::Graphics&, juce::Rectangle<float> bounds,
                                juce::Colour accent, float cornerSize = 6.0f);

    // A sub-panel sitting slightly raised on a panel: its shadow, then its face. Draw every
    // sub-panel's shadow before any face so no shadow lands on a neighbouring box.
    // Brushed grain plus diagonal reflection bands over `shape` (bounds `r`), lit from the
    // top right: the metal finish shared by panels and raised sub-panels.
    static void drawMetalFinish(juce::Graphics&, const juce::Path& shape, juce::Rectangle<float> r,
                                float brush, float bands);

    // Bezel round a display set into the panel (drawn over it): the recess is shaded on
    // its top-right inner edges and catches the light on its bottom-left ones.
    static void drawRecessedScreen(juce::Graphics&, juce::Rectangle<float> bounds);

    // LCD window: glass (backlit from the centre in `lit` when `backlit`), then whatever is
    // shown on it, then its front (glare + recessed bezel). Shared by LCD tabs and dropdowns.
    static bool lcdCombo(juce::ComboBox&);   // drawn as an LCD (its own flag, or metal style)
    static void drawLcdGlass(juce::Graphics&, juce::Rectangle<float> bounds, juce::Colour lit, bool backlit);
    static void drawLcdFront(juce::Graphics&, juce::Rectangle<float> bounds);
    static juce::Colour lcdLitColour();     // lettering colour on a lit LCD
    static juce::Font   lcdFont(float height);

    // A small name plate fixed to a panel: raised dark face, bevelled edge, a screw head
    // at each end and the text engraved in the middle, lit from the top right.
    static void drawNamePlate(juce::Graphics&, juce::Rectangle<float> bounds, const juce::String& text);

    static void drawRaisedSubPanelShadow(juce::Graphics&, juce::Rectangle<float> bounds,
                                         float cornerSize = 4.0f);
    static void drawRaisedSubPanel(juce::Graphics&, juce::Rectangle<float> bounds,
                                   juce::Colour accent, float cornerSize = 4.0f);

    // A panel's sections, each in a raised sub-panel: every shadow first, then the faces, the
    // corner screws (screws style) and each name plate centred in the kSectionPlateH band
    // just above its box. An empty name draws no plate.
    struct Section { juce::Rectangle<int> box; juce::String name; };
    static void drawSections(juce::Graphics&, const juce::Component& owner,
                             std::initializer_list<Section> sections, juce::Colour accent);

    // Text buttons
    void drawButtonBackground(juce::Graphics&, juce::Button&,
                              const juce::Colour& bg, bool over, bool down) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool over, bool down) override;

    // Check boxes: in metal style a small raised metal case with a lamp lens (lit in the app accent when on)
    // and the engraved label beside it; otherwise the stock tick box.
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool over, bool down) override;

    // ComboBox
    void drawComboBox(juce::Graphics&, int w, int h, bool down,
                      int bx, int by, int bw, int bh, juce::ComboBox&) override;
    void positionComboBoxText(juce::ComboBox&, juce::Label&) override;

    // Label
    void drawLabel(juce::Graphics&, juce::Label&) override;

    // Static palette helpers
    static juce::Colour colour(ColourIds id) noexcept;

    // Fixed 8-colour voice palette (Red / Cyan / Orange / Magenta / Lime / Rose / Silver /
    // Blue), index = the voice's colour index. Every entry is a Launchpad X palette colour, so
    // pad and screen agree. The app primaries (appPurple / appGreen / appBlue / appYellow) are
    // kept out of it so a voice highlight never matches the paint.
    static constexpr int kLayerPaletteSize = 8;
    static const juce::Colour layerPalette[kLayerPaletteSize];
    static const char* const layerPaletteNames[kLayerPaletteSize];   // lower-case hue name per entry

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
    static constexpr int kSidebarW      = 82;   // the shared LayerSidebar width

    // ── Layer panel bands (derived from the window minus chrome) ──────────
    // Main area: 1088 × 814 at default, tiled by the shared LayerPanel into three bands:
    // a 32 px preset strip, a 464 px product content band, and the 318 px modulator panel.
    static constexpr int kChannelPanelW    = kWindowWidth - kSidebarW;                          // 1088
    static constexpr int kChannelPanelH    = kWindowHeight - kTransportBarH - kStatusBarH;      // 814
    static constexpr int kLayerHeaderStripH = 32;    // preset strip holding the 28 px LayerHeaderBar
    static constexpr int kLayerModulatorH   = 318;   // the shared ModulatorPanel
    static constexpr int kLayerContentH     = kChannelPanelH - kLayerHeaderStripH - kLayerModulatorH;   // 464
    static_assert(kLayerHeaderStripH + kLayerContentH + kLayerModulatorH == kChannelPanelH,
                  "layer panel bands must fill the channel panel");

    // ModalDialog header icon (design-ui-family section 5): vector box, gap to the title, stroke.
    static constexpr int   kDialogIconSize   = 26;
    static constexpr int   kDialogIconGap    = 6;
    static constexpr float kDialogIconStroke = 1.6f;

    // mu-Clid's content band: sample bar (the strip height) + circle row + voice band = 464.
    static constexpr int kVoiceSectionH   = 144;
    static constexpr int kCircleSize      = kLayerContentH - kLayerHeaderStripH - kVoiceSectionH;   // 288
    static constexpr int kEuclidPanelW    = kChannelPanelW - kCircleSize;                       // 800
    static constexpr int kEuclidPanelH    = kCircleSize;                                        // 288
    static constexpr int kVoiceSectionW   = kChannelPanelW;                                     // 1088
    static constexpr int kModulatorPanelW = kChannelPanelW;                                     // 1088

    // ── Spacing scale ─────────────────────────────────────────────────────
    // Gaps and padding belong to the design system rather than to whichever panel
    // happens to need them, so every panel reaches for the same handful of values.
    // These are the values already shipping — naming them here is a relocation, not
    // a retune, so nothing moves on screen.
    static constexpr int kSpaceXS = 4;    // panel outer inset, tool gaps, inter-row gaps
    static constexpr int kSpaceS  = 6;    // panel padding, separation between sub-panel borders
    static constexpr int kSpaceM  = 8;    // divider between control groups
    static constexpr int kSpaceL  = 12;   // section margin

    // Knob-cluster gaps. Role-specific rather than scale steps: both were tuned by
    // eye so a row of knobs breathes and a labelled pair still reads as one pair.
    static constexpr int kKnobGapRow  = 18;   // between knobs in a row (Steps / Hits / Rotate)
    static constexpr int kKnobGapPair = 48;   // between the two knobs of a pair (Pre/Post Pad, Insert)

    // RhythmPanel applies a 7 px inset (kPanelPad + 1) when placing the four
    // big inner panels — these are the actual usable widths for layout math.
    static constexpr int kPanelPad        = kSpaceS;
    static constexpr int kDropdownEdgeGap = kSpaceS;   // space between a dropdown and the edge of its box / panel
    static constexpr int kDropdownW       = 120;       // family-standard dropdown (selector) width
    static constexpr int kDropdownCaptionW = 44;       // engraved caption to the left of a dropdown (fits "Trigger" at 11 pt)
    static constexpr int kDropdownH       = 24;        // family-standard dropdown height
    static constexpr int kGrooveBoxW      = 300;       // fixed width of a two-knob / envelope-editor box, so its layout never jumps
    // The Root / Scale row (mu-Tant's tonal centre): label, root and scale selector widths and the gap between pairs.
    static constexpr int kTonalLabelW = 44;
    static constexpr int kTonalRootW  = 56;
    static constexpr int kTonalScaleW = 100;
    static constexpr int kTonalGap    = 12;
    static constexpr int kLcdSegmentW = 56;   // one window of a two-option LCD selector (mu-Clid's Sample | MIDI; a Pattern's Free | Note)
    static constexpr int kAddButtonW      = 110;       // "+ Target" / "+ Assignment" (metal: a normal button, centred)
    static constexpr int kChannelInset     = kPanelPad + 1;                                     // 7
    static constexpr int kCircleInnerSize = kCircleSize     - 2 * kChannelInset;                // 274
    static constexpr int kEuclidInnerW    = kEuclidPanelW   - 2 * kChannelInset;                // 786
    static constexpr int kEuclidInnerH    = kEuclidPanelH   - 2 * kChannelInset;                // 274
    static constexpr int kVoiceInnerW     = kVoiceSectionW  - 2 * kChannelInset;                // 1074
    static constexpr int kVoiceInnerH     = kVoiceSectionH  - 2 * kChannelInset;                // 130
    static constexpr int kModulatorInnerW = kModulatorPanelW - 2 * kChannelInset;               // 1074
    static constexpr int kModulatorInnerH = kLayerModulatorH - 2 * kChannelInset;               // 304

    // ── VoiceSection sub-panel widths ─────────────────────────────────────
    // mu-Clid's VoiceSection lays out its own variant (see VoiceSection.h); these are the
    // shared defaults the other products use.
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
    static constexpr int kVoiceGap        = kSpaceXS;
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
    static constexpr int kMixerFXGap       = kSpaceS;
    static constexpr int kMixerFXPad       = kSpaceS;
    static constexpr int kMixerDivW        = kSpaceXS;
    static constexpr int kMixerChanGap     = 3;
    static constexpr int kMixerMasterW     = 80;
    static constexpr int kMixerLabelPanelW = 38;
    static constexpr int kMixerInsertPanelW = 136;   // the master insert column beside the Master strip
    // Derived heights/widths at the Medium baseline.
    static constexpr int kMixerFXAreaH     = 261;   // jmax(220, round(814 * 0.32)) = 261
    static constexpr int kMixerFXRowH      = (kMixerFXAreaH - kMixerFXGap * 2 - kMixerFXPad * 2) / 3;   // 79
    static constexpr int kMixerStripH      = kMixerOverlayH - kMixerFXAreaH - kMixerHeaderH;   // 531
    static constexpr int kMixerMasterTotalW = kMixerMasterW + kMixerInsertPanelW;              // 216
    static constexpr int kMixerNumChans    = 8 + 3;                                            // 8 rhythm + 3 returns
    static constexpr int kMixerChanW       = (kMixerOverlayW - kMixerLabelPanelW
                                              - 2 * kMixerDivW - kMixerMasterTotalW
                                              - (8 - 1) * kMixerChanGap) / kMixerNumChans;    // 73

    // The mixer's Buses page (a product with group buses): one unit per bus, a channel-style strip with its
    // insert column beside it, a divider after each unit, then the Master. The strip width is whatever the
    // page leaves after the units' insert columns; it must stay at least one channel strip wide, which holds
    // for three buses (138) and not for four, by design: a fourth needs a layout ruling, not squeezed strips.
    static constexpr int kMixerNumBuses    = mu_limits::kMaxBuses;
    static constexpr int kMixerBusStripW   = kMixerNumBuses > 0
        ? (kMixerOverlayW - kMixerLabelPanelW - kMixerMasterTotalW - kMixerNumBuses * kMixerDivW) / kMixerNumBuses - kMixerInsertPanelW
        : 0;
    static_assert(kMixerNumBuses == 0 || kMixerBusStripW >= kMixerChanW,
                  "the Buses page cannot fit this many buses at the strip width: it needs a layout ruling");

    // ── Canonical knob sizes ──────────────────────────────────────────────
    // Four buckets — every knob in the plugin picks one.
    //
    //   Size 1 (largest) — Euclid Steps/Hits/Rotate and pad/insert controls
    //                      (prePad/postPad/insStart/insLen); mixer FX-row knobs
    //                      (Effect / Delay / Reverb).
    //   Size 2           — Voice subsection (pitch/filter/amp/insert);
    //                      master insert effect controls.
    //   Size 3           — mixer channel-strip knobs (sends / pan /
    //                      sidechain Amount) except envelopes.
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
    // The empty-slot chooser's typography and chrome (docs/mu-tronic/design-ui.md 4.3): the card descriptor and hint,
    // the empty-registry message ("one size up"), the margin round the panel, the message inset and the card corner.
    static constexpr float kCardTextFont        = 12.0f;
    static constexpr float kChooserMessageFont  = 14.0f;
    static constexpr int   kChooserPanelMargin  = 2;
    static constexpr int   kChooserMessageInset = 20;
    static constexpr float kCardCorner          = 4.0f;
    static constexpr int   kKnobValueH    = 11;   // value-text box height inside the rotary dead zone

    // The family's rotary sweep — the single source of truth for every angle a knob is
    // drawn against. KnobWithLabel pushes these onto its slider so drag and draw agree,
    // drawRotarySlider draws to them, and the overlays (mod ring, live mod arc, GR arc)
    // read them here rather than restating them. 7 o'clock round through 12 back to
    // 5 o'clock: a 300-degree sweep with a 60-degree wedge out of the bottom.
    static constexpr float kRotaryStartAngle = (7.0f / 12.0f) * juce::MathConstants<float>::twoPi;
    static constexpr float kRotaryEndAngle   = (5.0f / 12.0f) * juce::MathConstants<float>::twoPi
                                             + juce::MathConstants<float>::twoPi;

    // The glowing ring, and the disc inside it, as fractions of the rotary's radius.
    static constexpr float kRotaryRingScale = 0.82f;
    static constexpr float kRotaryFaceScale = 0.74f;

    // A rotary is stepped when its interval leaves at most kMaxSteppedSegments steps across
    // the range — 64 admits the widest stepped control the family ships (Hits, 0..64 by 1)
    // while a fine-grained range that merely carries an interval (Fine, -100..100 by 1)
    // still reads as a sweep. Returns the step count (0 for a range collapsed to one
    // value), or -1 for a smooth control. Drives both the centred value and the ticks.
    static constexpr int kMaxSteppedSegments = 64;
    static int steppedSegments(const juce::Slider& slider) noexcept;

    // Smooth knobs get a fixed tick count; stepped ones get one per position, thinned to
    // every Nth when ticks would sit closer than kTickMinSpacing px along the ring.
    static constexpr int   kSmoothTickCount = 16;
    static constexpr float kTickMinSpacing  = 3.0f;

    // Shadow / highlight / tint strengths live in MuTheme::current().lighting — one place
    // to tune depth across every product (with master shadow + highlight amounts).
    static const MuTheme::Lighting& lighting() noexcept { return MuTheme::current().lighting; }

    // Draw a knob's cast shadow under its ring (centre + ring radius from getRotaryGeometry).
    static void drawKnobCastShadow(juce::Graphics& g, juce::Point<float> centre, float ringRadius);

    // A juce::DropShadow under an ellipse, its blurred mask cached: the mask depends only on the
    // ellipse's size + sub-pixel position, the blur and the offset, so every knob of one size
    // shares it instead of rendering and blurring a fresh image on each paint.
    static void drawEllipseShadow(juce::Graphics& g, juce::Rectangle<float> ellipse,
                                  juce::Colour colour, int radius, juce::Point<int> offset);

    // Below this radius a centred value would crowd the disc, so it is left to the
    // knob's label. Size 3 clears it; Size 4 does not, and nothing ships a stepped
    // control that small.
    static constexpr float kRotaryValueMinRadius = 12.0f;

    // SlideSwitch: the whole control (track + labels beside it), and the track's
    // width. 44 tall spans roughly a Size 1-2 knob's ring.
    static constexpr int   kSlideSwitchW      = 40;
    static constexpr int   kSlideSwitchH      = 44;
    static constexpr float kSlideSwitchTrackW = 9.0f;

    // Fader cap and groove (linear sliders), at the medium size; scale with sf() at the call site.
    // The cap is kFaderCapW across the travel and kFaderCapH along it; on a thin slider the
    // thickness clamps to the cross axis minus kFaderCapMargin and the length follows at
    // kFaderCapAspect x thickness.
    static constexpr float kFaderCapW      = 16.0f;
    static constexpr float kFaderCapH      = 26.0f;
    static constexpr float kFaderCapCorner = 3.0f;
    static constexpr float kFaderTrackW    = 4.0f;
    static constexpr float kFaderPointerW  = 2.0f;
    static constexpr float kFaderCapMargin = 2.0f;
    static constexpr float kFaderCapAspect = 1.6f;

    // The knob face's fixed colours, lit crown to shadowed rim; the knob disc and the fader
    // cap share them so both read as the same material.
    static inline const juce::Colour knobFaceLit    { 0xff3b2d47 };
    static inline const juce::Colour knobFaceUpper  { 0xff2d2238 };   // gradient stop at 0.35
    static inline const juce::Colour knobFaceLower  { 0xff1b1423 };   // gradient stop at 0.70
    static inline const juce::Colour knobFaceDark   { 0xff100c16 };

    // The recessed groove of the slide switch and the fader: dark at the top lip, lighter below.
    static inline const juce::Colour switchTrackDark  { 0xff07050a };
    static inline const juce::Colour switchTrackLight { 0xff17121d };

    // Alpha a disabled fader is drawn at.
    static constexpr float kDisabledAlpha = 0.4f;

    // Point a plain juce::Slider's drag at the sweep the knob is drawn to. Without it
    // the slider keeps JUCE's own defaults, so the angle it maps the mouse through
    // disagrees with the ring the user sees. KnobWithLabel calls this for itself;
    // the handful of rotaries built as bare Sliders need it too.
    static void applyRotarySweep(juce::Slider& slider)
    {
        slider.setRotaryParameters(kRotaryStartAngle, kRotaryEndAngle, true);
    }

    // Where a knob's annotations (modulation ring, live mod arc, GR arc) belong.
    // KnobWithLabel draws those itself but must not assume where drawRotarySlider put
    // the ring — a style that insets its ring would otherwise be annotated at the wrong
    // radius. Whoever draws the rotary answers this, so the two cannot disagree.
    struct RotaryGeometry
    {
        juce::Point<float> centre;
        float              radius;      // the ring the annotations should hug
        float              startAngle;
        float              endAngle;
    };

    virtual RotaryGeometry getRotaryGeometry(juce::Rectangle<int> sliderBounds) const;

    // Where a knob's value text goes. KnobWithLabel owns the text and decides whether
    // the control is stepped; placing it is a style decision, so it lives here — a
    // style that fills the rotary's dead zone has nowhere to put it and would
    // otherwise have the text drawn straight through its own graphics.
    virtual void drawKnobValueText(juce::Graphics&, juce::Rectangle<int> sliderBounds,
                                   const juce::String& text, bool isStepped) const;

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

    // Client strip (MixerChannel::Type::Client: name LCD, four-knob EQ, uncapped fader, no sends).
    static constexpr int kClientNameH     = 18;   // name LCD
    static constexpr int kClientGap       = 6;    // between the name LCD, the EQ stack and the fader
    static constexpr int kClientDbH       = 18;   // dB readout under the fader
    static constexpr int kClientDbGap     = 4;    // dB readout to the mute / solo row
    static constexpr int kClientButtonH   = 24;   // mute / solo row
    static constexpr int kClientLampH     = 14;   // the Dropouts lamp row under mute / solo (plus kClientDbGap above it)
    static constexpr int kClientBottomGap = 6;    // keeps the mute / solo row off the panel border
    static constexpr int kClientFaderMinH = 40;
    static constexpr float kClientFaderMax   = 1.5f;   // linear gain at the top of the travel
    static constexpr float kClientFaderUnity = 1.0f;
    static constexpr int kNamePlateH = 11;
    // Cross-head screws in panel / sub-panel corners (screws style): head diameter and
    // the distance from the panel edge to the screw's centre.
    static constexpr int kPanelScrewD       = 7;
    static constexpr int kPanelScrewInset   = 7;   // just inside the painted border
    // Distance from a panel's drawn edge that content must keep to clear its corner screws.
    static constexpr int kPanelScrewClear   = kPanelScrewInset + kPanelScrewD / 2 + 2;
    static constexpr int kSubPanelScrewD    = 6;
    static constexpr int kSubPanelScrewInset= 5;
    // Horizontal inset that keeps a sub-panel's content clear of its corner screws.
    static constexpr int kSubPanelScrewClear= kSubPanelScrewInset + kSubPanelScrewD / 2 + 2;
    // Content inset from a panel's bounds (its painted edge is 2 px in) that clears the corner screws;
    // and the same measured from inside the standard channel inset, for content laid out within it.
    static constexpr int kScrewedPanelInset = 2 + kPanelScrewClear;
    static constexpr int kScrewedChannelGap = kScrewedPanelInset - kChannelInset;
    // The band above a raised section box that holds its name plate (drawSections).
    static constexpr int kSectionPlateH     = kVoiceLabelH;

    // Channel sidebar edges: flat, metal, and metal with screws (items + Add button clear of them).
    static constexpr int kSidebarFlatEdge     = 4;
    static constexpr int kSidebarMetalEdge    = 7;
    static constexpr int kSidebarMetalTop     = 4;
    static constexpr int kSidebarScrewedEdgeX = 11;   // Add button in from the sides
    static constexpr int kSidebarScrewedItemX = 4;    // layer boxes in from the sides

    // Lamp displays (metal style step / LFO editor): lens spacing + corners, curve line widths.
    static constexpr int   kLampCellGap    = 2;
    static constexpr int   kLampCellCorner = 2;
    static constexpr float kLampCurveGlowW = 5.0f;
    static constexpr float kLampCurveW     = 1.8f;
    static constexpr int   kStatusLampD    = 8;      // StatusLamp disc diameter

    // Performance mode (docs/design-ui-family.md section 12): the Perf toggle, the accent frame round the
    // editor and the lit Perf button's glow, the exit hold, the indicator's lamp spacing and gap, and how long a performance
    // status line stays up.
    static constexpr int kPerfBtnW       = 44;
    static constexpr int kPerfFrameW     = 3;      // the window frame's width (scaled)
    static constexpr int kPerfGlowR      = 4;      // the lit Perf button's glow radius (scaled)
    static constexpr int kPerfExitHoldMs = 1000;
    static constexpr int kPerfStatusMs   = 3000;
    static constexpr int kModDestMaxW    = 240;    // widest a modulation row's destination selector grows; the amount slider takes the rest
    static constexpr int kPerfHintMs     = 8000;   // how long the 'how to leave' hint stays on the status line
    static constexpr int kStepLampPitch  = 11;
    static constexpr int kStepGap        = 4;
    static constexpr int kStepGhostMinW  = 36;   // narrower than this the ghost text is left out, not shown as a stub

    // The Performance chooser screen (section 12.5): the saved-performances list on the left, the item
    // rows on the right, with a header and a footer band.
    static constexpr int kPerfListW      = 300;
    static constexpr int kPerfItemRowH   = 40;
    static constexpr int kPerfListRowH   = 28;   // a row of the saved-performances list
    static constexpr int kPerfHeaderH    = 36;
    static constexpr int kPerfFooterH    = 40;
    static constexpr int kPerfNumW       = 44;   // an item row's number lamp and number
    static constexpr int kPerfProductW   = 110;  // an item row's product / status label
    static constexpr int kPerfRowBtnW    = 64;   // an item row's, header's and footer's buttons
    static constexpr int kPerfBrowserBtnW = 110; // the Preset Browser footer's "Performances" button

    // Tempo (BPM) fields: one decimal everywhere; an arrow click steps a whole BPM, Shift a tenth.
    static constexpr int    kBpmDecimals = 1;
    static constexpr double kBpmStep     = 1.0;
    static constexpr double kBpmFineStep = 0.1;

    // LCD text fields (metal style): lettering heights, side padding, the browse mark's width.
    static constexpr int kLcdTextH     = 11;   // selectors (ComboBox LCDs) and single-line readouts, e.g. a sample bar
    static constexpr int kLcdNameTextH = 13;   // the channel name display in the header bar
    static constexpr int kLcdTextPadX  = 6;
    static constexpr int kLcdBrowseW   = 24;
    static constexpr int kStripDisplayH = 20;   // a display / button in a thin strip (header bar, sample strip), centred

    // The layer's clip strip in the header bar (docs/design-ui-family.md 13.5): eight pads after an engraved caption.
    static constexpr int kClipPadW      = 24;
    static constexpr int kClipPadH      = kStripDisplayH;
    static constexpr int kClipPadGap    = 3;
    static constexpr int kClipLabelW    = 30;   // the engraved "Clip" caption's slot
    static constexpr int kClipStripGap  = 8;    // between the strip and the preset dropdown

    // The layer's Notes In control in the header bar (docs/design-ui-family.md 13.10): an engraved caption and a channel selector.
    static constexpr int kNotesInLabelW = 44;
    static constexpr int kNotesInW      = 92;

    // The StatusBar's CPU readout (docs/design-ui-family.md section 6): its slot, and the load fractions where the number
    // turns amber and red.
    static constexpr int   kStatusCpuW = 64;
    static constexpr float kCpuWarnAt  = 0.70f;
    static constexpr float kCpuHotAt   = 0.90f;

    // Name plates (drawNamePlate): section and row names

private:
    std::shared_ptr<void> shadowMaskCache;   // keeps drawEllipseShadow's cache alive while any look-and-feel exists
    bool         metalStyle   = false;
    bool         screws       = false;
    bool         hasAppAccent = false;
    juce::Colour appAccentColour;
    // The accent override that applies to `c` (the nearest ancestor's, itself included), if any.
    static bool accentOverrideFor(const juce::Component& c, juce::Colour& out);

    // The colour a knob is drawn in: the app colour on a main page (useAppColourKnobs), else its own.
    juce::Colour knobColour(const juce::Slider&) const;
};
