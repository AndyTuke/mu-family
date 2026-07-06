// ControlDesign — a GUI sandbox for iterating on the mu knob style.
//
// A single panel showing the family's four knob sizes (MuLookAndFeel::kKnobSize1..4),
// each a real KnobWithLabel with a 0–100 value, so we can trial new rotary styles in
// MuLookAndFeel::drawRotarySlider and see them live at every size.
//
//   Build:  cmake --build build --config Debug --target ControlDesign
//   Run:    build/ControlDesign/ControlDesign_artefacts/Debug/ControlDesign.exe

#include <juce_gui_extra/juce_gui_extra.h>

#include "UI/Components/MuLookAndFeel.h"
#include "UI/Components/KnobWithLabel.h"

#include <array>
#include <memory>

// The panel: four knobs, one per family size, labelled Knob 1–4, value 0–100.
class ControlDesignPanel : public juce::Component
{
public:
    ControlDesignPanel()
    {
        setLookAndFeel(&lnf);

        using LF = MuLookAndFeel;
        // One knob per size; the four category colours so a style shows across the palette.
        const LF::ColourIds colours[kNum] = { LF::knobEuclidean, LF::knobPostPad,
                                              LF::knobLevel,      LF::knobFxSend };
        sizeW = { LF::kKnobSize1W, LF::kKnobSize2W, LF::kKnobSize3W, LF::kKnobSize4W };
        sizeH = { LF::kKnobSize1H, LF::kKnobSize2H, LF::kKnobSize3H, LF::kKnobSize4H };

        for (int i = 0; i < kNum; ++i)
        {
            auto k = std::make_unique<KnobWithLabel>("Knob " + juce::String(i + 1), colours[i]);
            k->setRange(0.0, 100.0, 1.0);
            k->setValue(67.0);
            addAndMakeVisible(*k);
            knobs[(size_t) i] = std::move(k);
        }

        setSize(680, 300);
    }

    ~ControlDesignPanel() override { setLookAndFeel(nullptr); }

    void paint(juce::Graphics& g) override
    {
        using Id = MuLookAndFeel::ColourIds;
        g.fillAll(MuLookAndFeel::colour(Id::windowBackground));

        g.setColour(MuLookAndFeel::colour(Id::headingText));
        g.setFont(juce::Font(juce::FontOptions(15.0f, juce::Font::bold)));
        g.drawText("Control Design — knob sizes", getLocalBounds().removeFromTop(38).reduced(20, 0),
                   juce::Justification::centredLeft, false);

        // Dimension caption under each knob cell (the KnobWithLabel draws its own name).
        g.setColour(MuLookAndFeel::colour(Id::mutedText));
        g.setFont(juce::Font(juce::FontOptions(11.0f)));
        for (int i = 0; i < kNum; ++i)
        {
            const juce::String dims = "Size " + juce::String(i + 1) + " · "
                                    + juce::String(sizeW[(size_t) i]) + "×" + juce::String(sizeH[(size_t) i]);
            auto r = cell[(size_t) i].withY(cell[(size_t) i].getBottom() + 4).withHeight(16);
            g.drawText(dims, r, juce::Justification::centred, false);
        }
    }

    void resized() override
    {
        // Lay the four sizes in a row, vertically centred, with even gaps.
        int totalW = 0;
        for (int i = 0; i < kNum; ++i) totalW += sizeW[(size_t) i];
        const int gap = 56;
        int x = (getWidth() - (totalW + gap * (kNum - 1))) / 2;
        const int cy = getHeight() / 2 + 6;

        for (int i = 0; i < kNum; ++i)
        {
            const int w = sizeW[(size_t) i], h = sizeH[(size_t) i];
            cell[(size_t) i] = { x, cy - h / 2, w, h };
            knobs[(size_t) i]->setBounds(cell[(size_t) i]);
            x += w + gap;
        }
    }

private:
    static constexpr int kNum = 4;

    MuLookAndFeel lnf;
    std::array<std::unique_ptr<KnobWithLabel>, kNum> knobs;
    std::array<int, kNum> sizeW {}, sizeH {};
    std::array<juce::Rectangle<int>, kNum> cell;

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
            setResizable(true, true);
            setResizeLimits(520, 240, 1400, 900);
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
