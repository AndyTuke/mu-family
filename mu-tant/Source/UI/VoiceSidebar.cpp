#include "VoiceSidebar.h"

namespace mu_tant
{

VoiceSidebar::VoiceSidebar(PluginProcessor& p)
    : proc(p)
{
    using Id = MuLookAndFeel::ColourIds;
    for (int i = 0; i < PluginProcessor::kMaxVoices; ++i)
    {
        auto btn = std::make_unique<juce::TextButton>(
            juce::String("Voice ") + juce::String(i + 1));
        btn->setClickingTogglesState(true);
        btn->setRadioGroupId(1);   // mutually exclusive selection
        btn->onClick = [this, i]
        {
            selectedIndex = i;
            refreshButtonStates();
            if (onVoiceSelected) onVoiceSelected(i);
        };
        addAndMakeVisible(*btn);
        buttons[(size_t) i] = std::move(btn);
    }
    juce::ignoreUnused(Id{});
    refreshButtonStates();
}

void VoiceSidebar::setSelectedIndex(int idx)
{
    idx = juce::jlimit(0, PluginProcessor::kMaxVoices - 1, idx);
    if (idx == selectedIndex) return;
    selectedIndex = idx;
    refreshButtonStates();
}

void VoiceSidebar::refreshButtonStates()
{
    for (int i = 0; i < PluginProcessor::kMaxVoices; ++i)
        if (buttons[(size_t) i])
            buttons[(size_t) i]->setToggleState(i == selectedIndex,
                                                  juce::dontSendNotification);
}

void VoiceSidebar::paint(juce::Graphics& g)
{
    using Id = MuLookAndFeel::ColourIds;
    g.fillAll(MuLookAndFeel::colour(Id::panelBackground));
}

void VoiceSidebar::resized()
{
    using mu_ui::s;
    const int w   = getWidth();
    const int pad = s(4);
    const int gap = s(4);
    const int btnH = s(48);

    int y = pad;
    for (int i = 0; i < PluginProcessor::kMaxVoices; ++i)
    {
        if (auto* b = buttons[(size_t) i].get())
        {
            b->setBounds(pad, y, w - 2 * pad, btnH);
            y += btnH + gap;
        }
    }
}

} // namespace mu_tant
