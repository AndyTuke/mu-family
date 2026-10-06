#pragma once

#include "UI/StandardSettingsOverlay.h"   // mu-core: the family-standard settings page

namespace mu_toni
{

class PluginProcessor;

// mu-Toni's settings page: the family standard (master volume, UI size, tempo, standalone
// MIDI Clock), the MIDI Program Change tables, plus Tony's line, a quiet credit to the product's namesake below everything.
class SettingsOverlay : public mu_ui::StandardSettingsOverlay
{
public:
    explicit SettingsOverlay(PluginProcessor& proc);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsOverlay)
};

} // namespace mu_toni
