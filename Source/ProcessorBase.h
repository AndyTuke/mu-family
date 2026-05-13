#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "FX/FXChain.h"
#include "Audio/MixerEngine.h"
#include "Audio/VoiceEngine.h"

#include <array>
#include <memory>

// Abstract base for all mu-family plugin processors.
// Owns the FX chain and mixer engine that every mu plugin shares;
// provides processCoreBlock() as the common per-block mixing entry point.
// Derived classes supply the trigger engine (Euclidean, MIDI, etc.) and
// call processCoreBlock() from their own processBlock().
class ProcessorBase : public juce::AudioProcessor
{
public:
    explicit ProcessorBase(const BusesProperties& props = BusesProperties());
    ~ProcessorBase() override = default;

    // Public so UI panels (MixerOverlay, FXRow, etc.) can access them directly,
    // matching the previous layout where they lived on PluginProcessor.
    FXChain     fxChain;
    MixerEngine mixerEngine;

protected:

    // Sets the host BPM on the FX chain (tempo-synced FX) then calls
    // mixerEngine.processBlock(). Derived class calls this at the end of
    // processBlock() after triggers have fired and modulation has been applied.
    void processCoreBlock(juce::AudioBuffer<float>&                masterBus,
                          std::unique_ptr<VoiceEngine>*            voices,
                          int                                      numVoices,
                          int                                      numSamples,
                          double                                   effectiveBpm,
                          std::array<juce::AudioBuffer<float>*, 8>* directOuts  = nullptr,
                          juce::AudioBuffer<float>*                fxReturnsOut = nullptr);

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ProcessorBase)
};
