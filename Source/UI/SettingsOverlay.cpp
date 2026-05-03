#include "SettingsOverlay.h"
#include "../PluginProcessor.h"

SettingsOverlay::SettingsOverlay(PluginProcessor& p)
    : proc(p),
      isStandalone(p.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
{
    closeBtn.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible(closeBtn);

    if (isStandalone)
    {
        defaultBpmInput.setValue((int)proc.getInternalBpm());
        defaultBpmInput.onChange = [this](int v) { proc.setInternalBpm((double)v); };
        addAndMakeVisible(defaultBpmInput);
    }

    masterVolKnob.setRange(0.0, 1.0, 0.001);
    if (auto* raw = proc.apvts.getRawParameterValue("mstr_lvl"))
        masterVolKnob.setValue(*raw, juce::dontSendNotification);
    masterVolKnob.onValueChanged = [this](double v) {
        if (auto* p = proc.apvts.getParameter("mstr_lvl"))
            p->setValueNotifyingHost(p->convertTo0to1((float)v));
    };
    addAndMakeVisible(masterVolKnob);
}

void SettingsOverlay::resized()
{
    const int w = getWidth();

    closeBtn.setBounds(w - kPad - 60, kPad, 60, 28);

    int y = kHeaderH + kPad;
    const int ctrlW = 120;

    if (isStandalone)
    {
        defaultBpmInput.setBounds(kPad, y, ctrlW, kRowH);
        y += kRowH + kPad;
    }

    masterVolKnob.setBounds(kPad, y, ctrlW, kRowH);
}

void SettingsOverlay::paint(juce::Graphics& g)
{
    using Id = MuClidLookAndFeel::ColourIds;

    g.setColour(MuClidLookAndFeel::colour(Id::panelBackground));
    g.fillAll();

    g.setColour(MuClidLookAndFeel::colour(Id::headingText));
    g.setFont(juce::Font(14.0f));
    g.drawText("Settings", kPad, 0, 200, kHeaderH, juce::Justification::centredLeft, false);

    g.setColour(MuClidLookAndFeel::colour(Id::segmentInactiveBorder));
    g.drawLine(0.0f, (float)kHeaderH, (float)getWidth(), (float)kHeaderH, 0.5f);

    // Placeholder items (visual only, greyed out)
    const int w = getWidth();
    int y = kHeaderH + kPad + (isStandalone ? 2 : 1) * (kRowH + kPad);
    g.setColour(MuClidLookAndFeel::colour(Id::mutedText));
    g.setFont(juce::Font(10.0f).italicised());
    const juce::String placeholders[] = {
        "Hit pulse style  (Stage 11)",
        "Interpolation quality  (Stage 11)",
        "Oversampling quality  (Stage 11)",
        "Default overlap fade  (Stage 11)",
        "Restore factory presets  (Stage 11)",
    };
    for (auto& s : placeholders)
    {
        g.drawText(s, kPad, y, w - kPad * 2, 20, juce::Justification::centredLeft, false);
        y += 24;
    }
}
