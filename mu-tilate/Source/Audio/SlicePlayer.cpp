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
        // At 88.2 kHz and above one hop's analysis and synthesis would be several large FFTs inside a single callback: the
        // library can spread that over the hop instead (at the price of one hop more latency).
        const bool split = sr >= 88200.0;
        if (shortWindow) s.configure(2, (int) (0.03 * sr), (int) (0.0075 * sr), split);
        else             s.presetDefault(2, (float) sr, split);
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
    for (auto& v : preVoices) v = Voice {};
}

bool SlicePlayer::isActive() const noexcept
{
    for (const auto& v : voices)
        if (v.active) return true;
    return preVoices[0].active || preVoices[1].active;
}

void SlicePlayer::cut() noexcept
{
    cutSlices();
    for (auto& pv : preVoices)
        if (pv.active && ! pv.fading) { pv.fading = true; pv.cutStep = cutStep; }
}

void SlicePlayer::triggerPre(const juce::AudioBuffer<float>* src, SliceRange range, const SliceParams& p) noexcept
{
    if (src == nullptr) return;
    range.start = juce::jlimit(0, src->getNumSamples(), range.start);
    range.end   = juce::jlimit(range.start, src->getNumSamples(), range.end);
    if (range.length() <= 0 || src->getNumChannels() <= 0) return;

    const double pitch = pitchRatio(p.own);
    Voice v;
    v.src = src;
    v.range = range;
    // Played backwards ending at the slice's first sample; for a slice that itself plays backwards, forwards ending at its last.
    v.step = (p.reverse ? 1.0 : -1.0) * (double) std::max(0.01f, p.rate) * pitch;
    v.pos = p.reverse ? (double) range.start : (double) (range.end - 1);
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
    mu_core::equalPowerPan(p.own.pan, v.panL, v.panR);   // equal power; a mono output ignores it
    // A free pre-fade voice, else the one furthest along (it is nearest its hit).
    Voice* slot = &preVoices[0];
    if (preVoices[0].active && preVoices[1].active) slot = preVoices[1].age > preVoices[0].age ? &preVoices[1] : &preVoices[0];
    else if (preVoices[0].active) slot = &preVoices[1];
    *slot = v;
}

void SlicePlayer::cutSlices() noexcept
{
    for (auto& v : voices)
        if (v.active && ! v.fading)
        {
            // The release: the amp fades out over the longer of the cut fade and the slice's amp release.
            v.fading = true;
            v.cutStep = std::min(cutStep, (float) (1.0 / std::max(1.0, (double) v.own.ampR * 0.001 * sr)));
            v.fltRelStep = std::min(cutStep, (float) (1.0 / std::max(1.0, (double) v.own.fltR * 0.001 * sr)));
        }
}

bool SlicePlayer::filterModulation(float& octaves, float& resAdd) const noexcept
{
    octaves = 0.0f; resAdd = 0.0f;
    const Voice* newest = nullptr;
    for (const auto& v : voices)
        if (v.active && ! v.swell && (newest == nullptr || v.age < newest->age)) newest = &v;
    if (newest == nullptr) return false;
    const auto& o = newest->own;
    // The envelope moves the cutoff up to five octaves, and fades out over its release once the slice is cut.
    const float env = mu_core::adsAtAge((double) newest->age, o.fltA, o.fltD, o.fltS, sr) * (1.0f - newest->fltRel);
    octaves = o.cutoffOct + o.filterEnvAmt * env * 5.0f;
    resAdd  = o.resOffset;
    return true;
}

void SlicePlayer::updateFilter(Voice& v) noexcept
{
    const auto& o = v.own;
    // The cutoff moves up to five octaves with the filter envelope, which fades to 0 over its release once the slice is cut.
    const float env = mu_core::adsAtAge((double) v.age, o.fltA, o.fltD, o.fltS, sr) * (1.0f - v.fltRel);
    v.svf.setTap(o.filterType == 1 ? mu_core::TptSvf::Tap::lowpass : (o.filterType == 2 ? mu_core::TptSvf::Tap::highpass : mu_core::TptSvf::Tap::bandpass));
    v.svf.setCoefficients(o.cutoffHz * (float) std::exp2((double) o.filterEnvAmt * (double) env * 5.0), o.resonance, sr);
}

// One frame through the slice's shaping. State-variable filter: Cytomic's linear trapezoidal (zero-delay) form.
void SlicePlayer::emit(Voice& v, juce::AudioBuffer<float>& out, int index, float* frame, int channels, float gain) noexcept
{
    if (v.own.filterType > 0)
    {
        if (v.age % kFilterUpdate == 0) updateFilter(v);
        for (int ch = 0; ch < channels; ++ch) frame[ch] = v.svf.process(frame[ch], ch);
    }
    const float g = gain * mu_core::adsAtAge((double) v.age, v.own.ampA, v.own.ampD, v.own.ampS, sr);
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
    const double pitch = pitchRatio(p.own);
    const float rate = std::max(0.01f, p.rate);
    Voice v;
    v.own = p.own;
    v.loop = p.own.loop;
    v.pitch = pitch;
    v.cutStep = cutStep;
    mu_core::equalPowerPan(p.own.pan, v.panL, v.panR);   // equal power; a mono output ignores it
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

float SlicePlayer::readAt(const Voice& v, double pos, int channel) noexcept
{
    const int nch = v.src->getNumChannels();
    const float* d = v.src->getReadPointer(juce::jmin(channel, nch - 1));

    // Catmull-Rom through the four samples around the position; neighbours outside the slice repeat the edge sample.
    const int   i0 = (int) std::floor(pos);
    const float f  = (float) (pos - (double) i0);
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
        for (int done = 0, len = 0; done < n && v.active; done += len)
        {
            len = std::min(maxBlock, n - done);
            // The stretcher is primed at the end of the first chunk, which must be where the hold ends: otherwise the dry
            // head would last as long as the host block happens to be.
            if (v.stretcher >= 0 && ! v.primed && v.holdSamples > v.age) len = std::min(len, v.holdSamples - v.age);
            if (v.stretcher >= 0) renderStretched(v, out, done, len);
            else                  renderNative(v, out, done, len);
        }
    }
    for (auto& pv : preVoices)
        if (pv.active) renderNative(pv, out, 0, n);
}

void SlicePlayer::renderNative(Voice& v, juce::AudioBuffer<float>& out, int offset, int n) noexcept
{
    const int nOut = out.getNumChannels();
    const int nSrc = v.src->getNumChannels();
    const int nc   = std::min(nOut, 2);

    for (int i = 0; i < n && v.active; ++i)
    {
        // A looping slice plays until it is cut, wrapping at its ends; any other leaves the slice and ends. The last couple of
        // milliseconds before the end cross-fade into the start's material, and the wrap lands after that stretch, so the loop
        // point is seamless instead of a jump from the last sample to the first.
        const double loopLen = (double) v.range.length();
        const double fadeSrc = v.loop ? std::min(0.25 * loopLen, (double) kLoopFadeMs * 0.001 * sr * std::fabs(v.step)) : 0.0;
        if (! inRange(v))
        {
            if (! v.loop) { v.active = false; break; }
            v.pos += v.step > 0.0 ? -(loopLen - fadeSrc) : (loopLen - fadeSrc);
            double rel = std::fmod(v.pos - (double) v.range.start, loopLen);   // a step longer than the slice wraps more than once
            if (rel < 0.0) rel += loopLen;
            rel = std::min(rel, std::nextafter(loopLen, 0.0));   // a tiny negative rel can round up to the length, which is out of range
            v.pos = (double) v.range.start + rel;
            if (! inRange(v)) { v.active = false; break; }
        }
        float blend = 0.0f;       // how much of the start's material (`other`) is in the output
        double other = 0.0;
        if (fadeSrc > 0.0)
        {
            const double into = v.step > 0.0 ? v.pos - ((double) v.range.end - fadeSrc) : ((double) v.range.start + fadeSrc) - v.pos;
            if (into > 0.0)
            {
                blend = (float) std::min(1.0, into / fadeSrc);
                other = v.step > 0.0 ? (double) v.range.start + into : (double) v.range.end - into;   // the wrap lands at end - fadeSrc, so the material continues from end
            }
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
        for (int ch = 0; ch < nc; ++ch)
        {
            frame[ch] = read(v, nSrc == 1 ? 0 : ch);
            if (blend > 0.0f) frame[ch] += blend * (readAt(v, other, nSrc == 1 ? 0 : ch) - frame[ch]);
        }
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
