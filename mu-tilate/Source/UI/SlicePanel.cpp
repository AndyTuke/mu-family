#include "UI/SlicePanel.h"

#include "ValueFormat.h"   // mu-core: mu_fmt

#include <cmath>

namespace mu_tilate
{

namespace
{
    using LF = MuLookAndFeel;
    constexpr int kHeaderH = 22;   // the description line and the step buttons (unscaled)

    juce::String signedDb(double v)
    {
        return (v > 0.0 ? "+" : "") + juce::String(v, 1) + " dB";
    }
    juce::String panText(double v)
    {
        if (std::abs(v) < 0.005) return "Centre";
        return (v < 0.0 ? "L " : "R ") + juce::String((int) std::lround(std::abs(v) * 100.0)) + "%";
    }
    juce::String signedPercent(double v)
    {
        const int p = (int) std::lround(v * 100.0);
        return (p > 0 ? "+" : "") + juce::String(p) + "%";
    }
    juce::String octaves(double v)   { return (v > 0.0 ? "+" : "") + juce::String(v, 2) + " oct"; }
    juce::String percent(double v) { return juce::String((int) std::lround(v * 100.0)) + "%"; }
    juce::String semitones(double v) { return (v > 0.0 ? "+" : "") + juce::String((int) std::lround(v)) + " st"; }
    juce::String cents(double v)     { return (v > 0.0 ? "+" : "") + juce::String((int) std::lround(v)) + " cents"; }
}

SlicePanel::SlicePanel()
{
    addAndMakeVisible(prevButton);
    addAndMakeVisible(nextButton);
    prevButton.onClick = [this] { if (onStep) onStep(-1); };
    nextButton.onClick = [this] { if (onStep) onStep(+1); };

    // Slice: direction, level (a trim on the layer's), pan, and fades added to the layer's.
    addSwitch(reverseSwitch, Slice);
    addKnob(Slice, "Level", &SliceSettings::levelDb, -24.0, 12.0, 0.1, 0.0, LF::knobLevel,
            "Slice Level: this slice's level added to the layer's", signedDb);
    addKnob(Slice, "Pan", &SliceSettings::pan, -1.0, 1.0, 0.01, 0.0, LF::knobPan, "Slice Pan: where this slice sits in the stereo field", panText);
    addKnob(Slice, "In", &SliceSettings::fadeInMs, 0.0, 50.0, 0.1, 5.0, LF::knobModulation,
            "Fade In: extra fade at the start of this slice, on top of the layer's", [](double v) { return mu_fmt::unitText(mu_fmt::Unit::Ms, v); });
    addKnob(Slice, "Out", &SliceSettings::fadeOutMs, 0.0, 50.0, 0.1, 5.0, LF::knobModulation,
            "Fade Out: extra fade at the end of this slice, on top of the layer's", [](double v) { return mu_fmt::unitText(mu_fmt::Unit::Ms, v); });

    // Pitch.
    addKnob(Pitch, "Pitch", &SliceSettings::pitchSemi, -24.0, 24.0, 1.0, 0.0, LF::knobEuclidean, "Slice Pitch: transposes this slice", semitones);
    addKnob(Pitch, "Fine", &SliceSettings::fineCents, -100.0, 100.0, 1.0, 0.0, LF::knobEuclidean, "Slice Fine Tune: fine pitch of this slice", cents);

    // Pre-fade: a quiet reversed copy of the start of the slice that swells into the hit.
    addSwitch(preSwitch, Pre);
    addKnob(Pre, "Level", &SliceSettings::preLevelDb, -36.0, 0.0, 0.1, -12.0, LF::knobFxSend,
            "Pre-fade Level: how loud the reversed swell is, below the slice", [](double v) { return juce::String(v, 1) + " dB"; });
    addKnob(Pre, "Length", &SliceSettings::preLengthMs, 10.0, 1000.0, 1.0, 150.0, LF::knobFxSend,
            "Pre-fade Length: how long the swell runs before the hit", [](double v) { return mu_fmt::unitText(mu_fmt::Unit::Ms, v); });
    addKnob(Pre, "Tone", &SliceSettings::preTone, -1.0, 1.0, 0.01, 0.0, LF::knobFxSend,
            "Pre-fade Tone: darker to the left, thinner to the right", [](double v) { return std::abs(v) < 0.005 ? juce::String("Neutral") : (v < 0.0 ? "Dark " : "Thin ") + percent(std::abs(v)); });

    // Filter: the layer chooses the filter; the slice only moves its cutoff and resonance, and sets how far its envelope moves it.
    addKnob(Filter, "Cutoff", &SliceSettings::cutoffOct, -4.0, 4.0, 0.01, 0.0, LF::knobPostPad,
            "Slice Cutoff Offset: moves the layer's filter cutoff for this slice, in octaves", octaves);
    addKnob(Filter, "Res", &SliceSettings::resOffset, -1.0, 1.0, 0.01, 0.0, LF::knobPostPad,
            "Slice Resonance Offset: added to the layer's filter resonance for this slice", signedPercent);
    addKnob(Filter, "Env", &SliceSettings::filterEnvAmt, -1.0, 1.0, 0.01, 0.0, LF::knobPostPad,
            "Slice Filter Envelope Amount: how far this slice's filter envelope moves the cutoff", signedPercent);

    auto ms = [](double v) { return mu_fmt::unitText(mu_fmt::Unit::Ms, v); };
    addKnob(AmpEnv, "A", &SliceSettings::ampA, 0.0, 2000.0, 1.0, 100.0, LF::knobLevel, "Amp Attack: time for the slice to reach full level", ms);
    addKnob(AmpEnv, "D", &SliceSettings::ampD, 0.0, 2000.0, 1.0, 100.0, LF::knobLevel, "Amp Decay: time to fall to the sustain level", ms);
    addKnob(AmpEnv, "S", &SliceSettings::ampS, 0.0, 1.0, 0.01, 0.0, LF::knobLevel, "Amp Sustain: the level held after the decay", percent);
    addKnob(AmpEnv, "R", &SliceSettings::ampR, 0.0, 2000.0, 1.0, 100.0, LF::knobLevel, "Amp Release: fade-out time when the slice is cut", ms);
    addKnob(FilterEnv, "A", &SliceSettings::fltA, 0.0, 2000.0, 1.0, 100.0, LF::knobPostPad, "Filter Attack: time for the filter envelope to reach its peak", ms);
    addKnob(FilterEnv, "D", &SliceSettings::fltD, 0.0, 2000.0, 1.0, 100.0, LF::knobPostPad, "Filter Decay: time to fall to the sustain level", ms);
    addKnob(FilterEnv, "S", &SliceSettings::fltS, 0.0, 1.0, 0.01, 0.0, LF::knobPostPad, "Filter Sustain: the envelope level held after the decay", percent);
    addKnob(FilterEnv, "R", &SliceSettings::fltR, 0.0, 2000.0, 1.0, 100.0, LF::knobPostPad, "Filter Release: how long the envelope takes to close when the slice is cut", ms);

    addSwitch(loopSwitch, FilterEnv);   // loop sits with the last group; it is placed on its own in resized()
}

void SlicePanel::addKnob(Group group, const char* label, float SliceSettings::* field, double lo, double hi, double step, double skewMid,
                         MuLookAndFeel::ColourIds colour, const juce::String& statusName, std::function<juce::String(double)> format)
{
    Knob k;
    k.comp = std::make_unique<KnobWithLabel>(label, colour);
    k.field = field;
    k.group = group;
    k.comp->setRange(lo, hi, step);
    if (skewMid > 0.0 && skewMid > lo && skewMid < hi) k.comp->getSlider().setSkewFactorFromMidPoint(skewMid);   // 0 = linear
    k.comp->statusTextFromValue = std::move(format);
    k.comp->onStatusUpdate = [this, statusName](const juce::String&, const juce::String& v) { if (onStatusUpdate) onStatusUpdate(statusName, v); };
    k.comp->onValueChanged = [this](double) { changed(); };
    addAndMakeVisible(*k.comp);
    knobs.push_back(std::move(k));
}

void SlicePanel::addSwitch(SegmentControl& control, Group group)
{
    addAndMakeVisible(control);
    control.onChange = [this](int) { changed(); };
    switches.push_back({ &control, group });
}

// Reads every control into the settings and reports them as one change.
void SlicePanel::changed()
{
    if (updating || slice < 0) return;
    for (auto& k : knobs) settings.*(k.field) = (float) k.comp->getValue();
    settings.reverse    = reverseSwitch.getSelectedIndex() == 1;
    settings.preFade    = preSwitch.getSelectedIndex() == 1;
    settings.loop       = loopSwitch.getSelectedIndex() == 1;
    if (onChange) onChange(settings);
}

void SlicePanel::setSlice(int index, const SliceSettings& s, const juce::String& text)
{
    updating = true;
    slice = index;
    settings = s;
    description = text;
    for (auto& k : knobs) k.comp->setValue((double) (s.*(k.field)));
    reverseSwitch.setSelectedIndex(s.reverse ? 1 : 0);
    preSwitch.setSelectedIndex(s.preFade ? 1 : 0);
    loopSwitch.setSelectedIndex(s.loop ? 1 : 0);
    updating = false;
    setEnabled(index >= 0);
    repaint();
}

void SlicePanel::paint(juce::Graphics& g)
{
    using mu_ui::s;
    g.setFont(juce::Font(juce::FontOptions {}.withHeight(mu_ui::sf(11.0f))));
    g.setColour(LF::colour(LF::labelText));
    g.drawText(slice >= 0 ? description : juce::String("Select a hit on the timeline, or a slice in the waveform (or use < >)"),
               s(60), 0, getWidth() - s(60), s(kHeaderH), juce::Justification::centredLeft);
    LF::drawSections(g, *this, sections, LF::appAccent(*this));
}

void SlicePanel::resized()
{
    using mu_ui::s;
    auto area = getLocalBounds();
    auto header = area.removeFromTop(s(kHeaderH));
    prevButton.setBounds(header.removeFromLeft(s(26)));
    header.removeFromLeft(s(4));
    nextButton.setBounds(header.removeFromLeft(s(26)));
    area.removeFromTop(s(LF::kSpaceS));

    // Two rows of groups, each in a raised panel with its name plate above it.
    sections.clear();
    const int pad = s(LF::kSpaceS), plate = s(LF::kSectionPlateH);
    const int rowH = area.getHeight() / 2;
    auto placeGroup = [&](int row, int& x, Group g, const char* title, int switchW)
    {
        const int boxTop = area.getY() + row * rowH + plate;
        const int top = boxTop + pad;
        const int start = x;
        x += pad;
        for (auto& sw : switches)
            if (sw.second == g && sw.first != &loopSwitch)
            {
                sw.first->setBounds(x, top + (s(LF::kKnobSize2H) - s(kHeaderH)) / 2, s(switchW), s(kHeaderH));
                x += s(switchW) + pad;
            }
        for (auto& k : knobs)
            if (k.group == g) { k.comp->setBounds(x, top, s(LF::kKnobSize2W), s(LF::kKnobSize2H)); x += s(LF::kKnobSize2W); }
        x += pad;
        sections.push_back({ juce::Rectangle<int>(start, boxTop, x - start, s(LF::kKnobSize2H) + pad * 2), title });
        x += pad * 2;
    };

    int x = area.getX();
    placeGroup(0, x, Slice, "SLICE LEVEL", 54);
    placeGroup(0, x, Pitch, "SLICE PITCH", 0);
    placeGroup(0, x, Pre, "SLICE PRE-FADE", 54);

    // The second row starts after the layer's filter, which the host places in the indent, so the layer's filter and the slice's
    // offsets of it sit side by side.
    x = area.getX() + rowIndent;
    indent = juce::Rectangle<int>(area.getX(), area.getY() + rowH, rowIndent, plate + s(LF::kKnobSize2H) + pad * 2);
    placeGroup(1, x, Filter, "SLICE FILTER OFFSET", 0);
    placeGroup(1, x, AmpEnv, "SLICE AMP ENV", 0);
    placeGroup(1, x, FilterEnv, "SLICE FILTER ENV", 0);
    // The play-mode switch (once or loop) gets its own small panel after the filter envelope.
    const int loopTop = area.getY() + rowH + plate;
    loopSwitch.setBounds(x + pad, loopTop + pad + (s(LF::kKnobSize2H) - s(kHeaderH)) / 2, s(80), s(kHeaderH));
    sections.push_back({ juce::Rectangle<int>(x, loopTop, s(80) + pad * 2, s(LF::kKnobSize2H) + pad * 2), "SLICE PLAY" });
}

} // namespace mu_tilate
