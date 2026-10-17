#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

// Updating a juce::ADSR's parameters while it runs. juce::ADSR::setParameters recomputes the release rate as
// sustain / (release x sampleRate) (juce_ADSR.h recalculateRates): with Sustain 0 that rate is 0 and a release
// in progress jumps straight to idle, and with Sustain above 0 a release started from another level changes
// length (noteOff took its rate from the current level). Voices that push their parameters every block must
// therefore apply them only when they changed and never while the envelope is releasing; the change lands
// once the release has ended or the next note starts.
namespace mu_env
{
inline bool sameParameters (const juce::ADSR::Parameters& a, const juce::ADSR::Parameters& b) noexcept
{
    return a.attack == b.attack && a.decay == b.decay && a.sustain == b.sustain && a.release == b.release;
}

// `releasing` is the caller's flag: set at noteOff, cleared at noteOn.
inline void applyParameters (juce::ADSR& env, const juce::ADSR::Parameters& p, bool releasing) noexcept
{
    if (releasing && env.isActive())
        return;
    if (! sameParameters (env.getParameters(), p))
        env.setParameters (p);
}
} // namespace mu_env
