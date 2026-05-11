#include "SamplePlayer.h"

#include <algorithm>

void SamplePlayer::trigger()
{
    triggered = true;
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
    if (triggered.exchange(false))
        playPos = 0.0;

    if (playPos < 0.0)
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

        for (int ch = 0; ch < outChan; ++ch)
        {
            const int   sc  = ch % srcChan;
            const float smp = source.getSample(sc, i0) * (1.0f - frac)
                            + source.getSample(sc, i1) * frac;
            output.addSample(ch, s, smp);
        }

        playPos += ratios[s];
    }
}
