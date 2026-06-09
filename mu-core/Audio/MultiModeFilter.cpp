#include "MultiModeFilter.h"
#include <cmath>

#include "Audio/Filters/Lp12Filter.h"
#include "Audio/Filters/Hp12Filter.h"
#include "Audio/Filters/Bp12Filter.h"
#include "Audio/Filters/Notch12Filter.h"
#include "Audio/Filters/Lp24Filter.h"
#include "Audio/Filters/Hp24Filter.h"
#include "Audio/Filters/Bp24Filter.h"
#include "Audio/Filters/Lp6Filter.h"
#include "Audio/Filters/CombPlusFilter.h"
#include "Audio/Filters/Ap12Filter.h"
#include "Audio/Filters/Notch24Filter.h"
#include "Audio/Filters/Hp6Filter.h"
#include "Audio/Filters/PeakFilter.h"
#include "Audio/Filters/LowShelfFilter.h"
#include "Audio/Filters/HighShelfFilter.h"
#include "Audio/Filters/CombMinusFilter.h"

MultiModeFilter::MultiModeFilter()
{
    // pre-allocate every algorithm. Index = filter type code (matches the
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
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;

    for (auto& a : algorithms)
        if (a) a->prepare(sampleRate, blockSize, numChannels);

    lowCutFilter.prepare(sampleRate, blockSize, numChannels);

    smoothedDrive.reset(sampleRate, 0.01);
    smoothedDrive.setCurrentAndTargetValue(0.0f);
    smoothedLowCutHz.reset(sampleRate, 0.015);
    smoothedLowCutHz.setCurrentAndTargetValue(0.0f);
}

void MultiModeFilter::reset()
{
    for (auto& a : algorithms)
        if (a) a->reset();
    lowCutFilter.reset();
}

void MultiModeFilter::process(juce::AudioBuffer<float>& buffer,
                              int numSamples, int numChannels)
{
    const int nCh = juce::jmin(numChannels, MaxChannels, buffer.getNumChannels());
    const int ns  = juce::jmin(numSamples, buffer.getNumSamples());
    if (ns <= 0 || nCh <= 0) return;

    // Pre-filter valve saturation: asymmetric tanh/algebraic soft-clip adds
    // even harmonics (warmth). Bypassed when drive is zero and settled.
    if (smoothedDrive.getCurrentValue() > 0.001f || smoothedDrive.isSmoothing())
    {
        float* chData[MaxChannels] = {};
        for (int ch = 0; ch < nCh; ++ch)
            chData[ch] = buffer.getWritePointer(ch);

        for (int i = 0; i < ns; ++i)
        {
            const float drv     = smoothedDrive.getNextValue();
            // Squared drive curve: same 1×..6× pre-gain endpoints (drv=1 → 6×) but a
            // gradual onset, so the lower half of the knob adds far less saturation
            // (drv=0.5 → 2.25× rather than 3.5×) — drive no longer "kicks in" early.
            const float preGain = 1.0f + drv * drv * 5.0f;
            // Square-root makeup (not full 1/preGain): saturated peaks hold their
            // level and quiet passages get boosted by the residual gain → fatter,
            // not thinner. Full inverse compensation collapsed level past ~30%.
            const float invGain = 1.0f / std::sqrt(preGain);
            for (int ch = 0; ch < nCh; ++ch)
            {
                const float y = chData[ch][i] * preGain;
                // Positive: tanh.  Negative: x/(1−x) (slightly harder → even harmonics).
                chData[ch][i] = (y >= 0.0f ? std::tanh(y) : y / (1.0f - y)) * invGain;
            }
        }
    }

    // Clamp cutoff against the real Nyquist before any algorithm sees it. The
    // APVTS ceiling is 20 kHz, which exceeds Nyquist at sample rates ≤ 40 kHz —
    // the SVF/Ladder would assert (debug) or go unstable (release) above fs/2.
    const float maxCut  = 0.45f * (float) currentSampleRate;
    const float safeCut = juce::jlimit(20.0f, maxCut, cutoffHz);

    // Main filter algorithm.
    const int idx = juce::jlimit(0, kNumFilterAlgos - 1, typeCodeValue);
    if (auto* algo = algorithms[(size_t) idx].get())
        algo->process(buffer, ns, nCh, safeCut, resonance);

    // Post-filter 4-pole high-pass (lo-cut). Per-block smoothed; bypassed at 0.
    const float loHz = smoothedLowCutHz.skip(ns);
    if (loHz > 0.5f)
        lowCutFilter.process(buffer, ns, nCh, juce::jmin(loHz, maxCut), 0.0f);
}
