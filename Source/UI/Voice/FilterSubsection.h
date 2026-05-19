#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../Components/KnobWithLabel.h"
#include "../Components/DropdownSelect.h"
#include "../Components/MuClidLookAndFeel.h"

class PluginProcessor;

class FilterSubsection : public juce::Component
{
public:
    explicit FilterSubsection(PluginProcessor& p);

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

    DropdownSelect filterType;
    KnobWithLabel  filterCutoff { "Cutoff",    Id::knobPostPad };
    KnobWithLabel  filterRes    { "Resonance", Id::knobPostPad };
    KnobWithLabel  filterAtk    { "Attack",    Id::knobPostPad };
    KnobWithLabel  filterDec    { "Decay",     Id::knobPostPad };
    KnobWithLabel  filterSus    { "Sustain",   Id::knobPostPad };
    KnobWithLabel  filterRel    { "Release",   Id::knobPostPad };
    KnobWithLabel  filterDepth  { "Depth",     Id::knobPostPad };

    void apvtsSet(const char* suffix, float v);
    void wireCallbacks();
};
