#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <string_view>
#include <unordered_map>
#include "UI/Components/KnobWithLabel.h"
#include "UI/Components/DropdownSelect.h"
#include "UI/Components/MuLookAndFeel.h"

namespace juce { class RangedAudioParameter; }
class PluginProcessor;

class FilterSubsection : public juce::Component
{
public:
    explicit FilterSubsection(PluginProcessor& p);

    void setRhythm(int ri);
    void loadFromRhythm();
    void refreshSuffix(const juce::String& suffix);
    void bindModulationIndicators();

    void resized() override;

    std::function<void(const juce::String& name, const juce::String& value)> onStatusUpdate;

private:
    using Id = MuLookAndFeel::ColourIds;

    PluginProcessor& proc;
    int rhythmIndex = -1;

    DropdownSelect filterType;
    KnobWithLabel  filterCutoff { "Cutoff (kHz)", Id::knobPostPad };
    KnobWithLabel  filterRes    { "Resonance", Id::knobPostPad };
    KnobWithLabel  filterAtk    { "Attack (ms)",  Id::knobPostPad };
    KnobWithLabel  filterDec    { "Decay (ms)",   Id::knobPostPad };
    KnobWithLabel  filterSus    { "Sustain (%)",  Id::knobPostPad };
    KnobWithLabel  filterRel    { "Release (ms)", Id::knobPostPad };
    KnobWithLabel  filterDepth  { "Depth",     Id::knobPostPad };
    KnobWithLabel  filterLowCut { "Low Cut",   Id::knobPostPad };
    KnobWithLabel  filterDrive  { "Drive",     Id::knobPostPad };

    void apvtsSet(const char* suffix, float v);
    void wireCallbacks();

    std::unordered_map<std::string_view, juce::RangedAudioParameter*> paramPtrCache;
};
