#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <string_view>
#include <unordered_map>
#include "UI/Components/KnobWithLabel.h"
#include "UI/Components/MuClidLookAndFeel.h"

namespace juce { class RangedAudioParameter; }
class PluginProcessor;

class PitchSubsection : public juce::Component
{
public:
    explicit PitchSubsection(PluginProcessor& p);

    void setRhythm(int ri);
    void loadFromRhythm();
    void refreshSuffix(const juce::String& suffix);
    void refreshModulatedIndicators();

    void resized() override;

    std::function<void(const juce::String& name, const juce::String& value)> onStatusUpdate;

private:
    using Id = MuClidLookAndFeel::ColourIds;

    PluginProcessor& proc;
    int rhythmIndex = -1;

    KnobWithLabel pitchOctave { "Octave",      Id::knobEuclidean };
    KnobWithLabel pitchSemi   { "Semitone",   Id::knobEuclidean };
    KnobWithLabel pitchFine   { "Fine",       Id::knobEuclidean };
    KnobWithLabel pitchAtk    { "Attack (ms)", Id::knobEuclidean };
    KnobWithLabel pitchDec    { "Decay (ms)",  Id::knobEuclidean };
    KnobWithLabel pitchSus    { "Sustain (%)", Id::knobEuclidean };
    KnobWithLabel pitchRel    { "Release (ms)", Id::knobEuclidean };
    KnobWithLabel pitchDepth  { "Depth",      Id::knobEuclidean };

    void apvtsSet(const char* suffix, float v);
    void wireCallbacks();

    // See EuclideanPanel for the rationale — cached "r{N}_{suffix}" → APVTS
    // parameter pointer, keyed by `const char*` suffix literal.
    std::unordered_map<std::string_view, juce::RangedAudioParameter*> paramPtrCache;
};
