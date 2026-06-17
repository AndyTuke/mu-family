#include "ReverbSlot.h"
#pragma warning(push)
#pragma warning(disable: 4244)  // signalsmith-dsp/filters.h: double-to-float narrowing in BiquadStatic
#include <signalsmith-basics/reverb.h>
#pragma warning(pop)
#include <cmath>
#include <cstring>   // std::strcmp (alloc-free setParam)

// Pimpl: keeps Signalsmith headers (and their transitive windows.h) out of ReverbSlot.h.
struct ReverbSlot::ReverbImpl
{
    signalsmith::basics::ReverbFloat reverb;

    // snapshot of computed reverb parameters. Message thread (updateReverb)
    // writes the snapshot under pendingLock; audio thread (applyPendingReverbParams)
    // try-locks at block boundary and copies into reverb.* before reverb.process().
    // Without this, setParam/updateReverb wrote reverb.roomMs/rt20/etc. directly
    // while reverb.process() was running — undefined behaviour per typical embedded
    // reverb assumptions of single-threaded access to public state.
    struct PendingParams
    {
        double roomMs       = 0.0;
        double rt20         = 0.0;
        double early        = 0.0;
        double highDampRate = 0.0;
        double lowDampRate  = 0.0;
        double detune       = 0.0;
        double lowCutHz     = 0.0;
        double highCutHz    = 0.0;
    };
    PendingParams pending;
    std::atomic<bool> pendingDirty { false };
    juce::SpinLock    pendingLock;
};

ReverbSlot::ReverbSlot()
    : impl(std::make_unique<ReverbImpl>())
{
    preDelayBufL.assign(MaxPreDelaySamples, 0.0f);
    preDelayBufR.assign(MaxPreDelaySamples, 0.0f);
    applyAlgorithmPreset();
}

ReverbSlot::~ReverbSlot() = default;

void ReverbSlot::prepare(double sampleRate, int blockSize)
{
    sr = sampleRate;

    impl->reverb.configure(sampleRate, (size_t)blockSize, 2);

    const auto n = (size_t)blockSize;
    wetL.assign(n, 0.0f);
    wetR.assign(n, 0.0f);

    preDelayBufL.assign(MaxPreDelaySamples, 0.0f);
    preDelayBufR.assign(MaxPreDelaySamples, 0.0f);
    preDelayWrite = 0;

    updateReverb();
}

void ReverbSlot::runPreDelay(const juce::AudioBuffer<float>& src, int numSamples)
{
    const int numCh = src.getNumChannels();
    const auto* srcL = (numCh > 0) ? src.getReadPointer(0) : nullptr;
    const auto* srcR = (numCh > 1) ? src.getReadPointer(1) : srcL;

    const float preDelayMs = preDelay.load(std::memory_order_relaxed);
    const int delaySamples = juce::jlimit(0, MaxPreDelaySamples - 1,
        static_cast<int>(preDelayMs * sr / 1000.0));

    for (int i = 0; i < numSamples; ++i)
    {
        preDelayBufL[preDelayWrite] = srcL ? srcL[i] : 0.0f;
        preDelayBufR[preDelayWrite] = srcR ? srcR[i] : 0.0f;

        const int readPos = (preDelayWrite - delaySamples + MaxPreDelaySamples) % MaxPreDelaySamples;
        wetL[i] = preDelayBufL[readPos];
        wetR[i] = preDelayBufR[readPos];

        preDelayWrite = (preDelayWrite + 1) % MaxPreDelaySamples;
    }
}

void ReverbSlot::process(juce::AudioBuffer<float>& buffer)
{
    // FXSlotBase contract — mu-clid uses send/return architecture, so the in-place
    // process() form just forwards to processReturn(). Retained for v3 plugin hosting.
    processReturn(buffer);
}

void ReverbSlot::processReturn(juce::AudioBuffer<float>& buffer)
{
    if (!enabled.load(std::memory_order_relaxed)) return;

    // apply any pending Signalsmith reverb param changes BEFORE process().
    // Single point where the reverb's public fields get mutated — message thread
    // never touches them directly.
    applyPendingReverbParams();

    const int numCh      = buffer.getNumChannels();
    const int numSamples = juce::jmin(buffer.getNumSamples(), (int)wetL.size());

    runPreDelay(buffer, numSamples);

    const float dirtAmt = dirt.load(std::memory_order_relaxed);
    if (dirtAmt > 0.001f)
    {
        const float gain = 1.0f + dirtAmt * 4.0f;
        for (int i = 0; i < numSamples; ++i)
        {
            wetL[i] = std::tanh(wetL[i] * gain) / gain;
            wetR[i] = std::tanh(wetR[i] * gain) / gain;
        }
    }

    float* ins[2]  = { wetL.data(), wetR.data() };
    float* outs[2] = { wetL.data(), wetR.data() };
    impl->reverb.process(ins, outs, (size_t)numSamples);

    // Overwrite buffer with wet-only output (dry send is already in the main mix).
    const float levelSnap = level.load(std::memory_order_relaxed);
    for (int ch = 0; ch < numCh; ++ch)
    {
        auto* out = buffer.getWritePointer(ch);
        const auto* wet = (ch == 0) ? wetL.data() : wetR.data();
        for (int i = 0; i < numSamples; ++i)
            out[i] = wet[i] * levelSnap;
    }
}

void ReverbSlot::setAlgorithm(int index)
{
    algorithmIndex = juce::jlimit(0, 3, index);
    applyAlgorithmPreset();
    updateReverb();
}

void ReverbSlot::setParam(const char* id, float value)
{
    // strcmp on the raw id — no juce::String built, so this is safe to call per-block
    // (RumbleEngine) and on the audio-thread automation path (syncFxSlotParam).
    auto is = [id](const char* s) { return std::strcmp(id, s) == 0; };
    if      (is("size"))      size      = value;
    else if (is("predelay"))  preDelay.store(value, std::memory_order_relaxed);
    else if (is("diffusion")) diffusion = value;
    else if (is("damp"))      damp      = value;
    else if (is("mod"))       mod       = value;
    else if (is("dirt"))      dirt.store(value, std::memory_order_relaxed);
    updateReverb();
}

void ReverbSlot::applyAlgorithmPreset()
{
    // preDelay is atomic (read on the audio thread); use store for the assignment.
    auto setPreDelay = [this](float v) { preDelay.store(v, std::memory_order_relaxed); };
    switch (static_cast<Algorithm>(algorithmIndex))
    {
        case Algorithm::Room:
            size = 0.25f; rt20 = 0.6f;  damp = 0.6f; diffusion = 0.6f; setPreDelay(5.0f);  mod = 0.10f;
            break;
        case Algorithm::Hall:
            size = 0.75f; rt20 = 2.5f;  damp = 0.3f; diffusion = 0.8f; setPreDelay(25.0f); mod = 0.15f;
            break;
        case Algorithm::Plate:
            size = 0.45f; rt20 = 1.8f;  damp = 0.5f; diffusion = 0.9f; setPreDelay(10.0f); mod = 0.30f;
            break;
        case Algorithm::Spring:
            size = 0.15f; rt20 = 0.35f; damp = 0.8f; diffusion = 0.3f; setPreDelay(2.0f);  mod = 0.60f;
            break;
    }
}

void ReverbSlot::updateReverb()
{
    // compute the snapshot here (message thread) — the audio thread copies it
    // into impl->reverb at the next processReturn via applyPendingReverbParams().
    // Was: direct writes to impl->reverb.<field> from the message thread while the
    // audio thread was inside reverb.process() — undefined per typical embedded
    // reverb single-threaded-access assumptions.
    ReverbImpl::PendingParams p;
    p.roomMs       = 10.0 + (double)size * 190.0;     // size [0,1] → 10..200 ms
    p.rt20         = (double)rt20;
    p.early        = (double)diffusion * 2.5;          // diffusion → early reflections
    p.highDampRate = 1.0 + (double)damp * 8.0;
    p.lowDampRate  = 1.0 + (double)damp * 1.5;
    p.detune       = (double)mod * 30.0;
    switch (static_cast<Algorithm>(algorithmIndex))
    {
        case Algorithm::Room:   p.lowCutHz = 100.0; p.highCutHz = 8000.0;  break;
        case Algorithm::Hall:   p.lowCutHz =  60.0; p.highCutHz = 12000.0; break;
        case Algorithm::Plate:  p.lowCutHz = 150.0; p.highCutHz = 10000.0; break;
        case Algorithm::Spring: p.lowCutHz = 200.0; p.highCutHz = 5500.0;  break;
    }

    {
        juce::SpinLock::ScopedLockType lock(impl->pendingLock);
        impl->pending = p;
    }
    impl->pendingDirty.store(true, std::memory_order_release);
}

void ReverbSlot::applyPendingReverbParams()
{
    if (! impl->pendingDirty.load(std::memory_order_acquire))
        return;

    const juce::SpinLock::ScopedTryLockType lock(impl->pendingLock);
    if (! lock.isLocked())
        return;  // Message thread mid-write — leave dirty set; retry next block.

    auto& r = impl->reverb;
    r.dry          = 0.0;
    r.wet          = 1.0;
    r.roomMs       = impl->pending.roomMs;
    r.rt20         = impl->pending.rt20;
    r.early        = impl->pending.early;
    r.highDampRate = impl->pending.highDampRate;
    r.lowDampRate  = impl->pending.lowDampRate;
    r.detune       = impl->pending.detune;
    r.lowCutHz     = impl->pending.lowCutHz;
    r.highCutHz    = impl->pending.highCutHz;

    impl->pendingDirty.store(false, std::memory_order_release);
}
