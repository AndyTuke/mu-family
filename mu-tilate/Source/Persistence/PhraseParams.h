#pragma once

#include "Sequencer/LayerFilterTypes.h"

#include <juce_audio_processors/juce_audio_processors.h>

// The Phrase layer type's parameter table: the per-slot rows under the slot's p{N}_ prefix. The slice list and the
// timeline blocks are not parameters (they vary in number): they are ValueTree children of the layer node, as
// mu-On's step rows are. Saved data keys on these ids and their order, so rows are only ever appended.
namespace mu_tilate
{

namespace ppi
{
    enum
    {
        mode, ltempo, hold, strength, fadeIn, fadeOut, gain,       // playback (7)
        grid, bars, window,                                        // timeline and stretch window (3)
        dmethod, dsens, dgap, dpre, dlow, dhigh, snap,             // detection (7)
        drvChar, insP1, insP2, insP3, insP4,                       // insert (5)
        fType, fCut, fRes,                                         // the layer's filter (3)
        COUNT
    };
    inline constexpr const char* const suffix[COUNT] = {
        "mode", "ltempo", "hold", "strength", "fadeIn", "fadeOut", "gain",
        "grid", "bars", "window",
        "dmethod", "dsens", "dgap", "dpre", "dlow", "dhigh", "snap",
        "drvChar", "insP1", "insP2", "insP3", "insP4",
        "fType", "fCut", "fRes",
    };
}

inline constexpr int kNumPhraseParams = ppi::COUNT;
static_assert(kNumPhraseParams == 25, "a new row goes into ppi, suffix[] and addLayerParams together");

// "p{N}_": the prefix of every parameter of Phrase slot N.
inline juce::String layerParamPrefix(int slot) { return "p" + juce::String(slot) + "_"; }

// The root tag of a layer-preset file (.muPhrase).
inline constexpr const char* kPhrasePresetTag = "MuTilateLayer";

// Choice rows: the order here is the saved index.
inline const juce::StringArray& tempoModeNames()  { static const juce::StringArray n { "Off", "Re-pitch", "Stretch" }; return n; }
inline const juce::StringArray& gridNames()       { static const juce::StringArray n { "Off", "1/4", "1/8", "1/16", "1/32", "1/4T", "1/8T", "1/16T", "1/32T" }; return n; }
inline const juce::StringArray& windowNames()     { static const juce::StringArray n { "Short", "Default" }; return n; }
inline const juce::StringArray& filterTypeNames()
{
    static const juce::StringArray n = [] { juce::StringArray a; for (const auto& f : filterChoices()) a.add(f.name); return a; }();
    return n;
}
inline const juce::StringArray& detectMethodNames() { static const juce::StringArray n { "Transients", "Soft-tonal", "Even" }; return n; }

// The grid choice as beats (a quarter note is one beat); 0 is no snapping. The T choices are triplets: three in the time of two.
inline double gridBeats(int choice)
{
    constexpr double v[] = { 0.0, 1.0, 0.5, 0.25, 0.125, 2.0 / 3.0, 1.0 / 3.0, 1.0 / 6.0, 1.0 / 12.0 };
    return v[juce::jlimit(0, 8, choice)];
}

// The Phrase type's rows for one layer slot (p{slot}_*), in ppi order.
void addLayerParams(juce::AudioProcessorValueTreeState::ParameterLayout& layout, int slot);

} // namespace mu_tilate
