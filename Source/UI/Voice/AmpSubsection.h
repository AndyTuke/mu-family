#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../Components/KnobWithLabel.h"
#include "../Components/MuClidLookAndFeel.h"

class PluginProcessor;

class AmpSubsection : public juce::Component
{
public:
    explicit AmpSubsection(PluginProcessor& p);

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

    KnobWithLabel ampLevel   { "Level (dB)",   Id::knobLevel  };
    KnobWithLabel ampSendEff { "Effect",       Id::knobFxSend };
    KnobWithLabel ampSendDly { "Delay",        Id::knobFxSend };
    KnobWithLabel ampSendRev { "Reverb",       Id::knobFxSend };
    KnobWithLabel ampAccent  { "Accent",       Id::knobLevel  };
    KnobWithLabel ampAtk     { "Attack (ms)",  Id::knobLevel  };
    KnobWithLabel ampDec     { "Decay (ms)",   Id::knobLevel  };
    KnobWithLabel ampSus     { "Sustain (%)",  Id::knobLevel  };
    KnobWithLabel ampRel     { "Release (ms)", Id::knobLevel  };

    void apvtsSet(const char* suffix, float v);
    void wireCallbacks();
};
