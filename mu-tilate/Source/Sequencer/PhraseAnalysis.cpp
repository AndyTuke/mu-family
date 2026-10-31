#include "Sequencer/PhraseAnalysis.h"

namespace mu_tilate
{

mu_core::OnsetCurve analyseLoop(const juce::AudioBuffer<float>& audio, double sampleRate, const DetectSettings& s)
{
    if (s.method == 2) return {};   // Even divisions: no analysis
    const auto method = s.method == 1 ? mu_core::OnsetMethod::SuperFlux : mu_core::OnsetMethod::SpectralFlux;
    return mu_core::computeCurve(audio, sampleRate, method, s.lowCutHz, s.highCutHz);
}

SliceList detectSlices(const mu_core::OnsetCurve& curve, const juce::AudioBuffer<float>& audio,
                       const DetectSettings& s, const SliceList* previous)
{
    SliceList list = previous != nullptr && previous->loopLength() == audio.getNumSamples() ? *previous : SliceList(audio.getNumSamples());
    if (s.method == 2)
    {
        list.divideEvenly(s.evenParts);
        return list;
    }

    mu_core::OnsetPickSettings pick;
    pick.sensitivity = s.sensitivity;
    pick.minGapMs    = s.minGapMs;
    pick.preRollMs   = s.preRollMs;
    list.applyDetected(mu_core::pickOnsets(curve, pick, &audio));
    return list;
}

} // namespace mu_tilate
