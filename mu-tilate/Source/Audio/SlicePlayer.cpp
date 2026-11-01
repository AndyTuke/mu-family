#include "Audio/SlicePlayer.h"

#include "signalsmith-stretch.h"   // ThirdParty/signalsmith-stretch (MIT)

#include <algorithm>
#include <cmath>

namespace mu_tilate
{

using Stretch = signalsmith::stretch::SignalsmithStretch<float>;

// The two stretchers (the sounding slice and one being cut off), built once in prepare. Seeded constructors only:
// the default one calls std::random_device. Never setFreqMap or a pitch map here: that path can allocate.
struct SlicePlayer::StretchState
{
    std::array<Stretch, 2> st { Stretch(1L), Stretch(2L) };
    std::array<std::vector<float>, 2> in;    // per channel: seek pre-roll or one block's input
    std::array<std::vector<float>, 2> out;   // per channel: one block's stretched output
};

SlicePlayer::SlicePlayer() = default;
SlicePlayer::~SlicePlayer() = default;

void SlicePlayer::prepare(double sampleRate, int maxBlockSize, bool shortWindow)
{
    sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    maxBlock = std::max(16, maxBlockSize);
    const double cutSamples = std::max(1.0, (double) kCutFadeMs * 0.001 * sr);
    cutStep = (float) (1.0 / cutSamples);

    // Both stretchers are stereo and configured once; the analysis window is the per-layer choice.
    stretch = std::make_unique<StretchState>();
    for (auto& s : stretch->st)
    {
        if (shortWindow) s.configure(2, (int) (0.03 * sr), (int) (0.0075 * sr));
        else             s.presetDefault(2, (float) sr);
    }

    // Scratch: the largest seek (at the largest ratio) and the largest block's input at the smallest ratio.
    const auto& s0 = stretch->st[0];
    const int seekMax  = s0.inputLatency() + (int) std::ceil((double) s0.outputLatency() / (double) kMinTimeRatio) + 16;
    const int blockMax = (int) std::ceil((double) maxBlock / (double) kMinTimeRatio) + 16;
    for (int c = 0; c < 2; ++c)
    {
        stretch->in[(size_t) c].assign((size_t) std::max(seekMax, blockMax), 0.0f);
        stretch->out[(size_t) c].assign((size_t) maxBlock, 0.0f);
    }
    reset();
}

void SlicePlayer::reset() noexcept
{
    for (auto& v : voices) v = Voice {};
    preVoice = Voice {};
}

bool SlicePlayer::isActive() const noexcept
{
    for (const auto& v : voices)
        if (v.active) return true;
    return preVoice.active;
}

void SlicePlayer::cut() noexcept
{
    cutSlices();
    if (preVoice.active && ! preVoice.fading) { preVoice.fading = true; preVoice.cutStep = cutStep; }
}

void SlicePlayer::triggerPre(const juce::AudioBuffer<float>* src, SliceRange range, const SliceParams& p) noexcept
{
    if (src == nullptr) return;
    range.start = juce::jlimit(0, src->getNumSamples(), range.start);
    range.end   = juce::jlimit(range.start, src->getNumSamples(), range.end);
    if (range.length() <= 0 || src->getNumChannels() <= 0) return;

    const double pitch = std::exp2((double) (juce::jlimit(-24.0f, 24.0f, p.own.pitchSemi) + juce::jlimit(-100.0f, 100.0f, p.own.fineCents) / 100.0f) / 12.0);
    Voice v;
    v.src = src;
    v.range = range;
    v.step = -(double) std::max(0.01f, p.rate) * pitch;
    v.pos = (double) (range.end - 1);
    v.gain = p.gain;
    v.cutStep = cutStep;
    v.swell = true;
    v.fadeInLen = std::max(1, (int) std::lround((double) range.length() / std::fabs(v.step)));   // the swell lasts the whole voice
    v.fadeOutLen = 1;
    v.active = true;

    // Tone: negative darkens (low-pass down to 250 Hz), positive thins (high-pass up to 2 kHz).
    const float tone = juce::jlimit(-1.0f, 1.0f, p.own.preTone);
    if (tone < -0.01f)     { v.own.filterType = 1; v.own.cutoffHz = 20000.0f * std::pow(0.0125f, -tone); }
    else if (tone > 0.01f) { v.own.filterType = 2; v.own.cutoffHz = 20.0f * std::pow(100.0f, tone); }
    const float pa = (juce::jlimit(-1.0f, 1.0f, p.own.pan) + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
    v.panL = std::cos(pa) * 1.41421356f;
    v.panR = std::sin(pa) * 1.41421356f;
    preVoice = v;
}

void SlicePlayer::cutSlices() noexcept
{
    for (auto& v : voices)
        if (v.active && ! v.fading)
        {
            // The release: the amp fades out over the longer of the cut fade and the slice's amp release.
            v.fading = true;
            v.cutStep = std::min(cutStep, (float) (1.0 / std::max(1.0, (double) v.own.ampR * 0.001 * sr)));
            v.fltRelStep = (float) (1.0 / std::max(1.0, (double) v.own.fltR * 0.001 * sr));
        }
}

// ADSR value at `ageSamples` for a gate that stays on: linear attack and decay to the sustain level.
float SlicePlayer::envelope(double ageSamples, float aMs, float dMs, float sustain, double sampleRate) noexcept
{
    const double a = (double) aMs * 0.001 * sampleRate, d = (double) dMs * 0.001 * sampleRate;
    if (ageSamples < a) return (float) (ageSamples / a);
    if (d <= 0.0) return sustain;
    return (float) (1.0 - (1.0 - (double) sustain) * std::min(1.0, (ageSamples - a) / d));
}

void SlicePlayer::updateFilter(Voice& v) noexcept
{
    const auto& o = v.own;
    // The cutoff moves up to five octaves with the filter envelope, which fades to 0 over its release once the slice is cut.
    const float env = envelope((double) v.age, o.fltA, o.fltD, o.fltS, sr) * (1.0f - v.fltRel);
    const double fc = std::clamp((double) o.cutoffHz * std::exp2((double) o.filterEnvAmt * (double) env * 5.0), 20.0, 0.45 * sr);
    const float g = (float) std::tan(juce::MathConstants<double>::pi * fc / sr);
    v.fk  = 2.0f * (1.0f - 0.98f * juce::jlimit(0.0f, 1.0f, o.resonance));
    v.fa1 = 1.0f / (1.0f + g * (g + v.fk));
    v.fa2 = g * v.fa1;
    v.fa3 = g * v.fa2;
}

// One frame through the slice's shaping. State-variable filter: Cytomic's linear trapezoidal (zero-delay) form.
void SlicePlayer::emit(Voice& v, juce::AudioBuffer<float>& out, int index, float* frame, int channels, float gain) noexcept
{
    if (v.own.filterType > 0)
    {
        if (v.age % kFilterUpdate == 0) updateFilter(v);
        for (int ch = 0; ch < channels; ++ch)
        {
            const float in = frame[ch];
            const float v3 = in - v.ic2[ch];
            const float v1 = v.fa1 * v.ic1[ch] + v.fa2 * v3;
            const float v2 = v.ic2[ch] + v.fa2 * v.ic1[ch] + v.fa3 * v3;
            v.ic1[ch] = 2.0f * v1 - v.ic1[ch];
            v.ic2[ch] = 2.0f * v2 - v.ic2[ch];
            frame[ch] = v.own.filterType == 1 ? v2 : (v.own.filterType == 2 ? in - v.fk * v1 - v2 : v1);
        }
    }
    const float g = gain * envelope((double) v.age, v.own.ampA, v.own.ampD, v.own.ampS, sr);
    for (int ch = 0; ch < channels; ++ch)
        out.getWritePointer(ch)[index] += g * frame[ch] * (channels == 2 ? (ch == 0 ? v.panL : v.panR) : 1.0f);
    if (v.fading) v.fltRel = std::min(1.0f, v.fltRel + v.fltRelStep);
}

void SlicePlayer::trigger(const juce::AudioBuffer<float>* src, SliceRange range, const SliceParams& p) noexcept
{
    // The slice is clamped to the loop; an empty or missing slice is a gap, which still cuts what was sounding.
    cutSlices();
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

    // The pitch settings as a rate: semitones and cents. A stretched slice takes them as the stretcher's transposition.
    const double pitch = std::exp2((double) (juce::jlimit(-24.0f, 24.0f, p.own.pitchSemi) + juce::jlimit(-100.0f, 100.0f, p.own.fineCents) / 100.0f) / 12.0);
    const float rate = std::max(0.01f, p.rate);
    Voice v;
    v.own = p.own;
    v.loop = p.own.loop;
    v.pitch = pitch;
    v.cutStep = cutStep;
    // Equal-power pan; a mono output ignores it.
    const float pa = (juce::jlimit(-1.0f, 1.0f, p.own.pan) + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
    v.panL = std::cos(pa) * 1.41421356f;
    v.panR = std::sin(pa) * 1.41421356f;
    v.src        = src;
    v.range      = range;
    v.step       = (p.reverse ? -(double) rate : (double) rate) * pitch;
    v.pos        = p.reverse ? (double) (range.end - 1) : (double) range.start;
    v.gain       = p.gain;
    v.fadeInLen  = std::max(1, (int) std::lround((double) p.fadeInMs  * 0.001 * sr));
    v.fadeOutLen = std::max(1, (int) std::lround((double) p.fadeOutMs * 0.001 * sr));
    v.active     = true;

    // A stretched slice: rate 1, forwards, a ratio that is not 1, and a stretcher free to take (one is always free
    // unless two voices are already fading; then the slice plays unstretched rather than steal one mid-fade).
    const float ratio = juce::jlimit(kMinTimeRatio, kMaxTimeRatio, p.timeRatio);
    if (stretch != nullptr && ! p.reverse && std::fabs(rate - 1.0f) < 1.0e-3f && std::fabs(ratio - 1.0f) > 1.0e-3f)
    {
        bool used[2] = { false, false };
        for (const auto& o : voices)
            if (o.active && &o != slot && o.stretcher >= 0) used[o.stretcher] = true;
        const int idx = ! used[0] ? 0 : (! used[1] ? 1 : -1);
        if (idx >= 0)
        {
            stretch->st[(size_t) idx].setTransposeFactor((float) pitch);
            v.stretcher   = idx;
            v.ratio       = (double) ratio;
            v.total       = std::max(1, (int) std::lround((double) range.length() * v.ratio));
            v.holdSamples = (int) std::lround((double) std::max(0.0f, p.holdMs) * juce::jlimit(0.0f, 1.0f, p.strength) * 0.001 * sr);
            v.xfSamples   = v.holdSamples > 0 ? std::max(1, (int) std::lround((double) kStretchFadeMs * 0.001 * sr)) : 0;
            *slot = v;
            // No dry head: the stretch must sound from the first sample, so it is primed here (a Strength of 0 pays
            // this cost at the trigger; with a head the priming is deferred to the end of the first block).
            if (v.holdSamples == 0) primeStretch(*slot, 0);
            return;
        }
    }
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

void SlicePlayer::fetch(const Voice& v, int ch, int from, int count, float* dest) noexcept
{
    // Zeros outside the slice: the stretcher reads past the slice end by about its latency, and the next slice's
    // audio there would be heard as pre-echo in the tail.
    const float* d = v.src->getReadPointer(juce::jmin(ch, v.src->getNumChannels() - 1));
    for (int i = 0; i < count; ++i)
    {
        const int s = from + i;
        dest[i] = (s >= v.range.start && s < v.range.end) ? d[s] : 0.0f;
    }
}

void SlicePlayer::primeStretch(Voice& v, int startAge) noexcept
{
    auto& st = stretch->st[(size_t) v.stretcher];
    v.startAge = startAge;

    // The stretch at output time t is the source at t / ratio, so at startAge it is at startAge / ratio. The seek takes
    // the source from there, long enough for the stretcher's own latency at this rate.
    const int from = v.range.start + (int) std::floor((double) startAge / v.ratio);
    const int n    = std::min(st.outputSeekLength((float) (1.0 / v.ratio)), (int) stretch->in[0].size());
    for (int c = 0; c < 2; ++c) fetch(v, c, from, n, stretch->in[(size_t) c].data());
    const float* inCh[2] = { stretch->in[0].data(), stretch->in[1].data() };
    st.outputSeek(inCh, n);

    v.inPos  = from + n;
    v.inAcc  = (double) v.inPos;
    v.primed = true;
}

void SlicePlayer::render(juce::AudioBuffer<float>& out, int n) noexcept
{
    if (out.getNumChannels() <= 0) return;

    // Each voice runs the block in chunks no larger than the prepared size (the stretch scratch is sized by it).
    for (auto& v : voices)
    {
        for (int done = 0; done < n && v.active; done += maxBlock)
        {
            const int len = std::min(maxBlock, n - done);
            if (v.stretcher >= 0) renderStretched(v, out, done, len);
            else                  renderNative(v, out, done, len);
        }
    }
    if (preVoice.active) renderNative(preVoice, out, 0, n);
}

void SlicePlayer::renderNative(Voice& v, juce::AudioBuffer<float>& out, int offset, int n) noexcept
{
    const int nOut = out.getNumChannels();
    const int nSrc = v.src->getNumChannels();
    const int nc   = std::min(nOut, 2);

    for (int i = 0; i < n && v.active; ++i)
    {
        // A looping slice wraps at its ends and plays until it is cut; any other leaves the slice and ends.
        if (! inRange(v))
        {
            if (! v.loop) { v.active = false; break; }
            const double len = (double) v.range.length();
            v.pos += v.step > 0.0 ? -len : len;
            if (! inRange(v)) { v.active = false; break; }
        }

        // Fade-in from the start, fade-out over the last samples of the slice (in playback order).
        float env = std::min(1.0f, (float) (v.age + 1) / (float) v.fadeInLen);
        if (v.swell) env *= env;
        if (! v.loop)
        {
            const double remaining = v.step > 0.0 ? (double) v.range.end - v.pos : v.pos - (double) v.range.start + 1.0;
            env *= std::min(1.0f, (float) ((int) (remaining / std::fabs(v.step)) + 1) / (float) v.fadeOutLen);
        }
        if (v.fading)
        {
            v.cutGain -= v.cutStep;
            if (v.cutGain <= 0.0f) { v.active = false; break; }
        }

        float frame[2] = { 0.0f, 0.0f };
        for (int ch = 0; ch < nc; ++ch) frame[ch] = read(v, nSrc == 1 ? 0 : ch);
        emit(v, out, offset + i, frame, nc, v.gain * env * v.cutGain);
        for (int ch = 2; ch < nOut; ++ch)   // outputs beyond a stereo pair get the plain source
            out.getWritePointer(ch)[offset + i] += v.gain * env * v.cutGain * read(v, nSrc == 1 ? 0 : ch);

        v.pos += v.step;
        ++v.age;
    }
}

void SlicePlayer::renderStretched(Voice& v, juce::AudioBuffer<float>& out, int offset, int n) noexcept
{
    const int nOut = out.getNumChannels();
    auto& st = stretch->st[(size_t) v.stretcher];
    const int nn = std::min(n, v.total - v.age);   // the voice ends at its stretched length
    if (nn <= 0) { v.active = false; return; }

    // Phase 1: the stretch's share of this chunk: from startAge on (all of it once primed and past the hold, none before).
    const int firstStr = v.primed ? juce::jlimit(0, nn, v.startAge - v.age) : nn;
    const int nStr     = nn - firstStr;
    if (nStr > 0)
    {
        // Source samples to feed for nStr output samples: nStr / ratio, error-diffused so the position never drifts.
        v.inAcc += (double) nStr / v.ratio;
        const int nIn = std::max(0, std::min((int) std::lround(v.inAcc) - v.inPos, (int) stretch->in[0].size()));
        for (int c = 0; c < 2; ++c) fetch(v, c, v.inPos, nIn, stretch->in[(size_t) c].data());
        const float* inCh[2] = { stretch->in[0].data(), stretch->in[1].data() };
        float* outCh[2]      = { stretch->out[0].data(), stretch->out[1].data() };
        st.process(inCh, nIn, outCh, nStr);
        v.inPos += nIn;
    }

    // Phase 2: mix the dry head and the stretch, then the slice envelope and the cut fade.
    const int nSrc = v.src->getNumChannels();
    const int chunkStartAge = v.age;
    for (int i = 0; i < nn; ++i)
    {
        const int age = chunkStartAge + i;
        float dryW = 1.0f;
        if (v.primed && age >= v.startAge)
            dryW = (v.xfSamples > 0 && age < v.startAge + v.xfSamples) ? 1.0f - (float) (age - v.startAge) / (float) v.xfSamples : 0.0f;

        float env = std::min(1.0f, (float) (age + 1) / (float) v.fadeInLen);
        env      *= std::min(1.0f, (float) (v.total - age) / (float) v.fadeOutLen);
        if (v.fading)
        {
            v.cutGain -= v.cutStep;
            if (v.cutGain <= 0.0f) { v.active = false; break; }
        }

        // The dry head reads the source at the pitch rate (linear interpolation), so it matches the transposed stretch.
        const double headPos = (double) v.range.start + (double) age * v.pitch;
        const int h0 = (int) std::floor(headPos);
        const float hf = (float) (headPos - (double) h0);
        float frame[2] = { 0.0f, 0.0f };
        const int nc = std::min(nOut, 2);
        for (int ch = 0; ch < nc; ++ch)
        {
            const float* d = v.src->getReadPointer(nSrc == 1 ? 0 : juce::jmin(ch, nSrc - 1));
            const float dry = (dryW > 0.0f && h0 >= v.range.start && h0 + 1 < v.range.end) ? d[h0] + hf * (d[h0 + 1] - d[h0]) : 0.0f;
            const float str = i >= firstStr ? stretch->out[(size_t) juce::jmin(ch, 1)][(size_t) (i - firstStr)] : 0.0f;
            frame[ch] = dryW * dry + (1.0f - dryW) * str;
        }
        v.age = age;   // emit() reads the voice's age for the envelopes
        emit(v, out, offset + i, frame, nc, v.gain * env * v.cutGain);
        for (int ch = 2; ch < nOut; ++ch)
            out.getWritePointer(ch)[offset + i] += v.gain * env * v.cutGain * (i >= firstStr ? stretch->out[1][(size_t) (i - firstStr)] : 0.0f);
    }
    v.age = chunkStartAge;
    v.age += nn;
    if (v.age >= v.total) v.active = false;

    // Phase 3: with a dry head, the stretcher is primed at the end of the first chunk, when the head is already
    // sounding, so the seek's cost is hidden behind it. The stretch is heard from the end of the hold, or from the
    // start of the next chunk if that is later.
    if (v.active && ! v.primed)
        primeStretch(v, std::max(v.holdSamples, v.age));
}

} // namespace mu_tilate
