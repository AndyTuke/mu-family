#pragma once

#include "Audio/FX/Algorithms/FlangerCore.h"
#include "Audio/FX/Algorithms/FxUse.h"
#include "Audio/FX/Algorithms/InsertAlgorithmBase.h"
#include "Audio/FX/Algorithms/EffectAlgorithmBase.h"

#include <atomic>

namespace mu_audio
{
// Flanger as an FX algorithm, for both hosts. The DSP is FlangerCore; this class only maps each host's parameters onto it.
//   Send (Effect slot, index 1): Rate, Depth, Feedback and Mix; both channels share one LFO phase; wet-only into the return bus.
//   Insert (algo 18): the four generic knobs Rate, Depth, Feedback and Mix; the right channel's LFO is fixed a quarter turn
//   behind the left (a stereo offset the four knobs have no room for).
class FlangerFx final : public EffectAlgorithmBase, public InsertAlgorithmBase
{
public:
    static constexpr FxUse kUse = FxUse::Both;

    // The hidden insert value.
    static constexpr float kInsertStereoOffset = 0.25f;

    const FXAlgorithmDef& getDef() const override { return FXAlgorithmRegistry::effectAlgorithms()[1]; }

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
        if      (id == "rate")     rate     = value;
        else if (id == "depth")    depth    = value / 100.0f;
        else if (id == "feedback") feedback = (value / 100.0f) * 0.95f;   // cap to prevent runaway
        else if (id == "mix")      sendMix  = value / 100.0f;
    }

    void processInner(juce::dsp::AudioBlock<float>& block) override
    {
        if (! core.ready()) return;
        FlangerCore::Params q;
        q.rateHz   = rate.load(std::memory_order_relaxed);
        q.depth    = depth.load(std::memory_order_relaxed);
        q.feedback = feedback.load(std::memory_order_relaxed);
        q.stereoOffset = 0.0f;
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

    // Insert side: four generic knobs (Rate 0.05..5 Hz, Depth 0..100 %, Feedback -95..95 %, Mix 0..100 %).
    void process(juce::AudioBuffer<float>& buf, int ns, int nCh, const VoiceParams& p, float& /*grOut*/) override
    {
        if (! core.ready() || nCh <= 0) return;
        FlangerCore::Params q;
        q.rateHz   = juce::jlimit(0.05f, 5.0f, insertSlot(p, 0));
        // Sync: the rate is one sweep per 1/64 .. 64 sixteenth notes of the tempo (up to 20 Hz at the fast end).
        if (p.insertSync)
            q.rateHz = juce::jlimit(0.01f, 20.0f, (float) (1.0 / (mu_ui::rateSyncSixteenthsFromNorm(p.insertParam[0]) * mu_ui::insertSixteenthSeconds(tempoBpm))));
        q.depth    = juce::jlimit(0.0f, 1.0f, insertSlot(p, 1) * 0.01f);
        q.feedback = juce::jlimit(-0.95f, 0.95f, insertSlot(p, 2) * 0.01f);
        q.stereoOffset = kInsertStereoOffset;
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
    FlangerCore core;
    std::atomic<float> rate { 0.5f }, depth { 0.5f }, feedback { 0.0f }, sendMix { 0.5f };
    bool started = false;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mix;
};
}  // namespace mu_audio
