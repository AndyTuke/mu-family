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
    // Fixed Medium-baseline layout, wrapped in s() so the whole grid scales.
    using LF = MuClidLookAndFeel;
    using mu_ui::s;
    constexpr int divW   = LF::kVoiceDivW;
    constexpr int labelH = LF::kVoiceLabelH;
    constexpr int kW     = LF::kVoiceUnitW;
    constexpr int subH   = LF::kVoiceSubH;

    pitchSub .setBounds(0,                       s(labelH), s(5 * kW), s(subH));
    filterSub.setBounds(s(5 * kW + divW),        s(labelH), s(5 * kW), s(subH));
    ampSub   .setBounds(s(10 * kW + 2 * divW),   s(labelH), s(5 * kW), s(subH));
    insertSub.setBounds(s(15 * kW + 3 * divW),   s(labelH), s(4 * kW), s(subH));
}

void VoiceSection::paint(juce::Graphics& g)
{
    using Id = MuClidLookAndFeel::ColourIds;
    using LF = MuClidLookAndFeel;
    using mu_ui::s;

    const int h          = getHeight();
    constexpr int divW   = LF::kVoiceDivW;
    constexpr int labelH = LF::kVoiceLabelH;
    constexpr int kW     = LF::kVoiceUnitW;

    g.setColour(MuClidLookAndFeel::colour(Id::segmentInactiveBorder));
    const float kDivInset = mu_ui::sf(7.0f);
    const float div1X = static_cast<float>(s(5 * kW) + s(divW) / 2);
    const float div2X = static_cast<float>(s(10 * kW + divW) + s(divW) / 2);
    const float div3X = static_cast<float>(s(15 * kW + 2 * divW) + s(divW) / 2);
    g.drawLine(div1X, kDivInset, div1X, (float)h - kDivInset, 0.5f);
    g.drawLine(div2X, kDivInset, div2X, (float)h - kDivInset, 0.5f);
    g.drawLine(div3X, kDivInset, div3X, (float)h - kDivInset, 0.5f);

    g.setColour(MuClidLookAndFeel::colour(Id::mutedText));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(mu_ui::sf(10.0f))));
    g.drawText("PITCH",  0,                          0, s(5 * kW), s(labelH), juce::Justification::centred, false);
    g.drawText("FILTER", s(5 * kW + divW),           0, s(5 * kW), s(labelH), juce::Justification::centred, false);
    g.drawText("AMP",    s(10 * kW + 2 * divW),      0, s(5 * kW), s(labelH), juce::Justification::centred, false);
    g.drawText("INSERT", s(15 * kW + 3 * divW),      0, s(4 * kW), s(labelH), juce::Justification::centred, false);
}
