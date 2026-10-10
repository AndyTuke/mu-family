#pragma once

#include <algorithm>
#include <cmath>

// Tap tempo: the player taps the beat, the tempo follows. Pure logic (no JUCE, no clock of its own):
// the caller passes each tap's time in seconds.
//
// The tempo is the average of the last four tap intervals (so it settles quickly but shrugs off one
// sloppy tap) and needs two taps to exist at all. A pause of more than two seconds starts a fresh
// count, so a stray tap later never drags the tempo. The result is rounded to a tenth and held in
// the family tempo range.
namespace mu_core
{

class TapTempo
{
public:
    static constexpr double kMinBpm = 20.0, kMaxBpm = 300.0;
    static constexpr double kResetGapSeconds = 2.0;
    static constexpr int    kMaxIntervals = 4;

    // Register a tap at `timeSeconds` (any steady clock). Returns true and sets `bpm` once there is a tempo.
    bool tap(double timeSeconds, double& bpm)
    {
        if (numTaps > 0 && timeSeconds - last > kResetGapSeconds)
            numTaps = 0;                              // a long pause: start over

        if (numTaps > 0)
        {
            intervals[(size_t) (head % kMaxIntervals)] = timeSeconds - last;
            ++head;
        }
        else
            head = 0;
        last = timeSeconds;
        ++numTaps;

        const int count = std::min(numTaps - 1, kMaxIntervals);
        if (count < 1)
            return false;

        double sum = 0.0;
        for (int i = 0; i < count; ++i)
            sum += intervals[(size_t) (((head - 1 - i) % kMaxIntervals + kMaxIntervals) % kMaxIntervals)];
        const double raw = 60.0 / (sum / count);
        bpm = std::clamp(std::round(raw * 10.0) / 10.0, kMinBpm, kMaxBpm);
        return true;
    }

    void reset() { numTaps = 0; head = 0; }

private:
    double intervals[kMaxIntervals] {};
    double last    = 0.0;
    int    numTaps = 0;
    int    head    = 0;
};

} // namespace mu_core
