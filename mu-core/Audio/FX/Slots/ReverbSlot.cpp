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

    // Snapshot of the computed reverb parameters: any writer thread (updateReverb) stores each field
    // relaxed, then raises the dirty flag; the audio thread (applyPendingReverbParams) clears the flag
    // first, then copies into reverb.* before reverb.process(). No lock on either side, so a writer on
    // the audio thread (host automation) can never block behind another. A write that lands mid-copy
    // re-raises the flag, so the next block picks the newer values up; each field is valid on its own.
    struct PendingParams
    {
        std::atomic<double> roomMs       { 0.0 };
        std::atomic<double> rt20         { 0.0 };
        std::atomic<double> early        { 0.0 };
        std::atomic<double> highDampRate { 0.0 };
        std::atomic<double> lowDampRate  { 0.0 };
        std::atomic<double> detune       { 0.0 };
        std::atomic<double> lowCutHz     { 0.0 };
        std::atomic<double> highCutHz    { 0.0 };
    };
    PendingParams pending;
    std::atomic<bool> pendingDirty { false };
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

void ReverbSlot::reset()
{
    impl->reverb.reset();
    std::fill(preDelayBufL.begin(), preDelayBufL.end(), 0.0f);
    std::fill(preDelayBufR.begin(), preDelayBufR.end(), 0.0f);
    preDelayWrite = 0;
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
    // A disabled slot returns silence: the buffer arrives holding the dry send, which is already in
    // the main bus and must not be added back through the return.
    if (!enabled.load(std::memory_order_relaxed)) { buffer.clear(); return; }

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
    else if (is("decay"))     decay     = juce::jlimit(0.05f, 20.0f, value);   // RT60 seconds
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
    switch (static_cast<Algorithm>(algorithmIndex.load(std::memory_order_relaxed)))
    {
        case Algorithm::Room:
            size = 0.25f; decay = 0.6f;  damp = 0.6f; diffusion = 0.6f; setPreDelay(5.0f);  mod = 0.10f;
            break;
        case Algorithm::Hall:
            size = 0.75f; decay = 2.5f;  damp = 0.3f; diffusion = 0.8f; setPreDelay(25.0f); mod = 0.15f;
            break;
        case Algorithm::Plate:
            size = 0.45f; decay = 1.8f;  damp = 0.5f; diffusion = 0.9f; setPreDelay(10.0f); mod = 0.30f;
            break;
        case Algorithm::Spring:
            size = 0.15f; decay = 0.35f; damp = 0.8f; diffusion = 0.3f; setPreDelay(2.0f);  mod = 0.60f;
            break;
    }
}

void ReverbSlot::updateReverb()
{
    // Compute the snapshot here (any writer thread); the audio thread copies it into impl->reverb at
    // the next processReturn via applyPendingReverbParams(), so the library's public fields are only
    // ever touched by the thread that runs reverb.process().
    constexpr auto rx = std::memory_order_relaxed;
    auto& p = impl->pending;
    p.roomMs.store(10.0 + (double) size.load(rx) * 190.0, rx);   // size [0,1] -> 10..200 ms
    p.rt20.store((double) decay.load(rx) / 3.0, rx);              // decay is RT60 (to -60 dB); the library's rt20 is the time to -20 dB
    p.early.store((double) diffusion.load(rx) * 2.5, rx);         // diffusion -> early reflections
    p.highDampRate.store(1.0 + (double) damp.load(rx) * 8.0, rx);
    p.lowDampRate.store(1.0 + (double) damp.load(rx) * 1.5, rx);
    p.detune.store((double) mod.load(rx) * 30.0, rx);
    switch (static_cast<Algorithm>(algorithmIndex.load(rx)))
    {
        case Algorithm::Room:   p.lowCutHz.store(100.0, rx); p.highCutHz.store(8000.0, rx);  break;
        case Algorithm::Hall:   p.lowCutHz.store( 60.0, rx); p.highCutHz.store(12000.0, rx); break;
        case Algorithm::Plate:  p.lowCutHz.store(150.0, rx); p.highCutHz.store(10000.0, rx); break;
        case Algorithm::Spring: p.lowCutHz.store(200.0, rx); p.highCutHz.store(5500.0, rx);  break;
    }
    impl->pendingDirty.store(true, std::memory_order_release);
}

void ReverbSlot::applyPendingReverbParams()
{
    // Clear the flag before reading: a writer that lands during the copy raises it again.
    if (! impl->pendingDirty.exchange(false, std::memory_order_acquire))
        return;

    constexpr auto rx = std::memory_order_relaxed;
    auto& r = impl->reverb;
    const auto& p = impl->pending;
    r.dry          = 0.0;
    r.wet          = 1.0;
    r.roomMs       = p.roomMs.load(rx);
    r.rt20         = p.rt20.load(rx);
    r.early        = p.early.load(rx);
    r.highDampRate = p.highDampRate.load(rx);
    r.lowDampRate  = p.lowDampRate.load(rx);
    r.detune       = p.detune.load(rx);
    r.lowCutHz     = p.lowCutHz.load(rx);
    r.highCutHz    = p.highCutHz.load(rx);
}
