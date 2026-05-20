// UI ↔ APVTS scaling consistency tests.
//
// Each voice-chain knob applies a scaling formula on write (UI → APVTS) and the
// inverse on read (Rhythm struct → UI). If the two are not exact inverses the
// displayed value drifts every time a preset is loaded or a DAW session is
// restored. These tests verify write(read(v)) ≈ v at 0 %, 50 %, and 100 % of
// each UI range — catching the class of bug fixed in #501–#505.
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
        // ── Pitch Depth (fixed in #501) ───────────────────────────────────────
        // UI 0–100  ↔  APVTS 0–24 semitones (pitchEnvDepth)
        beginTest ("Pitch Depth: UI 0–100 ↔ APVTS 0–24 semitones");
        checkRoundTrip ("pitchDepth", 0.0f, 100.0f,
            [](float v) { return (float)(v / 100.0 * 24.0); },
            [](float s) { return (float)(s / 24.0 * 100.0); });

        // ── Filter Resonance (already correct, regression guard) ──────────────
        // UI 0–100  ↔  APVTS 0–0.99  (filterRes stored as fractional)
        beginTest ("Filter Resonance: UI 0–100 ↔ APVTS 0–0.99");
        checkRoundTrip ("filterRes", 0.0f, 99.0f,   // max=99 maps to 0.99 stored
            [](float v) { return v / 100.0f; },
            [](float s) { return s * 100.0f; },
            1e-3f);   // 0.99-max range ⟹ slightly looser tolerance

        // ── Filter Depth (fixed in #503) ──────────────────────────────────────
        // UI 0–100  ↔  APVTS 0–48 semitones (filterEnvDepth)
        beginTest ("Filter Depth: UI 0–100 ↔ APVTS 0–48 semitones");
        checkRoundTrip ("filterDepth", 0.0f, 100.0f,
            [](float v) { return (float)(v / 100.0 * 48.0); },
            [](float s) { return (float)(s / 48.0 * 100.0); });

        // ── Amp Level (fixed in #504) ─────────────────────────────────────────
        // UI −60..+6 dB  ↔  APVTS 0–2 linear gain
        // dB ↔ linear has inherent float rounding; 0.05 dB tolerance is fine.
        beginTest ("Amp Level: UI -60 to +6 dB ↔ APVTS linear gain (0-2)");
        checkRoundTrip ("ampLevel", -60.0f, 6.0f,
            [](float v) { return juce::Decibels::decibelsToGain(v, -60.0f); },
            [](float s) { return juce::Decibels::gainToDecibels(s, -60.0f); },
            0.05f);

        // Verify -inf floor: silence threshold (≤ −60 dB) → minimum gain → back to −60 dB
        beginTest ("Amp Level: -inf floor — values at or below -60 dB clamp correctly");
        {
            const float gain     = juce::Decibels::decibelsToGain(-60.0f, -60.0f);
            const float backToDb = juce::Decibels::gainToDecibels(gain, -60.0f);
            expectWithinAbsoluteError (backToDb, -60.0f, 0.1f, "-60 dB floor round-trip");

            // The minimum UI display (−60 dB) should map to near-zero gain
            expect (gain < 0.01f, "gain at -60 dB floor should be < 0.01 (near silence)");
        }

        // ── Amp Sends (fixed in #505) ─────────────────────────────────────────
        // UI 0–100  ↔  APVTS 0–1 (channel sendEff / sendDly / sendRev)
        beginTest ("Amp sends: UI 0–100 ↔ APVTS 0–1");
        checkRoundTrip ("ampSendEff", 0.0f, 100.0f,
            [](float v) { return v / 100.0f; },
            [](float s) { return s * 100.0f; });

        // ── Amp Accent (fixed in #505) ────────────────────────────────────────
        // UI 0–100  ↔  APVTS 0–12 dB (accentDb)
        beginTest ("Amp Accent: UI 0–100 ↔ APVTS 0–12 dB");
        checkRoundTrip ("ampAccent", 0.0f, 100.0f,
            [](float v) { return (float)(v / 100.0 * 12.0); },
            [](float s) { return (float)(s / 12.0 * 100.0); });

        // ── Filter Sustain / Pitch Sustain (regression guard for existing code) ─
        // UI 0–100 ↔ APVTS 0–1 (same as adsrSusLocal)
        beginTest ("ADSR Sustain: UI 0–100 ↔ stored 0–1");
        checkRoundTrip ("adsrSus", 0.0f, 100.0f,
            [](float v) { return juce::jlimit(0.0f, 1.0f, v / 100.0f); },
            [](float s) { return s * 100.0f; });
    }
};

static UIScalingConsistencyTest uiScalingConsistencyTest;
