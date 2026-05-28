#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Voice/PitchSubsection.h"
#include "Voice/FilterSubsection.h"
#include "Voice/AmpSubsection.h"
#include "UI/Voice/InsertSubsection.h"   // shared mu-core insert panel

class PluginProcessor;

// Four-column two-row voice chain panel: Pitch | Filter | Amp | Insert.
// Layout shell — owns the four subsections and draws the dividers + column labels.
class VoiceSection : public juce::Component
{
public:
    explicit VoiceSection(PluginProcessor& p);

    void setRhythm(int rhythmIndex);
    void loadFromRhythm();
    void refreshModulatedIndicators();
    void refreshSuffix(const juce::String& suffix);

    // Forwarder to AmpSubsection — see AmpSubsection::setEffectSendLabel.
    void setEffectSendLabel(const juce::String& name) { ampSub.setEffectSendLabel(name); }

    std::function<void(const juce::String& name, const juce::String& value)> onStatusUpdate;
    std::function<void(int insertAlgo)> onInsertAlgorithmChanged;

    void resized() override;
    void paint(juce::Graphics&) override;

private:
    PluginProcessor& proc;
    int              currentRhythm = -1;

    PitchSubsection  pitchSub;
    FilterSubsection filterSub;
    AmpSubsection    ampSub;
    InsertSubsection insertSub;
};
