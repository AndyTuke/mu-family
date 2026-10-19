#pragma once
// The app's settings page is not part of the library: it may name the processor.
#include "Plugin/PluginProcessor.h"
namespace mu_toni
{
struct SettingsOverlay { PluginProcessor* p = nullptr; };
}
