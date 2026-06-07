#include "GrooveSequencer.h"
#include <cmath>

namespace mu_on
{

void GrooveSequencer::process(double beatStart, int numSamples, double bpm, const TriggerFn& fire)
{
    if (numSamples <= 0 || bpm <= 0.0 || ! fire) return;

    const double beatsPerSample = (bpm / 60.0) / sampleRate;
    const double beatEnd        = beatStart + beatsPerSample * (double) numSamples;

    // Resync if the transport jumped (host scrub / a missed reset) so we never burst-fire
    // a long backlog: snap nextGlobalStep to just before the current block.
    const long long expected = (long long) std::floor(beatStart / kStepBeats);
    if (nextGlobalStep < expected - 1 || nextGlobalStep > expected + 1)
        nextGlobalStep = expected;

    // Fire each step whose (swing-adjusted) beat lands within [beatStart, beatEnd).
    for (int guard = 0; guard < StepPattern::kNumSteps * 2; ++guard)
    {
        const double t = stepBeat(nextGlobalStep);
        if (t >= beatEnd) break;
        if (t >= beatStart)
        {
            const int local = (int) (((nextGlobalStep % StepPattern::kNumSteps)
                                       + StepPattern::kNumSteps) % StepPattern::kNumSteps);
            const int sampleOffset = juce::jlimit(0, numSamples - 1,
                                                  (int) std::round((t - beatStart) / beatsPerSample));
            for (int tr = 0; tr < StepPattern::kNumTracks; ++tr)
                if (pattern.isOn(tr, local))
                    fire(tr, pattern.isAccent(tr, local) ? accentVel : normalVel, sampleOffset);
        }
        ++nextGlobalStep;
    }
}

} // namespace mu_on
