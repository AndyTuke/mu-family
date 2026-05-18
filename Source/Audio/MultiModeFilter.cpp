#include "MultiModeFilter.h"

#include "Audio/Processing/Filters/Lp12Filter.h"
#include "Audio/Processing/Filters/Hp12Filter.h"
#include "Audio/Processing/Filters/Bp12Filter.h"
#include "Audio/Processing/Filters/Notch12Filter.h"
#include "Audio/Processing/Filters/Lp24Filter.h"
#include "Audio/Processing/Filters/Hp24Filter.h"
#include "Audio/Processing/Filters/Bp24Filter.h"
#include "Audio/Processing/Filters/Lp6Filter.h"
#include "Audio/Processing/Filters/CombPlusFilter.h"
#include "Audio/Processing/Filters/Ap12Filter.h"
#include "Audio/Processing/Filters/Notch24Filter.h"
#include "Audio/Processing/Filters/Hp6Filter.h"
#include "Audio/Processing/Filters/PeakFilter.h"
#include "Audio/Processing/Filters/LowShelfFilter.h"
#include "Audio/Processing/Filters/HighShelfFilter.h"
#include "Audio/Processing/Filters/CombMinusFilter.h"

MultiModeFilter::MultiModeFilter()
{
    // #427: pre-allocate every algorithm. Index = filter type code (matches the
    // dropdown order: see the table in MultiModeFilter.h). Each algorithm owns
    // its own DSP state; switching `typeCodeValue` between blocks just changes
    // which one process() dispatches to.
    algorithms[0]  = std::make_unique<Lp12Filter>();
    algorithms[1]  = std::make_unique<Hp12Filter>();
    algorithms[2]  = std::make_unique<Bp12Filter>();
    algorithms[3]  = std::make_unique<Notch12Filter>();
    algorithms[4]  = std::make_unique<Lp24Filter>();
    algorithms[5]  = std::make_unique<Hp24Filter>();
    algorithms[6]  = std::make_unique<Bp24Filter>();
    algorithms[7]  = std::make_unique<Lp6Filter>();
    algorithms[8]  = std::make_unique<CombPlusFilter>();
    algorithms[9]  = std::make_unique<Ap12Filter>();
    algorithms[10] = std::make_unique<Notch24Filter>();
    algorithms[11] = std::make_unique<Hp6Filter>();
    algorithms[12] = std::make_unique<PeakFilter>();
    algorithms[13] = std::make_unique<LowShelfFilter>();
    algorithms[14] = std::make_unique<HighShelfFilter>();
    algorithms[15] = std::make_unique<CombMinusFilter>();
}

void MultiModeFilter::prepare(double sampleRate, int blockSize, int numChannels)
{
    for (auto& a : algorithms)
        if (a) a->prepare(sampleRate, blockSize, numChannels);
}

void MultiModeFilter::reset()
{
    for (auto& a : algorithms)
        if (a) a->reset();
}

void MultiModeFilter::process(juce::AudioBuffer<float>& buffer,
                              int numSamples, int numChannels)
{
    const int nCh = juce::jmin(numChannels, MaxChannels, buffer.getNumChannels());
    const int ns  = juce::jmin(numSamples, buffer.getNumSamples());
    if (ns <= 0 || nCh <= 0) return;

    const int idx = juce::jlimit(0, kNumAlgorithms - 1, typeCodeValue);
    if (auto* algo = algorithms[(size_t) idx].get())
        algo->process(buffer, ns, nCh, cutoffHz, resonance);
}
