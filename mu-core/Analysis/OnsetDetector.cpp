#include "Analysis/OnsetDetector.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>

namespace mu_core
{

namespace
{
constexpr int kFftOrder = 10;               // 1024-point frames
constexpr int kFftSize  = 1 << kFftOrder;
constexpr int kHop      = 256;
constexpr int kBins     = kFftSize / 2;
}

OnsetCurve computeCurve(const juce::AudioBuffer<float>& audio, double sampleRate, OnsetMethod method,
                        float lowCutHz, float highCutHz)
{
    OnsetCurve curve;
    curve.hop = kHop;
    curve.sampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    curve.numSamples = audio.getNumSamples();
    const int n = audio.getNumSamples();
    if (n < kFftSize || audio.getNumChannels() <= 0) return curve;

    // Phase 1: mix to mono.
    std::vector<float> mono((size_t) n, 0.0f);
    const int nch = audio.getNumChannels();
    for (int c = 0; c < nch; ++c)
    {
        const float* d = audio.getReadPointer(c);
        for (int i = 0; i < n; ++i) mono[(size_t) i] += d[i] / (float) nch;
    }

    // Phase 2: the band, as bin limits (default: every bin above DC).
    const double binHz = curve.sampleRate / (double) kFftSize;
    const int loBin = juce::jlimit(1, kBins - 1, (int) std::floor((double) std::max(0.0f, lowCutHz) / binHz));
    const int hiBin = highCutHz > 0.0f ? juce::jlimit(loBin + 1, kBins, (int) std::ceil((double) highCutHz / binHz)) : kBins;

    // Phase 3: windowed frames -> log-compressed magnitude spectra -> half-wave-rectified flux against the previous frame.
    juce::dsp::FFT fft(kFftOrder);
    juce::dsp::WindowingFunction<float> window((size_t) kFftSize, juce::dsp::WindowingFunction<float>::hann, false);
    std::vector<float> fftBuf((size_t) kFftSize * 2, 0.0f);
    std::vector<float> prev((size_t) kBins, 0.0f), cur((size_t) kBins, 0.0f), prevMax((size_t) kBins, 0.0f);

    const int frames = (n - kFftSize) / kHop + 1;
    curve.flux.assign((size_t) frames, 0.0f);
    for (int f = 0; f < frames; ++f)
    {
        std::fill(fftBuf.begin(), fftBuf.end(), 0.0f);
        std::copy(mono.begin() + (std::ptrdiff_t) f * kHop, mono.begin() + (std::ptrdiff_t) f * kHop + kFftSize, fftBuf.begin());
        window.multiplyWithWindowingTable(fftBuf.data(), (size_t) kFftSize);
        fft.performFrequencyOnlyForwardTransform(fftBuf.data());
        for (int b = 0; b < kBins; ++b) cur[(size_t) b] = std::log1p(100.0f * fftBuf[(size_t) b] / (float) kFftSize);

        // SuperFlux compares with the previous frame's max over three neighbouring bins, so a slow vibrato that moves a
        // partial by a bin is not novelty.
        if (method == OnsetMethod::SuperFlux)
            for (int b = 0; b < kBins; ++b)
                prevMax[(size_t) b] = std::max({ prev[(size_t) std::max(0, b - 1)], prev[(size_t) b], prev[(size_t) std::min(kBins - 1, b + 1)] });
        const std::vector<float>& ref = method == OnsetMethod::SuperFlux ? prevMax : prev;

        float flux = 0.0f;
        if (f > 0)
            for (int b = loBin; b < hiBin; ++b) flux += std::max(0.0f, cur[(size_t) b] - ref[(size_t) b]);
        curve.flux[(size_t) f] = flux;
        prev = cur;
    }

    // Phase 4: scale so the loudest frame is 1 (the picking thresholds are relative).
    const float peak = *std::max_element(curve.flux.begin(), curve.flux.end());
    if (peak > 1.0e-9f)
        for (auto& v : curve.flux) v /= peak;

    // Phase 5: the baseline. A median ignores the loud hits around a quiet one, so a soft hat between two kicks is
    // judged against the quiet floor, not against a mean the kicks have pulled up.
    const int half = std::max(2, (int) std::lround(0.5 * curve.sampleRate / (double) kHop));
    curve.baseline.assign((size_t) frames, 0.0f);
    std::vector<float> win;
    for (int i = 0; i < frames; ++i)
    {
        win.assign(curve.flux.begin() + std::max(0, i - half), curve.flux.begin() + std::min(frames, i + half + 1));
        std::nth_element(win.begin(), win.begin() + (std::ptrdiff_t) win.size() / 2, win.end());
        curve.baseline[(size_t) i] = win[win.size() / 2];
    }
    return curve;
}

std::vector<int> pickOnsets(const OnsetCurve& curve, const OnsetPickSettings& s, const juce::AudioBuffer<float>* audio)
{
    std::vector<int> out;
    const int frames = (int) curve.flux.size();
    if (frames < 3) return out;

    // Phase 1: the threshold is the curve's running median plus an offset set by Sensitivity (0.5 = 0.12, 1 = 0.02, 0 = 0.42).
    const float inv = 1.0f - juce::jlimit(0.0f, 1.0f, s.sensitivity);
    const float offset = 0.02f + 0.4f * inv * inv;

    // Phase 2: local maxima above the threshold (a peak must be the largest within +-3 frames, about 16 ms).
    struct Peak { int frame; float strength; };
    std::vector<Peak> peaks;
    for (int i = 1; i < frames - 1; ++i)
    {
        const float v = curve.flux[(size_t) i];
        if (v <= curve.baseline[(size_t) i] + offset) continue;
        bool isMax = true;
        for (int k = std::max(0, i - 3); k <= std::min(frames - 1, i + 3) && isMax; ++k)
            if (curve.flux[(size_t) k] > v || (curve.flux[(size_t) k] == v && k < i)) isMax = false;
        if (isMax) peaks.push_back({ i, v });
    }

    // Phase 3: minimum gap, the stronger peak wins.
    const double gapFrames = (double) s.minGapMs * 0.001 * curve.sampleRate / (double) curve.hop;
    std::sort(peaks.begin(), peaks.end(), [](const Peak& a, const Peak& b) { return a.strength > b.strength; });
    std::vector<int> kept;
    for (const auto& p : peaks)
    {
        bool clear = true;
        for (int k : kept)
            if (std::abs((double) (k - p.frame)) < gapFrames) { clear = false; break; }
        if (clear) kept.push_back(p.frame);
    }
    std::sort(kept.begin(), kept.end());

    // Phase 4: frame -> sample position. A frame's flux first rises when an attack enters the window's tail, so the
    // frame start plus a quarter window is the attack's estimate; then the pre-roll, then the nearest zero crossing.
    const int pre = (int) std::lround((double) s.preRollMs * 0.001 * curve.sampleRate);
    const int snapRange = (int) std::lround(0.001 * curve.sampleRate);
    for (int f : kept)
    {
        int pos = f * curve.hop + kFftSize * 3 / 4 - pre;
        pos = juce::jlimit(0, std::max(0, curve.numSamples - 1), pos);
        if (s.snapToZero && audio != nullptr && audio->getNumSamples() > 1)
        {
            const float* d = audio->getReadPointer(0);
            const int nS = audio->getNumSamples();
            int best = pos;
            float bestAbs = std::abs(d[pos]);
            for (int k = std::max(1, pos - snapRange); k <= std::min(nS - 1, pos + snapRange); ++k)
                if ((d[k - 1] <= 0.0f) != (d[k] <= 0.0f) && std::abs(d[k]) < bestAbs) { best = k; bestAbs = std::abs(d[k]); }
            pos = best;
        }
        if (out.empty() || pos > out.back()) out.push_back(pos);
    }
    return out;
}

} // namespace mu_core
