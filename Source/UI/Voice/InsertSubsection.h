#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../Components/KnobWithLabel.h"
#include "../Components/DropdownSelect.h"
#include "../Components/MuClidLookAndFeel.h"
#include "../InsertAlgoDefaults.h"

class PluginProcessor;

class InsertSubsection : public juce::Component
{
public:
    explicit InsertSubsection(PluginProcessor& p);

    void setRhythm(int ri);
    void loadFromRhythm();
    void refreshSuffix(const juce::String& suffix);
    void refreshModulatedIndicators();

    void resized() override;

    std::function<void(const juce::String& name, const juce::String& value)> onStatusUpdate;
    std::function<void(int insertAlgo)> onInsertAlgorithmChanged;

private:
    using Id = MuClidLookAndFeel::ColourIds;

    PluginProcessor& proc;
    int rhythmIndex = -1;

    using InsertAlgoSnapshot = InsertAlgoDefaults;
    InsertAlgoSnapshot insertSnapshots[14];
    bool               insertSnapshotValid[11] = {};

    DropdownSelect insertAlgo;
    KnobWithLabel  insertDrive  { "Drive",  Id::knobInsertPad };
    KnobWithLabel  insertOutput { "Output", Id::knobInsertPad };
    KnobWithLabel  insertDither { "Dither", Id::knobInsertPad };
    KnobWithLabel  insertTone   { "LPF",    Id::knobInsertPad };

    void apvtsSet(const char* suffix, float v);
    void wireCallbacks();
    void configureInsertAlgorithm(int charId);
};
