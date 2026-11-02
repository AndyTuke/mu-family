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

    // Phase 1: mix to mono, with half a window of silence at each end so frames centre on every sample of the loop (an attack
    // within the first or last half window still gets frames of its own).
    std::vector<float> mono((size_t) n + kFftSize, 0.0f);
    const int nch = audio.getNumChannels();
    for (int c = 0; c < nch; ++c)
    {
        const float* d = audio.getReadPointer(c);
        for (int i = 0; i < n; ++i) mono[(size_t) i + kFftSize / 2] += d[i] / (float) nch;
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

    const int frames = n / kHop + 1;   // frame f is centred on sample f * kHop
    curve.flux.assign((size_t) frames, 0.0f);
    curve.level.assign((size_t) frames, 0.0f);
    for (int f = 0; f < frames; ++f)
    {
        std::fill(fftBuf.begin(), fftBuf.end(), 0.0f);
        std::copy(mono.begin() + (std::ptrdiff_t) f * kHop, mono.begin() + (std::ptrdiff_t) f * kHop + kFftSize, fftBuf.begin());
        float lvl = 0.0f;
        for (int i = 0; i < kFftSize; ++i) lvl = std::max(lvl, std::abs(fftBuf[(size_t) i]));
        curve.level[(size_t) f] = lvl;
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

    // Phase 2: local maxima above the threshold (a peak must be the largest within +-3 frames, about 16 ms), in frames that
    // are not near-silent: the curve is scaled to its own peak, so a hiss would otherwise show as full-size onsets.
    constexpr float kLevelFloor = 0.001f;   // about -60 dBFS
    struct Peak { int frame; float strength; };
    std::vector<Peak> peaks;
    for (int i = 1; i < frames - 1; ++i)
    {
        const float v = curve.flux[(size_t) i];
        if (v <= curve.baseline[(size_t) i] + offset) continue;
        if (! curve.level.empty() && curve.level[(size_t) std::min(frames - 1, i + 2)] < kLevelFloor) continue;
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

    // Phase 4: frame -> sample position. Frames are centred on f * hop and see an attack first in the leading edge of the
    // window, so a quarter window after the centre is the estimate; the time domain then finds where the attack really starts,
    // then the pre-roll, then the nearest zero crossing of the mono mix.
    const int n = curve.numSamples;
    const int nch = audio != nullptr ? audio->getNumChannels() : 0;
    auto mono = [&](int i)
    {
        float sum = 0.0f;
        for (int c = 0; c < nch; ++c) sum += audio->getReadPointer(c)[i];
        return nch > 0 ? sum / (float) nch : 0.0f;
    };
    const bool haveAudio = audio != nullptr && nch > 0 && audio->getNumSamples() >= n && n > 1;
    const int pre = (int) std::lround((double) s.preRollMs * 0.001 * curve.sampleRate);
    const int snapRange = (int) std::lround(0.001 * curve.sampleRate);
    const int block = std::max(8, (int) std::lround(0.0007 * curve.sampleRate));   // about 0.7 ms
    for (int f : kept)
    {
        int pos = juce::jlimit(0, std::max(0, n - 1), f * curve.hop + kFftSize / 4);

        if (haveAudio)
        {
            // Backtrack: the loudest block of high-frequency energy (the squared first difference) near the estimate is the
            // attack's body; walk back while the blocks before it stay above 15% of it, and the attack starts where they stop.
            const int lo = juce::jlimit(1, n - 1, pos - kFftSize / 2), hi = juce::jlimit(lo, n - 1, pos + kFftSize / 4);
            const int count = std::max(1, (hi - lo) / block);
            std::vector<float> energy((size_t) count, 0.0f);
            int peakBlock = 0;
            for (int b = 0; b < count; ++b)
            {
                float e = 0.0f;
                for (int i = lo + b * block; i < std::min(hi, lo + (b + 1) * block); ++i)
                {
                    const float dlt = mono(i) - mono(i - 1);
                    e += dlt * dlt;
                }
                energy[(size_t) b] = e;
                if (e > energy[(size_t) peakBlock]) peakBlock = b;
            }
            int start = peakBlock;
            while (start > 0 && energy[(size_t) start - 1] >= 0.15f * energy[(size_t) peakBlock]) --start;
            if (energy[(size_t) peakBlock] > 0.0f) pos = lo + start * block;
        }

        pos = juce::jlimit(0, std::max(0, n - 1), pos - pre);
        if (s.snapToZero && haveAudio)
        {
            int best = pos;
            float bestAbs = std::abs(mono(pos));
            for (int k = std::max(1, pos - snapRange); k <= std::min(n - 1, pos + snapRange); ++k)
                if ((mono(k - 1) <= 0.0f) != (mono(k) <= 0.0f) && std::abs(mono(k)) < bestAbs) { best = k; bestAbs = std::abs(mono(k)); }
            pos = best;
        }
        if (out.empty() || pos > out.back()) out.push_back(pos);
    }
    return out;
}

int nearestOnset(const juce::AudioBuffer<float>& audio, double sampleRate, int position, float searchMs)
{
    const int n = audio.getNumSamples(), nch = audio.getNumChannels();
    if (n < 4 || nch < 1 || sampleRate <= 0.0) return position;
    auto mono = [&](int i) { float v = 0.0f; for (int c = 0; c < nch; ++c) v += audio.getSample(c, juce::jlimit(0, n - 1, i)); return v / (float) nch; };
    const int step = std::max(8, (int) (sampleRate * 0.001));
    const int range = (int) (sampleRate * searchMs / 1000.0);
    const int lo = std::max(step, position - range), hi = std::min(n - step, position + range);

    // Phase 1: the 1 ms step with the largest rise in energy over the step before it.
    auto energy = [&](int from) { float e = 0.0f; for (int i = from; i < from + step; ++i) { const float v = mono(i); e += v * v; } return e; };
    int best = -1;
    float bestRise = 1.0e-6f;
    for (int at = lo; at < hi; at += step)
    {
        const float rise = energy(at) - energy(at - step);
        if (rise > bestRise) { bestRise = rise; best = at; }
    }
    if (best < 0) return position;

    // Phase 2: back to the nearest zero crossing before the attack.
    for (int k = best; k > std::max(1, best - step * 2); --k)
        if ((mono(k - 1) <= 0.0f) != (mono(k) <= 0.0f)) return k;
    return best;
}

} // namespace mu_core
