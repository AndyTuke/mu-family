#pragma once

#include "Link/MuLinkBridge.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

// makeStandaloneBridge — the one-line, family-standard way to wire a product's standalone to
// mu-link. Every product's StandaloneApp built this identically (same MuLinkBridge ctor, same
// SafePointer capture, same "  •  mu-link connected" title-suffix callback); that boilerplate
// now lives here so a product passes only its window + registry display name.
//
// **Standalone-only on purpose** — it pulls in MuLinkBridge.h, which is header-only and never
// added to mu-core's INTERFACE sources, so this compiles only into each product's StandaloneApp
// translation unit. VST3/CLAP never see it and a plugin can never attach (the host owns its
// clock + device). See docs/mu-link/design-mulink.md §3.3.
namespace mu_link
{

// Build the bridge with the connected-title behaviour: while attached to mu-link the window's
// title gains a "  •  mu-link connected" suffix; on detach it reverts to the base title.
// `window` + `processor` + `player` must outlive the returned bridge (owned by the app).
inline std::unique_ptr<MuLinkBridge> makeStandaloneBridge(juce::Component& window,
                                                          juce::AudioProcessor& processor,
                                                          juce::AudioProcessorPlayer& player,
                                                          const juce::String& displayName)
{
    juce::Component::SafePointer<juce::Component> safeWin(&window);
    const juce::String baseTitle = window.getName();

    return std::make_unique<MuLinkBridge>(processor, player, displayName,
        [safeWin, baseTitle](bool connected) mutable
        {
            if (safeWin != nullptr)
                safeWin->setName(connected
                    ? baseTitle + juce::String(juce::CharPointer_UTF8("  \xe2\x80\xa2  mu-link connected"))
                    : baseTitle);
        });
}

} // namespace mu_link
