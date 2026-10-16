#include "GrooveSequencer.h"
#include <cmath>

namespace mu_on
{

void GrooveSequencer::process(double beatStart, int numSamples, double bpm, const TriggerFn& fire)
{
    if (bpm <= 0.0) return;
    processSpan(beatStart, numSamples, (bpm / 60.0) / sampleRate, fire);
}

// Every row's clock runs (spare rows too, so a row that goes live is already in step); only the live rows fire.
void GrooveSequencer::processSpan(double beatStart, int numSamples, double beatsPerSample, const TriggerFn& fire)
{
    if (! fire) return;
    for (int tr = 0; tr < StepPattern::kNumTracks; ++tr)
    {
        const bool live = tr < activeTracks;
        clocks[(size_t) tr].processSpan(PatternRow { pattern, tr }, beatStart, numSamples, beatsPerSample, swing, accentVel,
                                        [&](float velocity, int offset) { if (live) fire(tr, velocity, offset); });
    }
}

} // namespace mu_on
