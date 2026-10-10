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
    SelectLayer,       // declared for the surface drivers; not performed yet
    LaunchClip,
    PresetNext,
    PresetPrev,
    Panic
};

// When an action that changes what is heard lands: Default follows the app-wide setting.
enum class Quantise { Default, Off, Beat, Bar };

struct ControlAction
{
    ControlActionType type     = ControlActionType::None;
    int               layer    = 0;        // MuteLayer / SoloLayer / SelectLayer: the layer index
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
        case ControlActionType::PresetNext:      return "presetNext";
        case ControlActionType::PresetPrev:      return "presetPrev";
        case ControlActionType::Panic:           return "panic";
        case ControlActionType::None:            break;
    }
    return "none";
}

inline ControlActionType actionFromName(const juce::String& n) noexcept
{
    for (auto t : { ControlActionType::Parameter, ControlActionType::TransportToggle, ControlActionType::TransportPlay,
                    ControlActionType::TransportStop, ControlActionType::MuteLayer, ControlActionType::SoloLayer,
                    ControlActionType::SelectLayer, ControlActionType::LaunchClip, ControlActionType::PresetNext,
                    ControlActionType::PresetPrev, ControlActionType::Panic })
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
    return t == ControlActionType::MuteLayer || t == ControlActionType::SoloLayer || t == ControlActionType::TransportStop;
}

} // namespace mu_core
