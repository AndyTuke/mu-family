// ControlDesign — a GUI sandbox for refining the family's controls in isolation,
// without rebuilding a whole product.
//
// Currently focused on **mu-clid's Pad / Insert sub-panels**: the two bordered boxes
// that sit to the right of each Euclid row's Steps/Hits/Rotate block (and the
// Legato / Mono switch column).
//
// This is a 1:1 reproduction, not an impression. Every constant below is mirrored from
// mu-clid's EuclideanPanel (resized() and paint()) and the section is rendered at
// exactly the pixel size it occupies there — 538 x 266 — so anything judged here
// transfers directly. The knobs are real KnobWithLabel instances at the real Size 3,
// with the real ranges, and the toggles are real SegmentControls, so the label
// ellipsis on "Insert Start" / "Insert Length" shows up exactly as it does in the app.
//
// **Mirrored constants — keep in sync with EuclideanPanel.** If that layout changes,
// this one has to follow; there is no shared source for it, because the panel computes
// its geometry inline.
//
//   Build:  cmake --build build --config Debug --target ControlDesign
//   Run:    build/ControlDesign/ControlDesign_artefacts/Debug/ControlDesign.exe

#include <juce_gui_extra/juce_gui_extra.h>

#include "UI/Components/KnobWithLabel.h"
#include "UI/Components/MuLookAndFeel.h"
#include "UI/Components/SegmentControl.h"
#include "UI/Components/SlideSwitch.h"

#include <array>
#include <memory>
#include <vector>

// ── Geometry mirrored from mu-clid's EuclideanPanel ──────────────────────────
namespace euclid
{
    // Gaps come from the design system's spacing scale; the rest are EuclideanPanel's
    // own, mirrored here because it computes them inline.
    constexpr int kSwitchH      = 14;
    constexpr int kLabelH       = 10;
    constexpr int kOuter        = MuLookAndFeel::kSpaceXS;
    constexpr int kEucKnobGap   = MuLookAndFeel::kKnobGapRow;
    constexpr int kPadKnobGap   = MuLookAndFeel::kKnobGapPair;
    constexpr int kPadInsertGap = MuLookAndFeel::kSpaceS;

    constexpr int w      = MuLookAndFeel::kEuclidInnerW;          // 786
    constexpr int innerW = w - 2 * kOuter;                        // 778
    constexpr int innerH = MuLookAndFeel::kEuclidInnerH - 2 * kOuter;   // 266

    constexpr int rowH  = innerH / 3;                             // 88
    constexpr int ctrlH = rowH - kLabelH;                         // 70
    constexpr int mP    = 4;

    constexpr int eW        = MuLookAndFeel::kKnobSize1W;
    constexpr int eucBlockW = eW * 3 + kEucKnobGap * 2;           // 240
    constexpr int kModeColW = MuLookAndFeel::kSpaceM * 2 + MuLookAndFeel::kSlideSwitchW;   // 56
    constexpr int pW        = (innerW - eucBlockW - kModeColW) / 4;   // 120

    constexpr int padX      = kOuter + eucBlockW + kModeColW;     // 300
    constexpr int padPanelW = pW * 2 - kPadInsertGap / 2;         // 265
    constexpr int insX      = padX + pW * 2 + kPadInsertGap / 2;  // 515
    constexpr int insPanelW = w - kOuter - insX;                  // 267

    constexpr int knobH   = ctrlH - kSwitchH - 6;                 // 50
    constexpr int insSw   = (pW < 56) ? pW : 56;
    constexpr int insSwX  = insX + (insPanelW - insSw) / 2;

    constexpr int padKnobW = MuLookAndFeel::kKnobSize3W;          // 36
    constexpr int padKnobH = MuLookAndFeel::kKnobSize3H;          // 46
    constexpr int padPairW = padKnobW * 2 + kPadKnobGap;          // 120

    constexpr int prePadX  = padX + (padPanelW - padPairW) / 2;
    constexpr int postPadX = prePadX + padKnobW + kPadKnobGap;
    constexpr int padSwMax = (padPairW - 4) / 2;
    constexpr int padSw    = padSwMax < 56 ? padSwMax : 56;
    constexpr int preSwX   = prePadX  + (padKnobW - padSw) / 2;
    constexpr int postSwX  = postPadX + (padKnobW - padSw) / 2;

    constexpr int insStX  = insX + (insPanelW - padPairW) / 2;
    constexpr int insLenX = insStX + padKnobW + kPadKnobGap;

    // The three Euclid rows, as paint() positions them.
    constexpr int rowOffsets[3] = { kOuter, kOuter + rowH, kOuter + 2 * rowH };

    // The section this sandbox shows: everything from the Pad sub-panel's left edge to
    // the Insert sub-panel's right edge, full panel height. Coordinates below are
    // shifted left by kSectionX so the section sits at the window's origin.
    constexpr int kSectionX = padX;                               // 244
    constexpr int kSectionW = (w - kOuter) - padX;                // 538
    constexpr int kSectionH = innerH;                             // 266
}

// mu-clid's Pad and Insert sub-panels.
//
// Shipped  — knob above its Pad/Mute switch. The switch row costs the bottom 16 px of
//            the 68 px sub-panel, leaving 46 for the knob, which caps it at Size 3 (36
//            wide) — too narrow for "Insert Start" / "Insert Length", hence the
//            ellipsis.
// Proposed — switch moved alongside the knob as a vertical slide switch (Pad up, Mute
//            down). That frees the full height, so the knobs go up to Size 1 (68 x
//            70): nearly twice the label width, and a much larger target.
enum class PadLayout { Shipped, Proposed };

class PadSection : public juce::Component
{
public:
    explicit PadSection(PadLayout l) : layout(l)
    {
        using namespace euclid;

        for (int r = 0; r < kRows; ++r)
        {
            auto& row = rows[(size_t) r];
            row.prePad   = addKnob("Pre Pad",       MuLookAndFeel::knobPrePad,    0, 12, 0);
            row.postPad  = addKnob("Post Pad",      MuLookAndFeel::knobPostPad,   0, 12, 0);
            row.insStart = addKnob("Insert Start",  MuLookAndFeel::knobInsertPad, 0, 63, 0);
            row.insLen   = addKnob("Insert Length", MuLookAndFeel::knobInsertPad, 0,  8, 0);

            if (layout == PadLayout::Shipped)
            {
                row.preMode  = addSegment({ "Pad", "Mute" }, SegmentControl::ActiveStyle::Warning);
                row.postMode = addSegment({ "Pad", "Mute" }, SegmentControl::ActiveStyle::Warning);
                row.insMode  = addSegment({ "Pad", "Mute" }, SegmentControl::ActiveStyle::Warning);
            }
            else
            {
                // Each switch takes its knob's accent, so the pair reads as one unit.
                row.preMode  = addSlide(MuLookAndFeel::knobPrePad);
                auto* post   = addSlide(MuLookAndFeel::knobPostPad);
                row.postMode = post;
                row.insMode  = addSlide(MuLookAndFeel::knobInsertPad);
                if (r == 1) post->setSelectedIndex(1);   // one shown in Mute so both ends can be judged
            }
        }

        setSize(euclid::kSectionW, euclid::kSectionH);
    }

    void paint(juce::Graphics& g) override
    {
        using namespace euclid;
        using mu_ui::s;

        g.fillAll(MuLookAndFeel::colour(MuLookAndFeel::panelBackground));

        // Sub-panel borders, in the channel's own colour at half alpha — the blue a
        // fresh two-rhythm patch picks.
        g.setColour(MuLookAndFeel::channelPalette[1].withAlpha(0.5f));

        for (int rowY : rowOffsets)
        {
            const int cy = rowY + kLabelH;
            g.drawRoundedRectangle((float) s(padX - kSectionX), (float) s(cy),
                                   (float) s(padPanelW), (float) s(ctrlH) - 2.0f, 4.0f, 1.0f);
            g.drawRoundedRectangle((float) s(insX - kSectionX), (float) s(cy),
                                   (float) s(insPanelW), (float) s(ctrlH) - 2.0f, 4.0f, 1.0f);
        }
    }

    void resized() override
    {
        using namespace euclid;
        using mu_ui::s;

        for (int r = 0; r < kRows; ++r)
        {
            auto& row = rows[(size_t) r];
            const int cy = rowOffsets[r] + kLabelH;

            if (layout == PadLayout::Shipped)
            {
                row.prePad  ->setBounds(s(prePadX  - kSectionX), s(cy + mP), s(padKnobW), s(padKnobH));
                row.postPad ->setBounds(s(postPadX - kSectionX), s(cy + mP), s(padKnobW), s(padKnobH));
                row.insStart->setBounds(s(insStX   - kSectionX), s(cy + mP), s(padKnobW), s(padKnobH));
                row.insLen  ->setBounds(s(insLenX  - kSectionX), s(cy + mP), s(padKnobW), s(padKnobH));

                row.preMode ->setBounds(s(preSwX  - kSectionX), s(cy + knobH + 2), s(padSw), s(kSwitchH));
                row.postMode->setBounds(s(postSwX - kSectionX), s(cy + knobH + 2), s(padSw), s(kSwitchH));
                row.insMode ->setBounds(s(insSwX  - kSectionX), s(cy + knobH + 2), s(insSw), s(kSwitchH));
                continue;
            }

            // Proposed: each knob pairs with its switch side by side, so the knob owns
            // the sub-panel's full height instead of sharing it with a switch row.
            const int boxY   = cy;
            const int knobY  = boxY + (ctrlH - 2 - kBigKnobH) / 2;
            const int swY    = boxY + (ctrlH - 2 - kSlideH) / 2;

            // Pad panel: two [knob | switch] units, evenly spread.
            constexpr int unitW = kBigKnobW + kUnitGap + kSlideW;
            constexpr int padPairSpan = unitW * 2 + kUnitGap;
            constexpr int padLeft = padX - kSectionX + (padPanelW - padPairSpan) / 2;

            row.prePad  ->setBounds(s(padLeft), s(knobY), s(kBigKnobW), s(kBigKnobH));
            row.preMode ->setBounds(s(padLeft + kBigKnobW + kUnitGap), s(swY), s(kSlideW), s(kSlideH));

            constexpr int unit2X = padLeft + unitW + kUnitGap;
            row.postPad ->setBounds(s(unit2X), s(knobY), s(kBigKnobW), s(kBigKnobH));
            row.postMode->setBounds(s(unit2X + kBigKnobW + kUnitGap), s(swY), s(kSlideW), s(kSlideH));

            // Insert panel: the pair shares one switch, so it sits to their right.
            constexpr int insSpan  = kBigKnobW * 2 + kInsKnobGap + kUnitGap * 2 + kSlideW;
            constexpr int insLeft  = insX - kSectionX + (insPanelW - insSpan) / 2;
            constexpr int insLen2X = insLeft + kBigKnobW + kInsKnobGap;

            row.insStart->setBounds(s(insLeft),  s(knobY), s(kBigKnobW), s(kBigKnobH));
            row.insLen  ->setBounds(s(insLen2X), s(knobY), s(kBigKnobW), s(kBigKnobH));
            row.insMode ->setBounds(s(insLen2X + kBigKnobW + kUnitGap * 2), s(swY), s(kSlideW), s(kSlideH));
        }
    }

private:
    static constexpr int kRows = 3;   // Euclid A, Euclid B, Accent

    // Proposed layout: Size 1 — with the logic band gone each row is 88 px, so the
    // sub-panel's 76 px box takes a 70 px knob.
    static constexpr int kBigKnobW  = MuLookAndFeel::kKnobSize1W;   // 68
    static constexpr int kBigKnobH  = MuLookAndFeel::kKnobSize1H;   // 70
    static constexpr int kUnitGap    = MuLookAndFeel::kSpaceS;      // knob to its own switch
    static constexpr int kInsKnobGap = MuLookAndFeel::kKnobGapRow;  // between the two insert knobs
    static constexpr int kSlideW    = MuLookAndFeel::kSlideSwitchW;
    static constexpr int kSlideH    = MuLookAndFeel::kSlideSwitchH;

    struct Row
    {
        KnobWithLabel*  prePad   = nullptr;
        KnobWithLabel*  postPad  = nullptr;
        KnobWithLabel*  insStart = nullptr;
        KnobWithLabel*  insLen   = nullptr;
        juce::Component* preMode  = nullptr;   // SegmentControl (shipped) or SlideSwitch
        juce::Component* postMode = nullptr;
        juce::Component* insMode  = nullptr;
    };

    KnobWithLabel* addKnob(const juce::String& label, MuLookAndFeel::ColourIds colour,
                           double lo, double hi, double value)
    {
        auto k = std::make_unique<KnobWithLabel>(label, colour);
        k->setRange(lo, hi, 1);
        k->setValue(value, juce::dontSendNotification);
        addAndMakeVisible(*k);
        auto* raw = k.get();
        knobs.push_back(std::move(k));
        return raw;
    }

    SegmentControl* addSegment(std::initializer_list<juce::String> labels,
                               SegmentControl::ActiveStyle style,
                               SegmentControl::DrawStyle draw = SegmentControl::DrawStyle::Bar)
    {
        auto sc = std::make_unique<SegmentControl>(labels, style, draw);
        addAndMakeVisible(*sc);
        auto* raw = sc.get();
        segments.push_back(std::move(sc));
        return raw;
    }

    SlideSwitch* addSlide(MuLookAndFeel::ColourIds accent)
    {
        auto sw = std::make_unique<SlideSwitch>("Pad", "Mute", accent);
        addAndMakeVisible(*sw);
        auto* raw = sw.get();
        slides.push_back(std::move(sw));
        return raw;
    }

    PadLayout layout;
    std::array<Row, kRows> rows;

    std::vector<std::unique_ptr<KnobWithLabel>>  knobs;
    std::vector<std::unique_ptr<SegmentControl>> segments;
    std::vector<std::unique_ptr<SlideSwitch>>    slides;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PadSection)
};

// Both layouts stacked, each rendered at the exact size the section occupies in
// mu-clid, so they can be compared without either being reinterpreted.
class ControlDesignPanel : public juce::Component
{
public:
    ControlDesignPanel()
    {
        setLookAndFeel(&lookAndFeel);
        addAndMakeVisible(shipped);
        addAndMakeVisible(proposed);
        setSize(euclid::kSectionW + 2 * kMargin,
                euclid::kSectionH * 2 + kCaptionH * 2 + kMargin * 3);
    }

    ~ControlDesignPanel() override { setLookAndFeel(nullptr); }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(MuLookAndFeel::colour(MuLookAndFeel::windowBackground));

        g.setColour(MuLookAndFeel::colour(MuLookAndFeel::mutedText));
        g.setFont(juce::Font(juce::FontOptions(11.0f)));
        g.drawText("shipped - switch under the knob, Size 3",
                   kMargin, shipped.getY() - kCaptionH, euclid::kSectionW, kCaptionH,
                   juce::Justification::centredLeft, false);
        g.drawText("proposed - slide switch alongside, Size 1",
                   kMargin, proposed.getY() - kCaptionH, euclid::kSectionW, kCaptionH,
                   juce::Justification::centredLeft, false);
    }

    void resized() override
    {
        const int y1 = kMargin + kCaptionH;
        shipped .setBounds(kMargin, y1, euclid::kSectionW, euclid::kSectionH);
        const int y2 = y1 + euclid::kSectionH + kMargin + kCaptionH;
        proposed.setBounds(kMargin, y2, euclid::kSectionW, euclid::kSectionH);
    }

private:
    static constexpr int kMargin   = 16;
    static constexpr int kCaptionH = 16;

    MuLookAndFeel lookAndFeel;
    PadSection    shipped  { PadLayout::Shipped };
    PadSection    proposed { PadLayout::Proposed };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ControlDesignPanel)
};

class ControlDesignApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "ControlDesign"; }
    const juce::String getApplicationVersion() override { return "1.0"; }
    bool moreThanOneInstanceAllowed() override          { return true; }

    void initialise(const juce::String&) override
    {
        mainWindow = std::make_unique<MainWindow>(getApplicationName());
    }
    void shutdown() override { mainWindow = nullptr; }
    void systemRequestedQuit() override { quit(); }

private:
    class MainWindow : public juce::DocumentWindow
    {
    public:
        explicit MainWindow(const juce::String& name)
            : DocumentWindow(name, juce::Colour(0xff1c1c1b), DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar(true);
            setContentOwned(new ControlDesignPanel(), true);
            // Fixed: the whole point is that the section matches mu-clid pixel for pixel.
            setResizable(false, false);
            centreWithSize(getWidth(), getHeight());
            setVisible(true);
        }

        void closeButtonPressed() override { JUCEApplication::getInstance()->systemRequestedQuit(); }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
    };

    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION(ControlDesignApplication)
