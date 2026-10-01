#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <string_view>
#include <unordered_map>
#include "UI/Components/KnobWithLabel.h"
#include "UI/Components/MuLookAndFeel.h"

namespace juce { class RangedAudioParameter; }
class PluginProcessor;

class AmpSubsection : public juce::Component
{
public:
    explicit AmpSubsection(PluginProcessor& p);

    void setRhythm(int ri);
    void loadFromRhythm();
    void refreshSuffix(const juce::String& suffix);
    void bindModulationIndicators();

    // Update the "Effect" send knob label to show the mixer's currently-
    // selected effect algorithm name (e.g. "Phaser", "Echo"). PluginEditor
    // wires this from MixerOverlay::onEffectAlgorithmNameChanged.
    void setEffectSendLabel(const juce::String& name);

    void resized() override;

    std::function<void(const juce::String& name, const juce::String& value)> onStatusUpdate;

private:
    using Id = MuLookAndFeel::ColourIds;

    PluginProcessor& proc;
    int rhythmIndex = -1;

    KnobWithLabel ampLevel   { "Level",   Id::knobLevel  };
    KnobWithLabel ampSendEff { "Effect",       Id::knobFxSend };
    KnobWithLabel ampSendDly { "Delay",        Id::knobFxSend };
    KnobWithLabel ampSendRev { "Reverb",       Id::knobFxSend };
    KnobWithLabel ampAccent  { "Accent",       Id::knobLevel  };
    KnobWithLabel ampAtk     { "A",  Id::knobLevel  };
    KnobWithLabel ampDec     { "D",   Id::knobLevel  };
    KnobWithLabel ampSus     { "S",  Id::knobLevel  };
    KnobWithLabel ampRel     { "R", Id::knobLevel  };

    void apvtsSet(const char* suffix, float v);
    void wireCallbacks();

    std::unordered_map<std::string_view, juce::RangedAudioParameter*> paramPtrCache;
};
