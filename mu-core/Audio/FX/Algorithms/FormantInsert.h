#pragma once

#include "Audio/FX/Insert/InsertAlgorithmBase.h"
#include "Audio/FX/Algorithms/FxUse.h"
#include "Audio/FX/Algorithms/InsertSupport.h"
#include <cmath>

namespace mu_audio
{
// insertAlgo = 27. Formant: a vowel filter. Three band-pass peaks (the first three formants of a voice) sit in a fixed
// relationship and morph between the vowels A, E, I, O and U, so the sound turns into a voice-like "ah", "ee", "oh".
//   - Vowel      0..100 %: A at 0, E at 25, I at 50, O at 75, U at 100; between stops the formant frequencies are blended
//                on a log scale, so the morph is continuous.
//   - Shift      -12..12 semitones: moves all three formants together (up is a smaller throat, down a larger one).
//   - Resonance  0..100 %: how narrow the peaks are (Q 3 to 40, exponential): breathy and open to nasal and ringing.
//   - Mix        0..100 %: dry / wet; 0 leaves the signal untouched.
// Three parallel TPT state-variable band-passes (Zavalishin, "The Art of VA Filter Design"; Cytomic form), summed with fixed
// gains {1, 0.5, 0.2}. Vowel table: adult male formants, after Peterson & Barney (1952). No latency.
class FormantInsert : public InsertAlgorithmBase
{
public:
    static constexpr FxUse kUse = FxUse::Insert;

    void prepare(double sampleRate, int) override
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        vowelSm.reset(sr, 0.015);
        shiftSm.reset(sr, 0.015);
        resSm.reset(sr, 0.015);
        mixSm.reset(sr, 0.015);
        reset();
    }

    void reset() override
    {
        for (auto& ch : state) for (auto& b : ch) b = {};
        primed = false;
        untilUpdate = 0;
        lastV = lastSh = lastR = -1.0e9f;
        wasActive = true;
    }

    void process(juce::AudioBuffer<float>& buf, int ns, int nCh,
                 const VoiceParams& p, float& /*grOut*/) override
    {
        using namespace insert_support;
        const int nChState = juce::jmin(nCh, 2);
        if (nChState <= 0 || ns <= 0) return;
        // Slot 0 = Vowel 0..100 %, 1 = Shift -12..12 st, 2 = Resonance 0..100 %, 3 = Mix 0..100 %.
        const float vowel = juce::jlimit(0.0f, 100.0f, finiteOr(insertSlot(p, 0), 0.0f));
        const float shift = juce::jlimit(-12.0f, 12.0f, finiteOr(insertSlot(p, 1), 0.0f));
        const float res   = juce::jlimit(0.0f, 100.0f, finiteOr(insertSlot(p, 2), 40.0f));
        const float mix   = juce::jlimit(0.0f, 1.0f, finiteOr(insertSlot(p, 3), 100.0f) * 0.01f);
        if (! primed)
        {
            vowelSm.setCurrentAndTargetValue(vowel);
            shiftSm.setCurrentAndTargetValue(shift);
            resSm.setCurrentAndTargetValue(res);
            mixSm.setCurrentAndTargetValue(mix);
            primed = true;
            untilUpdate = 0;
        }
        vowelSm.setTargetValue(vowel);
        shiftSm.setTargetValue(shift);
        resSm.setTargetValue(res);
        mixSm.setTargetValue(mix);

        if (mix <= 0.0f && ! mixSm.isSmoothing() && mixSm.getCurrentValue() <= 0.0f)
        {
            if (wasActive)
            {
                for (auto& ch : state) for (auto& b : ch) b = {};
                wasActive = false;
            }
            return;
        }
        wasActive = true;

        auto chans = buf.getArrayOfWritePointers();
        // Per sample: while any control glides, retune the three bands every 32 samples; otherwise only when a target moved.
        for (int i = 0; i < ns; ++i)
        {
            const float v = vowelSm.getNextValue(), sh = shiftSm.getNextValue(), r = resSm.getNextValue();
            const float m = mixSm.getNextValue();
            const bool gliding = vowelSm.isSmoothing() || shiftSm.isSmoothing() || resSm.isSmoothing();
            if (untilUpdate <= 0 && (gliding || v != lastV || sh != lastSh || r != lastR))
            {
                retune(v, sh, r);
                lastV = v; lastSh = sh; lastR = r;
                untilUpdate = 32;
            }
            --untilUpdate;
            for (int ch = 0; ch < nChState; ++ch)
            {
                const float x = sane(chans[ch][i]);
                float sum = 0.0f;
                for (int b = 0; b < 3; ++b)
                    sum += kBandGain[b] * bandPass(state[ch][b], b, x);
                chans[ch][i] = x + m * (sum * level - x);
            }
        }
    }

private:
    struct Band { float ic1 = 0.0f, ic2 = 0.0f; };

    // Adult male formants F1 / F2 / F3 in Hz for A, E, I, O, U (Peterson & Barney 1952, rounded).
    static constexpr float kVowels[5][3] = { { 800.0f, 1150.0f, 2900.0f }, { 400.0f, 1600.0f, 2700.0f }, { 350.0f, 1700.0f, 2700.0f },
                                             { 450.0f, 800.0f, 2830.0f },  { 325.0f, 700.0f, 2530.0f } };
    static constexpr float kBandGain[3] = { 1.0f, 0.5f, 0.2f };
    static constexpr float kMakeup = 4.5f;

    // One unity-peak band-pass sample: TPT state-variable, k * v1 (Cytomic form, stable while the cut-off moves).
    float bandPass(Band& s, int b, float x) noexcept
    {
        const float v3 = x - s.ic2;
        const float v1 = a1[b] * s.ic1 + a2[b] * v3;
        const float v2 = s.ic2 + a2[b] * s.ic1 + a3[b] * v3;
        s.ic1 = insert_support::flush(2.0f * v1 - s.ic1);
        s.ic2 = insert_support::flush(2.0f * v2 - s.ic2);
        return k * v1;
    }

    // Blends the vowel table to the morph position, applies the shift, and derives the filter coefficients.
    void retune(float vowelPct, float shiftSt, float resPct) noexcept
    {
        const float pos = vowelPct * 0.04f;                      // 0..4 across the five stops
        const int   i0 = juce::jlimit(0, 3, (int) std::floor(pos));
        const float f = pos - (float) i0;
        const float shiftRatio = std::exp2(shiftSt / 12.0f);
        const float q = 3.0f * std::pow(40.0f / 3.0f, resPct * 0.01f);
        k = 1.0f / q;
        // Level: narrow bands pass less noise power, so the sum is scaled up with Q (see the ruling). The make-up of about
        // +13 dB brings pink noise out near its input level: three narrow bands keep only a small part of a broadband signal.
        level = kMakeup * std::sqrt(q / 4.0f) / std::sqrt(1.0f + 0.25f + 0.04f);
        const float maxHz = 0.45f * (float) sr;
        for (int b = 0; b < 3; ++b)
        {
            const float lo = std::log(kVowels[i0][b]), hi = std::log(kVowels[i0 + 1][b]);
            float hz = std::exp(lo + (hi - lo) * f) * shiftRatio;
            hz = juce::jlimit(60.0f, maxHz, hz);
            const float g = std::tan(juce::MathConstants<float>::pi * hz / (float) sr);
            a1[b] = 1.0f / (1.0f + g * (g + k));
            a2[b] = g * a1[b];
            a3[b] = g * a2[b];
        }
    }

    double sr = 44100.0;
    Band   state[2][3];
    float  a1[3] = {}, a2[3] = {}, a3[3] = {};   // per-band filter coefficients
    float  k = 0.25f, level = 1.0f;              // 1/Q shared by the bands, and the level compensation
    int    untilUpdate = 0;
    float  lastV = -1.0e9f, lastSh = -1.0e9f, lastR = -1.0e9f;   // the controls the bands were last tuned for
    bool   primed = false;
    bool   wasActive = true;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> vowelSm, shiftSm, resSm, mixSm;
};
}  // namespace mu_audio
