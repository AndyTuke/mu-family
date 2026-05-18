#include "ProcessorBase.h"

ProcessorBase::ProcessorBase(const BusesProperties& props)
    : juce::AudioProcessor(props)
{}

void ProcessorBase::processCoreBlock(juce::AudioBuffer<float>&                masterBus,
                                     std::unique_ptr<VoiceEngine>*            voices,
                                     int                                      numVoices,
                                     int                                      numSamples,
                                     double                                   effectiveBpm,
                                     std::array<juce::AudioBuffer<float>*, 8>* directOuts,
                                     juce::AudioBuffer<float>*                fxReturnsOut,
                                     const RetiredVoices*                     retired)
{
    fxChain.setHostBpm(effectiveBpm);
    mixerEngine.processBlock(masterBus, numVoices, voices, fxChain, numSamples,
                             directOuts, fxReturnsOut, retired);
}
