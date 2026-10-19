#pragma once

#include "UI/StandardSettingsOverlay.h"   // mu-core: the family-standard settings page

namespace mu_tronic
{

// mu-Tronic's settings page: the family standard (master volume, UI size, tempo, standalone MIDI Clock), with no
// licensing section — it is freeware. Rows of its own join here as the layer types register theirs.
class TronicSettingsOverlay : public mu_ui::StandardSettingsOverlay
{
public:
    explicit TronicSettingsOverlay(mu_core::ShellBackend& backend);

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TronicSettingsOverlay)
};

} // namespace mu_tronic
