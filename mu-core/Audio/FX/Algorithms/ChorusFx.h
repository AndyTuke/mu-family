#pragma once

#include "Audio/FX/Algorithms/ChorusCore.h"
#include "Audio/FX/Algorithms/FxUse.h"
#include "Audio/FX/Insert/InsertAlgorithmBase.h"
#include "Audio/FX/Send/EffectAlgorithmBase.h"

#include <atomic>

namespace mu_audio
{
// Chorus as an FX algorithm, for both hosts. The DSP is ChorusCore; this class only maps each host's parameters onto it.
//   Send (Effect slot, index 0): Rate, Depth, Voices and Spread knobs; centre 30 ms, swing 20 ms; wet-only into the return bus.
//   Insert (algo 17): the four generic knobs Rate, Depth, Delay and Mix; the voice count and spread are fixed (three voices,
//   spread 0.5: the first count that sounds like an ensemble, and the send's default spread).
class ChorusFx final : public EffectAlgorithmBase, public InsertAlgorithmBase
{
public:
    static constexpr FxUse kUse = FxUse::Both;

    // The hidden insert values (the four knobs have no room for them).
    static constexpr int   kInsertVoices = 3;
    static constexpr float kInsertSpread = 0.5f;

    const FXAlgorithmDef& getDef() const override { return FXAlgorithmRegistry::effectAlgorithms()[0]; }

    void prepareInner(double sampleRate, int /*blockSize*/) override { core.prepare(sampleRate); }

    void prepare(double sampleRate, int /*blockSize*/) override
    {
        core.prepare(sampleRate);
        mix.reset(sampleRate > 0.0 ? sampleRate : 44100.0, 0.02);
        reset();
    }

    void reset() override
    {
        core.reset();
        mix.setCurrentAndTargetValue(0.0f);
        started = false;
    }

    // Send side: values are written by the UI or host automation and read on the audio thread (relaxed atomics).
    void setParam(const juce::String& id, float value) override
    {
        if      (id == "rate")   rate   = value;
        else if (id == "depth")  depth  = value / 100.0f;
        else if (id == "voices") voices = value;
        else if (id == "spread") spread = value / 100.0f;
        else if (id == "mix")    sendMix = value / 100.0f;
    }

    void processInner(juce::dsp::AudioBlock<float>& block) override
    {
        if (! core.ready()) return;
        ChorusCore::Params q;
        q.rateHz   = rate.load(std::memory_order_relaxed);
        q.depth    = depth.load(std::memory_order_relaxed);
        q.centreMs = 30.0f;
        q.swingMs  = 20.0f;
        // Round, not truncate: a host-automated Voices value of 3.9 means 4 voices.
        q.voices   = juce::roundToInt(voices.load(std::memory_order_relaxed));
        q.spread   = spread.load(std::memory_order_relaxed);
        core.setParams(q);

        const float m = sendMix.load(std::memory_order_relaxed);
        const size_t n = block.getNumSamples(), nCh = block.getNumChannels();
        auto* dataL = nCh > 0 ? block.getChannelPointer(0) : nullptr;
        auto* dataR = nCh > 1 ? block.getChannelPointer(1) : nullptr;
        for (size_t i = 0; i < n; ++i)
        {
            const float inL = dataL ? dataL[i] : 0.0f, inR = dataR ? dataR[i] : inL;
            float wl, wr;
            core.tick(inL, inR, wl, wr);
            // Send mode is wet-only (the dry is already on the main bus); the blend is for a caller that clears it.
            if (dataL) dataL[i] = sendMode ? wl : inL * (1.0f - m) + wl * m;
            if (dataR) dataR[i] = sendMode ? wr : inR * (1.0f - m) + wr * m;
        }
    }

    // Insert side: four generic knobs (Rate 0.05..8 Hz, Depth 0..100 %, Delay 5..30 ms, Mix 0..100 %).
    void process(juce::AudioBuffer<float>& buf, int ns, int nCh, const VoiceParams& p, float& /*grOut*/) override
    {
        if (! core.ready() || nCh <= 0) return;
        ChorusCore::Params q;
        q.rateHz   = juce::jlimit(0.05f, 8.0f, insertSlot(p, 0));
        q.depth    = juce::jlimit(0.0f, 1.0f, insertSlot(p, 1) * 0.01f);
        q.centreMs = juce::jlimit(5.0f, 30.0f, insertSlot(p, 2));
        q.swingMs  = 0.4f * q.centreMs;   // at most 40 % of the centre delay either side
        q.voices   = kInsertVoices;
        q.spread   = kInsertSpread;
        core.setParams(q);

        const float mixAmt = juce::jlimit(0.0f, 1.0f, insertSlot(p, 3) * 0.01f);
        if (! started) { mix.setCurrentAndTargetValue(mixAmt); started = true; }
        mix.setTargetValue(mixAmt);

        const int nChState = juce::jmin(nCh, 2);
        auto chans = buf.getArrayOfWritePointers();
        for (int i = 0; i < ns; ++i)
        {
            const float m = mix.getNextValue();
            const float inL = chans[0][i], inR = nChState > 1 ? chans[1][i] : inL;
            float wl, wr;
            core.tick(inL, inR, wl, wr);
            chans[0][i] = inL * (1.0f - m) + wl * m;
            if (nChState > 1) chans[1][i] = inR * (1.0f - m) + wr * m;
        }
    }

private:
    ChorusCore core;
    std::atomic<float> rate { 1.0f }, depth { 0.5f }, voices { 2.0f }, spread { 0.5f }, sendMix { 0.5f };
    bool started = false;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mix;
};
}  // namespace mu_audio
