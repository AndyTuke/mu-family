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
    void refreshSuffix(const juce::String& suffix);

    // Forwarder to AmpSubsection — see AmpSubsection::setEffectSendLabel.
    void setEffectSendLabel(const juce::String& name) { ampSub.setEffectSendLabel(name); }

    std::function<void(const juce::String& name, const juce::String& value)> onStatusUpdate;
    std::function<void(int insertAlgo)> onInsertAlgorithmChanged;

    void resized() override;
    void paint(juce::Graphics&) override;

private:
    // mu-Clid's own voice-band layout (the shared MuLookAndFeel widths are the other
    // products' defaults): Pitch has Depth above R (4 columns), Amp is Level / Accent over
    // A / D / S / R (4), and Insert takes the FX sends beside a narrowed dropdown (6).
    static constexpr int kCols       = MuLookAndFeel::kVoiceUnitW;
    static constexpr int kPitchW     = 4 * kCols;                                          // 216
    static constexpr int kAmpW       = 4 * kCols;                                          // 216
    static constexpr int kInsertCols = 6;
    static constexpr int kSendCols   = 3;   // Effect / Delay / Reverb, right of the insert dropdown
    static constexpr int kInsertW    = kInsertCols * kCols;                                // 324
    static constexpr float kPlateH   = (float) MuLookAndFeel::kNamePlateH;   // section name plates

    PluginProcessor& proc;
    int              currentRhythm = -1;

    PitchSubsection  pitchSub;
    FilterSubsection filterSub;
    AmpSubsection    ampSub;
    InsertSubsection insertSub;
};
