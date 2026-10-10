#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Voice/PitchSubsection.h"
#include "Voice/FilterSubsection.h"
#include "Voice/AmpSubsection.h"
#include "UI/Voice/InsertSubsection.h"   // shared mu-core insert panel
#include "UI/Voice/VoiceBand.h"          // shared mu-core voice band layout + drawing

namespace mu_clid {

class PluginProcessor;

// mu-Clid's voice chain: the shared VoiceBand (Pitch | Filter | Amp | Effects layout + drawing)
// filled with mu-Clid's subsections, which bind to the selected rhythm.
class VoiceSection : public VoiceBand
{
public:
    explicit VoiceSection(PluginProcessor& p);

    void setRhythm(int rhythmIndex);
    void loadFromRhythm();
    void refreshSuffix(const juce::String& suffix);

    // Forwarder to AmpSubsection — see AmpSubsection::setEffectSendLabel.
    void setEffectSendLabel(const juce::String& name) { ampSub.setEffectSendLabel(name); }

    std::function<void(const juce::String& name, const juce::String& value)> onStatusUpdate;
    std::function<void(int insertAlgo)> onInsertAlgorithmChanged;

private:
    PluginProcessor& proc;
    int              currentRhythm = -1;

    PitchSubsection  pitchSub;
    FilterSubsection filterSub;
    AmpSubsection    ampSub;
    InsertSubsection insertSub;
};

} // namespace mu_clid
