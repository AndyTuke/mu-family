// ControlDesign — a GUI sandbox for refining the family knob in isolation, without
// rebuilding a whole product.
//
// Every knob here is a real example pulled from a shipping panel — same label, same
// category colour, same MuLookAndFeel::kKnobSize*, same range, value and value
// formatter — drawn by the actual KnobWithLabel component every product instantiates.
// So an edit to MuLookAndFeel::drawRotarySlider shows up here exactly as it will in
// mu-clid, mu-tant, mu-toni, mu-on and mu-link.
//
// Coverage is one knob per size a colour is used at, per control type where a size
// uses both, plus the overlay states and the whole category palette:
//
//   mu-clid, purple (knobEuclidean)
//     Size 1, step   — Steps     (1..64 x1)        EuclideanPanel
//     Size 1, smooth — Attack    (0..10s, skewed)  not a shipped combination
//     Size 2, step   — Octave    (-3..3 x1)        Voice/PitchSubsection
//     Size 2, smooth — Attack    (0..10s, skewed)  Voice/PitchSubsection
//   mu-tant, green (knobPostPad)
//     Size 1, smooth — Cutoff    (skewed, Hz)      not a shipped combination
//     Size 2, smooth — Cutoff    (skewed, Hz)      VoicePanel Filter 1
//   Small sizes
//     Size 3 — mu-clid pad knobs (step), mixer strip (smooth)
//     Size 4 — mixer sidechain envelope (smooth); the step case ships nowhere
//
// Knobs marked "not shipped" exist to judge the style at a size/type the products
// don't currently use. mu-clid's purple is step-only at Size 1, and mu-tant's green
// appears at Size 2 only, where all five filter knobs are continuous floats (their
// ranges come from APVTS parameters, not setRange), so green has no step case.
//
// Installs a real MuLookAndFeel via setLookAndFeel() on the panel, as EditorShellBase
// does for every product's editor — without it KnobWithLabel falls back to JUCE's
// default LookAndFeel and draws nothing like the shipped knob.
//
//   Build:  cmake --build build --config Debug --target ControlDesign
//   Run:    build/ControlDesign/ControlDesign_artefacts/Debug/ControlDesign.exe

#include <juce_gui_extra/juce_gui_extra.h>

#include "UI/Components/KnobWithLabel.h"
#include "UI/Components/MuLookAndFeel.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <functional>
#include <memory>
#include <vector>

// juce::String reads a bare const char* in the system codepage, which mangles the
// UTF-8 bytes in literals like an em dash or a middot; wrap them explicitly.
static juce::String utf8(const char* s) { return juce::String(juce::CharPointer_UTF8(s)); }

// The products' own value formatters, mirrored so each knob shows exactly the text it
// shows in the app (the unit lives in the label, not the value). Without these JUCE's
// raw float formatting takes over and we'd be judging something that never ships.
//   mu-clid Voice/PitchSubsection + AmpSubsection + FilterSubsection: adsrValueStr
static juce::String adsrValueText(double v)
{
    const double ms = std::max(1.0, v * 1000.0);
    return ms < 1000.0 ? juce::String((int) std::round(ms))
                       : juce::String(ms / 1000.0, 2);
}

//   mu-tant PluginProcessor_APVTS: the flt_cut cutoffText attribute
static juce::String cutoffValueText(double v)
{
    return v < 1000.0 ? juce::String((int) std::round(v))
                      : juce::String(v / 1000.0, 1);
}

// One row: a title and description in the left label column, then a strip of knobs
// laid out with even gaps and vertically centred, each captioned underneath.
class KnobRow : public juce::Component
{
public:
    KnobRow(juce::String rowName, juce::String rowDesc)
        : name(std::move(rowName)), desc(std::move(rowDesc)) {}

    // textFn, when given, is the product's own value formatter for this parameter.
    // Whether a value is drawn at all, and where, is the style's decision — and
    // stepped-vs-smooth is derived from the slider's interval — so neither is declared
    // here; this mirrors what a product does and nothing more.
    KnobWithLabel& addKnob(const juce::String& label, MuLookAndFeel::ColourIds colour,
                           int w, int h, double lo, double hi, double step, double value,
                           const juce::String& caption,
                           std::function<juce::String(double)> textFn = nullptr)
    {
        auto k = std::make_unique<KnobWithLabel>(label, colour);
        k->setSize(w, h);
        k->setRange(lo, hi, step);
        k->setValue(value, juce::dontSendNotification);
        if (textFn)
        {
            k->getSlider().textFromValueFunction = textFn;
            k->getSlider().setValue(value, juce::dontSendNotification);   // re-render with it
        }
        addAndMakeVisible(*k);
        auto& ref = *k;
        entries.push_back({ std::move(k), label, caption });
        return ref;
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

    // Outlines each knob's real bounds so anything drawn outside them reads at a
    // glance, captions each knob, and flags labels that don't fit their knob's width —
    // KnobWithLabel silently ellipsises those in the products, hiding exactly the
    // problem worth catching, so the full label is redrawn unclipped in red.
    void paintOverChildren(juce::Graphics& g) override
    {
        using mu_ui::sf;

        g.setColour(juce::Colours::white.withAlpha(0.25f));
        for (auto& e : entries)
            g.drawRect(e.knob->getBounds(), 1);

        g.setColour(MuLookAndFeel::colour(MuLookAndFeel::mutedText));
        g.setFont(juce::Font(juce::FontOptions(10.0f)));
        for (auto& e : entries)
        {
            if (e.caption.isEmpty()) continue;
            auto b = e.knob->getBounds();
            g.drawText(e.caption, b.withY(b.getBottom() + 4).withHeight(kCaptionH).expanded(26, 0),
                       juce::Justification::centred, false);
        }

        const juce::Font labelFont(juce::FontOptions{}.withHeight(sf(MuLookAndFeel::kKnobLabelFont)));
        g.setFont(labelFont);

        for (auto& e : entries)
        {
            const int textW = (int) juce::GlyphArrangement::getStringWidth(labelFont, e.label);
            if (textW <= e.knob->getWidth())
                continue;   // fits — the product's own rendering already shows it correctly

            const int labelH = (int) sf((float) MuLookAndFeel::kKnobLabelH);
            auto r = e.knob->getBounds();
            auto textArea = juce::Rectangle<int>(r.getCentreX() - textW / 2 - 4,
                                                 r.getBottom() - labelH, textW + 8, labelH);
            g.setColour(juce::Colours::black.withAlpha(0.6f));
            g.fillRect(textArea);
            g.setColour(juce::Colour(0xffff5a4d));   // overflow warning — not a shipped token
            g.drawText(e.label, textArea, juce::Justification::centred, false);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds();
        area.removeFromLeft(kLabelW);
        area.removeFromBottom(kCaptionH + 6);   // room for the captions

        const int gap = 40;
        int totalW = 0;
        for (auto& e : entries) totalW += e.knob->getWidth();
        totalW += gap * juce::jmax(0, (int) entries.size() - 1);

        int x = area.getX() + (area.getWidth() - totalW) / 2;
        const int cy = area.getCentreY();

        for (auto& e : entries)
        {
            e.knob->setTopLeftPosition(x, cy - e.knob->getHeight() / 2);
            x += e.knob->getWidth() + gap;
        }
    }

    static constexpr int kLabelW   = 170;
    static constexpr int kCaptionH = 13;

private:
    struct Entry
    {
        std::unique_ptr<KnobWithLabel> knob;
        juce::String label;
        juce::String caption;   // size and control type
    };

    juce::String name, desc;
    std::vector<Entry> entries;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KnobRow)
};

class ControlDesignPanel : public juce::Component
{
public:
    ControlDesignPanel()
    {
        using Id  = MuLookAndFeel::ColourIds;
        using MLF = MuLookAndFeel;

        setLookAndFeel(&lookAndFeel);

        auto clid = std::make_unique<KnobRow>(utf8("mu-clid \xe2\x80\x94 purple"), "knobEuclidean");
        clid->addKnob("Steps", Id::knobEuclidean, MLF::kKnobSize1W, MLF::kKnobSize1H,
                      1, 64, 1, 5, utf8("Size 1 \xc2\xb7 step"));
        clid->addKnob("Attack (ms)", Id::knobEuclidean, MLF::kKnobSize1W, MLF::kKnobSize1H,
                      0, 10, 0.001, 0.24, utf8("Size 1 \xc2\xb7 smooth (not shipped)"), adsrValueText);
        clid->addKnob("Octave", Id::knobEuclidean, MLF::kKnobSize2W, MLF::kKnobSize2H,
                      -3, 3, 1, 2, utf8("Size 2 \xc2\xb7 step"));
        clid->addKnob("Attack (ms)", Id::knobEuclidean, MLF::kKnobSize2W, MLF::kKnobSize2H,
                      0, 10, 0.001, 0.24, utf8("Size 2 \xc2\xb7 smooth"), adsrValueText);
        addAndMakeVisible(*clid);
        rows.push_back(std::move(clid));

        auto tant = std::make_unique<KnobRow>(utf8("mu-tant \xe2\x80\x94 green"), "knobPostPad");
        tant->addKnob("Cutoff (kHz)", Id::knobPostPad, MLF::kKnobSize1W, MLF::kKnobSize1H,
                      20, 20000, 0.0, 8000, utf8("Size 1 \xc2\xb7 smooth (not shipped)"), cutoffValueText);
        tant->addKnob("Cutoff (kHz)", Id::knobPostPad, MLF::kKnobSize2W, MLF::kKnobSize2H,
                      20, 20000, 0.0, 8000, utf8("Size 2 \xc2\xb7 smooth"), cutoffValueText);
        addAndMakeVisible(*tant);
        rows.push_back(std::move(tant));

        // The two small sizes, where the style is under the most pressure. Size 4 has no
        // shipped step control — included anyway, to confirm the centred value stays
        // suppressed rather than crowding the disc.
        auto small = std::make_unique<KnobRow>(utf8("Small sizes"), "Size 3 + Size 4");
        small->addKnob("Pre Pad", Id::knobPrePad, MLF::kKnobSize3W, MLF::kKnobSize3H,
                       0, 16, 1, 3, utf8("Size 3 \xc2\xb7 step"));
        small->addKnob("SC Amount", Id::knobPan, MLF::kKnobSize3W, MLF::kKnobSize3H,
                       0, 100, 0.1, 62, utf8("Size 3 \xc2\xb7 smooth"));
        small->addKnob("Steps", Id::knobEuclidean, MLF::kKnobSize4W, MLF::kKnobSize4H,
                       1, 64, 1, 5, utf8("Size 4 \xc2\xb7 step (not shipped)"));
        small->addKnob("Attack", Id::knobLevel, MLF::kKnobSize4W, MLF::kKnobSize4H,
                       0, 10, 0.001, 0.24, utf8("Size 4 \xc2\xb7 smooth"), adsrValueText);
        addAndMakeVisible(*small);
        rows.push_back(std::move(small));

        // The overlays KnobWithLabel paints over the rotary, which source their
        // placement from the style's own geometry.
        auto over = std::make_unique<KnobRow>(utf8("Overlays"), "drawn by KnobWithLabel");
        over->addKnob("Cutoff", Id::knobPostPad, MLF::kKnobSize2W, MLF::kKnobSize2H,
                      20, 20000, 0.0, 8000, utf8("mod ring"), cutoffValueText).setIsModulated(true);

        auto& arc = over->addKnob("Cutoff", Id::knobPostPad, MLF::kKnobSize2W, MLF::kKnobSize2H,
                                  20, 20000, 0.0, 8000, utf8("mod ring + live arc"), cutoffValueText);
        arc.setIsModulated(true);
        arc.setModulatedNorm(0.78f);

        over->addKnob("Level", Id::knobLevel, MLF::kKnobSize2W, MLF::kKnobSize2H,
                      -60, 6, 0.1, -6, utf8("GR arc")).setGRSource(&grLevel);
        addAndMakeVisible(*over);
        rows.push_back(std::move(over));

        auto palette = std::make_unique<KnobRow>(utf8("Palette"), "every category colour, Size 2");
        struct Swatch { const char* label; MuLookAndFeel::ColourIds colour; };
        static constexpr Swatch swatches[] = {
            { "Euclid",   Id::knobEuclidean }, { "Insert",   Id::knobInsertPad },
            { "Level",    Id::knobLevel     }, { "FX Send",  Id::knobFxSend    },
            { "Reverb",   Id::knobReverb    }, { "Pan",      Id::knobPan       },
            { "Pre Pad",  Id::knobPrePad    }, { "Post Pad", Id::knobPostPad   },
        };
        for (auto& sw : swatches)
            palette->addKnob(sw.label, sw.colour, MLF::kKnobSize2W, MLF::kKnobSize2H,
                             0, 100, 1, 62, {});
        addAndMakeVisible(*palette);
        rows.push_back(std::move(palette));

        setSize(980, 680);
    }

    ~ControlDesignPanel() override { setLookAndFeel(nullptr); }

    void paint(juce::Graphics& g) override
    {
        using Id = MuLookAndFeel::ColourIds;
        g.fillAll(MuLookAndFeel::colour(Id::windowBackground));

        auto header = getLocalBounds().removeFromTop(kHeader).reduced(20, 0);
        g.setColour(MuLookAndFeel::colour(Id::headingText));
        g.setFont(juce::Font(juce::FontOptions(15.0f, juce::Font::bold)));
        g.drawText(utf8("Control Design \xe2\x80\x94 the family knob"),
                   header, juce::Justification::centredLeft, false);

        g.setColour(MuLookAndFeel::colour(Id::mutedText));
        g.setFont(juce::Font(juce::FontOptions(11.0f)));
        g.drawText("real examples, drawn by MuLookAndFeel",
                   header, juce::Justification::centredRight, false);

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

    MuLookAndFeel lookAndFeel;

    // A fixed gain-reduction reading for the GR-arc example; KnobWithLabel polls it at
    // 30 Hz, so it has to outlive the knob pointing at it.
    std::atomic<float> grLevel { 0.45f };

    std::vector<std::unique_ptr<KnobRow>> rows;

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
