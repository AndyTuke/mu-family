#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Audio/FX/Slots/FXChain.h"
#include "Audio/MixerEngine.h"
#include "Audio/VoiceEngine.h"

#include <array>
#include <memory>

// Abstract base for all mu-family plugin processors.
//
// Owns the three pieces every mu-family plugin shares:
//   1. apvts  — the APVTS instance. Derived plugins supply the layout
//               at construction (createParameterLayout()).
//   2. fxChain + mixerEngine — shared DSP plumbing.
// Provides processCoreBlock() as the common per-block mixing entry point.
// Derived classes supply the trigger engine (Euclidean, MIDI, etc.) and
// call processCoreBlock() from their own processBlock().
//
// Keeping apvts on the base lets mu-core UI components (MixerChannel, FXRow,
// MixerOverlay) take a `ProcessorBase*` instead of forward-declaring each
// plugin's concrete `PluginProcessor` type — eliminates the layering
// violation that previously had mu-core including mu-clid headers.
class ProcessorBase : public juce::AudioProcessor
{
public:
    ProcessorBase(const BusesProperties& props,
                  juce::AudioProcessorValueTreeState::ParameterLayout layout,
                  const juce::Identifier& stateTreeType = juce::Identifier("MuFamilyState"));
    ~ProcessorBase() override = default;

    // Public so UI panels (MixerOverlay, FXRow, MixerChannel, etc.) can access
    // them directly — matches the layout PluginProcessor had before extraction.
    // `apvts` must be the FIRST data member: ProcessorBase's other members and
    // derived-class members may depend on it during their construction.
    juce::AudioProcessorValueTreeState apvts;
    FXChain     fxChain;
    MixerEngine mixerEngine;

    // ─── Channel metadata for shared mixer UI ────────────────────────────────
    // Each mu-family plugin has some N "channels" (rhythms in mu-clid; whatever
    // the trigger model dictates in mu-tant / mu-toni) — each with a display
    // name and a palette colour index. The shared MixerOverlay / MixerChannel
    // UI calls these to label channel strips, populate sidechain-source
    // dropdowns, etc., without needing to know what a channel actually IS.
    virtual int         getNumChannels()              const = 0;
    virtual juce::String getChannelName(int idx)       const = 0;
    virtual int         getChannelColourIndex(int idx) const = 0;

protected:

    // Sets the host BPM on the FX chain (tempo-synced FX) then calls
    // mixerEngine.processBlock(). Derived class calls this at the end of
    // processBlock() after triggers have fired and modulation has been applied.
    // `retired` (Stage 34) forwards the polyphonic-tail descriptor through to
    // the mixer's per-channel render phase — nullptr disables the feature.
    void processCoreBlock(juce::AudioBuffer<float>&                masterBus,
                          std::unique_ptr<VoiceEngine>*            voices,
                          int                                      numVoices,
                          int                                      numSamples,
                          double                                   effectiveBpm,
                          std::array<juce::AudioBuffer<float>*, 8>* directOuts  = nullptr,
                          juce::AudioBuffer<float>*                fxReturnsOut = nullptr,
                          const RetiredVoices*                     retired      = nullptr);

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ProcessorBase)
};
