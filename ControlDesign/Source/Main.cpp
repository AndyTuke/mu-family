// ControlDesign — a GUI sandbox for comparing knob styles at the family sizes.
//
// A style × size matrix: four rows, one per JUCE built-in LookAndFeel (V1–V4), and
// four columns, one per family knob size (MuLookAndFeel::kKnobSize1..4). Read down a
// column to compare the stock rotary appearances at a fixed size; read across a row to
// see one style at every size we actually ship. Colours are held identical across all
// rows so only the drawing geometry differs; every knob sits at the same value.
//
//   Build:  cmake --build build --config Debug --target ControlDesign
//   Run:    build/ControlDesign/ControlDesign_artefacts/Debug/ControlDesign.exe

#include <juce_gui_extra/juce_gui_extra.h>

#include "UI/Components/MuLookAndFeel.h"

#include <array>
#include <memory>
#include <vector>

// One row: a named LookAndFeel applied to a row of rotary sliders, one per family knob
// size. The LookAndFeel is set on this component so it propagates to the child sliders —
// swapping the row's LookAndFeel swaps how every knob in the row is drawn.
class StyleRow : public juce::Component
{
public:
    // The family's four canonical knob dimensions (W×H), largest → smallest.
    static constexpr int kNum = 4;
    static constexpr int kSizeW[kNum] { MuLookAndFeel::kKnobSize1W, MuLookAndFeel::kKnobSize2W,
                                        MuLookAndFeel::kKnobSize3W, MuLookAndFeel::kKnobSize4W };
    static constexpr int kSizeH[kNum] { MuLookAndFeel::kKnobSize1H, MuLookAndFeel::kKnobSize2H,
                                        MuLookAndFeel::kKnobSize3H, MuLookAndFeel::kKnobSize4H };

    StyleRow(juce::String styleName, juce::String descriptor,
             std::unique_ptr<juce::LookAndFeel> lookAndFeel, bool showSizeCaptions)
        : name(std::move(styleName)), desc(std::move(descriptor)),
          lnf(std::move(lookAndFeel)), captions(showSizeCaptions)
    {
        setLookAndFeel(lnf.get());

        // Shared knob colours so only the LookAndFeel's geometry varies row to row.
        using Id = MuLookAndFeel::ColourIds;
        const auto accent  = MuLookAndFeel::colour(Id::knobLevel);
        const auto outline = MuLookAndFeel::colour(Id::mutedText);
        const auto thumb   = MuLookAndFeel::colour(Id::headingText);

        for (int i = 0; i < kNum; ++i)
        {
            auto s = std::make_unique<juce::Slider>(juce::Slider::RotaryVerticalDrag,
                                                    juce::Slider::NoTextBox);
            s->setRange(0.0, 100.0, 1.0);
            s->setValue(67.0, juce::dontSendNotification);
            s->setColour(juce::Slider::rotarySliderFillColourId,    accent);
            s->setColour(juce::Slider::rotarySliderOutlineColourId, outline);
            s->setColour(juce::Slider::thumbColourId,               thumb);
            addAndMakeVisible(*s);
            knobs[(size_t) i] = std::move(s);
        }
    }

    ~StyleRow() override { setLookAndFeel(nullptr); }

    void paint(juce::Graphics& g) override
    {
        using Id = MuLookAndFeel::ColourIds;

        // Style name + one-line descriptor in the left label column.
        auto label = getLocalBounds().removeFromLeft(kLabelW).reduced(14, 0);
        g.setColour(MuLookAndFeel::colour(Id::headingText));
        g.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
        g.drawText(name, label.removeFromTop(label.getHeight() / 2).withTrimmedTop(14),
                   juce::Justification::bottomLeft, false);

        g.setColour(MuLookAndFeel::colour(Id::mutedText));
        g.setFont(juce::Font(juce::FontOptions(11.0f)));
        g.drawText(desc, label.withTrimmedBottom(14), juce::Justification::topLeft, false);

        // Size caption under each knob — drawn on the bottom row only, so the columns
        // are labelled once without repeating under every style.
        if (captions)
        {
            g.setColour(MuLookAndFeel::colour(Id::mutedText));
            g.setFont(juce::Font(juce::FontOptions(10.0f)));
            for (int i = 0; i < kNum; ++i)
            {
                const juce::String dims = "Size " + juce::String(i + 1) + " · "
                                        + juce::String(kSizeW[i]) + "×" + juce::String(kSizeH[i]);
                auto r = cell[(size_t) i].withY(cell[(size_t) i].getBottom() + 3).withHeight(13)
                                         .expanded(24, 0);
                g.drawText(dims, r, juce::Justification::centred, false);
            }
        }
    }

    void resized() override
    {
        // Knobs in a row, vertically centred, in the area right of the style label.
        auto area = getLocalBounds();
        area.removeFromLeft(kLabelW);
        if (captions) area.removeFromBottom(kCaptionH);   // reserve room for the size labels

        int totalW = 0;
        for (int i = 0; i < kNum; ++i) totalW += kSizeW[i];
        const int gap = 44;
        int x = area.getX() + (area.getWidth() - (totalW + gap * (kNum - 1))) / 2;
        const int cy = area.getCentreY();

        for (int i = 0; i < kNum; ++i)
        {
            cell[(size_t) i] = { x, cy - kSizeH[i] / 2, kSizeW[i], kSizeH[i] };
            knobs[(size_t) i]->setBounds(cell[(size_t) i]);
            x += kSizeW[i] + gap;
        }
    }

    static constexpr int kLabelW   = 170;
    static constexpr int kCaptionH = 16;

private:
    juce::String name, desc;
    std::unique_ptr<juce::LookAndFeel> lnf;
    bool captions;
    std::array<std::unique_ptr<juce::Slider>, kNum> knobs;
    std::array<juce::Rectangle<int>, kNum> cell;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StyleRow)
};

// The panel: a title strip plus one StyleRow per built-in JUCE rotary appearance.
class ControlDesignPanel : public juce::Component
{
public:
    ControlDesignPanel()
    {
        struct Style { juce::String name, desc; std::unique_ptr<juce::LookAndFeel> lf; };
        std::vector<Style> styles;
        styles.push_back({ "LookAndFeel_V1", "flat notched knob",     std::make_unique<juce::LookAndFeel_V1>() });
        styles.push_back({ "LookAndFeel_V2", "glossy gradient body",  std::make_unique<juce::LookAndFeel_V2>() });
        styles.push_back({ "LookAndFeel_V3", "dark knob + value arc", std::make_unique<juce::LookAndFeel_V3>() });
        styles.push_back({ "LookAndFeel_V4", "flat arc + pointer",    std::make_unique<juce::LookAndFeel_V4>() });

        for (size_t i = 0; i < styles.size(); ++i)
        {
            const bool last = (i == styles.size() - 1);   // captions on the bottom row only
            auto r = std::make_unique<StyleRow>(styles[i].name, styles[i].desc,
                                                std::move(styles[i].lf), last);
            addAndMakeVisible(*r);
            rows.push_back(std::move(r));
        }

        setSize(740, 500);
    }

    void paint(juce::Graphics& g) override
    {
        using Id = MuLookAndFeel::ColourIds;
        g.fillAll(MuLookAndFeel::colour(Id::windowBackground));

        g.setColour(MuLookAndFeel::colour(Id::headingText));
        g.setFont(juce::Font(juce::FontOptions(15.0f, juce::Font::bold)));
        g.drawText("Control Design — JUCE rotary styles at the family sizes",
                   getLocalBounds().removeFromTop(kHeader).reduced(20, 0),
                   juce::Justification::centredLeft, false);

        // Row separators.
        g.setColour(MuLookAndFeel::colour(Id::mutedText).withAlpha(0.25f));
        const int rowH = (getHeight() - kHeader) / (int) rows.size();
        for (size_t i = 1; i < rows.size(); ++i)
            g.drawHorizontalLine(kHeader + (int) i * rowH, 20.0f, (float) getWidth() - 20.0f);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        area.removeFromTop(kHeader);
        const int rowH = area.getHeight() / (int) rows.size();
        for (auto& r : rows)
            r->setBounds(area.removeFromTop(rowH));
    }

private:
    static constexpr int kHeader = 44;

    std::vector<std::unique_ptr<StyleRow>> rows;

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
            setResizeLimits(560, 380, 1400, 900);
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
