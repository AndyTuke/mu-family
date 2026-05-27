#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Plugin/PluginProcessor.h"
#include "UI/Components/MuLookAndFeel.h"

namespace mu_tant
{

// Voice-list sidebar: one button per voice (Voice 1..N). Clicking selects
// the voice for editing in the main VoicePanel and fires onVoiceSelected.
// First-stab visual style: plain text buttons with a colour-tag strip on the
// left edge matching the channel palette. Width follows MuLookAndFeel::kSidebarW
// so the shell's layout maths stay consistent with mu-clid's RhythmSidebar.
class VoiceSidebar : public juce::Component
{
public:
    explicit VoiceSidebar(PluginProcessor& proc);
    ~VoiceSidebar() override = default;

    std::function<void(int voiceIndex)> onVoiceSelected;

    void setSelectedIndex(int idx);
    int  getSelectedIndex() const noexcept { return selectedIndex; }

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    PluginProcessor& proc;
    int selectedIndex = 0;

    std::array<std::unique_ptr<juce::TextButton>, PluginProcessor::kMaxVoices> buttons;

    void refreshButtonStates();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VoiceSidebar)
};

} // namespace mu_tant
