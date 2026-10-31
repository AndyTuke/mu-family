#include "Audio/SlicePlayer.h"

#include <algorithm>
#include <cmath>

namespace mu_tilate
{

void SlicePlayer::prepare(double sampleRate)
{
    sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    const double cutSamples = std::max(1.0, (double) kCutFadeMs * 0.001 * sr);
    cutStep = (float) (1.0 / cutSamples);
    reset();
}

void SlicePlayer::reset() noexcept
{
    for (auto& v : voices) v = Voice {};
}

bool SlicePlayer::isActive() const noexcept
{
    for (const auto& v : voices)
        if (v.active) return true;
    return false;
}

void SlicePlayer::cut() noexcept
{
    for (auto& v : voices)
        if (v.active) v.fading = true;
}

void SlicePlayer::trigger(const juce::AudioBuffer<float>* src, SliceRange range, const SliceParams& p) noexcept
{
    // The slice is clamped to the loop; an empty or missing slice is a gap, which still cuts what was sounding.
    cut();
    if (src == nullptr) return;
    range.start = juce::jlimit(0, src->getNumSamples(), range.start);
    range.end   = juce::jlimit(range.start, src->getNumSamples(), range.end);
    if (range.length() <= 0 || src->getNumChannels() <= 0) return;

    // Take a free voice; with none free, steal the one that has been fading longest (lowest cut gain).
    Voice* slot = nullptr;
    for (auto& v : voices)
        if (! v.active) { slot = &v; break; }
    if (slot == nullptr)
    {
        slot = &voices[0];
        for (auto& v : voices)
            if (v.cutGain < slot->cutGain) slot = &v;
    }

    const float rate = std::max(0.01f, p.rate);
    Voice v;
    v.src        = src;
    v.range      = range;
    v.step       = p.reverse ? -(double) rate : (double) rate;
    v.pos        = p.reverse ? (double) (range.end - 1) : (double) range.start;
    v.gain       = p.gain;
    v.fadeInLen  = std::max(1, (int) std::lround((double) p.fadeInMs  * 0.001 * sr));
    v.fadeOutLen = std::max(1, (int) std::lround((double) p.fadeOutMs * 0.001 * sr));
    v.active     = true;
    *slot = v;
}

bool SlicePlayer::inRange(const Voice& v) noexcept
{
    return v.pos >= (double) v.range.start && v.pos < (double) v.range.end;
}

float SlicePlayer::read(const Voice& v, int channel) noexcept
{
    const int nch = v.src->getNumChannels();
    const float* d = v.src->getReadPointer(juce::jmin(channel, nch - 1));

    // Catmull-Rom through the four samples around the position; neighbours outside the slice repeat the edge sample.
    const int   i0 = (int) std::floor(v.pos);
    const float f  = (float) (v.pos - (double) i0);
    auto at = [&](int i) { return d[juce::jlimit(v.range.start, v.range.end - 1, i)]; };
    const float y0 = at(i0 - 1), y1 = at(i0), y2 = at(i0 + 1), y3 = at(i0 + 2);
    return y1 + 0.5f * f * (y2 - y0 + f * (2.0f * y0 - 5.0f * y1 + 4.0f * y2 - y3 + f * (3.0f * (y1 - y2) + y3 - y0)));
}

void SlicePlayer::render(juce::AudioBuffer<float>& out, int n) noexcept
{
    const int nOut = out.getNumChannels();
    if (nOut <= 0) return;

    // Each voice runs the block on its own: slice fades, cut fade, then added into the output.
    for (auto& v : voices)
    {
        if (! v.active) continue;
        const int nSrc = v.src->getNumChannels();

        for (int i = 0; i < n && v.active; ++i)
        {
            if (! inRange(v)) { v.active = false; break; }

            // Fade-in from the start, fade-out over the last samples of the slice (in playback order).
            const double remaining = v.step > 0.0 ? (double) v.range.end - v.pos : v.pos - (double) v.range.start + 1.0;
            const int    left      = (int) (remaining / std::fabs(v.step));
            float env = std::min(1.0f, (float) (v.age + 1) / (float) v.fadeInLen);
            env      *= std::min(1.0f, (float) (left + 1) / (float) v.fadeOutLen);
            if (v.fading)
            {
                v.cutGain -= cutStep;
                if (v.cutGain <= 0.0f) { v.active = false; break; }
            }
            const float g = v.gain * env * v.cutGain;

            for (int ch = 0; ch < nOut; ++ch)
                out.getWritePointer(ch)[i] += g * read(v, nSrc == 1 ? 0 : ch);

            v.pos += v.step;
            ++v.age;
        }
    }
}

} // namespace mu_tilate
