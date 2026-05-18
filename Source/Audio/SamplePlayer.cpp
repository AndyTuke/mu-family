#include "SamplePlayer.h"

#include <algorithm>

void SamplePlayer::trigger(int fadeInSamples)
{
    // #419: store fade-in length before flipping `triggered`. The audio thread
    // consumes both in process() — exchange on triggered acts as the release
    // barrier that publishes pendingFadeInSamples.
    pendingFadeInSamples.store(fadeInSamples, std::memory_order_relaxed);
    triggered.store(true, std::memory_order_release);
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
        // #419: latch the pending fade-in length into audio-thread-only state.
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

        // #419: linear fade-in ramp. When fadeInRemaining == 0 the multiplier
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

        playPos += ratios[s];
    }
}
