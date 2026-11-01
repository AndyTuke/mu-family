#include "Sequencer/PhraseAnalysis.h"

namespace mu_tilate
{

namespace
{
    // The rate the curve was analysed at (the Even method has no curve: assume 48 kHz, only a tolerance depends on it).
    double sampleRateOf(const juce::AudioBuffer<float>&, const mu_core::OnsetCurve& curve) { return curve.sampleRate > 0.0 ? curve.sampleRate : 48000.0; }
}

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
        list.divideEvenly(s.evenParts, (int) (0.02 * sampleRateOf(audio, curve)));
        return list;
    }

    mu_core::OnsetPickSettings pick;
    pick.sensitivity = s.sensitivity;
    pick.minGapMs    = s.minGapMs;
    pick.preRollMs   = s.preRollMs;
    list.applyDetected(mu_core::pickOnsets(curve, pick, &audio), 32, (int) (0.02 * sampleRateOf(audio, curve)));   // settings stay with a cut that moved by up to 20 ms
    return list;
}

} // namespace mu_tilate
