#include "SamplePlayer.h"

#include <algorithm>

void SamplePlayer::trigger(int fadeInSamples)
{
    // store fade-in length before flipping `triggered`. The audio thread
    // consumes both in process() — exchange on triggered acts as the release
    // barrier that publishes pendingFadeInSamples.
    pendingFadeInSamples.store(fadeInSamples, std::memory_order_relaxed);
    triggered.store(true, std::memory_order_release);
}

// A NaN, zero or negative ratio would stall or run backwards; NaN fails the > test so it maps to the floor.
static double safeRatio(double r) noexcept
{
    return (r > 1.0e-3) ? std::min(r, 64.0) : 1.0e-3;
}

void SamplePlayer::skip(const double* ratios, int numSamples) noexcept
{
    // A pending trigger stays pending: the new hit starts at its sample 0 (with its fade-in) in the next rendered
    // block, rather than losing its attack to a block that could not read the sample.
    if (playPos < 0.0 || ratios == nullptr)
        return;

    // The same per-sample advance as process(), with nothing read or written.
    for (int s = 0; s < numSamples; ++s)
        playPos += safeRatio(ratios[s]);
    fadeInRemaining = std::max(0, fadeInRemaining - numSamples);
}

bool SamplePlayer::isActive() const
{
    return playPos >= 0.0 || triggered.load();
}

void SamplePlayer::process(const juce::AudioBuffer<float>& source,
                           const double*                    ratios,
                           juce::AudioBuffer<float>&        output,
                           int                              numSamples)
{
    if (triggered.exchange(false, std::memory_order_acquire))
    {
        playPos = 0.0;
        // latch the pending fade-in length into audio-thread-only state.
        // Reset both `remaining` and `total` so each new trigger starts at the
        // beginning of the ramp; a tied retrigger arriving mid-fade gets the
        // fresh fade length without inheriting the previous one's progress.
        fadeInTotal     = pendingFadeInSamples.load(std::memory_order_relaxed);
        fadeInRemaining = fadeInTotal;
    }

    if (playPos < 0.0 || ratios == nullptr)
        return;

    const int srcLen  = source.getNumSamples();
    const int srcChan = source.getNumChannels();
    const int outChan = output.getNumChannels();

    for (int s = 0; s < numSamples; ++s)
    {
        if (playPos >= static_cast<double>(srcLen))
        {
            playPos = -1.0;
            break;
        }

        const int   i0   = static_cast<int>(playPos);
        const int   i1   = std::min(i0 + 1, srcLen - 1);
        const float frac = static_cast<float>(playPos - i0);

        // linear fade-in ramp. When fadeInRemaining == 0 the multiplier
        // is 1.0 (no-op for the common, untied trigger path).
        float fadeGain = 1.0f;
        if (fadeInRemaining > 0)
        {
            fadeGain = 1.0f - (float)fadeInRemaining / (float)fadeInTotal;
            --fadeInRemaining;
        }

        for (int ch = 0; ch < outChan; ++ch)
        {
            const int   sc  = ch % srcChan;
            const float smp = source.getSample(sc, i0) * (1.0f - frac)
                            + source.getSample(sc, i1) * frac;
            output.addSample(ch, s, smp * fadeGain);
        }

        playPos += safeRatio(ratios[s]);
    }
}
