#pragma once

#include <Plugin/PluginProcessor.h>   // mu-Tronic's processor (the shell), found through the include path: never this file
#include "Persistence/HostState.h"

namespace mu_host
{

// mu-Host: one mu instrument channel as a plug-in. It is the mu-Tronic shell with one slot (MU_MAX_LAYERS=1): the engine in the slot is
// picked from a drop-down, its voice, inserts, sequencer, modulation and layer presets are shown, and its output goes straight to the
// host. It has no scenes, pages, clips, mixer or full-preset controls (the DAW mixes and saves).
//
// Licensing (Andy, 2026-11-07): mu-Host has no licence of its own. Opening an engine runs the licence check of the product the engine
// belongs to and from then on it is that product's own app: its key, activation, checkout page and support prompt. A key covers the
// whole product (a mu-On key, all seven mu-On engines).
class PluginProcessor : public mu_tronic::PluginProcessor
{
public:
    PluginProcessor();

    const juce::String getName() const override { return juce::String(juce::CharPointer_UTF8("\xce\xbc-Host")); }
    juce::String getProductSlug() const override { return "mu-host"; }
    bool offersSceneControls() const override { return false; }
    bool offersLayerPages() const override { return false; }
    juce::String getFullPresetExtension() const override { return kFullPresetExtension; }
    const char*  getFullPresetTag() const override { return kFullPresetTag; }

    juce::AudioProcessorEditor* createEditor() override;

    // Points the licence at the product of the engine `type` (the key and activation are looked up in that product's own content
    // folder and in this one's) unless it already is. Returns the product's display name, or empty for an Empty slot, which has no
    // licensing and no prompt. Message thread.
    juce::String licenseEngine(mu_core::LayerTypeId type);
};

} // namespace mu_host
