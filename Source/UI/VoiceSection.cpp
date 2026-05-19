#include "VoiceSection.h"
#include "../Plugin/PluginProcessor.h"

VoiceSection::VoiceSection(PluginProcessor& p)
    : pitchSub(p), filterSub(p), ampSub(p), insertSub(p)
{
    addAndMakeVisible(pitchSub);
    addAndMakeVisible(filterSub);
    addAndMakeVisible(ampSub);
    addAndMakeVisible(insertSub);

    // Forward status updates from each subsection through our own callback.
    auto fwd = [this](const juce::String& n, const juce::String& v) {
        if (onStatusUpdate) onStatusUpdate(n, v);
    };
    pitchSub .onStatusUpdate = fwd;
    filterSub.onStatusUpdate = fwd;
    ampSub   .onStatusUpdate = fwd;
    insertSub.onStatusUpdate = fwd;

    insertSub.onInsertAlgorithmChanged = [this](int charId) {
        if (onInsertAlgorithmChanged) onInsertAlgorithmChanged(charId);
    };
}

void VoiceSection::setRhythm(int ri)
{
    pitchSub .setRhythm(ri);
    filterSub.setRhythm(ri);
    ampSub   .setRhythm(ri);
    insertSub.setRhythm(ri);
}

void VoiceSection::loadFromRhythm()
{
    pitchSub .loadFromRhythm();
    filterSub.loadFromRhythm();
    ampSub   .loadFromRhythm();
    insertSub.loadFromRhythm();
}

void VoiceSection::refreshModulatedIndicators()
{
    pitchSub .refreshModulatedIndicators();
    filterSub.refreshModulatedIndicators();
    ampSub   .refreshModulatedIndicators();
    insertSub.refreshModulatedIndicators();
}

void VoiceSection::refreshSuffix(const juce::String& suffix)
{
    pitchSub .refreshSuffix(suffix);
    filterSub.refreshSuffix(suffix);
    ampSub   .refreshSuffix(suffix);
    insertSub.refreshSuffix(suffix);
}

void VoiceSection::resized()
{
    const int w      = getWidth();
    const int h      = getHeight();
    const int divW   = 6;
    const int labelH = 14;
    const int kW     = (w - 3 * divW) / 19;
    const int subH   = h - labelH;

    pitchSub .setBounds(0,                labelH, 5 * kW, subH);
    filterSub.setBounds(5 * kW + divW,    labelH, 5 * kW, subH);
    ampSub   .setBounds(10 * kW + 2*divW, labelH, 5 * kW, subH);
    insertSub.setBounds(15 * kW + 3*divW, labelH, 4 * kW, subH);
}

void VoiceSection::paint(juce::Graphics& g)
{
    using Id = MuClidLookAndFeel::ColourIds;

    const int w      = getWidth();
    const int h      = getHeight();
    const int divW   = 6;
    const int labelH = 14;
    const int kW     = (w - 3 * divW) / 19;

    g.setColour(MuClidLookAndFeel::colour(Id::segmentInactiveBorder));
    const float div1X = static_cast<float>(5 * kW) + divW * 0.5f;
    const float div2X = static_cast<float>(10 * kW + divW) + divW * 0.5f;
    const float div3X = static_cast<float>(15 * kW + 2 * divW) + divW * 0.5f;
    g.drawLine(div1X, h * 0.05f, div1X, h * 0.95f, 0.5f);
    g.drawLine(div2X, h * 0.05f, div2X, h * 0.95f, 0.5f);
    g.drawLine(div3X, h * 0.05f, div3X, h * 0.95f, 0.5f);

    g.setColour(MuClidLookAndFeel::colour(Id::mutedText));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(10.0f)));
    g.drawText("PITCH",  0,                  0, 5 * kW, labelH, juce::Justification::centred, false);
    g.drawText("FILTER", 5 * kW + divW,      0, 5 * kW, labelH, juce::Justification::centred, false);
    g.drawText("AMP",    10 * kW + 2 * divW, 0, 5 * kW, labelH, juce::Justification::centred, false);
    g.drawText("INSERT", 15 * kW + 3 * divW, 0, 4 * kW, labelH, juce::Justification::centred, false);
}
