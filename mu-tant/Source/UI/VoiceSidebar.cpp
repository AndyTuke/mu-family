#include "VoiceSidebar.h"
#include "Plugin/PluginProcessor.h"
#include "UI/Components/MuLookAndFeel.h"

#include <juce_dsp/juce_dsp.h>
#include <cmath>

namespace mu_tant
{

namespace
{
    // Concentric-ring spectrum glyph — reads the voice's post-insert ring buffer
    // at 30 Hz, runs a 512-point FFT, maps bins to 8 log-spaced frequency bands,
    // and draws filled concentric circles: low frequencies at the centre, high
    // frequencies at the outer edge, alpha proportional to band energy.
    class VoiceSpectrumGlyph : public juce::Component,
                               private juce::Timer
    {
    public:
        static constexpr int kBands    = 8;
        static constexpr int kFftOrder = 9;            // 512-point FFT
        static constexpr int kFftSize  = 1 << kFftOrder;

        // Fired when the audio level crosses the pulse threshold (UI thread).
        std::function<void()> onPulse;

        VoiceSpectrumGlyph(juce::Colour col, const VoiceRingBuffer* rb)
            : colour(col), ringBuffer(rb), fft(kFftOrder)
        {
            startTimerHz(30);
        }

    private:
        void timerCallback() override
        {
            if (!ringBuffer) return;

            // Read the most-recent kFftSize samples from the ring buffer.
            ringBuffer->read(fftData.data(), kFftSize);

            // Apply a Hann window to reduce spectral leakage.
            for (int i = 0; i < kFftSize; ++i)
                fftData[i] *= 0.5f * (1.0f - std::cos(
                    juce::MathConstants<float>::twoPi * (float) i / (float)(kFftSize - 1)));

            // Zero the imaginary half before the in-place transform.
            std::fill(fftData.begin() + kFftSize, fftData.end(), 0.0f);

            // Forward FFT → output overwrites fftData[0..kFftSize-1] with magnitudes.
            fft.performFrequencyOnlyForwardTransform(fftData.data());

            // Octave-spaced bin edges: each band spans ~one octave of the spectrum.
            // At 48 kHz / 512 points, bin k ≈ k * 93.75 Hz.
            static constexpr int kEdges[kBands + 1] = { 1, 2, 4, 8, 16, 32, 64, 128, 214 };
            static constexpr float kNorm  = 1.0f / (float)(kFftSize / 2); // full-scale = 1
            static constexpr float kGain  = 12.0f;  // scale moderate audio to visible range
            static constexpr float kDecay = 0.88f;  // ~110 ms hold at 30 Hz

            float peak = 0.0f;
            bool changed = false;

            for (int b = 0; b < kBands; ++b)
            {
                float sum = 0.0f;
                const int lo = kEdges[b], hi = kEdges[b + 1];
                for (int bin = lo; bin < hi; ++bin)
                    sum += fftData[bin];
                // Mean magnitude normalised to full scale, then scaled for visibility.
                const float target = juce::jlimit(0.0f, 1.0f,
                    (sum / (float)(hi - lo)) * kNorm * kGain);
                const float prev = bands[b];
                bands[b] = std::max(target, kDecay * bands[b]);
                if (std::abs(bands[b] - prev) > 0.004f) changed = true;
                peak = std::max(peak, target);
            }

            // Pulse the sidebar ring when audio crosses threshold (rising edge only).
            if (peak > 0.06f && prevPeak <= 0.06f)
                if (onPulse) onPulse();
            prevPeak = peak;

            if (changed) repaint();
        }

        void paint(juce::Graphics& g) override
        {
            const auto b  = getLocalBounds().toFloat().reduced(1.0f);
            const float cx = b.getCentreX(), cy = b.getCentreY();
            const float maxR = std::min(b.getWidth(), b.getHeight()) * 0.5f;

            // Background disc — faint presence even when silent.
            g.setColour(colour.withAlpha(0.10f));
            g.fillEllipse(cx - maxR, cy - maxR, maxR * 2.0f, maxR * 2.0f);

            // Each band is a separate stroked ring so bands are visually independent.
            // Band 0 = innermost (low freq, ~94–188 Hz).
            // Band 7 = outermost (high freq, ~12–20 kHz).
            // A pixel gap between rings keeps them readable at small sizes.
            const float usableR  = maxR - 1.0f;
            const float bandStep = usableR / (float) kBands;
            const float strokeW  = bandStep - 1.0f;  // 1 px gap between rings

            for (int band = 0; band < kBands; ++band)
            {
                // Centre radius of this ring.
                const float r     = (float)(band + 1) * bandStep - strokeW * 0.5f;
                const float alpha = juce::jlimit(0.0f, 0.90f, 0.06f + bands[band] * 0.84f);
                g.setColour(colour.withAlpha(alpha));
                juce::Path ring;
                ring.addEllipse(cx - r, cy - r, r * 2.0f, r * 2.0f);
                g.strokePath(ring, juce::PathStrokeType(strokeW));
            }

            // Outer border.
            g.setColour(colour.withAlpha(0.40f));
            g.drawEllipse(b.reduced(0.5f), 1.0f);
        }

        juce::Colour              colour;
        const VoiceRingBuffer*    ringBuffer;
        juce::dsp::FFT            fft;
        std::array<float, kFftSize * 2> fftData {};
        std::array<float, kBands>       bands   {};
        float                     prevPeak = 0.0f;
    };
}

VoiceSidebar::VoiceSidebar(PluginProcessor& p)
    : ChannelSidebar(p, "Voice")
{
    createMiniVisual = [&p, this](int i) -> std::unique_ptr<juce::Component>
    {
        const auto col = MuLookAndFeel::channelPalette[
            (size_t) (p.getChannelColourIndex(i) % MuLookAndFeel::kChannelPaletteSize)];
        auto glyph = std::make_unique<VoiceSpectrumGlyph>(col, &p.voiceRingBuffers[(size_t) i]);
        glyph->onPulse = [this, i] { pulseItem(i); };
        return glyph;
    };
    onSwapChannels = [&p](int a, int b) { p.swapVoices(a, b); };

    // Per-voice hot-swap badge: the shared ChannelSidebar polls isPendingSwap at
    // 5 Hz to show the staging pill, and lets the user cancel a staged voice swap.
    isPendingSwap       = [&p](int i) { return p.hasPendingSwap(i); };
    onCancelPendingSwap = [&p](int i) { p.cancelStagedSwap(i); };

    refreshItems();
}

} // namespace mu_tant
