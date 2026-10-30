#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace mu_core
{

// Sets a parameter from its real (denormalised) value and notifies the host. The UI knobs and selectors hold
// real values (Hz, dB, steps) while a parameter takes 0..1, so every UI-to-parameter write needs this pair of
// calls; null is ignored so a lookup that found nothing can be passed straight in. Message thread.
inline void setParameterActual(juce::RangedAudioParameter* p, float actual)
{
    if (p != nullptr)
        p->setValueNotifyingHost(p->convertTo0to1(actual));
}

} // namespace mu_core
