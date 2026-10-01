// ControlDesign — a GUI sandbox for refining the family's controls in isolation,
// without rebuilding a whole product.
//
// Currently focused on **mu-clid's Pad / Insert sub-panels**: the two bordered boxes
// that sit to the right of each Euclid row's Steps/Hits/Rotate block, plus the Legato
// and Mono band between rows A and B.
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

#include <array>
#include <memory>
#include <vector>

// ── Geometry mirrored from mu-clid's EuclideanPanel ──────────────────────────
namespace euclid
{
    // EuclideanPanel's own constants
    constexpr int kLogicH       = 24;
    constexpr int kSwitchH      = 14;
    constexpr int kOuter        = 4;
    constexpr int kLabelH       = 10;
    constexpr int kLogicVOffset = 3;
    constexpr int kEucKnobGap   = 18;
    constexpr int kPadKnobGap   = 48;
    constexpr int kPadInsertGap = 6;

    constexpr int w      = MuLookAndFeel::kEuclidInnerW;          // 786
    constexpr int innerW = w - 2 * kOuter;                        // 778
    constexpr int innerH = MuLookAndFeel::kEuclidInnerH - 2 * kOuter;   // 266

    constexpr int rowH  = (innerH - kLogicH) / 3;                 // 80
    constexpr int ctrlH = rowH - kLabelH;                         // 70
    constexpr int mP    = 4;

    constexpr int eW        = MuLookAndFeel::kKnobSize1W;
    constexpr int eucBlockW = eW * 3 + kEucKnobGap * 2;           // 240
    constexpr int pW        = (innerW - eucBlockW) / 4;           // 134

    constexpr int padX      = kOuter + eucBlockW;                 // 244
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
    constexpr int rowOffsets[3] = { kOuter, kOuter + rowH + kLogicH, kOuter + 2 * rowH + kLogicH };

    // The section this sandbox shows: everything from the Pad sub-panel's left edge to
    // the Insert sub-panel's right edge, full panel height. Coordinates below are
    // shifted left by kSectionX so the section sits at the window's origin.
    constexpr int kSectionX = padX;                               // 244
    constexpr int kSectionW = (w - kOuter) - padX;                // 538
    constexpr int kSectionH = innerH;                             // 266
}

// mu-clid's Pad and Insert sub-panels, at the size and spacing they ship at.
class PadSection : public juce::Component
{
public:
    PadSection()
    {
        using namespace euclid;

        for (int r = 0; r < kRows; ++r)
        {
            auto& row = rows[(size_t) r];
            row.prePad   = addKnob("Pre Pad",       MuLookAndFeel::knobPrePad,    0, 12, 0);
            row.postPad  = addKnob("Post Pad",      MuLookAndFeel::knobPostPad,   0, 12, 0);
            row.insStart = addKnob("Insert Start",  MuLookAndFeel::knobInsertPad, 0, 63, 0);
            row.insLen   = addKnob("Insert Length", MuLookAndFeel::knobInsertPad, 0,  8, 0);

            row.preMode  = addSegment({ "Pad", "Mute" }, SegmentControl::ActiveStyle::Warning);
            row.postMode = addSegment({ "Pad", "Mute" }, SegmentControl::ActiveStyle::Warning);
            row.insMode  = addSegment({ "Pad", "Mute" }, SegmentControl::ActiveStyle::Warning);
        }

        legato = addSegment({ "Trig", "Leg" },  SegmentControl::ActiveStyle::General,
                            SegmentControl::DrawStyle::Pills);
        mono   = addSegment({ "Poly", "Mono" }, SegmentControl::ActiveStyle::Warning,
                            SegmentControl::DrawStyle::Pills);

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

        constexpr int rectY = kOuter + rowH + 2 + kLogicVOffset;
        constexpr int rectH = kLogicH - 4;
        g.drawRoundedRectangle((float) s(padX - kSectionX), (float) s(rectY),
                               (float) s(padPanelW), (float) s(rectH), 4.0f, 1.0f);
        g.drawRoundedRectangle((float) s(insX - kSectionX), (float) s(rectY),
                               (float) s(insPanelW), (float) s(rectH), 4.0f, 1.0f);
    }

    void resized() override
    {
        using namespace euclid;
        using mu_ui::s;

        for (int r = 0; r < kRows; ++r)
        {
            auto& row = rows[(size_t) r];
            const int cy = rowOffsets[r] + kLabelH;

            row.prePad  ->setBounds(s(prePadX  - kSectionX), s(cy + mP), s(padKnobW), s(padKnobH));
            row.postPad ->setBounds(s(postPadX - kSectionX), s(cy + mP), s(padKnobW), s(padKnobH));
            row.insStart->setBounds(s(insStX   - kSectionX), s(cy + mP), s(padKnobW), s(padKnobH));
            row.insLen  ->setBounds(s(insLenX  - kSectionX), s(cy + mP), s(padKnobW), s(padKnobH));

            row.preMode ->setBounds(s(preSwX  - kSectionX), s(cy + knobH + 2), s(padSw), s(kSwitchH));
            row.postMode->setBounds(s(postSwX - kSectionX), s(cy + knobH + 2), s(padSw), s(kSwitchH));
            row.insMode ->setBounds(s(insSwX  - kSectionX), s(cy + knobH + 2), s(insSw), s(kSwitchH));
        }

        // Legato aligns with the Pad sub-panel, Mono with the Insert sub-panel.
        constexpr int rectY = kOuter + rowH + 2 + kLogicVOffset;
        constexpr int rectH = kLogicH - 4;
        legato->setBounds(s(padX - kSectionX) + s(4), s(rectY) + s(2),
                          s(padPanelW) - s(8), s(rectH) - s(4));
        mono  ->setBounds(s(insX - kSectionX) + s(4), s(rectY) + s(2),
                          s(insPanelW) - s(8), s(rectH) - s(4));
    }

private:
    static constexpr int kRows = 3;   // Euclid A, Euclid B, Accent

    struct Row
    {
        KnobWithLabel*  prePad   = nullptr;
        KnobWithLabel*  postPad  = nullptr;
        KnobWithLabel*  insStart = nullptr;
        KnobWithLabel*  insLen   = nullptr;
        SegmentControl* preMode  = nullptr;
        SegmentControl* postMode = nullptr;
        SegmentControl* insMode  = nullptr;
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

    std::array<Row, kRows> rows;
    SegmentControl* legato = nullptr;
    SegmentControl* mono   = nullptr;

    std::vector<std::unique_ptr<KnobWithLabel>>  knobs;
    std::vector<std::unique_ptr<SegmentControl>> segments;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PadSection)
};

// Holds the section at its exact shipped size, centred on the window's background so
// the borders aren't flush against the frame.
class ControlDesignPanel : public juce::Component
{
public:
    ControlDesignPanel()
    {
        setLookAndFeel(&lookAndFeel);
        addAndMakeVisible(section);
        setSize(euclid::kSectionW + 2 * kMargin, euclid::kSectionH + 2 * kMargin);
    }

    ~ControlDesignPanel() override { setLookAndFeel(nullptr); }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(MuLookAndFeel::colour(MuLookAndFeel::windowBackground));
    }

    void resized() override
    {
        section.setBounds(kMargin, kMargin, euclid::kSectionW, euclid::kSectionH);
    }

private:
    static constexpr int kMargin = 16;

    MuLookAndFeel lookAndFeel;
    PadSection    section;

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
