#pragma once

#include "Audio/FX/Algorithms/InsertAlgorithmBase.h"
#include "Audio/FX/Algorithms/FxUse.h"
#include <cmath>

namespace mu_audio
{
// insertAlgo = 23. Gate: a downward noise gate that cuts the sound below a level, no look-ahead (so no latency).
//   - Threshold -80..0 dB: the level above which the gate opens.
//   - Range     -80..0 dB: how far a closed gate cuts; 0 leaves the signal untouched, -80 shuts it fully (gain exactly 0).
//   - Attack    0.1..50 ms: time of the gain rise when the gate opens (two smoothing stages, so it eases in and out).
//   - Release   5..2000 ms: time of the gain fall once the gate closes (same two stages).
// The detector is the louder of the two channels through a peak follower (instant rise, 5 ms fall). The gate opens above the
// threshold and closes 4 dB below it (hysteresis, so a level hovering at the threshold does not chatter) after a 20 ms hold; both
// values are fixed. One gain serves both channels, so the stereo image does not shift.
class GateInsert : public InsertAlgorithmBase
{
public:
    static constexpr FxUse kUse = FxUse::Insert;

    void prepare(double sampleRate, int) override
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        holdSamples = juce::jmax(1, (int) std::lround(kHoldSeconds * sr));
        detRelease  = std::exp(-1.0f / juce::jmax(1.0f, (float) (kDetectorReleaseSeconds * sr)));
        reset();
    }

    void reset() override
    {
        // Starts open, so the first note is not clipped; it closes by itself if the signal stays below the threshold.
        detector = 0.0f;
        gain = 1.0f;
        gainStage = 1.0f;
        open = true;
        hold = holdSamples;
    }

    void process(juce::AudioBuffer<float>& buf, int ns, int nCh,
                 const VoiceParams& p, float& grOut) override
    {
        if (nCh <= 0 || ns <= 0) return;
        // Slot 0 = Threshold dB, 1 = Range dB, 2 = Attack ms, 3 = Release ms. A NaN knob falls back to its default.
        auto finiteOr = [](float v, float fb) { return std::isfinite(v) ? v : fb; };
        const float thrDb   = juce::jlimit(-80.0f, 0.0f,  finiteOr(insertSlot(p, 0), -40.0f));
        const float rangeDb = juce::jlimit(-80.0f, 0.0f,  finiteOr(insertSlot(p, 1), -60.0f));
        const float attMs   = juce::jlimit(0.1f,  50.0f,  finiteOr(insertSlot(p, 2), 1.0f));
        const float relMs   = juce::jlimit(5.0f,  2000.0f, finiteOr(insertSlot(p, 3), 150.0f));
        const float openThr  = std::pow(10.0f, thrDb / 20.0f);
        const float closeThr = openThr * std::pow(10.0f, -kHysteresisDb / 20.0f);
        const bool  cuts     = rangeDb < -0.05f;                       // Range 0: the gate does nothing, the signal stays bit-exact
        const float floorGain = rangeDb <= -79.9f ? 0.0f : std::pow(10.0f, rangeDb / 20.0f);
        const float attCoef = 1.0f - std::exp(-1.0f / juce::jmax(1.0f, 0.5f * attMs * 0.001f * (float) sr));   // half time per stage
        const float relCoef = 1.0f - std::exp(-1.0f / juce::jmax(1.0f, 0.5f * relMs * 0.001f * (float) sr));
        const int   nDet = juce::jmin(nCh, 2);
        auto chans = buf.getArrayOfWritePointers();

        float blockPeak = 0.0f;   // the loudest detected sample of the block, for the Threshold knob's level meter
        float minGain   = 1.0f;   // the deepest cut of the block, for its gain-reduction meter
        // Per sample: detect the level, decide open or closed, move the shared gain toward its target, apply it to every channel.
        for (int i = 0; i < ns; ++i)
        {
            float d = 0.0f;
            for (int ch = 0; ch < nCh; ++ch)
            {
                float& s = chans[ch][i];
                if (! std::isfinite(s)) s = 0.0f;   // a NaN or Inf must not latch the detector or the gain
                if (ch < nDet) d = juce::jmax(d, std::abs(s));
            }
            blockPeak = juce::jmax(blockPeak, d);
            detector = juce::jmax(d, detector * detRelease);
            if (detector < 1.0e-20f) detector = 0.0f;

            if (detector > openThr)       { open = true; hold = holdSamples; }
            else if (open && detector < closeThr)
            {
                if (hold > 0) --hold;
                else          open = false;
            }

            // Two cascaded one-pole smoothers, each at half the knob's time: the gain eases in and out of every move instead of
            // starting at full speed, which would click. The pair reaches 63 % in about 1.07x the knob time.
            const float target = open ? 1.0f : floorGain;
            const float coef = target > gain ? attCoef : relCoef;
            gainStage += (target - gainStage) * coef;
            gain      += (gainStage - gain) * coef;
            if (std::abs(gainStage - target) < 1.0e-7f) gainStage = target;
            if (std::abs(gain - target) < 1.0e-7f && gainStage == target) gain = target;   // lands exactly (Range -80 gives exact zeros)

            if (cuts)
            {
                minGain = juce::jmin(minGain, gain);
                for (int ch = 0; ch < nCh; ++ch)
                    chans[ch][i] *= gain;
            }
        }

        // Publish both meters for the Threshold knob's rings, each on the knob's own -80..0 dB sweep (0..1): the input level,
        // and the gain reduction (80 dB of cut = the full sweep), packed into the one meter value.
        const float levelDb = 20.0f * std::log10(juce::jmax(blockPeak, 1.0e-6f));
        const float grDb    = -20.0f * std::log10(juce::jmax(minGain, 1.0e-4f));
        grOut = mu_ui::packLevelAndGR((levelDb + 80.0f) / 80.0f, grDb / 80.0f);
    }

private:
    static constexpr double kHoldSeconds            = 0.020;
    static constexpr double kDetectorReleaseSeconds = 0.005;
    static constexpr float  kHysteresisDb           = 4.0f;

    double sr = 44100.0;
    int    holdSamples = 882;
    float  detRelease = 0.0f;
    float  detector = 0.0f;
    float  gain = 1.0f;        // the applied gain (second smoothing stage)
    float  gainStage = 1.0f;   // the first smoothing stage
    bool   open = true;
    int    hold = 0;
};
}  // namespace mu_audio
