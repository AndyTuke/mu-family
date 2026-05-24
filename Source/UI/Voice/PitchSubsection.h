#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <string_view>
#include <unordered_map>
#include "../Components/KnobWithLabel.h"
#include "../Components/SegmentControl.h"
#include "../Components/MuClidLookAndFeel.h"

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
    // Per-envelope legato (pEnvLeg). Skips ADSR retrigger on contiguous hits.
    // Sits in the empty row 1 col 4 cell.
    SegmentControl pitchLegCtrl { {"Trig","Leg"},
                                  SegmentControl::ActiveStyle::General,
                                  SegmentControl::DrawStyle::Pills };

    void apvtsSet(const char* suffix, float v);
    void wireCallbacks();

    // See EuclideanPanel for the rationale — cached "r{N}_{suffix}" → APVTS
    // parameter pointer, keyed by `const char*` suffix literal.
    std::unordered_map<std::string_view, juce::RangedAudioParameter*> paramPtrCache;
};
