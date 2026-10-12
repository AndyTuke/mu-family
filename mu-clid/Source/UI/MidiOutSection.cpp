#include "MidiOutSection.h"
#include "Plugin/PluginProcessor.h"
#include "Audio/MidiOutputMapping.h"   // mu-core: note names

namespace mu_clid {

MidiOutSection::MidiOutSection(PluginProcessor& p) : proc(p)
{
    // Note: all 128 MIDI notes, item id = note + 1 (as mu-Clid MIDI's selector).
    for (int n = 0; n < 128; ++n)
        noteSelect.addItem(mu_core::midiNoteName(n), n + 1);
    noteSelect.onChange = [this](int id)
    {
        apvtsSet("midiNote", (float) (id - 1));
        if (onStatusUpdate) onStatusUpdate("MIDI Note", mu_core::midiNoteName(id - 1));
    };

    // Channel: 1 to 16, item id = channel.
    for (int c = 1; c <= 16; ++c)
        chanSelect.addItem(juce::String(c), c);
    chanSelect.onChange = [this](int id)
    {
        apvtsSet("midiChan", (float) id);
        if (onStatusUpdate) onStatusUpdate("MIDI Channel", juce::String(id));
    };

    // Velocity and Accent are whole percentages.
    velKnob.setRange(1.0, 100.0, 1.0);     velKnob.setValue(100.0);
    accentKnob.setRange(0.0, 100.0, 1.0);  accentKnob.setValue(0.0);
    velKnob.onValueChanged    = [this](double v) { apvtsSet("midiVel",    (float) v); };
    accentKnob.onValueChanged = [this](double v) { apvtsSet("midiAccent", (float) v); };
    velKnob.onStatusUpdate    = [this](const juce::String&, const juce::String&)
    { if (onStatusUpdate) onStatusUpdate("MIDI Velocity", juce::String((int) std::round(velKnob.getValue())) + "%"); };
    accentKnob.onStatusUpdate = [this](const juce::String&, const juce::String&)
    { if (onStatusUpdate) onStatusUpdate("MIDI Accent", juce::String((int) std::round(accentKnob.getValue())) + "%"); };

    addAndMakeVisible(noteSelect);
    addAndMakeVisible(chanSelect);
    addAndMakeVisible(velKnob);
    addAndMakeVisible(accentKnob);
}

void MidiOutSection::apvtsSet(const char* suffix, float v)
{
    if (rhythmIndex < 0) return;
    if (auto* p = proc.apvts.getParameter("r" + juce::String(rhythmIndex) + "_" + suffix))
        p->setValueNotifyingHost(p->convertTo0to1(v));
}

float MidiOutSection::apvtsGet(const char* suffix, float fallback) const
{
    if (rhythmIndex < 0) return fallback;
    if (auto* raw = proc.apvts.getRawParameterValue("r" + juce::String(rhythmIndex) + "_" + suffix))
        return raw->load();
    return fallback;
}

void MidiOutSection::setRhythm(int ri)
{
    rhythmIndex = ri;
    loadFromRhythm();
}

// Shows the bound rhythm's saved values without writing anything back.
void MidiOutSection::loadFromRhythm()
{
    noteSelect.setSelectedId((int) apvtsGet("midiNote", 36.0f) + 1, false);
    chanSelect.setSelectedId((int) apvtsGet("midiChan", 1.0f), false);
    velKnob   .setValue(apvtsGet("midiVel", 100.0f), juce::dontSendNotification);
    accentKnob.setValue(apvtsGet("midiAccent", 0.0f), juce::dontSendNotification);
}

// Host automation moved one parameter: update only its control.
void MidiOutSection::refreshSuffix(const juce::String& suffix)
{
    if      (suffix == "midiNote")   noteSelect.setSelectedId((int) apvtsGet("midiNote", 36.0f) + 1, false);
    else if (suffix == "midiChan")   chanSelect.setSelectedId((int) apvtsGet("midiChan", 1.0f), false);
    else if (suffix == "midiVel")    velKnob   .setValue(apvtsGet("midiVel", 100.0f), juce::dontSendNotification);
    else if (suffix == "midiAccent") accentKnob.setValue(apvtsGet("midiAccent", 0.0f), juce::dontSendNotification);
}

juce::Rectangle<int> MidiOutSection::boxBounds() const
{
    using mu_ui::s;
    return { s(kBoxX), s(kBoxY), s(kBoxW), s(kBoxH) };
}

// The cell for control `index` (0 Note, 1 Channel, 2 Velocity, 3 Accent): one row, vertically centred in the box.
juce::Rectangle<int> MidiOutSection::cell(int index) const
{
    using mu_ui::s;
    constexpr int cols[]  = { 0, kDropCols, 2 * kDropCols, 2 * kDropCols + 1 };
    constexpr int spans[] = { kDropCols, kDropCols, 1, 1 };
    const auto box = boxBounds();
    return { box.getX() + s(cols[index] * kCols), box.getY() + (box.getHeight() - s(kRowH)) / 2,
             s(spans[index] * kCols), s(kRowH) };
}

void MidiOutSection::resized()
{
    using mu_ui::s;
    // Dropdowns: a caption band above, then the dropdown kept kDropdownEdgeGap off the cell's sides.
    for (int i = 0; i < 2; ++i)
    {
        const auto c = cell(i);
        auto& drop = i == 0 ? noteSelect : chanSelect;
        drop.setBounds(c.getX() + s(MuLookAndFeel::kDropdownEdgeGap), c.getY() + s(kCaptionH + 2),
                       c.getWidth() - 2 * s(MuLookAndFeel::kDropdownEdgeGap), s(kDropH));
    }
    velKnob.setBounds(cell(2));
    accentKnob.setBounds(cell(3));
}

void MidiOutSection::paint(juce::Graphics& g)
{
    using mu_ui::s;
    const auto box = boxBounds();
    if (MuLookAndFeel::isMetal(*this))
    {
        MuLookAndFeel::drawSections(g, *this, { { box, "MIDI Out" } }, MuLookAndFeel::appAccent(*this));
    }
    else
    {
        const float plateH = (float) s(MuLookAndFeel::kSectionPlateH);
        g.setColour(MuLookAndFeel::colour(MuLookAndFeel::segmentInactiveBorder));
        g.drawRoundedRectangle(box.toFloat().reduced(0.5f), 4.0f, 1.0f);
        MuLookAndFeel::drawCentredNamePlate(g, { (float) box.getX(), (float) box.getY() - plateH,
                                                  (float) box.getWidth(), plateH }, "MIDI Out");
    }

    // Captions over the two dropdowns, as the knobs carry theirs below them.
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(mu_ui::sf(MuLookAndFeel::kKnobLabelFont))));
    for (int i = 0; i < 2; ++i)
    {
        const auto c = cell(i);
        const juce::Rectangle<int> area(c.getX(), c.getY(), c.getWidth(), s(kCaptionH));
        const char* text = i == 0 ? "Note" : "Channel";
        if (MuLookAndFeel::isMetal(*this))
        {
            MuLookAndFeel::drawEngravedText(g, text, area, juce::Justification::centred,
                                            MuLookAndFeel::colour(MuLookAndFeel::labelText));
        }
        else
        {
            g.setColour(MuLookAndFeel::colour(MuLookAndFeel::labelText));
            g.drawText(text, area, juce::Justification::centred, true);
        }
    }
}

} // namespace mu_clid
