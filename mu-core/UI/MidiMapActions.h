#pragma once
#include "Control/ControlAction.h"
#include <vector>

// The actions the MIDI mapping editor offers a controller (docs/design-ui-family.md section 15). Pure data, so a test can check the
// list without building any UI. Clip writes (store, clear, copy, cut, paste) and the surface-only Performance / Page / App actions
// are never offered: the engine refuses them from MIDI.
namespace mu_ui::midimap
{
struct ActionChoice
{
    mu_core::ControlActionType type;
    const char*                label;       // what the Action drop-down shows
    bool                       needsLayer;  // the row also shows a Layer drop-down
    bool                       needsClip;   // the row also shows a Clip 1..8 drop-down
    bool                       needsParam;  // the row also shows a Parameter button
};

// `isNote`: a note source cannot drive a knob, so Parameter is left out. `hasClipBank`: the clip actions only exist on a clip-bank product.
inline std::vector<ActionChoice> offeredActions(bool isNote, bool hasClipBank)
{
    using T = mu_core::ControlActionType;
    std::vector<ActionChoice> list;
    if (! isNote)
        list.push_back({ T::Parameter, "Parameter", false, false, true });
    list.push_back({ T::TransportToggle, "Play / Stop",    false, false, false });
    list.push_back({ T::TransportPlay,   "Play",           false, false, false });
    list.push_back({ T::TransportStop,   "Stop",           false, false, false });
    list.push_back({ T::MuteLayer,       "Mute layer",     true,  false, false });
    list.push_back({ T::SoloLayer,       "Solo layer",     true,  false, false });
    list.push_back({ T::SelectLayer,     "Select layer",   true,  false, false });
    if (hasClipBank)
    {
        list.push_back({ T::LaunchClip,  "Launch clip",    true,  true,  false });
        list.push_back({ T::LaunchScene, "Launch scene",   false, true,  false });
        list.push_back({ T::ClipNext,    "Next clip",      false, false, false });
        list.push_back({ T::ClipPrev,    "Previous clip",  false, false, false });
    }
    list.push_back({ T::PresetNext, "Next preset",     false, false, false });
    list.push_back({ T::PresetPrev, "Previous preset", false, false, false });
    list.push_back({ T::Panic,      "Panic",           false, false, false });
    return list;
}

// The offered entry for `type`, or nullptr when the editor does not offer it (a mapping loaded from an older file).
inline const ActionChoice* findChoice(const std::vector<ActionChoice>& list, mu_core::ControlActionType type)
{
    for (const auto& c : list)
        if (c.type == type) return &c;
    return nullptr;
}
} // namespace mu_ui::midimap
