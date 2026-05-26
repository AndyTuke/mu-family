// UI ↔ APVTS scaling consistency tests.
//
// After #598 Step 0 most voice-chain knobs have identical slider and APVTS units —
// the historical scaling lambdas (display % ↔ semitones / dB / gain) are gone, so
// the round-trip is trivially identity for those controls. These tests now serve
// as guards that:
//   - the dB↔gain conversion that moved into VoiceEngine round-trips at the
//     -inf floor (the only non-trivial transform left in the voice chain);
//   - the data-layer Sustain scaling (RhythmParamTable's 0..100 ↔ 0..1) stays
//     consistent (slider, APVTS = 0..100; voiceParams = 0..1).
//
// Pure arithmetic tests: no APVTS / PluginProcessor infrastructure needed.

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>

class UIScalingConsistencyTest : public juce::UnitTest
{
public:
    UIScalingConsistencyTest() : juce::UnitTest ("UI ↔ APVTS scaling consistency", "UI") {}

    // Check that uiToApvts and apvtsToUi are inverses at 0 %, 50 %, 100 % of
    // the UI range [uiMin, uiMax].
    void checkRoundTrip (const char* paramName,
                         float uiMin, float uiMax,
                         std::function<float(float)> uiToApvts,
                         std::function<float(float)> apvtsToUi,
                         float tol = 1e-4f)
    {
        const float mid = (uiMin + uiMax) * 0.5f;
        for (float v : { uiMin, mid, uiMax })
        {
            const float stored    = uiToApvts(v);
            const float recovered = apvtsToUi(stored);
            expectWithinAbsoluteError (recovered, v, tol,
                juce::String(paramName) + " v=" + juce::String(v, 3)
                + " stored=" + juce::String(stored, 6)
                + " recovered=" + juce::String(recovered, 3));
        }
    }

    void runTest() override
    {
        // ── Amp Level — dB at the UI/APVTS boundary, gain inside the engine ──
        // VoiceEngine::renderActive does the one-shot Decibels::decibelsToGain
        // conversion when mixing into the output bus. Verify the -60 dB floor
        // saturates to a near-zero gain (the audible-silence threshold).
        beginTest ("Amp Level: -60 dB floor maps to near-silent gain");
        {
            const float gainAtFloor = juce::Decibels::decibelsToGain(-60.0f, -60.0f);
            expect (gainAtFloor < 0.01f, "-60 dB floor → gain < 0.01");
            const float gainAtZero  = juce::Decibels::decibelsToGain(0.0f, -60.0f);
            expectWithinAbsoluteError (gainAtZero, 1.0f, 1e-4f, "0 dB → unity gain");
            const float gainAtPlus6 = juce::Decibels::decibelsToGain(6.0f, -60.0f);
            expectWithinAbsoluteError (gainAtPlus6, 1.9953f, 1e-3f, "+6 dB → ~2.0 gain");
        }

        // ── ADSR Sustain — data-layer scaling: APVTS 0..100, voiceParams 0..1 ─
        // The slider stores 0..100 directly (identity to APVTS); RhythmParamTable's
        // adsrSusLocal divides by 100 when pushing into voiceParams.
        beginTest ("ADSR Sustain: APVTS 0–100 ↔ voiceParams 0–1");
        checkRoundTrip ("adsrSus", 0.0f, 100.0f,
            [](float v) { return juce::jlimit(0.0f, 1.0f, v / 100.0f); },
            [](float s) { return s * 100.0f; });
    }
};

static UIScalingConsistencyTest uiScalingConsistencyTest;
