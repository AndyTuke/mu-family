// ControlDesign — a GUI sandbox showing the knob we actually ship, for refining
// its design in isolation without rebuilding a whole product.
//
// Every row below uses the real KnobWithLabel component (mu-core/UI/Components/
// KnobWithLabel.h) — the same class every product instantiates — so a change to
// MuLookAndFeel::drawRotarySlider or to KnobWithLabel's label/value/overlay drawing
// shows up here exactly as it will in mu-clid / mu-tant / mu-toni / mu-on.
//
//   Row 1 — the four family sizes (kKnobSize1..4), same colour, so you can judge
//           the knob's proportions as it shrinks.
//   Row 2 — every category colour token at Size 2, the size used in most voice/
//           mixer rows, so the palette can be judged side by side.
//   Row 3 — Size 2 in each overlay state a knob can be drawn in: plain, statically
//           modulated (ring only), actively modulated (ring + live arc), and under
//           gain reduction (GR arc) — these overlays are as much "the knob design"
//           as the base rotary.
//
//   Build:  cmake --build build --config Debug --target ControlDesign
//   Run:    build/ControlDesign/ControlDesign_artefacts/Debug/ControlDesign.exe

#include <juce_gui_extra/juce_gui_extra.h>

#include "UI/Components/KnobWithLabel.h"
#include "UI/Components/MuLookAndFeel.h"

#include <atomic>
#include <memory>
#include <vector>

// One row: a title + description in the left label column, and a strip of
// KnobWithLabel instances (each already sized by the caller) laid out with even
// gaps, vertically centred on the row.
class KnobRow : public juce::Component
{
public:
    KnobRow(juce::String rowName, juce::String rowDesc)
        : name(std::move(rowName)), desc(std::move(rowDesc)) {}

    // Adds a knob at (w, h), sets a placeholder value, and returns it so the
    // caller can apply overlay state (setIsModulated, setGRSource, etc).
    KnobWithLabel& addKnob(const juce::String& label, MuLookAndFeel::ColourIds colour,
                           int w, int h)
    {
        auto k = std::make_unique<KnobWithLabel>(label, colour);
        k->setSize(w, h);
        k->setRange(0.0, 100.0, 1.0);
        k->setValue(67.0, juce::dontSendNotification);
        addAndMakeVisible(*k);
        knobs.push_back(std::move(k));
        return *knobs.back();
    }

    void paint(juce::Graphics& g) override
    {
        using Id = MuLookAndFeel::ColourIds;
        auto label = getLocalBounds().removeFromLeft(kLabelW).reduced(14, 0);
        g.setColour(MuLookAndFeel::colour(Id::headingText));
        g.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
        g.drawText(name, label.removeFromTop(label.getHeight() / 2).withTrimmedTop(14),
                   juce::Justification::bottomLeft, false);

        g.setColour(MuLookAndFeel::colour(Id::mutedText));
        g.setFont(juce::Font(juce::FontOptions(11.0f)));
        g.drawText(desc, label.withTrimmedBottom(14), juce::Justification::topLeft, false);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        area.removeFromLeft(kLabelW);

        int totalW = 0;
        for (auto& k : knobs) totalW += k->getWidth();
        const int gap = 28;
        int x = area.getX() + (area.getWidth() - (totalW + gap * ((int) knobs.size() - 1))) / 2;
        const int cy = area.getCentreY();

        for (auto& k : knobs)
        {
            k->setTopLeftPosition(x, cy - k->getHeight() / 2);
            x += k->getWidth() + gap;
        }
    }

    static constexpr int kLabelW = 170;

private:
    juce::String name, desc;
    std::vector<std::unique_ptr<KnobWithLabel>> knobs;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KnobRow)
};

// The panel: a title strip plus the three KnobRows described above.
class ControlDesignPanel : public juce::Component
{
public:
    ControlDesignPanel()
    {
        using Id = MuLookAndFeel::ColourIds;

        // Row 1 — family sizes, held at one colour so only proportion varies.
        auto sizes = std::make_unique<KnobRow>("Family sizes",
                                               "kKnobSize1..4, same colour + value");
        sizes->addKnob("Size 1", Id::knobLevel, MuLookAndFeel::kKnobSize1W, MuLookAndFeel::kKnobSize1H);
        sizes->addKnob("Size 2", Id::knobLevel, MuLookAndFeel::kKnobSize2W, MuLookAndFeel::kKnobSize2H);
        sizes->addKnob("Size 3", Id::knobLevel, MuLookAndFeel::kKnobSize3W, MuLookAndFeel::kKnobSize3H);
        sizes->addKnob("Size 4", Id::knobLevel, MuLookAndFeel::kKnobSize4W, MuLookAndFeel::kKnobSize4H);
        addAndMakeVisible(*sizes);
        rows.push_back(std::move(sizes));

        // Row 2 — every category colour token, at Size 2 (the most common row size).
        auto colours = std::make_unique<KnobRow>("Category colours",
                                                  "Size 2 across every knob colour token");
        struct ColourEntry { const char* label; MuLookAndFeel::ColourIds colour; };
        static constexpr ColourEntry entries[] = {
            { "Euclid",   Id::knobEuclidean },
            { "Insert",   Id::knobInsertPad },
            { "Level",    Id::knobLevel     },
            { "FX Send",  Id::knobFxSend    },
            { "Reverb",   Id::knobReverb    },
            { "Pan",      Id::knobPan       },
            { "Pre Pad",  Id::knobPrePad    },
            { "Post Pad", Id::knobPostPad   },
        };
        for (auto& e : entries)
            colours->addKnob(e.label, e.colour, MuLookAndFeel::kKnobSize2W, MuLookAndFeel::kKnobSize2H);
        addAndMakeVisible(*colours);
        rows.push_back(std::move(colours));

        // Row 3 — overlay states a shipped knob can be drawn in.
        auto states = std::make_unique<KnobRow>("Overlay states",
            juce::String(juce::CharPointer_UTF8(
                "Size 2 \xe2\x80\x94 plain / mod ring / mod arc / GR arc")));
        states->addKnob("Plain", Id::knobLevel, MuLookAndFeel::kKnobSize2W, MuLookAndFeel::kKnobSize2H);

        auto& modRing = states->addKnob("Mod ring", Id::knobLevel,
                                        MuLookAndFeel::kKnobSize2W, MuLookAndFeel::kKnobSize2H);
        modRing.setIsModulated(true);

        auto& modArc = states->addKnob("Mod + arc", Id::knobLevel,
                                       MuLookAndFeel::kKnobSize2W, MuLookAndFeel::kKnobSize2H);
        modArc.setIsModulated(true);
        modArc.setModulatedNorm(0.85f);

        auto& grKnob = states->addKnob("GR arc", Id::knobLevel,
                                       MuLookAndFeel::kKnobSize2W, MuLookAndFeel::kKnobSize2H);
        grKnob.setGRSource(&grLevel);

        addAndMakeVisible(*states);
        rows.push_back(std::move(states));

        setSize(900, 480);
    }

    void paint(juce::Graphics& g) override
    {
        using Id = MuLookAndFeel::ColourIds;
        g.fillAll(MuLookAndFeel::colour(Id::windowBackground));

        g.setColour(MuLookAndFeel::colour(Id::headingText));
        g.setFont(juce::Font(juce::FontOptions(15.0f, juce::Font::bold)));
        g.drawText(juce::String(juce::CharPointer_UTF8(
                       "Control Design \xe2\x80\x94 the knob we ship")),
                   getLocalBounds().removeFromTop(kHeader).reduced(20, 0),
                   juce::Justification::centredLeft, false);

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

    std::vector<std::unique_ptr<KnobRow>> rows;
    std::atomic<float> grLevel { 0.4f };   // fixed GR level for the "GR arc" demo knob

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
