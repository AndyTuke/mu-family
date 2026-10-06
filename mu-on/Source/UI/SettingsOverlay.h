#pragma once

#include "UI/StandardSettingsOverlay.h"   // mu-core: the family-standard settings page

namespace mu_on
{

// mu-On's settings page is the family standard as-is (master volume, UI size, tempo and the
// standalone MIDI Clock rows) — it has no sections of its own yet.
using SettingsOverlay = mu_ui::StandardSettingsOverlay;

} // namespace mu_on
