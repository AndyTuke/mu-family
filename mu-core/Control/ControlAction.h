#pragma once

#include <juce_core/juce_core.h>

// The family's device-independent control vocabulary: what a MIDI message, a Launchpad pad or any
// future surface asks the app to do. A source (mu_core::MidiControlMap today, a mu-control surface
// later) produces ControlActions; ProcessorBase performs them (ControlSink). No device bytes and
// no product names live here.
namespace mu_core
{

enum class ControlActionType
{
    None,
    Parameter,         // set an APVTS parameter's base value (knob / fader / button)
    TransportToggle,   // the Play button
    TransportPlay,
    TransportStop,
    MuteLayer,         // toggle a layer strip's mute
    SoloLayer,         // toggle a layer strip's solo
    SelectLayer,       // select a layer (Performance mode: the screen follows the surface)
    LaunchClip,        // launch clip `index` on `layer` (allowed with Performance mode off, also when mapped to MIDI)
    ClipNext,          // the selected layer's next stored clip, wrapping
    ClipPrev,          // the selected layer's previous stored clip, wrapping
    StoreClip,         // store `layer`'s current state as clip `index` (screen only: never mapped, refused in Performance mode)
    ClearClip,         // empty clip `index` of `layer` (screen only, as StoreClip)
    PresetNext,
    PresetPrev,
    Panic,
    PerformanceMode,   // enter / leave Performance mode (`index`: -1 toggles, 0 off, 1 on; `layer` seeds the selection)
    PerformanceItem,   // switch to performance item `index` (0..7)
    PerformancePrev,   // the previous loadable item, wrapping
    PerformanceNext,   // the next loadable item, wrapping
    MuteSelectedLayer, // toggle the mute of the layer Performance mode has selected
    AppPrev,           // switch the surface to the previous mu app (no multi-app hub yet: always refused)
    AppNext            // switch the surface to the next mu app (as AppPrev)
};

// When an action that changes what is heard lands: Default follows the app-wide setting.
enum class Quantise { Default, Off, Beat, Bar };

struct ControlAction
{
    ControlActionType type     = ControlActionType::None;
    int               layer    = 0;        // MuteLayer / SoloLayer / SelectLayer / LaunchClip / StoreClip / ClearClip: the layer index
    int               index    = -1;       // PerformanceItem: the item index; PerformanceMode: -1 toggle, 0 off, 1 on; LaunchClip / StoreClip / ClearClip: the clip index (0..7)
    juce::String      paramId;             // Parameter: the APVTS parameter id
    float             value    = 0.0f;     // Parameter: normalised 0..1
    Quantise          quantise = Quantise::Default;
};

// Actions are saved by name, never by enum integer, so the list can grow and reorder freely.
inline const char* actionName(ControlActionType t) noexcept
{
    switch (t)
    {
        case ControlActionType::Parameter:       return "parameter";
        case ControlActionType::TransportToggle: return "transportToggle";
        case ControlActionType::TransportPlay:   return "transportPlay";
        case ControlActionType::TransportStop:   return "transportStop";
        case ControlActionType::MuteLayer:       return "muteLayer";
        case ControlActionType::SoloLayer:       return "soloLayer";
        case ControlActionType::SelectLayer:     return "selectLayer";
        case ControlActionType::LaunchClip:      return "launchClip";
        case ControlActionType::ClipNext:        return "clipNext";
        case ControlActionType::ClipPrev:        return "clipPrev";
        case ControlActionType::StoreClip:       return "storeClip";
        case ControlActionType::ClearClip:       return "clearClip";
        case ControlActionType::PresetNext:      return "presetNext";
        case ControlActionType::PresetPrev:      return "presetPrev";
        case ControlActionType::Panic:           return "panic";
        case ControlActionType::PerformanceMode: return "performanceMode";
        case ControlActionType::PerformanceItem: return "performanceItem";
        case ControlActionType::PerformancePrev: return "performancePrev";
        case ControlActionType::PerformanceNext: return "performanceNext";
        case ControlActionType::MuteSelectedLayer: return "muteSelectedLayer";
        case ControlActionType::AppPrev:         return "appPrev";
        case ControlActionType::AppNext:         return "appNext";
        case ControlActionType::None:            break;
    }
    return "none";
}

// StoreClip and ClearClip are deliberately absent from the list: they write data, so no saved map, surface
// mapping or MIDI message can name them (a pad never writes a clip).
inline ControlActionType actionFromName(const juce::String& n) noexcept
{
    for (auto t : { ControlActionType::Parameter, ControlActionType::TransportToggle, ControlActionType::TransportPlay,
                    ControlActionType::TransportStop, ControlActionType::MuteLayer, ControlActionType::SoloLayer,
                    ControlActionType::SelectLayer, ControlActionType::LaunchClip, ControlActionType::ClipNext,
                    ControlActionType::ClipPrev, ControlActionType::PresetNext,
                    ControlActionType::PresetPrev, ControlActionType::Panic, ControlActionType::PerformanceMode,
                    ControlActionType::PerformanceItem, ControlActionType::PerformancePrev, ControlActionType::PerformanceNext,
                    ControlActionType::MuteSelectedLayer, ControlActionType::AppPrev, ControlActionType::AppNext })
        if (n == actionName(t)) return t;
    return ControlActionType::None;
}

inline const char* quantiseName(Quantise q) noexcept
{
    switch (q) { case Quantise::Off: return "off"; case Quantise::Beat: return "beat"; case Quantise::Bar: return "bar"; default: return "default"; }
}

inline Quantise quantiseFromName(const juce::String& n) noexcept
{
    if (n == "off") return Quantise::Off;
    if (n == "beat") return Quantise::Beat;
    if (n == "bar") return Quantise::Bar;
    return Quantise::Default;
}

// Mute / solo / stop are the actions a quantise setting applies to (they change what is heard
// abruptly); parameters, starting playback and preset changes never wait for a boundary.
inline bool isQuantisable(ControlActionType t) noexcept
{
    return t == ControlActionType::MuteLayer || t == ControlActionType::MuteSelectedLayer || t == ControlActionType::SoloLayer || t == ControlActionType::TransportStop;
}

} // namespace mu_core
